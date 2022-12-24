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

rTmpPacked      equ r11
rTmpPix         equ r12

@row
	mtctr	rW ; using the ctr is worth about 2% speed
@thirtytwopixels
	lwzu	rTmpPacked,4(rSrcPix)

	rlwinm  rTmpPix,rTmpPacked,1,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,0(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,2,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,4(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,3,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,8(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,4,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,12(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,5,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,16(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,6,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,20(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,7,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,24(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,8,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,28(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,9,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,32(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,10,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,36(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,11,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,40(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,12,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,44(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,13,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,48(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,14,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,52(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,15,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,56(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,16,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,60(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,17,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,64(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,18,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,68(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,19,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,72(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,20,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,76(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,21,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,80(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,22,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,84(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,23,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,88(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,24,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,92(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,25,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,96(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,26,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,100(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,27,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,104(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,28,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,108(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,29,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,112(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,30,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,116(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,31,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,120(rDstPix)

	rlwinm  rTmpPix,rTmpPacked,0,31,31
	neg     rTmpPix,rTmpPix
	and     rTmpPix,rTmpPix,rColorXOR
	xor     rTmpPix,rTmpPix,rColor0
	stw     rTmpPix,124(rDstPix)

	addi	rDstPix,rDstPix,128 ; avoiding stwu speeds up by 4%

	bdnz	@thirtytwopixels

	add		rDstPix,rDstPix,rDstRowSkip ; skip to next row
	add		rSrcPix,rSrcPix,rSrcRowSkip

	subi	rH,rH,1 ; decrement and check row counter
	cmpwi   rH,0
	bne		@row

	blr
