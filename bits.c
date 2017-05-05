/*
 *                .__  _____.__                .__
 *   ____________ |__|/ ____\  | _____    _____|  |__
 *  /  ___/\____ \|  \   __\|  | \__  \  /  ___/  |  \
 *  \___ \ |  |_> >  ||  |  |  |__/ __ \_\___ \|   Y  \
 * /____  >|   __/|__||__|  |____(____  /____  >___|  /
 *      \/ |__|                       \/     \/     \/
 *
 * SPI Flash reader.
 *
 * Very fast reader for SPI flashes for Teensy 2.x.
 *
 * Original code by Trammell Hudson (https://trmm.net/SPI)
 * Modifications and addons by Pedro Vilaça - https://reverse.put.as - reverser@put.as
 *
 * Copyright (C) 2012 Trammell Hudson
 * Copyright (C) 2015, 2016, 2017 Pedro Vilaça
 *
 * Hardware pinout reference: https://papers.put.as/papers/macosx/2015/44Con_2015_-_Efi_Monsters.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * bits.c
 *
 * Access to AVR pins via constants.
 *
 * ddr(0xA3, 1) == enable DDRA |= (1 << 3)
 * out(0xA3, 1) == PORTA |= (1 << 3)
 * in(0xA3) == PINA & (1 << 3)
 *
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "bits.h"

void
__bits_ddr(
	const uint8_t id,
	const uint8_t value
)
{
	__inline_ddr(id, value);
}


void
__bits_out(
	const uint8_t id,
	const uint8_t value
)
{
	__inline_out(id, value);
}


uint8_t
__bits_in(
	const uint8_t id
)
{
	return __inline_in(id);
}
