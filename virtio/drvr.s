/*
Just stub driver routines, no async support
The linker script implements the DRVR header
The DoDriverIO routine (in C) does the work
*/

		.global drvrOpen, drvrClose, drvrControl, drvrStatus, drvrPrime, dce
		.global IOCommandIsComplete

		.section .text.drvr
drvrOpen:
drvrPrime:
drvrControl:
drvrStatus:
drvrClose:


		movem.l %d2/%a0/%a1,-(%sp)      /* save registers */

/* Push a DriverInitInfo/DriverFinalInfo struct */
		subq    #6,%sp

		moveq.l #0,%d0
		move.b  7(%a0),%d0              /* ioTrap number */

		pea     1                       /* IOCommandKind */
		move.l  %d0,-(%sp)              /* IOCommandCode */
		move.l  %a0,-(%sp)              /* IOCommandContents */
		clr.l   -(%sp)                  /* IOCommandID */
		move.l  #-1,-(%sp)              /* AddressSpaceID */

/* Special case for Open and Close calls */
		cmp.b   #1,%d0
		bhi.s   notOpenOrClose

		lea     20(%sp),%a0
		move.l  %a0,8(%sp)

		move.w  24(%a1),(%a0)+ /* DCE.refnum */
		move.l  #12,(%a0) /* RegEntryID becomes slot number TODO */

		addq.l  #7,12(%sp)
notOpenOrClose:

/* Opportunistically set DCE global */
		lea     dce(%pc),%a0
		move.l  %a1,(%a0)

		bsr     DoDriverIO
		add.w   #26,%sp

		movem.l (%sp)+,%d2/%a0/%a1

		move.w  6(%a0),%d1              /* get PB.ioTrap */

/* Peculiarly Open touches ioResult */
		tst.b   %d1
		bne.s   notOpenDontTouchResult
		move.w  %d0,16(%a0)
notOpenDontTouchResult:

/* Check noQueueBit ("immed" call?) */
		btst    #9,%d1
		bne.s   returnDirectly
		move.l  0x8fc,-(%sp)            /* jIODone */
returnDirectly:
		rts
		.byte   0x84
		.ascii  "DRVR"
		.align  2

		.section .text.IOCommandIsComplete
IOCommandIsComplete:
		move.l  8(%sp),%d0
		rts


		.section .bss
dce:
		.long   0                       /* extern struct DCtlEntry *dce; */
