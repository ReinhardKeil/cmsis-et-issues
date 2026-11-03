# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# The AI layer adds its Kconfig fragments before Zephyr processes configuration.
add_subdirectory(ai_layer)

# Include the NPU node even when an older build cache has an empty overlay list.
if(NOT DTC_OVERLAY_FILE)
  set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_app)

target_sources(app PRIVATE
  src/main.c
  src/arm_embedded_module.cpp
  src/executorch_test.cpp
)

# Keep runtime logging consistent with the existing AI layer configuration.
zephyr_compile_definitions(ET_LOG_ENABLED=0)
