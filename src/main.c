/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <ethosu_driver.h>

#include "executorch_test.h"

#define NPU_NODE DT_NODELABEL(ethosu0)

int main(void)
{
	const struct device *npu = DEVICE_DT_GET(NPU_NODE);
	struct ethosu_driver_version version;
	struct ethosu_hw_info hw;
	struct ethosu_driver *drv;
	int ret = 0;

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	printf("Ethos-U85 (NPU-HG) driver communication check\n");
	printf("Device: %s, base: 0x%08" PRIxPTR ", IRQ: %u\n", npu->name,
	       (uintptr_t)DT_REG_ADDR(NPU_NODE), DT_IRQN(NPU_NODE));

	ethosu_get_driver_version(&version);
	printf("Driver version: %u.%u.%u\n", version.major, version.minor, version.patch);

	/* Do not reserve a driver if initialization failed: reservation would block. */
	if (!device_is_ready(npu)) {
		printf("ERROR: Ethos-U85 device initialization failed. Check the driver logs "
		       "and the board's NPU power, clock and security configuration.\n");
		return -ENODEV;
	}

	drv = ethosu_reserve_driver();
	if (drv == NULL) {
		printf("ERROR: Could not reserve the Ethos-U85 driver.\n");
		return -ENODEV;
	}

	/* This API reads the NPU's ID and CONFIG registers through the core driver. */
	ethosu_get_hw_info(drv, &hw);
	printf("Hardware architecture: %" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
	       hw.version.arch_major_rev, hw.version.arch_minor_rev,
	       hw.version.arch_patch_rev);
	printf("Hardware revision: %" PRIu32 ".%" PRIu32 " (status %" PRIu32 ")\n",
	       hw.version.version_major, hw.version.version_minor, hw.version.version_status);
	printf("Product revision: %" PRIu32 "\n", hw.version.product_major);
	printf("Command stream version: %" PRIu32 "\n", hw.cfg.cmd_stream_version);
	printf("Custom DMA: %" PRIu32 "\n", hw.cfg.custom_dma);

	/* macs_per_cc is encoded as log2(MACs per cycle) by the hardware. */
	if (hw.cfg.macs_per_cc < 32U) {
		uint32_t macs = UINT32_C(1) << hw.cfg.macs_per_cc;

		printf("MACs per cycle: %" PRIu32 "\n", macs);
		if (macs != 256U) {
			printf("ERROR: Expected the E8's 256-MAC Ethos-U85 configuration.\n");
			ret = -EINVAL;
		}
	} else {
		printf("ERROR: Invalid MAC configuration: %" PRIu32 "\n", hw.cfg.macs_per_cc);
		ret = -EINVAL;
	}

	ethosu_release_driver(drv);
	if (ret == 0) {
		printf("Ethos-U85 driver communication OK (ID/configuration read; no inference).\n");
		return executorch_run_test();
	}

	return ret;
}
