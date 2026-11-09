# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gigaspark_m7)

# Strict compilation: treat all warnings as errors (Fase 18)
target_compile_options(app PRIVATE -Werror -Wall -Wextra)

target_sources(app PRIVATE
	m7/src/main.c
	m7/src/app_runner.c
	m7/src/fault_manager.c
	m7/src/nav_bar.c
	m7/src/power_manager.c
	m7/src/settings_store.c
	m7/src/video_decoder.c
	m7/src/audio_i2s.c
	m7/src/hw/oscilloscope.c
	m7/src/hw/power_supply.c
	m7/src/hw/rf_tools.c
	m7/src/net_manager.c
	m7/src/app_downloader.c
	m7/src/display_giga.c
	sdk/gigaspark_api.c
)

target_include_directories(app PRIVATE
	m7/src
	m7/src/hw
	common
	common/include
	sdk
)
