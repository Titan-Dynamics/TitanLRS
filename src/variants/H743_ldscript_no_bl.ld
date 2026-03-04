/*
**  Linker script for STM32H743VIT6 Device with
**  2048KByte FLASH, 512KByte RAM (DTCM), 128KByte ITCM
**
**  WeAct Mini H743 WITHOUT bootloader - full flash available
*/

/* Specify the memory areas */
MEMORY
{
/* RAM CODE is in ITCM RAM for fast execution */
RAM_CODE (rx)   : ORIGIN = 0x00000000, LENGTH = 4K
/* DTCM RAM for data */
RAM_DATA (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K - LENGTH(RAM_CODE)
/* AXI SRAM for larger data structures */
CCMRAM (rw)     : ORIGIN = 0x24000000, LENGTH = 384K
/* FLASH - full 2MB available, no bootloader */
FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 2048K
}

INCLUDE "variants/ldscript_gen.ld"
