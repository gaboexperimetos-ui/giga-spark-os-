# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gigaspark_tests)

set(GIGASPARK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
	test_buddy_allocator.c
	${GIGASPARK_ROOT}/m4/src/mem_engine.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${GIGASPARK_ROOT}/m4/src
	${GIGASPARK_ROOT}/common/include
	${GIGASPARK_ROOT}/common
)
