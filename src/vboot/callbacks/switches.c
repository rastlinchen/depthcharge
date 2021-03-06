/*
 * Copyright 2014 Google Inc.
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

#include <libpayload.h>
#include <vboot_api.h>
#include <vboot/util/flag.h>

/*
 * Return the state of the switches specified in request_mask.
 * TODO(semenzato): find a better interface than the INIT_FLAGS.
 */
uint32_t VbExGetSwitches(uint32_t request_mask)
{
	uint32_t result = 0;

	if ((request_mask & VB_SWITCH_FLAG_PHYS_PRESENCE_PRESSED) &&
	    flag_fetch(FLAG_PHYS_PRESENCE) > 0) /* < 0 is an error */
		result |= VB_SWITCH_FLAG_PHYS_PRESENCE_PRESSED;

	return result;
}
