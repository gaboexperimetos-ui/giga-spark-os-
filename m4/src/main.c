/*
 * Gigaspark OS - M4 Remote Core
 * STM32H747XI Cortex-M4 @ 240MHz
 *
 * Architecture: 2 Dedicated Threads (Strict Multiplexation)
 *
 * Thread 1 - ipc_listener (Priority: High):
 *   EXCLUSIVELY receives RPMsg messages and queues commands.
 *   EXCLUSIVELY sends responses back to M7.
 *   Never processes commands directly.
 *
 * Thread 2 - mem_engine (Priority: Medium):
 *   EXCLUSIVELY processes commands from the queue.
 *   Runs all compression, decompression, SD swap, and filesystem ops.
 *   Puts responses into resp_msgq for ipc_listener to send.
 *
 * Communication: k_msgq between ISR callback and mem_engine thread.
 * If mem_engine is compressing a 1KB block, ipc_listener
 * keeps receiving new commands without blocking.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>

#include "ipc_config.h"
#include "ipc_protocol.h"
#include "mem_engine.h"
#include "fs_manager.h"

LOG_MODULE_REGISTER(gigaspark_m4, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================
 * Shared State
 * ============================================================ */

#define CMD_QUEUE_SIZE  8

/* Request item for the command queue */
struct cmd_item {
	struct ipc_msg req;
};

/* Message queues */
K_MSGQ_DEFINE(cmd_msgq, sizeof(struct cmd_item), CMD_QUEUE_SIZE, 4);
K_MSGQ_DEFINE(resp_msgq, sizeof(struct ipc_msg), CMD_QUEUE_SIZE, 4);

/* Semaphores */
static K_SEM_DEFINE(bound_sem, 0, 1);
static K_SEM_DEFINE(init_sem, 0, 1);

/* IPC endpoint */
static struct ipc_ept ept;
static volatile bool ept_ready;

/* Buffers for IPC responses (owned by mem_engine thread) */
static uint8_t io_buf[MEM_IO_MAX];
static uint8_t app_buf[APP_BINARY_MAX_SIZE];
uint8_t g_file_write_buf[FILE_WRITE_CHUNK_MAX];
uint8_t g_media_read_buf[MEDIA_READ_CHUNK_MAX];

/* ============================================================
 * IPC Callbacks (run in ISR context, must be FAST)
 * ============================================================ */

static void ept_bound(void *priv)
{
	(void)priv;
	ept_ready = true;
	k_sem_give(&bound_sem);
	LOG_INF("Endpoint bound to M7");
}

static void ept_received(const void *data, size_t len, void *priv)
{
	(void)priv;
	if (len < sizeof(struct ipc_msg)) {
		return;
	}

	/* Copy the message into our queue item (safe from ISR) */
	struct cmd_item item;
	memcpy(&item.req, data, sizeof(struct ipc_msg));

	/* Non-blocking put into the queue */
	int ret = k_msgq_put(&cmd_msgq, &item, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("Command queue full, dropping cmd 0x%02x", item.req.cmd);
		return;
	}

}

/* IPC endpoint configuration */
static struct ipc_ept_cfg ept_cfg = {
	.name = IPC_ENDPOINT_NAME,
	.cb = {
		.bound    = ept_bound,
		.received = ept_received,
	},
};

/* ============================================================
 * IPC Listener Thread (High Priority = 5)
 * Receives RPMsg, queues commands, sends responses.
 * ============================================================ */

static void ipc_listener_thread(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;
	const struct device *ipc_instance;
	int ret;

	LOG_INF("IPC Listener starting...");

	/* Initialize IPC */
	ipc_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

	ret = ipc_service_open_instance(ipc_instance);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to open IPC instance: %d", ret);
		return;
	}

	ret = ipc_service_register_endpoint(ipc_instance, &ept, &ept_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to register endpoint: %d", ret);
		return;
	}

	LOG_INF("Waiting for M7 handshake...");
	k_sem_take(&bound_sem, K_FOREVER);
	LOG_INF("M4 IPC Listener ready.");

	/* Main loop: send responses that mem_engine has prepared */
	while (1) {
		struct ipc_msg resp;
		/* The message queue already provides blocking wait + synchronization. */
		if (k_msgq_get(&resp_msgq, &resp, K_FOREVER) == 0) {
			ret = ipc_service_send(&ept, &resp, sizeof(resp));
			if (ret < 0) {
				LOG_ERR("Failed to send response: %d", ret);
			}
		}
	}
}

/* Static thread: IPC listener, high priority (PRIORITY=5) */
K_THREAD_DEFINE(ipc_listener_tid, 2048,
		ipc_listener_thread, NULL, NULL, NULL,
		5, 0, 0);

/* ============================================================
 * Memory Engine Thread (Medium Priority = 7)
 * Reads commands from queue, processes them, sends responses.
 * All heavy work (compress, decompress, swap, filesystem) runs here.
 * ============================================================ */

static void process_command(const struct ipc_msg *req, struct ipc_msg *resp)
{
	resp->cmd = CMD_RESPONSE;

	switch (req->cmd) {
	case CMD_ALLOC: {
		uint16_t handle = mem_alloc(req->size);
		resp->status = (handle != MEM_HANDLE_INVALID)
			       ? STATUS_OK : STATUS_ERR_NOMEM;
		resp->handle = handle;
		resp->size = req->size;
		LOG_DBG("CMD_ALLOC size=%u -> handle=0x%04x", req->size, handle);
		break;
	}
	case CMD_FREE: {
		int ret = mem_free(req->handle);
		resp->status = (ret == 0) ? STATUS_OK : STATUS_ERR_BAD_HANDLE;
		resp->handle = req->handle;
		resp->size = 0;
		LOG_DBG("CMD_FREE handle=0x%04x -> %d", req->handle, ret);
		break;
	}
	case CMD_READ: {
		size_t to_read = req->size;
		if (to_read > MEM_IO_MAX) {
			to_read = MEM_IO_MAX;
		}
		size_t actual = mem_read(req->handle, io_buf, to_read);
		resp->handle = req->handle;
		resp->size = actual;
		resp->status = (actual > 0) ? STATUS_OK : STATUS_ERR_READ_FAILED;
		LOG_DBG("CMD_READ handle=0x%04x -> %u bytes", req->handle, actual);
		break;
	}
	case CMD_WRITE: {
		int ret = mem_write(req->handle, &req->size, sizeof(req->size));
		resp->status = (ret == 0) ? STATUS_OK : STATUS_ERR_WRITE_FAILED;
		resp->handle = req->handle;
		resp->size = 0;
		LOG_DBG("CMD_WRITE handle=0x%04x -> %d", req->handle, ret);
		break;
	}
	case CMD_GET_APP_LIST: {
		int count = fs_get_app_count();
		resp->status = (count > 0) ? STATUS_OK : STATUS_ERR_NO_APPS;
		resp->size = count;
		resp->handle = 0;
		LOG_INF("CMD_GET_APP_LIST -> %d apps", count);
		break;
	}
	case CMD_LOAD_APP: {
		uint8_t app_id = (uint8_t)(req->handle & 0xFF);
		size_t loaded = fs_load_app(app_id, app_buf, sizeof(app_buf));

		if (loaded == 0) {
			resp->status = STATUS_ERR_APP_NOT_FOUND;
			resp->handle = MEM_HANDLE_INVALID;
			resp->size = 0;
			LOG_INF("CMD_LOAD_APP id=%u -> NOT FOUND", app_id);
		} else {
			uint16_t mem_handle = mem_alloc(loaded);
			if (mem_handle == MEM_HANDLE_INVALID) {
				resp->status = STATUS_ERR_NOMEM;
				resp->handle = MEM_HANDLE_INVALID;
				resp->size = 0;
				LOG_INF("CMD_LOAD_APP id=%u -> NO MEMORY", app_id);
			} else {
				mem_write(mem_handle, app_buf, loaded);
				resp->status = STATUS_OK;
				resp->handle = mem_handle;
				resp->size = loaded;
				LOG_INF("CMD_LOAD_APP id=%u -> handle=0x%04x size=%u",
					app_id, mem_handle, loaded);
			}
		}
		break;
	}
	case CMD_FILE_OPEN: {
		char filename[32];
		snprintf(filename, sizeof(filename), "app_%04x.bin", req->handle);

		int fd = fs_file_open(filename);
		if (fd >= 0) {
			resp->status = STATUS_OK;
			resp->handle = (uint16_t)fd;
			resp->size = 0;
			LOG_INF("CMD_FILE_OPEN %s -> fd=%d", filename, fd);
		} else {
			resp->status = STATUS_ERR_FILE_OPEN;
			resp->handle = MEM_HANDLE_INVALID;
			resp->size = 0;
			LOG_ERR("CMD_FILE_OPEN %s -> %d", filename, fd);
		}
		break;
	}
	case CMD_FILE_WRITE: {
		extern uint8_t g_file_write_buf[FILE_WRITE_CHUNK_MAX];

		int written = fs_file_write(req->handle, g_file_write_buf, req->size);
		if (written > 0) {
			resp->status = STATUS_OK;
			resp->size = written;
		} else {
			resp->status = STATUS_ERR_FILE_WRITE;
			resp->size = 0;
			LOG_ERR("CMD_FILE_WRITE fd=%d -> %d", req->handle, written);
		}
		break;
	}
	case CMD_FILE_CLOSE: {
		int ret = fs_file_close(req->handle);
		if (ret == 0) {
			resp->status = STATUS_OK;
			LOG_INF("CMD_FILE_CLOSE fd=%d -> OK", req->handle);
		} else {
			resp->status = STATUS_ERR_FILE_CLOSE;
			LOG_ERR("CMD_FILE_CLOSE fd=%d -> %d", req->handle, ret);
		}
		break;
	}
	case CMD_MEDIA_OPEN: {
		/* El nombre del archivo viene en el campo handle como indice
		 * simplificado. En un caso real se enviaria en el payload. */
		char filename[32];
		snprintf(filename, sizeof(filename), "media_%04x.dat", req->handle);

		int fd = fs_media_open(filename);
		if (fd >= 0) {
			resp->status = STATUS_OK;
			resp->handle = (uint16_t)fd;
			resp->size = 0;
			LOG_INF("CMD_MEDIA_OPEN %s -> fd=%d", filename, fd);
		} else {
			resp->status = STATUS_ERR_MEDIA_OPEN;
			resp->handle = MEM_HANDLE_INVALID;
			LOG_ERR("CMD_MEDIA_OPEN %s -> %d", filename, fd);
		}
		break;
	}
	case CMD_MEDIA_READ: {
		extern uint8_t g_media_read_buf[MEDIA_READ_CHUNK_MAX];

		size_t bytes_read = 0;
		int ret = fs_media_read(req->handle, g_media_read_buf,
					req->size, &bytes_read);
		if (ret == 0 && bytes_read > 0) {
			resp->status = STATUS_OK;
			resp->size = bytes_read;
		} else {
			resp->status = STATUS_ERR_MEDIA_READ;
			resp->size = 0;
		}
		break;
	}
	case CMD_MEDIA_SEEK: {
		/* offset en resp->size, whence en resp->handle (0=SET,1=CUR,2=END) */
		int ret = fs_media_seek(req->handle, (int32_t)req->size,
					req->handle & 0xFF);
		if (ret == 0) {
			resp->status = STATUS_OK;
			LOG_INF("CMD_MEDIA_SEEK fd=%d offset=%d -> OK",
				req->handle, req->size);
		} else {
			resp->status = STATUS_ERR_MEDIA_SEEK;
			LOG_ERR("CMD_MEDIA_SEEK fd=%d -> %d", req->handle, ret);
		}
		break;
	}
	case CMD_MEDIA_CLOSE: {
		int ret = fs_media_close(req->handle);
		if (ret == 0) {
			resp->status = STATUS_OK;
			LOG_INF("CMD_MEDIA_CLOSE fd=%d -> OK", req->handle);
		} else {
			resp->status = STATUS_ERR_MEDIA_CLOSE;
			LOG_ERR("CMD_MEDIA_CLOSE fd=%d -> %d", req->handle, ret);
		}
		break;
	}
	default:
		LOG_WRN("Unknown cmd: 0x%02x", req->cmd);
		resp->status = STATUS_ERR_BAD_HANDLE;
		resp->handle = MEM_HANDLE_INVALID;
		resp->size = 0;
		break;
	}
}

static void mem_engine_thread(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;
	struct cmd_item item;
	struct ipc_msg resp;
	int ret;

	LOG_INF("Memory Engine thread waiting for initialization...");
	k_sem_take(&init_sem, K_FOREVER);
	LOG_INF("Memory Engine thread starting...");

	while (1) {
		/* Block directly on the queue; no duplicate semaphore wakeup. */
		ret = k_msgq_get(&cmd_msgq, &item, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		LOG_DBG("Processing cmd 0x%02x", item.req.cmd);

		/* Process the command (this is where heavy work happens) */
		process_command(&item.req, &resp);

		/* Put response in the response queue */
		ret = k_msgq_put(&resp_msgq, &resp, K_NO_WAIT);
		if (ret < 0) {
			LOG_ERR("Response queue full, dropping response");
			continue;
		}

	}
}

/* Static thread: Memory engine, medium priority (PRIORITY=7) */
K_THREAD_DEFINE(mem_engine_tid, 4096,
		mem_engine_thread, NULL, NULL, NULL,
		7, 0, 0);

/* ============================================================
 * Main (Initialization Only)
 * Initializes hardware and subsystems, then the threads take over.
 * main() thread has lowest priority and does nothing after init.
 * ============================================================ */

int main(void)
{
	int ret;

	LOG_INF("Gigaspark OS M4 Remote Core starting...");
	LOG_INF("Architecture: 2 dedicated threads (ipc_listener + mem_engine)");

	/* Initialize memory engine (includes SD swap init) */
	mem_init();

	/* Initialize filesystem manager (mount SD, scan apps) */
	ret = fs_init();
	if (ret < 0) {
		LOG_WRN("Filesystem init failed: %d (SD not available)", ret);
	} else {
		LOG_INF("Filesystem ready, %d app(s) found", fs_get_app_count());
	}

	k_sem_give(&init_sem);
	LOG_INF("M4 subsystems initialized. Threads are now running.");
	LOG_INF("  ipc_listener: tid=%d, prio=5 (HIGH)", (int)ipc_listener_tid);
	LOG_INF("  mem_engine:   tid=%d, prio=7 (MEDIUM)", (int)mem_engine_tid);

	/* main() thread sleeps forever - threads do all the work */
	return 0;
}
