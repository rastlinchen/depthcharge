/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2019, The Linux Foundation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <assert.h>
#include <libpayload.h>

#include "base/init_funcs.h"
#include "drivers/gpio/gpio.h"
#include "vboot/util/flag.h"
#include "boot/fit.h"
#include "drivers/storage/sdhci_msm.h"
#include "drivers/bus/usb/usb.h"
#include "drivers/gpio/sc7180.h"

#define SDC1_HC_BASE          0x7C4000
#define SDC1_TLMM_CFG_ADDR    0x3D7A000
#define SDC2_TLMM_CFG_ADDR 0x3D7B000
#define SDC2_HC_BASE 0x08804000

static int board_setup(void)
{
	/* stub out required GPIOs for vboot */
	flag_replace(FLAG_LIDSW, new_gpio_high());
	flag_replace(FLAG_WPSW,  new_gpio_high());
	flag_replace(FLAG_PWRSW, new_gpio_low());

	/* Support USB3.0 XHCI controller in firmware. */
	UsbHostController *usb_host = new_usb_hc(XHCI, 0xa600000);
	list_insert_after(&usb_host->list_node, &usb_host_controllers);

	/*eMMC card support */
	SdhciHost *emmc = new_sdhci_msm_host(SDC1_HC_BASE,
			SDHCI_PLATFORM_EMMC_1V8_POWER |
			SDHCI_PLATFORM_NO_EMMC_HS200,
			100*MHz, SDC1_TLMM_CFG_ADDR,
			NULL);

	list_insert_after(&emmc->mmc_ctrlr.ctrlr.list_node,
			&fixed_block_dev_controllers);

	/* SD card support */
	Sc7180GpioCfg *cd_gpio_cfg = new_sc7180_gpio_input(GPIO(69));
        SdhciHost *sd = new_sdhci_msm_host(SDC2_HC_BASE,
                                SDHCI_PLATFORM_REMOVABLE,
                                50*MHz, SDC2_TLMM_CFG_ADDR,
                                new_gpio_not(&cd_gpio_cfg->ops));
        list_insert_after(&sd->mmc_ctrlr.ctrlr.list_node,
                        &removable_block_dev_controllers);

	return 0;
}

INIT_FUNC(board_setup);
