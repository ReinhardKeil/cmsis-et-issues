// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <zephyr/init.h>
#include <stdint.h>

typedef void (*constructor_fn)(void);
extern const uint32_t __ac6_init_array_start[];
extern const uint32_t __ac6_init_array_end[];

static int ac6_static_init(void)
{
  for (const uint32_t *entry = __ac6_init_array_start;
       entry != __ac6_init_array_end; ++entry) {
    if (*entry != 0U) {
      // armlink resolves .init_array entries relative to their own slot.
      // Unsigned addition handles negative offsets and preserves the Thumb bit.
      constructor_fn fn = (constructor_fn)((uintptr_t)entry + *entry);
      fn();
    }
  }
  return 0;
}

// Run after POST_KERNEL and before main, as Zephyr's GNU initializer does.
SYS_INIT(ac6_static_init, APPLICATION, 0);
