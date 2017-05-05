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
 * probe.c
 *
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "usb_serial.h"
#include "bits.h"
#include "xmodem.h"

#define SPI_SS   0xB0 // white
#define SPI_SCLK 0xB1 // green
#define SPI_MOSI 0xB2 // blue
#define SPI_MISO 0xB3 // brown
#define SPI_POW  0xB7 // red

#define SPI_PAGE_SIZE	4096
#define SPI_PAGE_MASK	(SPI_PAGE_SIZE - 1)

#define FLASH_PAGE_SIZE 256
#define FLASH_PAGE_MASK (FLASH_PAGE_SIZE - 1)

#define FLASH_SUBSECTOR_SIZE    4096
#define FLASH_SUBSECTOR_MASK    (FLASH_SUBSECTOR_SIZE - 1)

#define SPI_WIP 1
#define SPI_WEL 2
#define SPI_WRITE_ENABLE 0x06

#define CONFIG_SPI_HW

/* size of array to hold possible password locations */
#define MAX_PWDS    4

static xmodem_block_t xmodem_block;
static uint32_t bytes_uploaded;

/* default size is 8Mbyte (64 mbits) */
static uint32_t target_flash_size = 8L << 20;

static void spi_erase_sector(uint32_t addr);

static void
help(void)
{
    send_str(PSTR("Help:\r\n"));
    send_str(PSTR("---[ ID commands ]---\r\n"));
    send_str(PSTR("i: print manufacturer and product ID\r\n"));
    
    send_str(PSTR("---[ Read commands ]---\r\n"));
    send_str(PSTR("r: read 16 bytes from address - r0<enter>\r\n"));
    send_str(PSTR("R: read XX bytes from address - R0 10<enter>\r\n"));
    send_str(PSTR("d: dump to console\r\n"));
    send_str(PSTR("w: write enable interactive\r\n"));
    
    send_str(PSTR("---[ Flash commands ]---\r\n"));
    send_str(PSTR("u: upload\r\n"));
    send_str(PSTR("b: upload bios area only\r\n"));
    send_str(PSTR("1: flash first ffs\r\n"));
    send_str(PSTR("2: flash second ffs\r\n"));
    send_str(PSTR("3: flash third ffs\r\n"));
    send_str(PSTR("S: set target flash size\r\n"));
    
    send_str(PSTR("---[ Erase commands ]---\r\n"));
    send_str(PSTR("e: erase sector interactive\r\n"));
    send_str(PSTR("E: total erase 8mb\r\n"));
    send_str(PSTR("B: bulk erase Spansion S25FL128S\r\n"));
    send_str(PSTR("Q: bulk erase Micron N25Q064A or Winbond W25Q64FV\r\n"));
    send_str(PSTR("A: bulk erase Macronix MX25L64\r\n"));
    send_str(PSTR("f: erase firmware password\r\n"));
    send_str(PSTR("l: locate firmware password\r\n"));
    send_str(PSTR("x:\r\n"));
    send_str(PSTR("download: \r\n"));
}

static int
usb_serial_getchar_echo()
{
	while (1)
	{
		int c = usb_serial_getchar();
		if (c == -1)
        {
			continue;
        }
        
		usb_serial_putchar(c);
		if (c == '\r')
        {
			usb_serial_putchar('\n');
        }
		return c;
	}
}

static inline void
spi_power(int i)
{
	out(SPI_POW, i);
}

/* set clock select high or low */
/* CS/SS/S# is pin PB0 in Teensy 2.0 */
static inline void
spi_cs(int i)
{
    /* low */
	if (i)
    {
		cbi(PORTB, 0);
    }
    /* high */
	else
    {
		sbi(PORTB, 0);
    }
}

static char
hexdigit(uint8_t x)
{
	x &= 0xF;
	if (x < 0xA)
    {
		return x + '0';
    }
	else
    {
		return x + 'A' - 0xA;
    }
}

// Send a string to the USB serial port.  The string must be in
// flash memory, using PSTR
//
void send_str(const char *s)
{
    char c;
    while (1) {
        c = pgm_read_byte(s++);
        if (!c)
        {
            break;
        }
		usb_serial_putchar(c);
	}
}

static inline uint8_t
spi_send(uint8_t c)
{
    uint8_t bits[80];
    uint8_t i = 0;
#ifdef CONFIG_SPI_HW
    /* put data to be shifted into MOSI in the data register */
    SPDR = c;
    /* the SPIF bit is set when a value is shifted in/out of the SPI */
    while (bit_is_clear(SPSR, SPIF))
    {
        ;
    }
    /* get data from the data register, if avaliable */
    uint8_t val = SPDR;
    return val;
#else
	// shift out and into one register
	uint8_t val = c;
	for (int i = 0 ; i < 8 ; i++)
	{
		out(SPI_MOSI, val & 0x80);
		val <<= 1;
		asm("nop");
		asm("nop");
		asm("nop");

		out(SPI_SCLK, 1);

		asm("nop");
		asm("nop");
		asm("nop");

		if (in(SPI_MISO))
        {
			val |= 1;
        }

		out(SPI_SCLK, 0);
	}

	out(SPI_MOSI, 0); // return to zero
#endif

	bits[i++] = hexdigit(c >> 4);
	bits[i++] = hexdigit(c >> 0);
	bits[i++] = '-';
	bits[i++] = hexdigit(val >> 4);
	bits[i++] = hexdigit(val >> 0);
	bits[i++] = '\r';
	bits[i++] = '\n';
	usb_serial_write(bits, i);

	return val;
}


static void
spi_passthrough(void)
{
	int c = usb_serial_getchar_echo();

	SPDR = c;
	while (bit_is_clear(SPSR, SPIF))
    {
		;
    }
	uint8_t val = SPDR;

	char buf[2];
	buf[0] = hexdigit(val >> 4);
	buf[1] = hexdigit(val >> 0);
	usb_serial_write(buf, 2);
}

/* retrieve unique ID available in Micron N25Q064A */
static uint8_t *
spi_runiqueid(void)
{
    /* we already sent the RDID command
     * so we are just retrieving the data here
     */
    static uint8_t uniqueid[16] = {0};
    /* total is 17 bytes */
    /* first byte is length of the data */
    uint8_t length = spi_send(0x1);
    /* read 16 bytes */
    /* extended device ID and config */
    /* and 14 bytes to unique ID */
    for (int i = 0; i < 16; i++)
    {
        uniqueid[i] = spi_send(0x1);
    }
    return uniqueid;
}

/** Read electronic manufacturer and device id */
static void
spi_rdid(void)
{
    spi_power(1);
    //_delay_ms(2);

    spi_cs(1);
    _delay_us(100);

    // JEDEC RDID: 1 byte out, three bytes back
    spi_send(0x9F);

    // read the manufacturer and device id
    // appear to be common between manufacturers
    uint8_t b1 = spi_send(0x1);
    uint8_t b2 = spi_send(0x2);
    uint8_t b3 = spi_send(0x3);
//    uint8_t b4 = spi_send(0x4);
//    uint8_t b5 = spi_send(0x5);
    /* test if we have extended info and retrieve it */
    uint8_t *uniqueid = NULL;
    /* test if it's a Micron N25Q064A */
    if (b1 == 0x20 && b2 == 0xBA)
    {
        uniqueid = spi_runiqueid();
    }

	spi_cs(0);
	_delay_ms(1);
	spi_power(0);

    /* print some chip/manufacturer info */
    switch (b1) {
        case 0x20:
            send_str(PSTR("Micron "));
            break;
        case 0xC2:
            send_str(PSTR("Macronix "));
            break;
        case 0x01:
            send_str(PSTR("Spansion "));
            break;
        case 0xEF:
            send_str(PSTR("Winbond "));
            break;
        case 0x1C:
            send_str(PSTR("Eon "));
            break;
        case 0xBF:
            send_str(PSTR("SST "));
            break;
        case 0x7F:
            send_str(PSTR("pFLASH "));
            break;
        default:
            send_str(PSTR("Unknown manufacturer "));
            break;
    }
    if (b2 == 0xBA && b3 == 0x17)
    {
        send_str(PSTR("N25Q064A\r\n"));
    }
    else if (b2 == 0x20 && b3 == 0x17)
    {
        send_str(PSTR("MX25L6406E\r\n"));
    }
    else if (b2 == 0x20 && b3 == 0x14)
    {
        send_str(PSTR("MX25L8006E (8 Mbit)\r\n"));
    }
    else if (b2 == 0x20 && b3 == 0x15)
    {
        send_str(PSTR("MX25L1606E (16 Mbit)\r\n"));
    }
    else if (b2 == 0x20 && b3 == 0x18)
    {
        send_str(PSTR("S25FL128S/P\r\n"));
    }
    else if (b2 == 0x2 && b3 == 0x19)
    {
        send_str(PSTR("S25FL256S/P\r\n"));
    }
    else if (b2 == 0x15 && b3 == 0x20)
    {
        send_str(PSTR("EN25P16\r\n"));
    }
    else if (b2 == 0x25 && b3 == 0x8D)
    {
        send_str(PSTR("SST25VF040B (4 Mbit)\r\n"));
    }
    else if (b2 == 0x25 && b3 == 0x8E)
    {
        send_str(PSTR("SST25VF080B (8 Mbit)\r\n"));
    }
    else if (b2 == 0x25 && b3 == 0x41)
    {
        send_str(PSTR("SST25VF016B (16 Mbit)\r\n"));
    }
    else if (b2 == 0x25 && b3 == 0x4A)
    {
        send_str(PSTR("SST25VF032B (32 Mbit)\r\n"));
    }
    else if (b1 == 0x7F && b2 == 0x9D && b3 == 0x20)
    {
        send_str(PSTR("Pm25LD512 (512 Kbit)\r\n"));
    }
    else if (b1 == 0x7F && b2 == 0x9D && b3 == 0x21)
    {
        send_str(PSTR("Pm25LD010 (1 Mbit)\r\n"));
    }
    else if (b1 == 0x7F && b2 == 0x9D && b3 == 0x22)
    {
        send_str(PSTR("Pm25LD020 (2 Mbit)\r\n"));
    }
    else
    {
        send_str(PSTR("unknown chip\r\n"));
    }
    
    char buf[32] = {0};
	uint8_t off = 0;
	buf[off++] = hexdigit(b1 >> 4);
	buf[off++] = hexdigit(b1 >> 0);
	buf[off++] = hexdigit(b2 >> 4);
	buf[off++] = hexdigit(b2 >> 0);
	buf[off++] = hexdigit(b3 >> 4);
	buf[off++] = hexdigit(b3 >> 0);
    if (b1 == 0x20 && b2 == 0xBA)
    {
        buf[off++] = '-';
        for (int i = 0; i < 2; i++)
        {
            buf[off++] = hexdigit(uniqueid[i] >> 4);
            buf[off++] = hexdigit(uniqueid[i] >> 4);
        }
        buf[off++] = '-';
        for (int i = 2; i < 16; i++)
        {
            buf[off++] = hexdigit(uniqueid[i] >> 4);
            buf[off++] = hexdigit(uniqueid[i] >> 4);
        }
    }
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}

/* read status register */
static uint8_t
spi_status(void)
{
	spi_cs(1);
    /* read status register command */
	spi_send(0x05);
	uint8_t r1 = spi_send(0x00);
	spi_cs(0);
	return r1;
}

static uint32_t
usb_serial_readhex(void)
{
	uint32_t val = 0;

	while (1)
	{
        int c = usb_serial_getchar_echo();
        if ('0' <= c && c <= '9')
        {
            val = (val << 4) | (c - '0');
        }
        else
        {
            if ('A' <= c && c <= 'F')
            {
                val = (val << 4) | (c - 'A' + 0xA);
            }
            else
            {
                if ('a' <= c && c <= 'f')
                {
                    val = (val << 4) | (c - 'a' + 0xA);
                }
                else
                {
                    return val;
                }
            }
        }
	}
}

/* write enable the flash */
/* must be always set before writing */
static void
spi_write_enable(void)
{
	spi_power(1);
	_delay_ms(2);
    /* retrieve status */
	uint8_t r1 = spi_status();
    /* XXX: check status ? */
	spi_cs(1);
	spi_send(SPI_WRITE_ENABLE);
	spi_cs(0);
}


static void
spi_write_enable_interactive(void)
{
	spi_write_enable();

	uint8_t r2 = spi_status();

	char buf[16];
	uint8_t off =0;
	buf[off++] = hexdigit(r2 >> 4);
	buf[off++] = hexdigit(r2 >> 0);
	if ((r2 & SPI_WEL) == 0)
    {
		buf[off++] = '!';
    }
    
	buf[off++] = '\r';
	buf[off++] = '\n';
	usb_serial_write(buf, off);
}

static void
print_address(uint32_t addr, uint8_t newline)
{
    uint8_t addr_buf[16] = {0};
    uint8_t off = 0;
    addr_buf[off++] = '0';
    addr_buf[off++] = 'x';
    addr_buf[off++] = hexdigit(addr >> 20);
    addr_buf[off++] = hexdigit(addr >> 16);
    addr_buf[off++] = hexdigit(addr >> 12);
    addr_buf[off++] = hexdigit(addr >> 8);
    addr_buf[off++] = hexdigit(addr >> 4);
    addr_buf[off++] = hexdigit(addr >> 0);
    if (newline)
    {
        addr_buf[off++] = '\r';
        addr_buf[off++] = '\n';
    }
    usb_serial_write(addr_buf, off);
}

/* locate NVRAM variable(s) that hold the firmware passowrd */
static void
spi_locate_pwd(void)
{
    uint32_t start_addr = 0;
    const uint32_t end_addr = 8L << 20;

    spi_power(1);
    _delay_ms(2);
    spi_cs(1);
    // read a page
    spi_send(0x03);
    /* command is followed by three address bytes */
    spi_send(start_addr >> 16);
    spi_send(start_addr >>  8);
    spi_send(start_addr >>  0);
    uint8_t data[256];

    uint32_t led_on = 1;
    uint32_t led_count = 0;
    /* turn LED on if it wasn't already */
    out(0xD6, 1);
    send_str(PSTR("Locating passwords...\r\n"));
    
    while (1)
    {
        for (int i = 0 ; i < 256 ; i++)
        {
            data[i] = spi_send(0);
        }
        /* turn on/off led */
        if (led_count == 0x50)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;
        /* compare GUID */
        for (int i = 0; i < 255; i++)
        {
            if (data[i] == 0xFF && data[i+1] == 0x23)
            {
                if (i < 254 && data[i+2] == 0x80)
                {
                    send_str(PSTR("Found potential password at address: "));
                    print_address(start_addr + i, 1);
                }
            }
        }
        start_addr += sizeof(data);
        if (start_addr >= end_addr)
        {
            break;
        }
    }
    send_str(PSTR("All done!\r\n"));
    /* set clock signal high to end the operation */
    spi_cs(0);
    spi_power(0);
}

/* locate and remove NVRAM variable(s) that hold the firmware passowrd */
static void
spi_erase_pwd(void)
{
    uint32_t pwd_addr[MAX_PWDS] = {0};
    if (MAX_PWDS > UINT8_MAX)
    {
        return;
    }
    /* find password variables */
    /* we should expect only one hit, more are possible */
    uint32_t start_addr = 0;
    const uint32_t end_addr = 8L << 20;
    uint8_t pwd_count = 0;

    /* read a page */
    spi_power(1);
    _delay_ms(2);
    spi_cs(1);
    spi_send(0x03);
    /* command is followed by three address bytes */
    spi_send(start_addr >> 16);
    spi_send(start_addr >>  8);
    spi_send(start_addr >>  0);
    uint8_t data[256];
    send_str(PSTR("Locating passwords...\r\n"));
    uint32_t led_on = 1;
    uint32_t led_count = 0;
    /* turn LED on if it wasn't already */
    out(0xD6, 1);

    while (1)
    {
        for (int i = 0 ; i < 256 ; i++)
        {
            data[i] = spi_send(0);
        }
        /* turn on/off led */
        if (led_count == 0x1000)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;
        /* compare GUID */
        for (int i = 0; i < 255; i++)
        {
            if (data[i] == 0xFF && data[i+1] == 0x23)
            {
                if (i < 253 && // avoid reading off limits
                    data[i+2] == 0x80 &&
                    data[i+3] == 0x4E)
                {
                    /* we want to erase the whole sector so only store that address */
                    pwd_addr[pwd_count++] = start_addr & ~FLASH_SUBSECTOR_MASK;
                    /* ooops */
                    if (pwd_count > MAX_PWDS)
                    {
                        return;
                    }
                }
            }
        }
        start_addr += sizeof(data);
        if (start_addr >= end_addr)
        {
            break;
        }
    }

    /* set clock signal high to end the read */
    spi_cs(0);
    spi_power(0);
    
    send_str(PSTR("Erasing passwords...\r\n"));
    /* finally erase the sectors we found */
    for (int i = 0; i < pwd_count; i++)
    {
        send_str(PSTR("Clearing password from address: "));
        print_address(pwd_addr[i], 1);
        spi_write_enable();
        spi_erase_sector(pwd_addr[i]);
    }
    send_str(PSTR("All done!\r\n"));
}

static void
spi_bulk_erase_S25FL128S(void)
{
    send_str(PSTR("Starting Spansion S25FL128S bulk erase...\r\n"));

    spi_write_enable();

    spi_cs(1);
    spi_send(0x60);
    spi_cs(0);
    while (spi_status() & SPI_WIP)
    {
        ;
    }
    send_str(PSTR("\r\nFinished bulk erase!\r\n"));
    spi_power(0);
}

static void
spi_bulk_erase_N25Q064A(void)
{
    send_str(PSTR("Starting Micron N25Q064A/Winbond W25Q64FV bulk erase...\r\n"));
    
    spi_write_enable();
    
    spi_cs(1);
    spi_send(0xC7);
    spi_cs(0);
    while (spi_status() & SPI_WIP)
    {
        send_str(PSTR("."));
    }
    send_str(PSTR("\r\nFinished bulk erase!\r\n"));
    spi_power(0);
}

/* MX25L64 doesn't have bulk erase command but we can erase 64k sectors */
static void
spi_bulk_erase_MX25L64(void)
{
    send_str(PSTR("Starting Macronix MX25L64 bulk erase...\r\n"));

    uint32_t addr = 0;
    /* 128 * 64k blocks */
    for (int i = 0; i < 128; i++)
    {
        spi_write_enable();
        spi_cs(1);
        spi_send(0xD8);
        spi_send(addr >> 16);
        spi_send(addr >>  8);
        spi_send(addr >>  0);
        spi_cs(0);
        
        while (spi_status() & SPI_WIP)
        {
            ;
        }
        addr += 65536;
    }
    spi_power(0);
}

static void
spi_erase_sector(uint32_t addr)
{
	spi_cs(1);
	spi_send(0x20);
	spi_send(addr >> 16);
	spi_send(addr >>  8);
	spi_send(addr >>  0);
	spi_cs(0);

	while (spi_status() & SPI_WIP)
    {
		;
    }
}

static void
spi_erase_block(uint32_t addr)
{
    spi_cs(1);
    spi_send(0xD8);
    spi_send(addr >> 16);
    spi_send(addr >>  8);
    spi_send(addr >>  0);
    spi_cs(0);
    
    while (spi_status() & SPI_WIP)
    {
        ;
    }
}

static void
spi_erase_sector_interactive(void)
{
	uint32_t addr = usb_serial_readhex();

	if ((spi_status() & SPI_WEL) == 0)
	{
		send_str(PSTR("wp!\r\n"));
		return;
	}

	spi_erase_sector(addr);

	char buf[16];
	uint8_t off = 0;
	buf[off++] = 'E';
	buf[off++] = hexdigit(addr >> 20);
	buf[off++] = hexdigit(addr >> 16);
	buf[off++] = hexdigit(addr >> 12);
	buf[off++] = hexdigit(addr >>  8);
	buf[off++] = hexdigit(addr >>  4);
	buf[off++] = hexdigit(addr >>  0);
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}
	
static void
spi_erase_8mb(void)
{
    uint32_t addr = 0;
    for (int i = 0; i < 2048; i++)
    {
        spi_write_enable();
        spi_erase_sector(addr);
        addr += 4096;
    }
    char buf[16] = "done!\r\n";
    
    usb_serial_write(buf, 8);
}

static void
spi_erase_16mb(void)
{
    send_str(PSTR("Starting total erase...\r\n"));
    uint32_t addr = 0;
    for (int i = 0; i < 256; i++)
    {
        spi_write_enable();
        spi_erase_sector(addr);
        addr += 65536;
    }
    send_str(PSTR("Finished total erase!\r\n"));
}

static void
spi_zap_8mb(void)
{
    uint32_t addr = 0;
    for (int i = 0; i < 128; i++)
    {
        spi_write_enable();
        spi_erase_block(addr);
        addr += 65536;
    }
    char buf[16] = "done!\r\n";
    
    usb_serial_write(buf, 8);
}

/* read user defined number of bytes from user defined address */
static void
spi_read_size(void)
{
    uint32_t addr = usb_serial_readhex();
    uint32_t len = usb_serial_readhex();
    spi_power(1);
    _delay_ms(2);
    spi_cs(1);

    // read a page
    spi_send(0x03);
    /* command is followed by three address bytes */
    spi_send(addr >> 16);
    spi_send(addr >>  8);
    spi_send(addr >>  0);

    uint8_t data[16];

    if (len > INT32_MAX)
    {
        return;
    }
    int x = (int)len;
    while (x > 0)
    {
        int read_size = (x < 16) ? x : 16;
        for (int i = 0 ; i < read_size ; i++)
        {
            data[i] = spi_send(0);
        }
        char buf[16*3+2];
        uint8_t off = 0;
        for (int i = 0 ; i < read_size ; i++)
        {
            buf[off++] = hexdigit(data[i] >> 4);
            buf[off++] = hexdigit(data[i] >> 0);
            buf[off++] = ' ';
        }
        buf[off++] = '\r';
        buf[off++] = '\n';
        
        usb_serial_write(buf, off);
        x -= read_size;
    }
    spi_cs(0);
    spi_power(0);
}

static void
spi_read(void)
{
    /* retrieve the address to read from the command line
     * user must input address and press enter
     */
	uint32_t addr = usb_serial_readhex();

	spi_power(1);
	_delay_ms(2);

	spi_cs(1);
	//_delay_ms(1);

	// read a page
	spi_send(0x03);
    /* command is followed by three address bytes */
	spi_send(addr >> 16);
	spi_send(addr >>  8);
	spi_send(addr >>  0);

	uint8_t data[16];
    /* we can keep reading till the end until we drive the clock HIGH
     * because the address auto increments after data is shifted
     * so in this case we read 16 bytes and stop
     */
	for (int i = 0 ; i < 16 ; i++)
    {
		data[i] = spi_send(0);
    }
    
	spi_cs(0);
	spi_power(0);

	char buf[16*3+2];
	uint8_t off = 0;
	for (int i = 0 ; i < 16 ; i++)
	{
		buf[off++] = hexdigit(data[i] >> 4);
		buf[off++] = hexdigit(data[i] >> 0);
		buf[off++] = ' ';
	}
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}


/** Read the entire ROM out to the serial port. */
static void
spi_dump(void)
{
	const uint32_t end_addr = 8L << 20;

	spi_power(1);
	_delay_ms(1);

	uint32_t addr = 0;
	uint8_t buf[64];

    spi_cs(1);
    /* set the initial address for the read */
    spi_send(0x03);
    spi_send(addr >> 16);
    spi_send(addr >>  8);
    spi_send(addr >>  0);

	while (1)
	{
        /* read in 64 bytes increments */
        /* XXX: we can probably buffer more than that */
		for (uint8_t off = 0 ; off < sizeof(buf) ; off++)
        {
			buf[off] = spi_send(0);
        }
        
        /* send data to serial */
		usb_serial_write(buf, sizeof(buf));
        /* verify if we reach the end */
		addr += sizeof(buf);
		if (addr >= end_addr)
        {
			break;
        }
	}
    /* set clock signal high to end the operation */
    spi_cs(0);
	spi_power(0);
}

/* dump the rom via xmodem */
static void
prom_send(void)
{
	// We have already received the first nak.
	// Fire it up!
	if (xmodem_init(&xmodem_block, 1) < 0)
    {
		return;
    }
    
    uint32_t end_addr = target_flash_size;

	spi_power(1);
	_delay_ms(1);

	uint32_t addr = 0;
	uint32_t led_on = 1;
	uint32_t led_count = 0;
	/* turn LED on if it wasn't already */
	out(0xD6, 1);

    spi_cs(1);
    spi_send(0x03); // read
    spi_send(addr >> 16);
    spi_send(addr >>  8);
    spi_send(addr >>  0);

	while (1)
	{
		for (uint8_t off = 0 ; off < sizeof(xmodem_block.data) ; off++)
        {
			xmodem_block.data[off] = spi_send(0);
        }

		if (xmodem_send(&xmodem_block, 1) < 0)
        {
			return;
        }
        
		addr += sizeof(xmodem_block.data);
		if (addr >= end_addr)
		{
			out(0xD6, 0);
			break;
        }
        /* turn on/off led */
        if (led_count == 0x50)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;
	}

    spi_cs(0);
	spi_power(0);

	xmodem_fini(&xmodem_block);
}

static void
spi_resetnvram(void)
{
    uint32_t addr = 0x6D8028;
    spi_write_enable();
    spi_cs(1);
    /* page program command */
    spi_send(0x02);
    spi_send(addr >> 16);
    spi_send(addr >>  8);
    spi_send(addr >>  0);
    spi_send(0x00);
    spi_cs(0);
    
    // wait for write to finish
    while (spi_status() & SPI_WIP)
    {
        ;
    }
    send_str(PSTR("done!\r\n"));
}

/** Write some number of pages into the PROM. */
static void
spi_upload(void)
{
    bytes_uploaded = 0;
	uint32_t addr = usb_serial_readhex();
	uint32_t len = usb_serial_readhex();

	// addr and len must be 4k aligned
	const int fail = ((len & SPI_PAGE_MASK) != 0) || ((addr & SPI_PAGE_MASK) != 0);

	char outbuf[32];
	uint8_t off = 0;
	
	outbuf[off++] = fail ? '!' : 'G';
	outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(addr >> 24);
	outbuf[off++] = hexdigit(addr >> 20);
	outbuf[off++] = hexdigit(addr >> 16);
	outbuf[off++] = hexdigit(addr >> 12);
	outbuf[off++] = hexdigit(addr >>  8);
	outbuf[off++] = hexdigit(addr >>  4);
	outbuf[off++] = hexdigit(addr >>  0);
	outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(len >> 24);
	outbuf[off++] = hexdigit(len >> 20);
	outbuf[off++] = hexdigit(len >> 16);
	outbuf[off++] = hexdigit(len >> 12);
	outbuf[off++] = hexdigit(len >>  8);
	outbuf[off++] = hexdigit(len >>  4);
	outbuf[off++] = hexdigit(len >>  0);
	outbuf[off++] = '\r';
	outbuf[off++] = '\n';

	usb_serial_write(outbuf, off);
	if (fail)
    {
		return;
    }

    uint32_t led_on = 1;
    uint32_t led_count = 0;
    /* turn LED on if it wasn't already */
    out(0xD6, 1);

	uint32_t offset = 0;
	const size_t chunk_size = sizeof(xmodem_block.data);
	uint8_t * const buf = xmodem_block.data;

	for (offset = 0 ; offset < len ; offset += chunk_size)
	{
		// read 128 bytes into the xmodem data block
		for (uint8_t i = 0 ; i < chunk_size; i++)
		{
			int c;
			while ((c = usb_serial_getchar()) == -1)
            {
				;
            }
			buf[i] = c;
		}
#if 0
		if ((addr & SPI_PAGE_MASK) == 0)
		{
			// new sector; erase this one
			spi_write_enable();
			spi_erase_sector(addr);
		}
#endif
		spi_write_enable();
		uint8_t r2 = spi_status();

		spi_cs(1);
        /* page program command */
		spi_send(0x02);
		spi_send(addr >> 16);
		spi_send(addr >>  8);
		spi_send(addr >>  0);
			
		for (uint8_t i = 0 ; i < chunk_size ; i++)
        {
			spi_send(buf[i]);
            bytes_uploaded++;
        }

        /* turn on/off led */
        if (led_count == 0x50)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;

		spi_cs(0);

		// wait for write to finish
		while (spi_status() & SPI_WIP)
        {
			;
        }

		//usb_serial_putchar('.');
		addr += chunk_size;
	}

	send_str(PSTR("done!\r\n"));
}

/** Write only bios pages into the PROM. */
static void
spi_biosupload(void)
{
    bytes_uploaded = 0;
    /* bios starts at 0x190000 */
    uint32_t addr = 0x190000;
    uint32_t len = 0x670000;
    
    // addr and len must be 4k aligned
    const int fail = ((len & SPI_PAGE_MASK) != 0) || ((addr & SPI_PAGE_MASK) != 0);
    
    char outbuf[32];
    uint8_t off = 0;
    
    outbuf[off++] = fail ? '!' : 'G';
    outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(addr >> 20);
    outbuf[off++] = hexdigit(addr >> 16);
    outbuf[off++] = hexdigit(addr >> 12);
    outbuf[off++] = hexdigit(addr >>  8);
    outbuf[off++] = hexdigit(addr >>  4);
    outbuf[off++] = hexdigit(addr >>  0);
    outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(len >> 20);
    outbuf[off++] = hexdigit(len >> 16);
    outbuf[off++] = hexdigit(len >> 12);
    outbuf[off++] = hexdigit(len >>  8);
    outbuf[off++] = hexdigit(len >>  4);
    outbuf[off++] = hexdigit(len >>  0);
    outbuf[off++] = '\r';
    outbuf[off++] = '\n';
    
    usb_serial_write(outbuf, off);
    if (fail)
    {
        return;
    }
    
    uint32_t led_on = 1;
    uint32_t led_count = 0;
    /* turn LED on if it wasn't already */
    out(0xD6, 1);
    
    uint32_t offset = 0;
    const size_t chunk_size = sizeof(xmodem_block.data);
    uint8_t * const buf = xmodem_block.data;
    
    for (offset = 0 ; offset < len ; offset += chunk_size)
    {
        // read 128 bytes into the xmodem data block
        for (uint8_t i = 0 ; i < chunk_size; i++)
        {
            int c;
            while ((c = usb_serial_getchar()) == -1)
            {
                ;
            }
            buf[i] = c;
        }
        
        if ((addr & SPI_PAGE_MASK) == 0)
        {
            // new sector; erase this one
            spi_write_enable();
            spi_erase_sector(addr);
        }
        
        spi_write_enable();
        uint8_t r2 = spi_status();
        
        spi_cs(1);
        /* page program command */
        spi_send(0x02);
        spi_send(addr >> 16);
        spi_send(addr >>  8);
        spi_send(addr >>  0);
        
        for (uint8_t i = 0 ; i < chunk_size ; i++)
        {
            spi_send(buf[i]);
            bytes_uploaded++;
        }
        
        /* turn on/off led */
        if (led_count == 0x50)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;
        
        spi_cs(0);
        
        // wait for write to finish
        while (spi_status() & SPI_WIP)
        {
            ;
        }
        
        addr += chunk_size;
    }
    
    send_str(PSTR("done!\r\n"));
}

/* generic function to flash input bios area */
static void
spi_flasharea(uint32_t addr, uint32_t len)
{
    bytes_uploaded = 0;
    
    // addr and len must be 4k aligned
    const int fail = ((len & SPI_PAGE_MASK) != 0) || ((addr & SPI_PAGE_MASK) != 0);
    
    char outbuf[32];
    uint8_t off = 0;
    
    outbuf[off++] = fail ? '!' : 'G';
    outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(addr >> 20);
    outbuf[off++] = hexdigit(addr >> 16);
    outbuf[off++] = hexdigit(addr >> 12);
    outbuf[off++] = hexdigit(addr >>  8);
    outbuf[off++] = hexdigit(addr >>  4);
    outbuf[off++] = hexdigit(addr >>  0);
    outbuf[off++] = ' ';
    outbuf[off++] = hexdigit(len >> 20);
    outbuf[off++] = hexdigit(len >> 16);
    outbuf[off++] = hexdigit(len >> 12);
    outbuf[off++] = hexdigit(len >>  8);
    outbuf[off++] = hexdigit(len >>  4);
    outbuf[off++] = hexdigit(len >>  0);
    outbuf[off++] = '\r';
    outbuf[off++] = '\n';
    
    usb_serial_write(outbuf, off);
    if (fail)
    {
        return;
    }
    
    uint32_t led_on = 1;
    uint32_t led_count = 0;
    /* turn LED on if it wasn't already */
    out(0xD6, 1);
    
    uint32_t offset = 0;
    const size_t chunk_size = sizeof(xmodem_block.data);
    uint8_t * const buf = xmodem_block.data;
    
    for (offset = 0 ; offset < len ; offset += chunk_size)
    {
        // read 128 bytes into the xmodem data block
        for (uint8_t i = 0 ; i < chunk_size; i++)
        {
            int c;
            while ((c = usb_serial_getchar()) == -1)
            {
                ;
            }
            buf[i] = c;
        }
        
        if ((addr & SPI_PAGE_MASK) == 0)
        {
            // new sector; erase this one
            spi_write_enable();
            spi_erase_sector(addr);
        }
        
        spi_write_enable();
        uint8_t r2 = spi_status();
        
        spi_cs(1);
        /* page program command */
        spi_send(0x02);
        spi_send(addr >> 16);
        spi_send(addr >>  8);
        spi_send(addr >>  0);
        
        for (uint8_t i = 0 ; i < chunk_size ; i++)
        {
            spi_send(buf[i]);
            bytes_uploaded++;
        }
        
        /* turn on/off led */
        if (led_count == 0x50)
        {
            if (led_on == 0)
            {
                out(0xD6, 1);
                led_on = 1;
            }
            else if (led_on == 1)
            {
                led_on = 0;
                out(0xD6,0);
            }
            led_count = 0;
        }
        led_count++;
        
        spi_cs(0);
        
        // wait for write to finish
        while (spi_status() & SPI_WIP)
        {
            ;
        }
        
        addr += chunk_size;
    }
    
    send_str(PSTR("done!\r\n"));
}

static void
spi_stats(void)
{
    send_str(PSTR("Uploaded and written bytes: "));
    print_address(bytes_uploaded, 1);
}

static void
spi_change_flash_size(void)
{
    send_str(PSTR("Select target flash size:\r\n"));
    send_str(PSTR("0 - 1MB (8 Mbit)\r\n"));
    send_str(PSTR("1 - 2MB (16 Mbit)\r\n"));
    send_str(PSTR("2 - 4MB (32 Mbit)\r\n"));
    send_str(PSTR("3 - 8MB (64 Mbit)\r\n"));
    send_str(PSTR("4 - 16MB (128 Mbit)\r\n"));
    send_str(PSTR("5 - 32MB (256 Mbit)\r\n"));
    send_str(PSTR("6 - 64K (512 Kbit)\r\n"));
    send_str(PSTR("7 - 128K (1 Mbit)\r\n"));
    send_str(PSTR("8 - 256K (2 Mbit)\r\n"));
    send_str(PSTR("\r\nDefault is 64 Mbit\r\n"));
    uint32_t size = usb_serial_readhex();
    
    switch (size) {
            /* 1 mb */
        case 0:
            target_flash_size = 1L << 20;
            break;
            /* 2 mb */
        case 1:
            target_flash_size = 2L << 20;
            break;
            /* 4 mb */
        case 2:
            target_flash_size = 4L << 20;
            break;
            /* 8mb */
        case 3:
            target_flash_size = 8L << 20;
            break;
            /* 16 mb */
        case 4:
            target_flash_size = 16L << 20;
            break;
        case 5:
            target_flash_size = 32L << 20;
            break;
            /* 512 Kbit */
        case 6:
            target_flash_size = 1L << 16;
            break;
            /* 1 Mbit */
        case 7:
            target_flash_size = 1L << 17;
            break;
            /* 2 Mbit */
        case 8:
            target_flash_size = 1L << 18;
            break;
        default:
            send_str(PSTR("ERROR: Invalid target size selected.\r\n"));
            break;
    }
    
}

int main(void)
{
	// set for 8 MHz clock since we are running at 3.3 V
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
	CPU_PRESCALE(1);

	// Disable the ADC
	ADMUX = 0;

	// initialize the USB, and then wait for the host
	// to set configuration.  If the Teensy is powered
	// without a PC connected to the USB port, this 
	// will wait forever.
	usb_init();
	while (!usb_configured())
    {
		continue;
    }
	// turn on led
	ddr(0xD6, 1);
	out(0xD6, 1);

	_delay_ms(500);

	// wait for the user to run their terminal emulator program
	// which sets DTR to indicate it is ready to receive.
	while (!(usb_serial_get_control() & USB_SERIAL_DTR))
    {
		continue;
    }
    
	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	// Make sure that everything is tri-stated
	ddr(SPI_MISO, 0);
	ddr(SPI_MOSI, 1);
	ddr(SPI_SCLK, 1);
	ddr(SPI_SS, 1);
	//ddr(SPI_POW, 1); // do not enable power pin for now

	// No pull ups enabled
	out(SPI_MISO, 0);

	// just to be sure that MISO is configured correctly
	cbi(PORTB, 3); // no pull up
	cbi(DDRB, 3);

	// keep it off and unselected
	spi_power(0);
	spi_cs(0);

	send_str(PSTR("spi\r\n"));

#ifdef CONFIG_SPI_HW
	// Enable SPI in master mode, clock/16 == 500 KHz
	// Clocked on falling edge (CPOL=0, CPHA=1, PIC terms == CKP=0, CKE=1)
    SPCR = 0
        | (1 << SPE)  // enable SPI
        | (1 << MSTR) // master mode
        | (0 << SPR1) // fastest SPI speed
        | (0 << SPR0)
        | (0 << CPOL) // clock idle when low
        | (0 << CPHA) // Samples data on the falling edge of the data clock when 1, rising edge when 0
        ;

	// Wait for any transactions to complete (shouldn't happen)
	if (bit_is_set(SPCR, SPIF))
    {
		(void) SPDR;
    }
#endif

	while (1)
	{
		usb_serial_putchar('>');

		int c;
		while ((c = usb_serial_getchar()) == -1)
        {
			;
        }
        
        switch(c)
        {
            case 'i': spi_rdid(); break;
            case 'r': spi_read(); break;
            case 'R': spi_read_size(); break;
            case 'd': spi_dump(); break;
            case 'w': spi_write_enable_interactive(); break;
            case 'e': spi_erase_sector_interactive(); break;
            case 'u': spi_upload(); break;
            case 'b': spi_biosupload(); break;
            case '1': spi_flasharea(0x190000, 0x1A0000); break;
            case '2': spi_flasharea(0x330000, 0x30000); break;
            case '3': spi_flasharea(0x360000, 0x2A0000); break;
            case XMODEM_NAK:
                prom_send();
                send_str(PSTR("xmodem done\r\n"));
                break;
            case 'x': {
                uint8_t x = DDRB;
                usb_serial_putchar(hexdigit(x >> 4));
                usb_serial_putchar(hexdigit(x >> 0));
                break;
            }
            case 'f': spi_erase_pwd(); break;
            case 'l': spi_locate_pwd(); break;
            case 's': spi_stats(); break;
            case 'h': help(); break;
            case 'k': spi_resetnvram(); break;
            case 'E': spi_erase_8mb(); break;
            case 'B': spi_bulk_erase_S25FL128S(); break;
            case 'Q': spi_bulk_erase_N25Q064A(); break;
            case 'A': spi_bulk_erase_MX25L64(); break;
            case 'z': spi_zap_8mb(); break;
            case 'S': spi_change_flash_size(); break;
            default:
                usb_serial_putchar('?');
                break;
        }
	}
}
