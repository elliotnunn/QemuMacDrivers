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

;# Python script to generate the tight loop
;columns = ([], [])
;
;for i in range(32):
;    col = columns[i % 2]
;    register = ["rTmpPix1", "rTmpPix2"][i % 2]
;    col.append(f"rlwinm  {register},rTmpPacked,{(i+1)%32},31,31")
;    col.append(f"neg     {register},{register}")
;    col.append(f"and     {register},{register},rColorXOR")
;    col.append(f"xor     {register},{register},rColor0")
;    col.append(f"stw     {register},{4*i}(rDstPix)")
;
;for i in range(max(len(l) for l in columns)):
;    for col in columns:
;        if i < len(col) and col[i]:
;            print('\t' + col[i])

	MakeFunction blit1asm

rSrcPix         equ r3
rSrcRowSkip     equ r4
rDstPix         equ r5
rDstRowSkip     equ r6
rW              equ r7
rH              equ r8
rColor0         equ r9
rColorXOR       equ r10

rTmpPacked      equ r0
rTmpPix1        equ r11
rTmpPix2        equ r12

@row
	mtctr   rW ; using the ctr is worth about 2% speed
@thirtytwopixels
	lwzu    rTmpPacked,4(rSrcPix) ; prefetching does not help

	; interleaving doesn't help on QEMU/TCG but is good practice on real PowerPC
	rlwinm  rTmpPix1,rTmpPacked,1,31,31
	rlwinm  rTmpPix2,rTmpPacked,2,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,0(rDstPix)
	stw     rTmpPix2,4(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,3,31,31
	rlwinm  rTmpPix2,rTmpPacked,4,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,8(rDstPix)
	stw     rTmpPix2,12(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,5,31,31
	rlwinm  rTmpPix2,rTmpPacked,6,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,16(rDstPix)
	stw     rTmpPix2,20(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,7,31,31
	rlwinm  rTmpPix2,rTmpPacked,8,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,24(rDstPix)
	stw     rTmpPix2,28(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,9,31,31
	rlwinm  rTmpPix2,rTmpPacked,10,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,32(rDstPix)
	stw     rTmpPix2,36(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,11,31,31
	rlwinm  rTmpPix2,rTmpPacked,12,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,40(rDstPix)
	stw     rTmpPix2,44(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,13,31,31
	rlwinm  rTmpPix2,rTmpPacked,14,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,48(rDstPix)
	stw     rTmpPix2,52(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,15,31,31
	rlwinm  rTmpPix2,rTmpPacked,16,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,56(rDstPix)
	stw     rTmpPix2,60(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,17,31,31
	rlwinm  rTmpPix2,rTmpPacked,18,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,64(rDstPix)
	stw     rTmpPix2,68(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,19,31,31
	rlwinm  rTmpPix2,rTmpPacked,20,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,72(rDstPix)
	stw     rTmpPix2,76(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,21,31,31
	rlwinm  rTmpPix2,rTmpPacked,22,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,80(rDstPix)
	stw     rTmpPix2,84(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,23,31,31
	rlwinm  rTmpPix2,rTmpPacked,24,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,88(rDstPix)
	stw     rTmpPix2,92(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,25,31,31
	rlwinm  rTmpPix2,rTmpPacked,26,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,96(rDstPix)
	stw     rTmpPix2,100(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,27,31,31
	rlwinm  rTmpPix2,rTmpPacked,28,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,104(rDstPix)
	stw     rTmpPix2,108(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,29,31,31
	rlwinm  rTmpPix2,rTmpPacked,30,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,112(rDstPix)
	stw     rTmpPix2,116(rDstPix)
	rlwinm  rTmpPix1,rTmpPacked,31,31,31
	rlwinm  rTmpPix2,rTmpPacked,0,31,31
	neg     rTmpPix1,rTmpPix1
	neg     rTmpPix2,rTmpPix2
	and     rTmpPix1,rTmpPix1,rColorXOR
	and     rTmpPix2,rTmpPix2,rColorXOR
	xor     rTmpPix1,rTmpPix1,rColor0
	xor     rTmpPix2,rTmpPix2,rColor0
	stw     rTmpPix1,120(rDstPix)
	stw     rTmpPix2,124(rDstPix)

	addi    rDstPix,rDstPix,128 ; avoiding stwu speeds up by 4%

	bdnz    @thirtytwopixels

	add     rDstPix,rDstPix,rDstRowSkip ; skip to next row
	add     rSrcPix,rSrcPix,rSrcRowSkip

	subi    rH,rH,1 ; decrement and check row counter
	cmpwi   rH,0
	bne     @row

	blr
