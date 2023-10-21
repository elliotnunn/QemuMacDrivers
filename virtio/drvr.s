/*
Just stub driver routines, no async support
The linker script implements the DRVR header
C code does the actual work
*/

		.global drvrOpen, drvrClose, drvrControl, drvrStatus, drvrPrime, dce

		.section .text
drvrOpen:
		movem.l %a0/%a1,-(%sp)          /* save registers */

		move.l  %a0,-(%sp)              /* arg: PB */
		clr.l   -(%sp)                  /* arg: trap number */

		lea     dce(%pc),%a0            /* opportunistically set DCE global */
		move.l  %a1,(%a0)

		bsr     funnel
		addq    #8,%sp

		movem.l (%sp)+,%a0/%a1          /* restore registers */

		move.w  %d0,16(%a0)             /* peculiarly Open touches ioResult */
		rts

drvrPrime:
drvrControl:
drvrStatus:
drvrClose:
		movem.l %a0/%a1,-(%sp)          /* save registers */

		move.l  %a0,-(%sp)              /* arg: PB */
		clr.l   -(%sp)                  /* arg: trap number */
		move.b  7(%a0),(%sp)            /* trap number as argument */

		lea     dce(%pc),%a0            /* opportunistically set DCE global */
		move.l  %a1,(%a0)

		bsr     funnel
		addq    #8,%sp

		movem.l (%sp)+,%a0/%a1

		move.w  6(%a0),%d1              /* getPB.ioTrap */
		btst    #9,%d1                  /* check noQueueBit ("immed" call?) */
		bne.s   returnDirectly
		move.l  0x8fc,-(%sp)            /* jIODone */
returnDirectly:
		rts

dce:
		.long   0                       /* extern struct DCtlEntry *dce; */
