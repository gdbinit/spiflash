spiflash
====

Original code by Trammell Hudson.

Modifications and addons by Pedro Vilaça.

I have added a few new commands and options. Also added led flashing when dumping/uploading contents. I'm definitely not an AVR coder so excuse me some ugly things :-)

To be used with Teensy 2.x devices (and maybe Chinese clones).

More info: https://trmm.net/SPI_flash

Hardware pinout and examples: https://papers.put.as/papers/macosx/2015/44Con_2015_-_Efi_Monsters.pdf

To compile install the Arduino IDE - https://www.arduino.cc/en/Main/Software - and use the provided Makefile. Tested with OS X only. Xcode project provided only for editing, unable to compile the project.

Commands

* `i`: Read chip ID; if all 0xFF or 0x00, then something is wrong. You get some extra information for chips I worked with but it's an incomplete list.
* `r7f0000↵`: read 16 bytes from 0x7f0000 and hex dump them.
* `R7f0000 32↵`: read 32 bytes from 0x7f0000 and hex dump them.
* `e7f0000↵`: erase a sector at address 7f0000.
* `u190000 1a0000↵`: Upload (and erase) 0x1a0000 bytes to 0x190000.
* `S`: set the target flash size (default is 64mbit so you might want to change if dumping other chips).
* to read the entire rom, shell out and run:
  rx < /dev/ttyACM0 > /dev/ttyACM0 rom.bin
* to read the entire rom in OS X, shell out (or disconnect if using a terminal program like CoolTerm) and run:
  lrx -X -O < /dev/cu.usbmodem12341 > /dev/cu.usbmodem12341 rom.bin (or whatever your usb to serial device is - use the cu version)
* `l` to try to locate firmware password
* `f` to try to remove firmware password

Warning: the firmware password code might have hardcoded things and fail - I can't remember the code anymore - proceed with caution. Good reference on the subject: https://reverse.put.as/2016/06/25/apple-efi-firmware-passwords-and-the-scbo-myth/



