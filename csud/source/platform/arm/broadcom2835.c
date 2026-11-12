/******************************************************************************
*	platform/arm/broadcom2835.c
*	 by Alex Chadwick
*
*	A light weight implementation of the USB protocol stack fit for a simple
*	driver.
*
*	platform/arm/broadcom2835.c contains code for the broadcom2835 chip, used 
*	in the Raspberry Pi. Compiled conditionally on LIB_BCM2835=1.
******************************************************************************/
#include <configuration.h>
#include <platform/platform.h>
#include <types.h>

void Bcm2835Load()
{
	LOG_DEBUG("CSUD: Broadcom2835 driver version 0.1.\n");
}

void MicroDelay(u32 delay) {
	volatile u64* timeStamp = (u64*)0xFE003004;
	u64 stop = *timeStamp + delay;

	while (*timeStamp < stop) 
		__asm__("nop");
}

Result PowerOnUsb() {
	return OK;
}