/*
 * Copyright 2012 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <assert.h>
#include <libpayload.h>
#include <stdint.h>
#include <vb2_api.h>

#include "base/cleanup_funcs.h"
#include "base/timestamp.h"
#include "base/vpd_util.h"
#include "boot/commandline.h"
#include "boot/multiboot.h"
#include "drivers/ec/vboot_ec.h"
#include "drivers/flash/flash.h"
#include "drivers/power/power.h"
#include "drivers/storage/blockdev.h"
#include "drivers/bus/usb/usb.h"
#include "image/fmap.h"
#include "image/symbols.h"
#include "vboot/boot.h"
#include "vboot/boot_policy.h"
#include "vboot/stages.h"
#include "vboot/crossystem/crossystem.h"
#include "vboot/util/commonparams.h"
#include "vboot/util/flag.h"
#include "vboot/util/memory.h"
#include "vboot/nvdata.h"

int vboot_in_recovery(void)
{
	return !!(vboot_get_context()->flags & VB2_CONTEXT_RECOVERY_MODE);
}

int vboot_in_developer(void)
{
	return !!(vboot_get_context()->flags & VB2_CONTEXT_DEVELOPER_MODE);
}

int vboot_check_wipe_memory(void)
{
	if (vboot_in_recovery() || vboot_in_developer())
		return memory_wipe_unused();
	return 0;
}

int vboot_check_enable_usb(void)
{
	/* Initialize USB in developer or recovery mode, skip in normal mode. */
	if (vboot_in_recovery() || vboot_in_developer())
		dc_usb_initialize();
	return 0;
}

static int x86_ec_powerbtn_cleanup_func(struct CleanupFunc *c, CleanupType t)
{
	VbootEcOps *ec = c->data;
	// Reenable power button pulse that we inhibited on x86 systems with UI.
	return ec->enable_power_button(ec, 1);
}
static CleanupFunc x86_ec_powerbtn_cleanup = {
	&x86_ec_powerbtn_cleanup_func,
	CleanupOnReboot | CleanupOnPowerOff |
	CleanupOnHandoff | CleanupOnLegacy,
	NULL,
};

static int vendor_data_settable(void)
{
	int i = 0;
	char vendor_data[CONFIG_VENDOR_DATA_LENGTH + 1];

	if (CONFIG_VENDOR_DATA_LENGTH == 0)
		return 0;

	if (flash_is_wp_enabled())
		return 0;

	if (!vpd_gets(CONFIG_VENDOR_DATA_KEY, vendor_data, sizeof(vendor_data)))
		return 0;

	printf("Vendor data %s is '%s'.\n",
		CONFIG_VENDOR_DATA_KEY, vendor_data);

	while (i < CONFIG_VENDOR_DATA_LENGTH && vendor_data[i] == ' ')
		i++;

	return i == CONFIG_VENDOR_DATA_LENGTH;
}

int vboot_select_and_load_kernel(void)
{
	struct vb2_context *ctx = vboot_get_context();
	void *shared;
	int size;

	VbSelectAndLoadKernelParams kparams = {
		.kernel_buffer = &_kernel_start,
		.kernel_buffer_size = &_kernel_end - &_kernel_start,
	};
	VbootEcOps *ec = vboot_get_ec(PRIMARY_VBOOT_EC);

	// On x86 systems, inhibit power button pulse from EC.
	if (CONFIG(ARCH_X86) && ec &&
	    ec->enable_power_button) {
		ec->enable_power_button(ec, 0);
		x86_ec_powerbtn_cleanup.data = ec;
		list_insert_after(&x86_ec_powerbtn_cleanup.list_node,
				  &cleanup_funcs);
	}

	if (vendor_data_settable())
		ctx->flags |= VB2_CONTEXT_VENDOR_DATA_SETTABLE;

	/* Set legacy vboot1 VbSharedDataHeader. */
	if (find_common_params(&shared, &size))
		return 1;

	printf("Calling VbSelectAndLoadKernel().\n");
	vb2_error_t res = VbSelectAndLoadKernel(ctx, shared, &kparams);

	if (res == VBERROR_EC_REBOOT_TO_RO_REQUIRED) {
		printf("EC Reboot requested. Doing cold reboot.\n");
		if (ec && ec->reboot_to_ro)
			ec->reboot_to_ro(ec);
		if (power_off())
			return 1;
	} else if (res == VBERROR_EC_REBOOT_TO_SWITCH_RW) {
		printf("Switch EC slot requested. Doing cold reboot.\n");
		if (ec && ec->reboot_switch_rw)
			ec->reboot_switch_rw(ec);
		if (power_off())
			return 1;
	} else if (res == VBERROR_SHUTDOWN_REQUESTED) {
		printf("Powering off.\n");
		if (power_off())
			return 1;
	} else if (res == VBERROR_REBOOT_REQUIRED) {
		printf("Reboot requested. Doing warm reboot.\n");
		if (cold_reboot())
			return 1;
	}
	if (res != VB2_SUCCESS) {
		printf("VbSelectAndLoadKernel returned %d, "
		       "Doing a cold reboot.\n", res);
		if (cold_reboot())
			return 1;
	}

	vboot_boot_kernel(&kparams);

	return 1;
}

void vboot_boot_kernel(VbSelectAndLoadKernelParams *kparams)
{
	static char cmd_line_buf[2 * CmdLineSize];
	struct boot_info bi;

	timestamp_add_now(TS_VB_VBOOT_DONE);

	memset(&bi, 0, sizeof(bi));

	if (fill_boot_info(&bi, kparams) == -1) {
		printf("ERROR!!! Unable to parse boot info\n");
		goto fail;
	}

	bi.kparams = kparams;

	BlockDev *bdev = (BlockDev *)kparams->disk_handle;

	struct commandline_info info = {
		.devnum = 0,
		.partnum = kparams->partition_number + 1,
		.guid = kparams->partition_guid,
		.external_gpt = bdev->external_gpt,
	};

	if (bi.cmd_line) {
		if (commandline_subst(bi.cmd_line, cmd_line_buf,
				      sizeof(cmd_line_buf), &info))
			return;
		bi.cmd_line = cmd_line_buf;
	}

	if (crossystem_setup(FIRMWARE_TYPE_AUTO_DETECT))
		return;

	boot(&bi);

fail:
	/*
	 * If the boot succeeded we'd never end up here. If configured, let's
	 * try booting in alternative way.
	 */
	if (CONFIG_KERNEL_LEGACY)
		legacy_boot(bi.kernel, cmd_line_buf);
	if (CONFIG_KERNEL_MULTIBOOT)
		multiboot_boot(&bi);
}
