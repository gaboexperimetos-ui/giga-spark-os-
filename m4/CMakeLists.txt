# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Gigaspark OS

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gigaspark_m4)

# Strict compilation: treat all warnings as errors (Fase 18)
target_compile_options(app PRIVATE -Werror -Wall -Wextra)

target_sources(app PRIVATE
	src/main.c
	src/mem_engine.c
	src/zram_comp.c
	src/sd_swap.c
	src/fs_manager.c
)
target_include_directories(app PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../common
	${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
