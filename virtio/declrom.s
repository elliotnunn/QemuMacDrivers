        .macro MacsbugSymbol Name
        .byte 0x7F + _macsbugSymbol\@ - .
        .ascii "\Name"
_macsbugSymbol\@:
        .align 2
        .word 0 /* length of constants */
        .endm
        /* random defines and macros that we would normally get from MPW */
        .macro OSLstEntry Id,Offset
        .long (\Id<<24)+((\Offset-.)&0xFFFFFF)
        .endm
        .macro DatLstEntry Id,Data
        .long (\Id<<24)+(\Data&0xFFFFFF)
        .endm
        .set sCPU_68020, 2
        .set endOfList, 255
        .set sRsrcType, 1
        .set sRsrcName, 2
        .set sRsrcIcon, 3
        .set sRsrcDrvrDir, 4
        .set sRsrcFlags, 7
        .set sRsrcHWDevId, 8
        .set minorBase, 10
        .set minorLength, 11
        .set majorBase, 12
        .set majorLength, 13
        .set sRsrcCicn, 15
        .set boardId, 32
        .set primaryInit, 34
        .set vendorInfo, 36
        .set sGammaDir, 64
        .set vendorId, 1
        .set revLevel, 3
        .set partNum, 4
        .set mVidParams, 1
        .set mPageCnt, 3
        .set mDevType, 4
        .set oneBitMode, 128
        .set twoBitMode, 129
        .set fourBitMode, 130
        .set eightBitMode, 131
        .set sixteenBitMode, 132
        .set thirtyTwoBitMode, 133
        .set catBoard, 1
        .set catDisplay, 3
        .set typeBoard, 0
        .set typeVideo, 1
        .set typeVideo, 1
        .set typeDesk, 2
        .set drSwMacCPU, 0
        .set catCPU, 10
        /* SpBlock; parameter block for Slot Manager routines */
        .struct 0
spResult: .space 4
spsPointer: .space 4
spSize: .space 4
spOffsetData: .space 4
spIOFileName: .space 4
spsExecPBlk: .space 4
spParamData: .space 4
spMisc: .space 4
spReserved: .space 4
spIOReserved: .space 2
spRefNum: .space 2
spCategory: .space 2
spCType: .space 2
spDrvrSW: .space 2
spDrvrHW: .space 2
spTBMask: .space 1
spSlot: .space 1
spID: .space 1
spExtDev: .space 1
spHwDev: .space 1
spByteLanes: .space 1
spFlags: .space 1
spKey: .space 1
spBlock.size:
        /* SEBlock; parameter block for SExec blocks (like PrimaryInit) */
        .struct 0
seSlot: .space 1
sesRsrcId: .space 1
seStatus: .space 2
seFlags: .space 1
seFiller0: .space 1
seFiller1: .space 1
seFiller2: .space 1
seResult: .space 4
seIOFileName: .space 4
seDevice: .space 1
sePartition: .space 1
seOSType: .space 1
seReserved: .space 1
seRefNum: .space 1
seNumDevices: .space 1
seBootState: .space 1
filler: .space 1
SEBlock.size:
        .set JIODone, 0x08FC
        .set JVBLTask, 0x0D28
        .set ioTrap, 6
        .set ioResult, 16
        .set noQueueBit, 9
        .set dCtlSlot, 40




        .global _DeclHeader,qfb_interrupt_service_routine
        .global _GammaTableMac, _GammaTableMac_Name
        .global _GammaTableSrgb, _GammaTableSrgb_Name
        .global _GammaTableLinear, _GammaTableLinear_Name
        .global _GammaTableNTSC, _GammaTableNTSC_Name
        .global _GammaTableSGI, _GammaTableSGI_Name
        .global _GammaTablePAL, _GammaTablePAL_Name

        /* defines that have to do with our ROM */
        .set ourBoardID, 0x9545 /* hope no real board used this! */
        /* ID of the board resource, must be in the range 0-127 */
        .set sResource_Board_ID, 1
        /* ID of the video resource, must be in the range 128-254 */
        .set sResource_Video_ID, 128

        .section .text

ROMSTART:

        /* sResource directory, the list of all the sResources on the ROM */
        /* Note: our ROM header assumes this is the first thing in the ROM! */
_sResourceDirectory:
        OSLstEntry sResource_Board_ID, _sRsrc_Board
        OSLstEntry sResource_Video_ID, _sRsrc_Generique
        DatLstEntry endOfList, 0
_sRsrc_Board:
        OSLstEntry sRsrcType, _BoardTypeRec
        OSLstEntry sRsrcName, _BoardName
        OSLstEntry sRsrcIcon, _Icon
        OSLstEntry sRsrcCicn, _Cicn
        DatLstEntry boardId, ourBoardID
        OSLstEntry primaryInit, _PrimaryInitRec
        OSLstEntry vendorInfo, _VendorInfoRec
        DatLstEntry endOfList, 0

_BoardTypeRec:
        .short catBoard /* Category: Board! */
        .short typeBoard /* Type: Board! */
        .short 0 /* DrvrSw: 0, because it's a board */
        .short 0 /* DrvrHw: 0, because... it's a board */
_BoardName:
        .asciz "Elliot's dodgy driver"

_PrimaryInitRec:
        .long _PrimaryInitRecEnd-_PrimaryInitRec
        .byte 2 /* code revision? */
        .byte sCPU_68020 /* CPU type */
        .short 0 /* reserved */
        .long 4 /* offset to code */




        .set QFB_VERSION, 0x0
        .set QFB_MODE_WIDTH, 0x4
        .set QFB_MODE_HEIGHT, 0x8
        .set QFB_MODE_DEPTH, 0xC
        .set QFB_MODE_BASE, 0x10
        .set QFB_MODE_STRIDE, 0x14
        .set QFB_PAL_INDEX, 0x1C
        .set QFB_PAL_COLOR, 0x20
        .set QFB_LUT_INDEX, 0x24
        .set QFB_LUT_COLOR, 0x28
        .set QFB_IRQ_MASK, 0x2C
        .set QFB_IRQ, 0x30
        .set QFB_CUSTOM_WIDTH, 0x34
        .set QFB_CUSTOM_HEIGHT, 0x38
        .set QFB_CUSTOM_DEPTH, 0x3C
        .set QFB_IRQ_VBL, 1
        /* A0: Address of an SEBlock, our parameters */
        CLR.W seStatus(%A0) /* presume success */
        /* save non-volatile registers */
        MOVEM.L %A2/%A3/%D2, -(%SP)
        /* A3 := address of control registers */
        /* (we should use the SResources to determine this, but it's really
           inconvenient to do that in a primary init) */
        CLR.L %D0
        MOVE.B seSlot(%A0), %D0
        ORI.L #0xF0, %D0
        MOVEQ.L #24, %D1
        LSL.L %D1, %D0
        MOVEA.L %D0, %A3
        /* Check that a QFB device is present */
        MOVE.L #0x71666231, %D0
        CMP.L QFB_VERSION(%A3), %D0
        BNE returnErr
        /* Reset */
        MOVE.L #0, QFB_VERSION(%A3)
        /* Disable interrupts (which reset should have already done)*/
        CLR.L QFB_IRQ_MASK(%A3)
        MOVE.L #QFB_IRQ_VBL, QFB_IRQ(%A3)
        /* Set up the requested custom mode, at 1-bpp */
        MOVE.L QFB_CUSTOM_WIDTH(%A3), QFB_MODE_WIDTH(%A3)
        MOVE.L QFB_CUSTOM_HEIGHT(%A3), QFB_MODE_HEIGHT(%A3)
        MOVE.L #1, QFB_MODE_DEPTH(%A3)
        MOVE.L #0, QFB_MODE_BASE(%A3)
        /* Put in colors */
        CLR.L QFB_PAL_INDEX(%A3)
        MOVE.L #0xFFFFFF, QFB_PAL_COLOR(%A3)
        MOVE.L #1, QFB_PAL_INDEX(%A3)
        CLR.L QFB_PAL_COLOR(%A3)
        /* We don't need to do anything to the gamma ramp, resetting the card
           makes it linear */
        /* A2 := address of video RAM */
        CLR.L %D0
        MOVE.B seSlot(%A0), %D0
        MOVEQ.L #28, %D1
        LSL.L %D1, %D0
        MOVEA.L %D0, %A2
        /* Let's fill the screen with gray! */
        MOVE.L #0xAAAAAAAA, %D2
        MOVE.L QFB_MODE_HEIGHT(%A3), %D1
        CMP.L #2160, %D1
        BLS 1f
        MOVE.L #2160, %D1
1:
_screenFillLoopOuter:
        MOVE.L QFB_MODE_STRIDE(%A3), %D0
        CMP.L #16384, %D0
        BLS 1f
        MOVE.L #16384, %D0
1:      MOVE.L %A2, %A1
_screenFillLoopInner:
        MOVE.L %D2, (%A1)+
        SUBQ.L #4, %D0
        BGT _screenFillLoopInner
        ADD.L QFB_MODE_STRIDE(%A3), %A2
        NOT.L %D2
        SUBQ.L #1, %D1
        BGT _screenFillLoopOuter
        /* now it gets real... now it's time to patch our sResources */
        /* ...or it would be if QEMU hadn't done it for us. */
        /* make room on the stack for the spBlock */
        SUBA #spBlock.size, %SP
        /* put a pointer to our brand new Slot Manager parameter block in A0 */
        MOVE %SP, %A0
        /* deallocate stack-allocated spBlock */
        ADDA #spBlock.size, %SP
        /* restore registers */
1:      MOVEM.L (%SP)+, %A2/%A3/%D2
        RTS
returnErr:
        MOVE.W #0, seStatus(%A0)
        BRA 1b
        /* note that this code will only be correct with a fairly recent ROM
           (ha ha), so if you add support for older Macs to QEMU, or want to
           use this driver in another emulator, you'll need to pay attention to
           some gnarly issues. see "Designing Cards & Drivers for the
           Macintosh Family" and, as always, the various Inside Macintoshes. */




_PrimaryInitRecEnd:

_VendorInfoRec:
        OSLstEntry vendorId, _VendorId
        OSLstEntry revLevel, _RevLevel
        OSLstEntry partNum, _PartNum
        DatLstEntry endOfList, 0

_VendorId:
        .asciz "QEMU"
_RevLevel:
        .asciz "1.0"
_PartNum:
        .asciz "QFB0"

_sRsrc_Generique:
        OSLstEntry sRsrcType, _VideoTypeRec
        OSLstEntry sRsrcName, _VideoName
        OSLstEntry sRsrcDrvrDir, _VideoDriverDirectory
        DatLstEntry sRsrcFlags, 2 /* open at start, use 32-bit addressing */
        DatLstEntry sRsrcHWDevId, 1



        /* Now we need sResource records for every bit depth */
        DatLstEntry endOfList, 0

_VideoTypeRec:
        .short catCPU/* Category: Display */
        .short typeDesk /* Type: Video */
        .short drSwMacCPU
        .short 0

_VideoName:
        /* the video name is derived from the above, and must take this form */
        /* special note, the third element is the driver software interface,
           NOT the vendor of the board */
        .asciz "ElliotThing"

/* is this MinorBaseOS? */
_MinorBaseRec:
        .long 0 /* offset of video device within our slot space */
_MinorLengthRec:
        .long 0x10000000 /* size of our video device */
        /* it's a slight fib, since the declaration ROM is at the end of that,
           but the Slot Manager won't care :) */

_MajorBaseRec:
        .long 0 /* offset of video RAM within our super slot space */
_MajorLengthRec:
        .long 32 << 20 /* size of our video RAM, 32MiB */

_VideoDriverDirectory:
        OSLstEntry sCPU_68020, _DRVRBlock
        DatLstEntry endOfList, 0

_GammaDirectory:
        DatLstEntry endOfList, 0

        .macro ModeResource name, paramLink, type
\name\():
        OSLstEntry mVidParams, \paramLink
        DatLstEntry mPageCnt, 0x4545 /* will be patched by QEMU */
        DatLstEntry mDevType, \type /* 0 = clut, 1 = fixed CLUT, 2 = direct */
        DatLstEntry endOfList, 0
        .endm
        .macro ModeParams type, bpp, cpp, bpc, name
\name\():
        .long \name\()End-\name\() /* size of block */
        .long 0x10000 /* offset within device memory */
        .short 0x4545 /* bytes per row, will be patched by QEMU */
        /* bounds (top, left, bottom, right) */
        .short 0
        .short 0
        .short 0x4545 /* height, will be patched by QEMU */
        .short 0x4545 /* width, will be patched by QEMU */
        .short 1 /* version (always 1) */
        .short 0 /* packType (not used) */
        .long 0 /* packSize (not used) */
        .long 72 << 16 /* 72 dots per inch horizontally */
        .long 72 << 16 /* again vertically */
        .short \type /* 0 = chunky indexed, 16 = chunky direct */
        .short \bpp /* bits per pixel */
        .short \cpp /* components per pixel */
        .short \bpc /* bits per component */
        .long 0 /* "plane bytes" (reserved) */
\name\()End:
        .endm
        ModeResource _OneBitRec, _OneBitParams, 0
        ModeResource _TwoBitRec, _TwoBitParams, 0
        ModeResource _FourBitRec, _FourBitParams, 0
        ModeResource _EightBitRec, _EightBitParams, 0
        ModeResource _SixteenBitRec, _SixteenBitParams, 2
        ModeResource _ThirtyTwoBitRec, _ThirtyTwoBitParams, 2
        ModeParams 0, 1, 1, 1, _OneBitParams
        ModeParams 0, 2, 1, 2, _TwoBitParams
        ModeParams 0, 4, 1, 4, _FourBitParams
        ModeParams 0, 8, 1, 8, _EightBitParams
        ModeParams 16, 16, 3, 5, _SixteenBitParams
        ModeParams 16, 32, 3, 8, _ThirtyTwoBitParams

        /* The icon! */
_Icon:  .long 0x000FF000,0x007FFE00,0x01FFFF80,0x03E3FFC0,0x07C01FE0,0x0FC00FF0
        .long 0x1FC1CFF8,0x3F81C7FC,0x3F0001FC,0x7F03C07E,0x7F003F3E,0x7E0000BE
        .long 0xFE003FFF,0xFE007FFF,0xFE01FFFF,0xFE01FFFF,0xFF81FFFF,0xFFC0FFFF
        .long 0xFFF0FFFF,0xFFF87FFF,0x7FFC7FFE,0x7FFE7FFE,0x7FFE3FFE,0x3FFF3FFC
        .long 0x3FFF3FFC,0x1FFFBFF8,0x0FFFBFF0,0x07FFFFF8,0x03FFFFF8,0x01FFFFFC
        .long 0x007FFE7C,0x000FF03E
        /* Now in color! */
_Cicn:  .long _CicnEnd-_Cicn
        .long 0x00000000,0x80100000,0x00000020,0x00200000,0x00000000,0x00000048
        .long 0x00000048,0x00000000,0x00040001,0x00040000,0x00000000,0x00000000
        .long 0x00000000,0x00000004,0x00000000,0x00200020,0x00000000,0x00040000
        .long 0x00000020,0x00200000,0x0000000F,0xF000007F,0xFE0001FF,0xFF8003FF
        .long 0xFFC007FF,0xFFE00FFF,0xFFF01FFF,0xFFF83FFF,0xFFFC3FFF,0xFFFC7FFF
        .long 0xFFFE7FFF,0xFFFE7FFF,0xFFFEFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF7FFF,0xFFFE7FFF
        .long 0xFFFE7FFF,0xFFFE3FFF,0xFFFC3FFF,0xFFFC1FFF,0xFFF80FFF,0xFFF007FF
        .long 0xFFF803FF,0xFFF801FF,0xFFFC007F,0xFE7C000F,0xF03EFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFE3,0xFFFFFFC0,0x1FFFFFC0,0x0FFFFFC1,0xCFFFFF81
        .long 0xC7FFFF00,0x01FFFF03,0xC07FFF00,0x3F3FFE00,0x00BFFE00,0x3FFFFE00
        .long 0x7FFFFE01,0xFFFFFE01,0xFFFFFF81,0xFFFFFFC0,0xFFFFFFF0,0xFFFFFFF8
        .long 0x7FFFFFFC,0x7FFFFFFE,0x7FFFFFFE,0x3FFFFFFF,0x3FFFFFFF,0x3FFFFFFF
        .long 0xBFFFFFFF,0xBFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFF0000,0x00000000,0x000A0000,0xFFFF6666,0x33330001,0xCCCCCCCC
        .long 0xCCCC0002,0xBBBBBBBB,0xBBBB0003,0xAAAAAAAA,0xAAAA0004,0x88888888
        .long 0x88880005,0x77777777,0x77770006,0x55555555,0x55550007,0x44444444
        .long 0x44440008,0x22222222,0x22220009,0x11111111,0x1111000F,0x00000000
        .long 0x0000FFFF,0xFFFFFFFF,0x11111111,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF12
        .long 0x12121212,0x12FFFFFF,0xFFFFFFFF,0xFFFF2222,0x22222222,0x2222FFFF
        .long 0xFFFFFFFF,0xFFF23230,0x00323232,0x32323FFF,0xFFFFFFFF,0xFF333300
        .long 0x00000003,0x333333FF,0xFFFFFFFF,0xFF434300,0x00000000,0x434343FF
        .long 0xFFFFFFFF,0xF4444400,0x000FFF00,0x4444444F,0xFFFFFFFF,0xF4545000
        .long 0x000FFF00,0x0454545F,0xFFFFFFFF,0xF5550000,0x00000000,0x0005555F
        .long 0xFFFFFFFF,0xF5650000,0x00FFFF00,0x0000056F,0xFFFFFFFF,0xFF660000
        .long 0x000000FF,0xFFFF00FF,0xFFFFFFFF,0xFF700000,0x00000000,0x0000F0FF
        .long 0xFFFFFFFF,0xFFF00000,0x00000077,0x77777FFF,0xFFFFFFFF,0xFFF00000
        .long 0x00000787,0x8787FFFF,0xFFFFFFFF,0xFFF00000,0x00088888,0x88FFFFFF
        .long 0xFFFFFFFF,0xFFF00000,0x00089898,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF000
        .long 0x000FFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF00,0x0000FFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0x0000FFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xF0000FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFF000FFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFF00FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFF000FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFF0FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF0FF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF
        .byte 0xFF
        .align 2
_CicnEnd:

        /* Now for the DRIVER! */
        /* THIS MUST BE THE LAST THING IN THE `.text.begin` SECTION! */
_DRVRBlock:
        .long _DRVREnd-_DRVRBlock
		.incbin "build-drvr/device-input.drvr"
        .align 2
_DRVREnd:
        .byte 0
        .byte 0

_DeclHeader:
        .long (_sResourceDirectory-.) & 0xffffff
        .long ROMEND-ROMSTART
        .long 0 /* Checksum goes here */
        .byte 1 /* ROM format revision */
        .byte 1 /* Apple format */
        .long 0x5A932BC7 /* Magic number */
        .byte 0 /* Must be zero */
        .byte 0x0F /* use all four byte lanes */
ROMEND:

        .end
