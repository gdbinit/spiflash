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
 * bits.h
 *
 * Easy access to AVR ports.
 *
 * For compile time constants, the operations will be single
 * instruction.  For runtime constants a function call will
 * be required.
 *
 */

#ifndef _prom_bits_h_
#define _prom_bits_h_

#include <stdint.h>

#define sbi(PORT, PIN) ((PORT) |=  (1 << (PIN)))
#define cbi(PORT, PIN) ((PORT) &= ~(1 << (PIN)))
#define array_count(ARRAY) (sizeof(ARRAY) / sizeof(*(ARRAY)))
#define _STR(X) #X
#define STR(X) _STR(X)



/*
#if defined(__AVR_AT90USB162__) \
	case 0xA: func(cat(prefix, A), pin); break; \
#endif
*/

#define cat(x,y) x ## y

#define BITS_FOR_ALL(prefix, func, port, pin) do { \
	switch (port) \
	{ \
	case 0xB: func(cat(prefix, B), pin); break; \
	case 0xC: func(cat(prefix, C), pin); break; \
	case 0xD: func(cat(prefix, D), pin); break; \
	case 0xE: func(cat(prefix, E), pin); break; \
	case 0xF: func(cat(prefix, F), pin); break; \
	} \
} while (0)


/** Function call version for non-compile time constants */
extern void
__bits_out(
	const uint8_t port,
	uint8_t value
);


static inline void
__inline_out(
	const uint8_t id,
	const uint8_t value
)
{
	const uint8_t port = (id >> 4) & 0xF;
	const uint8_t pin = (id >> 0) & 0xF;

	if (value)
		BITS_FOR_ALL(PORT,sbi, port, pin);
	else
		BITS_FOR_ALL(PORT,cbi, port, pin);
}


static inline void
out(
	const uint8_t port,
	uint8_t value
)
{
	if (__builtin_constant_p(port)
	&&  __builtin_constant_p(value))
		__inline_out(port, value);
	else
		__bits_out(port, value);
}


extern void
__bits_ddr(
	const uint8_t port,
	const uint8_t value
);


static inline void
__inline_ddr(
	const uint8_t id,
	const uint8_t value
)
{
	const uint8_t port = (id >> 4) & 0xF;
	const uint8_t pin = (id >> 0) & 0xF;

	if (value)
		BITS_FOR_ALL(DDR,sbi, port, pin);
	else
		BITS_FOR_ALL(DDR,cbi, port, pin);
}


static inline void
ddr(
	const uint8_t port,
	uint8_t value
)
{
	if (__builtin_constant_p(port)
	&&  __builtin_constant_p(value))
		__inline_ddr(port, value);
	else
		__bits_ddr(port, value);
}


extern uint8_t
__bits_in(
	const uint8_t port
);


static inline uint8_t
__inline_in(
	const uint8_t id
)
{
	const uint8_t port = (id >> 4) & 0xF;
	const uint8_t pin = (id >> 0) & 0xF;

	BITS_FOR_ALL(PIN,return bit_is_set, port, pin);
	return -1; // unreached?
}


static inline uint8_t
in(
	const uint8_t port
)
{
	if (__builtin_constant_p(port))
		return __inline_in(port);
	else
		return __bits_in(port);
}


#endif
