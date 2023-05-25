#include <DriverSynchronization.h>

#include "scc.h"

static void reg(char key, char val) {
	volatile char *addr = *(char **)0x1dc + 2; // ACtl

	*addr = key;
	SynchronizeIO;
	*addr = val;
	SynchronizeIO;
}

void sccWrite(char c) {
	static int didInit;

	if (!didInit) {
		didInit = 1;

		reg(9, 0x80); // reset A/modem
		reg(4, 0x48); // SB1 | X16CLK
		reg(12, 0); // basic baud rate
		reg(13, 0); // basic baud rate
		reg(14, 3); // baud rate generator = BRSRC | BRENAB
		// skip enabling receive via reg 3
		reg(5, 0xca); // enable tx, 8 bits/char, set RTS & DTR
	}

	volatile char *addr = *(char **)0x1dc + 6; // AData
	*addr = c;
	SynchronizeIO;
}
