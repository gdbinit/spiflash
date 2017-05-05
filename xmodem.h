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
 * xmode.h
 *
 * xmodem protocol
 *
 * Using USB serial
 *
 */

#ifndef _xmodem_h_
#define _xmodem_h_

#include <avr/io.h>
#include <stdint.h>


typedef struct
{
	uint8_t soh;
	uint8_t block_num;
	uint8_t block_num_complement;
	uint8_t data[128];
	uint8_t cksum;
} __attribute__((__packed__))
xmodem_block_t;

#define XMODEM_SOH 0x01
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_CAN 0x18
#define XMODEM_C 0x43
#define XMODEM_NAK 0x15
#define XMODEM_EOF 0x1a


int
xmodem_init(
	xmodem_block_t * const block,
	int already_received_first_nak
);


int
xmodem_send(
	xmodem_block_t * const block,
	int wait_for_ack
);


int xmodem_fini(
	xmodem_block_t * const block
);


#endif
