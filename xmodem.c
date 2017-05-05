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
 * xmode.c
 *
 * xmodem protocol
 *
 * Using USB serial
 *
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "usb_serial.h"
#include "xmodem.h"



/** Send a block.
 * Compute the checksum and complement.
 *
 * \return 0 if all is ok, -1 if a cancel is requested or more
 * than 10 retries occur.
 */
int
xmodem_send(
	xmodem_block_t * const block,
	int wait_for_ack
)
{
	// Compute the checksum and complement
	uint8_t cksum = 0;
	for (uint8_t i = 0 ; i < sizeof(block->data) ; i++)
		cksum += block->data[i];

	block->cksum = cksum;
	block->block_num++;
	block->block_num_complement = 0xFF - block->block_num;

	// Send the block, and wait for an ACK
	uint8_t retry_count = 0;

	while (retry_count++ < 10)
	{
		usb_serial_write((void*) block, sizeof(*block));
		// Wait for an ACK (done), CAN (abort) or NAK (retry)
		while (1)
		{
			uint8_t c = usb_serial_getchar();
			if (c == XMODEM_ACK)
				return 0;
			if (c == XMODEM_CAN)
				return -1;
			if (c == XMODEM_NAK)
				break;

			if (!wait_for_ack)
				return 0;
		}
	}

	// Failure or cancel
	return -1;
}


int
xmodem_init(
	xmodem_block_t * const block,
	int already_received_first_nak
)
{
	block->soh = 0x01;
	block->block_num = 0x00;

	if (already_received_first_nak)
		return 0;

	// wait for initial nak
	while (1)
	{
		uint8_t c = usb_serial_getchar();
		if (c == XMODEM_NAK)
			return 0;
		if (c == XMODEM_CAN)
			return -1;
	}
}


int
xmodem_fini(
	xmodem_block_t * const block
)
{
#if 0
/* Don't send EOF?  rx adds it to the file? */
	block->block_num++;
	memset(block->data, XMODEM_EOF, sizeof(block->data));
	if (xmodem_send_block(block) < 0)
		return;
#endif

	// File transmission complete.  send an EOT
	// wait for an ACK or CAN
	while (1)
	{
		usb_serial_putchar(XMODEM_EOT);

		while (1)
		{
			uint16_t c = usb_serial_getchar();
			if (c == -1)
				continue;
			if (c == XMODEM_ACK)
				return 0;
			if (c == XMODEM_CAN)
				return -1;
		}
	}
}
