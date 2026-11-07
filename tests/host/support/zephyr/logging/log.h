/* Host-test-only logging shim. This is never used by the firmware build. */
#ifndef GIGASPARK_HOST_LOG_H
#define GIGASPARK_HOST_LOG_H
#define LOG_MODULE_REGISTER(...) extern int gigaspark_host_log_unused
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif
