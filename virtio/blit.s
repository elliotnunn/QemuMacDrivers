	MACRO
	MakeFunction &fnName
		EXPORT &fnName[DS]
 		EXPORT .&fnName[PR]

		TC &fnName[TC], &fnName[DS]

		CSECT &fnName[DS]
			DC.L .&fnName[PR]
 			DC.L TOC[tc0]

		CSECT .&fnName[PR]
		FUNCTION .&fnName[PR]

	ENDM

	MakeFunction blit1asm

rSrcPix         equ r3
rSrcRowSkip     equ r4
rDstPix         equ r5
rDstRowSkip     equ r6
rW              equ r7
rH              equ r8
rColor0         equ r9
rColorXOR       equ r10

	stmw	r30,-40(r1)

@row
	mtctr	rW
@thirtytwopixels
	lwzu	r31,4(rSrcPix)

	rlwinm  r30,r31,1,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,0(rDstPix)

	rlwinm  r30,r31,2,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,4(rDstPix)

	rlwinm  r30,r31,3,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,8(rDstPix)

	rlwinm  r30,r31,4,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,12(rDstPix)

	rlwinm  r30,r31,5,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,16(rDstPix)

	rlwinm  r30,r31,6,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,20(rDstPix)

	rlwinm  r30,r31,7,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,24(rDstPix)

	rlwinm  r30,r31,8,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,28(rDstPix)

	rlwinm  r30,r31,9,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,32(rDstPix)

	rlwinm  r30,r31,10,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,36(rDstPix)

	rlwinm  r30,r31,11,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,40(rDstPix)

	rlwinm  r30,r31,12,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,44(rDstPix)

	rlwinm  r30,r31,13,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,48(rDstPix)

	rlwinm  r30,r31,14,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,52(rDstPix)

	rlwinm  r30,r31,15,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,56(rDstPix)

	rlwinm  r30,r31,16,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,60(rDstPix)

	rlwinm  r30,r31,17,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,64(rDstPix)

	rlwinm  r30,r31,18,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,68(rDstPix)

	rlwinm  r30,r31,19,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,72(rDstPix)

	rlwinm  r30,r31,20,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,76(rDstPix)

	rlwinm  r30,r31,21,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,80(rDstPix)

	rlwinm  r30,r31,22,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,84(rDstPix)

	rlwinm  r30,r31,23,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,88(rDstPix)

	rlwinm  r30,r31,24,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,92(rDstPix)

	rlwinm  r30,r31,25,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,96(rDstPix)

	rlwinm  r30,r31,26,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,100(rDstPix)

	rlwinm  r30,r31,27,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,104(rDstPix)

	rlwinm  r30,r31,28,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,108(rDstPix)

	rlwinm  r30,r31,29,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,112(rDstPix)

	rlwinm  r30,r31,30,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,116(rDstPix)

	rlwinm  r30,r31,31,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,120(rDstPix)

	rlwinm  r30,r31,0,31,31
	neg     r30,r30
	and     r30,r30,rColorXOR
	xor     r30,r30,rColor0
	stw     r30,124(rDstPix)

	addi	rDstPix,rDstPix,128 ; avoiding stwu speeds up by 4%

	bdnz	@thirtytwopixels

	add		rDstPix,rDstPix,rDstRowSkip ; skip to next row
	add		rSrcPix,rSrcPix,rSrcRowSkip

	subi	rH,rH,1 ; decrement and check row counter
	cmpwi   rH,0
	bne		@row

	lmw		r30,-40(r1)
	blr
