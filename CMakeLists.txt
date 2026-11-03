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

# Zephyr already provides sys/types.h for AC6; the bare-metal pack shim
# must not shadow it. Only libc++ needs compatibility adjustments here.
if(CMAKE_CXX_COMPILER_ID STREQUAL "ARMClang")
  # AC6 6.24's register coalescer crashes on this pack's quantization code
  # with hard-float at both -Og and -O1. Keep the workaround on that file.
  if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "6.24.0" AND CONFIG_FP_HARDABI)
    get_target_property(_et_sources cmsis_executorch SOURCES)
    foreach(_source IN LISTS _et_sources)
      if(_source MATCHES "[/\\\\]op_quantize\\.cpp$")
        set_property(SOURCE "${_source}" TARGET_DIRECTORY cmsis_executorch
          APPEND PROPERTY COMPILE_OPTIONS -O0)
      endif()
    endforeach()
  endif()

  # Zephyr explicitly adds AC6's C resource headers as system includes.
  # AC6 6.24's libc++ wrappers must precede them (notably stddef.h).
  get_filename_component(_ac6_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_ac6_libcxx "${_ac6_bin}/../include/libcxx" ABSOLUTE)
  if(IS_DIRECTORY "${_ac6_libcxx}")
    target_include_directories(zephyr_interface SYSTEM BEFORE INTERFACE
      "$<$<COMPILE_LANGUAGE:CXX>:${_ac6_libcxx}>")
  endif()
  zephyr_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/zephyr_ac6_compat.hpp>")

  # Zephyr's CMake scatter generator disables CONFIG_STATIC_INIT_GNU,
  # omitting the constructor runner and the companion destructor handler.
  if(NOT CONFIG_STATIC_INIT_GNU)
    zephyr_linker_section_configure(SECTION ctors
      INPUT ".init_array*" SORT NAME KEEP
      SYMBOLS __ac6_init_array_start __ac6_init_array_end)
    zephyr_link_libraries("--keep=*.o(.init_array*)")
    target_sources(app PRIVATE src/zephyr_ac6_init.c
      "${ZEPHYR_BASE}/lib/cpp/abi/cpp_dtors.c")
  endif()

  # Zephyr hard-codes microlib for armlink. ExecuTorch's full libc++ needs
  # the standard Arm C runtime (wide strings and AEABI assertions).
  foreach(_elf zephyr_pre0 zephyr_pre1 zephyr_final)
    if(TARGET ${_elf})
      get_target_property(_libs ${_elf} LINK_LIBRARIES)
      list(REMOVE_ITEM _libs "--library_type=microlib")
      set_property(TARGET ${_elf} PROPERTY LINK_LIBRARIES "${_libs}")
    endif()
  endforeach()
endif()

target_sources(app PRIVATE
  src/main.c
  src/arm_embedded_module.cpp
  src/executorch_test.cpp
)

# Keep runtime logging consistent with the existing AI layer configuration.
zephyr_compile_definitions(ET_LOG_ENABLED=0)
