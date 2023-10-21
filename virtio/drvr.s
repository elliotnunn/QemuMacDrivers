/*
Just stub driver routines, no async support
The linker script implements the DRVR header
C code does the actual work
*/

		.global drvrOpen, drvrClose, drvrControl, drvrStatus, drvrPrime

drvrOpen:
		movem.l %a0/%a1,-(%sp)          /* save registers */

		movem.l %a0/%a1,-(%sp)          /* call C bottleneck */
		move.l  #0,-(%sp)
		bsr     funnel
		add     #12,%sp

		movem.l (%sp)+,%a0/%a1          /* restore registers */

		move.w  %d0,16(%a0)             /* peculiarly Open touches ioResult */
		rts

drvrPrime:
drvrControl:
drvrStatus:
drvrClose:
		movem.l %a0/%a1,-(%sp)          /* save registers */

		movem.l %a0/%a1,-(%sp)          /* call C bottleneck */
		clr.l   -(%sp)
		move.b  7(%a0),(%sp)            /* trap number as argument */
		bsr     funnel
		addq    #8,%sp

		movem.l (%sp)+,%a0/%a1

		move.w  6(%a0),%d1              /* getPB.ioTrap */
		btst    #9,%d1                  /* check noQueueBit ("immed" call?) */
		bne.s   returnDirectly
		move.l  0x8fc,-(%sp)            /* jIODone */
returnDirectly:
		rts
