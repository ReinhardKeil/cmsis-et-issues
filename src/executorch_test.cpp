// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Adapted from CMSIS-Executorch/src/app_main.cpp for the Zephyr application.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include <zephyr/linker/devicetree_regions.h>

#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/exec_aten/exec_aten.h>
#include <executorch/runtime/core/memory_allocator.h>
#include <executorch/runtime/platform/runtime.h>

#include "arm_embedded_module.hpp"
#include "executorch_test.h"
#include "model_pte.h"

using arm::embedded::EmbeddedModule;
using executorch::aten::DimOrderType;
using executorch::aten::ScalarType;
using executorch::aten::SizesType;
using executorch::aten::Tensor;
using executorch::aten::TensorImpl;
using executorch::extension::BufferDataLoader;
using executorch::runtime::EValue;
using executorch::runtime::MemoryAllocator;

namespace {

// TinyCNN needs much less space than the original example's two 4 MiB pools.
constexpr size_t kMethodPoolSize = 128 * 1024;
constexpr size_t kTempPoolSize = 64 * 1024;

alignas(64) uint8_t g_method_pool[kMethodPoolSize];
// The NPU uses scratch directly. Keep it in system SRAM rather than CPU-local DTCM.
alignas(64) uint8_t g_temp_pool[kTempPoolSize]
    Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sram0)));

} // namespace

extern "C" int executorch_run_test(void)
{
  executorch::runtime::runtime_init();
  printf("ExecuTorch Ethos-U85-256 application: %lu byte model\n", model_pte_size);

  // Devicetree memory-region sections are NOLOAD and are not zeroed at boot.
  std::memset(g_temp_pool, 0, sizeof(g_temp_pool));

  EmbeddedModule module(
      model_pte, model_pte_size,
      std::make_unique<BufferDataLoader>(model_pte, model_pte_size),
      std::make_unique<MemoryAllocator>(kMethodPoolSize, g_method_pool),
      std::make_unique<MemoryAllocator>(kTempPoolSize, g_temp_pool));

  // Same deterministic image values as the CMSIS pack example. The supplied
  // U85 PTE exposes NCHW, whereas the original runner hard-codes NHWC.
  alignas(16) static float input_data[16 * 16 * 3];
  auto method_meta = module.method_meta("forward");
  if (!method_meta.ok()) {
    printf("Could not read forward metadata (err=%u)\nTest_result: FAIL\n",
           static_cast<unsigned>(method_meta.error()));
    return 1;
  }
  if (method_meta->num_inputs() != 1) {
    printf("forward expected one input\nTest_result: FAIL\n");
    return 1;
  }
  auto input_meta = method_meta->input_tensor_meta(0);
  if (!input_meta.ok() || input_meta->scalar_type() != ScalarType::Float ||
      input_meta->sizes().size() != 4 || input_meta->dim_order().size() != 4 ||
      input_meta->nbytes() != sizeof(input_data)) {
    printf("forward expected a rank-four RGB image with 768 floats\n"
           "Test_result: FAIL\n");
    return 1;
  }

  for (size_t i = 0; i < sizeof(input_data) / sizeof(input_data[0]); ++i) {
    input_data[i] = static_cast<float>(i % 32) / 32.0f - 0.5f;
  }
  std::array<SizesType, 4> sizes;
  std::array<DimOrderType, 4> dim_order;
  for (size_t i = 0; i < sizes.size(); ++i) {
    sizes[i] = input_meta->sizes()[i];
    dim_order[i] = input_meta->dim_order()[i];
  }
  printf("Input shape: [%ld, %ld, %ld, %ld]\n", static_cast<long>(sizes[0]),
         static_cast<long>(sizes[1]), static_cast<long>(sizes[2]),
         static_cast<long>(sizes[3]));
  TensorImpl input_impl(ScalarType::Float, sizes.size(), sizes.data(), input_data,
                        dim_order.data());
  Tensor input(&input_impl);

  auto outputs = module.forward(input);
  if (!outputs.ok()) {
    printf("forward failed (err=%u)\nTest_result: FAIL\n",
           static_cast<unsigned>(outputs.error()));
    return 1;
  }
  if (outputs->size() != 1 || !outputs->front().isTensor()) {
    printf("forward expected one output tensor\nTest_result: FAIL\n");
    return 1;
  }

  const EValue& out = outputs->front();
  Tensor tensor = out.toTensor();
  if (tensor.scalar_type() != ScalarType::Float || tensor.numel() != 10) {
    printf("forward expected ten float logits\nTest_result: FAIL\n");
    return 1;
  }

  printf("Output: %u element(s):", static_cast<unsigned>(tensor.numel()));
  const float* data = tensor.const_data_ptr<float>();
  for (size_t i = 0; i < tensor.numel(); ++i) {
    printf(" %.4f", static_cast<double>(data[i]));
  }
  printf("\nTest_result: PASS\n");
  fflush(stdout);
  return 0;
}
