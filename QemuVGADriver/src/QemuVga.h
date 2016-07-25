#ifndef __QEMU_VGA_H__
#define __QEMU_VGA_H__

/* --- Qemu/Bochs special registers --- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* --- Internal APIs */

extern OSStatus	QemuVga_Init();
extern OSStatus	QemuVga_Exit();

extern OSStatus	QemuVga_Open();
extern OSStatus	QemuVga_Close();

extern void QemuVga_EnableInterrupts(void);
extern void QemuVga_DisableInterrupts(void);

extern OSStatus	QemuVga_SetDepth(UInt32 bpp);

extern OSStatus	QemuVga_SetColorEntry(UInt32 index, RGBColor *color);
extern OSStatus	QemuVga_GetColorEntry(UInt32 index, RGBColor *color);

extern OSStatus QemuVga_GetModeInfo(UInt32 index, UInt32 *width, UInt32 *height);
extern OSStatus QemuVga_SetMode(UInt32 modeIndex, UInt32 depth);

#endif
