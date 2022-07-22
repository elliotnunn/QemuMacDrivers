#include <Video.h>
#include "VideoDriverPrivate.h"
#include "logger.h"
#include "QemuVga.h"


/* List of video modes */
struct vMode *vModes;

/* Default modes if no EDID (must be 0 terminated) */
static struct _vMode defaultVModes[] =  {
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 1600, 1200 },
	{ 1920, 1080 },
	{ 1920, 1200 },
};

/* EDID standard timings */
static struct _vMode edidStdVModes[] = {
    {  800,  600 },    /* 800x600 @ 60Hz */
    {  800,  600 },    /* 800x600 @ 56Hz */
    {  640,  480 },    /* 640x480 @ 75Hz */
    {  640,  480 },    /* 640x480 @ 72Hz */
    {  640,  480 },    /* 640x480 @ 67Hz */
    {  640,  480 },    /* 640x480 @ 60Hz */
    {  720,  400 },    /* 720x400 @ 88Hz */
    {  720,  400 },    /* 720x400 @ 70Hz */
    
    { 1280, 1024 },    /* 1280x1024 @ 75Hz */
    { 1024,  768 },    /* 1024x768 @ 75Hz  */
    { 1024,  768 },    /* 1024x768 @ 72Hz  */
    { 1024,  768 },    /* 1024x768 @ 60Hz  */
    { 1024,  768 },    /* 1024x768 @ 87Hz  */
    {  832,  624 },    /*  832x624 @ 75Hz  */
    {  800,  600 },    /*  800x600 @ 75Hz  */
    {  800,  600 },    /*  800x600 @ 72Hz  */
    
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    { 1152,  870 },    /* 1152x870 @ 75Hz */
};

/* EDID extended standard timings */
static struct _vMode edidExtStdVModes[] = {
    { 1152,  864 },    /* 1152x864 @ 85Hz */
    { 1024,  768 },    /* 1024x768 @ 75Hz */
    {  800,  600 },    /*  800x600 @ 85Hz */
    {  848,  480 },    /*  848x480 @ 60Hz */
    {  640,  480 },    /*  640x480 @ 85Hz */
    {  720,  400 },    /*  720x400 @ 85Hz */
    {  640,  400 },    /*  640x400 @ 85Hz */
    {  640,  350 },    /*  640x350 @ 85Hz */
    
    { 1280, 1024 },    /* 1280x1024 @ 85Hz */
    { 1280, 1024 },    /* 1280x1024 @ 60Hz */
    { 1280,  960 },    /* 1280x960 @ 85Hz */
    { 1280,  960 },    /* 1280x960 @ 60Hz */
    { 1280,  768 },    /* 1280x768 @ 85Hz */
    { 1280,  768 },    /* 1280x768 @ 75Hz */
    { 1280,  768 },    /* 1280x768 @ 60Hz */
    { 1280,  768 },    /* 1280x768 @ 60Hz (CVT-RB) */

    { 1440, 1050 },    /* 1440x1050 @ 75Hz */
    { 1440, 1050 },    /* 1440x1050 @ 60Hz */
    { 1440, 1050 },    /* 1440x1050 @ 60Hz (CVT-RB) */
    { 1440,  900 },    /* 1440x900 @ 85Hz */
    { 1440,  900 },    /* 1440x900 @ 75Hz */
    { 1440,  900 },    /* 1440x900 @ 60Hz (CVT-RB) */
    { 1360,  768 },    /* 1360x768 @ 60Hz */
    { 1360,  768 },    /* 1360x768 @ 60Hz (CVT-RB) */

    { 1600, 1200 },    /* 1600x1200 @ 70Hz */
    { 1600, 1200 },    /* 1600x1200 @ 65Hz */
    { 1600, 1200 },    /* 1600x1200 @ 60Hz */
    { 1680, 1050 },    /* 1680x1050 @ 85Hz */
    { 1680, 1050 },    /* 1680x1050 @ 75Hz */
    { 1680, 1050 },    /* 1680x1050 @ 60Hz */
    { 1680, 1050 },    /* 1680x1050 @ 60Hz (CVT-RB) */
    { 1440, 1050 },    /* 1440x1050 @ 85Hz */
    
    { 1920, 1200 },    /* 1920x1200 @ 60Hz */
    { 1920, 1200 },    /* 1920x1200 @ 60Hz (CVT-RB) */
    { 1856, 1392 },    /* 1856x1392 @ 75Hz */
    { 1856, 1392 },    /* 1856x1392 @ 60Hz */
    { 1792, 1344 },    /* 1792x1344 @ 75Hz */
    { 1792, 1344 },    /* 1792x1344 @ 60Hz */
    { 1600, 1200 },    /* 1600x1200 @ 85Hz */
    { 1600, 1200 },    /* 1600x1200 @ 75Hz */
    
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    {    0,    0 },    /* Reserved */
    { 1920, 1440 },    /* 1920x1440 @ 75Hz */
    { 1920, 1440 },    /* 1920x1440 @ 60Hz */
    { 1920, 1200 },    /* 1920x1200 @ 75Hz */
    { 1920, 1200 },    /* 1920x1200 @ 60Hz */
};

/* EDID timing extension modes */
static struct _vMode edidTimingExtensionVModes[] = {
    {  640,  480 },    /* DMT0659 */
    {  720,  480 },    /* 480p */
    {  720,  480 },    /* 480pH */
    { 1280,  720 },    /* 720p */
    { 1920,  540 },    /* 1080i */
    { 1440,  240 },    /* 480i */
    { 1440,  240 },    /* 480iH */
    { 1440,  240 },    /* 240p */

    { 1440,  240 },    /* 240pH */
    { 2880,  240 },    /* 480i4x */
    { 2880,  240 },    /* 480i4xH */
    { 2880,  240 },    /* 240p4x */
    { 2880,  240 },    /* 240p4xH */
    { 1440,  480 },    /* 480p2x */
    { 1440,  480 },    /* 480p2xH */
    { 1920, 1080 },    /* 1080p */
    
    {  720,  576 },    /* 576p */
    {  720,  576 },    /* 576pH */
    { 1280,  720 },    /* 720p50 */
    { 1920,  540 },    /* 1080i25 */
    { 1440,  288 },    /* 576i */
    { 1440,  288 },    /* 576iH */
    { 1440,  288 },    /* 288p */
    { 1440,  288 },    /* 288pH */
    
    { 2880,  288 },    /* 576i4x */
    { 2880,  288 },    /* 576i4xH */
    { 2880,  288 },    /* 288p4x */
    { 2880,  288 },    /* 288p4xH */
    { 1440,  576 },    /* 576p2x */
    { 1440,  576 },    /* 576p2xH */
    { 1920, 1080 },    /* 1080p50 */
    { 1920, 1080 },    /* 1080p24 */
    
    { 1920, 1080 },    /* 1080p25 */
    { 1920, 1080 },    /* 1080p30 */
    { 2880,  240 },    /* 480p4x */
    { 2880,  240 },    /* 480p4xH */
    { 2880,  576 },    /* 576p4x */
    { 2880,  576 },    /* 576p4xH */
    { 1920,  540 },    /* 1080i25 */
    { 1920,  540 },    /* 1080i50 */
    
    { 1280,  720 },    /* 720p100 */
    {  720,  576 },    /* 576p100 */
    {  720,  576 },    /* 576p100H */
    { 1440,  576 },    /* 576i50 */
    { 1440,  576 },    /* 576i50H */
    { 1920,  540 },    /* 1080i60 */
    { 1280,  720 },    /* 720p120 */
    {  720,  576 },    /* 480p119 */
    
    {  720,  576 },    /* 480p119H */
    { 1440,  576 },    /* 480i59 */
    { 1440,  576 },    /* 480i59H */
    {  720,  576 },    /* 576p200 */
    {  720,  576 },    /* 576p200H */
    { 1440,  288 },    /* 576i100 */
    { 1440,  288 },    /* 576i100H */
    {  720,  480 },    /* 480p239 */

    {  720,  480 },    /* 480p239H */
    { 1440,  240 },    /* 480i119 */
    { 1440,  240 },    /* 480i119H */
    { 1280,  720 },    /* 720p24 */
    { 1280,  720 },    /* 720p25 */
    { 1280,  720 },    /* 720p30 */
    { 1920, 1080 },    /* 1080p120 */
    { 1920, 1080 },    /* 1080p100 */
    
    { 1280,  720 },    /* 720p24 */
    { 1280,  720 },    /* 720p25 */
    { 1280,  720 },    /* 720p30 */
    { 1280,  720 },    /* 720p50 */
    { 1650,  750 },    /* 720p */
    { 1280,  720 },    /* 720p100 */
    { 1280,  720 },    /* 720p120 */
    { 1920, 1080 },    /* 1080p24 */
    
    { 1920, 1280 },    /* 1080p25 */
    { 1920, 1280 },    /* 1080p30 */
    { 1920, 1280 },    /* 1080p50 */
    { 1920, 1280 },    /* 1080p */
    { 1920, 1280 },    /* 1080p100 */
    { 1920, 1280 },    /* 1080p120 */
    { 1680,  720 },    /* 720p2x24 */
    { 1680,  720 },    /* 720p2x25 */

    { 1680,  720 },    /* 720p2x30 */
    { 1680,  720 },    /* 720p2x50 */
    { 1680,  720 },    /* 720p2x */
    { 1680,  720 },    /* 720p2x100 */
    { 1680,  720 },    /* 720p2x120 */
    { 2560, 1080 },    /* 1080p2x24 */
    { 2560, 1080 },    /* 1080p2x25 */
    { 2560, 1080 },    /* 1080p2x30 */
    
    { 2560, 1080 },    /* 1080p2x50 */
    { 2560, 1080 },    /* 1080p2x */
    { 2560, 1080 },    /* 1080p2x100 */
    { 2560, 1080 },    /* 1080p2x120 */
    { 3840, 2160 },    /* 2160p24 */
    { 3840, 2160 },    /* 2160p25 */
    { 3840, 2160 },    /* 2160p30 */
    { 3840, 2160 },    /* 2160p50 */
    
    { 3840, 2160 },    /* 2160p */
    { 4096, 2160 },    /* 2160p24 */
    { 4096, 2160 },    /* 2160p25 */
    { 4096, 2160 },    /* 2160p30 */
    { 4096, 2160 },    /* 2160p50 */
    { 4096, 2160 },    /* 2160p */
    { 3840, 2160 },    /* 2160p24 */
    { 3840, 2160 },    /* 2160p25 */
    
    { 3840, 2160 },    /* 2160p30 */
    { 3840, 2160 },    /* 2160p50 */
    { 3840, 2160 },    /* 2160p */
    { 1280,  720 },    /* 720p48 */
    { 1280,  720 },    /* 720p48 */
    { 1680,  720 },    /* 720p2x48 */
    { 1920, 1280 },    /* 1080p48 */
    { 1920, 1280 },    /* 1080p48 */
    
    { 2560, 1080 },    /* 1080p2x48 */
    { 3840, 2160 },    /* 2160p48 */
    { 4096, 2160 },    /* 2160p48 */
    { 3840, 2160 },    /* 2160p48 */
    { 3840, 2160 },    /* 2160p100 */
    { 3840, 2160 },    /* 2160p120 */
    { 3840, 2160 },    /* 2160p100 */
    { 3840, 2160 },    /* 2160p120 */

    { 5120, 2160 },    /* 2160p2x24 */
    { 5120, 2160 },    /* 2160p2x25 */
    { 5120, 2160 },    /* 2160p2x30 */
    { 5120, 2160 },    /* 2160p2x48 */
    { 5120, 2160 },    /* 2160p2x50 */
    { 5120, 2160 },    /* 2160p2x */
    { 5120, 2160 },    /* 2160p2x100 */
    {    0,    0 },    /* Reserved */
};


struct _vMode *getVMode(UInt16 idx)
{
    struct vMode *v = vModes;
    UInt16 i = 0;

    while (v != NULL) {
        if (i == idx) {
            return v->mode;
        }
        
        v = v->next;
        i++;
    }

    return NULL;
}

static void dumpVModeList(void)
{
    struct vMode *v = vModes;

    while (v != NULL) {
        lprintf("   %d, %d\n", v->mode->width, v->mode->height);
        v = v->next;
    }
}

static UInt16 getVModeListSize(void)
{
    struct vMode *v = vModes;
    UInt16 i = 0;

    while (v != NULL) {
        v = v->next;
        i++;
    }

    return i;
}

static void addVModeToList(struct _vMode *vMode)
{
    /* Add mode to list */
    struct vMode **v = &vModes;
    struct vMode *n;
    
    while (*v != NULL) {
        /* Don't add mode if resulting framebuffer is too
           large for PCI MMIO space */
        if (vMode->width * vMode->height * 4 > GLOBAL.boardFBMappedSize) {
            return;
        }
           
        /* Don't add duplicate mode if it already exists */
        if ((*v)->mode->width == vMode->width &&
            (*v)->mode->height == vMode->height) {

            return;
        }

        /* Add mode in order */
        if (((*v)->mode->width == vMode->width &&        
            (*v)->mode->height > vMode->height) ||
            ((*v)->mode->width > vMode->width)) {

            n = PoolAllocateResident(sizeof(struct vMode), true);
            n->next = *v;
            n->mode = vMode;
            *v = n;

            return;
        }

        v = &(*v)->next;
    }

    n = PoolAllocateResident(sizeof(struct vMode), true);
    n->next = NULL;
    n->mode = vMode;
    *v = n;
}

static UInt8 EdidReadB(UInt16 port)
{
	UInt8 *ptr, val;
	
	ptr = (UInt8 *)((UInt32)GLOBAL.boardRegAddress + port);
	val = *ptr;
	SynchronizeIO();
	return val;
}

static UInt16 GetEdidSize()
{
    return 0x80 * ((UInt16)EdidReadB(0x7e) + 1);
}

static void ParseEdid(UInt8 *edid)
{
    int i, j, d, idx;

    /* Standard modes */
    lprintf("Standard modes: \n");
    for (i = 35; i <= 37; i++) {
        for (j = 0; j <= 7; j++) {
            if (edid[i] & (1 << j)) {
                idx = ((i - 35) << 3) + j;
                lprintf("   %d, %d\n", edidStdVModes[idx].width,
                        edidStdVModes[idx].height);
                addVModeToList(&edidStdVModes[idx]);
            }
        }
    }
    
    /* Extended standard modes */
    lprintf("Extended standard modes: \n");
    d = i = 54;
    while (d < 126) {
        /* Search for descriptor */
        if (edid[d] == 0x0 && edid[d + 1] == 0x0 && edid[d + 2] == 0x0 &&
            edid[d + 3] == 0xf7 && edid[d + 4] == 0x0) {

            for (i = d + 6; i <= d + 11; i++) {
                for (j = 0; j <= 7; j++) {
                    if (edid[i] & (1 << j)) {
                        idx = ((i - d - 6) << 3) + j;
                        lprintf("   %d, %d\n", edidExtStdVModes[idx].width,
                                edidExtStdVModes[idx].height);
                        addVModeToList(&edidExtStdVModes[idx]);
                    }
                }
            }

            break;
        }

        d += 18;
    }

    /* Extension block modes */
    lprintf("Extension block modes: \n");
    if (edid[126] > 0) {
        /* Search for descriptor */
        i = 128;
        if (edid[i] == 0x2 && edid[i + 1] == 0x3) {
            /* Data present: check for video block */
            d = edid[i + 2];
            if (d > 0x4 && (edid[i + 4] & 0xf0) == (2 << 5)) {
                for (j = 0; j < (edid[i + 4] & 0x1f); j++) {
                    idx = edid[(i + 5 + j)];
                    lprintf("   %d, %d\n", edidTimingExtensionVModes[idx].width,
                            edidTimingExtensionVModes[idx].height);                    
                    addVModeToList(&edidTimingExtensionVModes[idx]);
                }
            }
        }
    }
}

static void ParseDefaultVModes() {
    UInt8 defaultVModeCount;
	int i;
	
	defaultVModeCount = sizeof(defaultVModes) / sizeof(struct _vMode);

    lprintf("Default modes:\n");
	for (i = 0; i < defaultVModeCount; i++) {
	    lprintf("   %d, %d\n", defaultVModes[i].width, defaultVModes[i].height);
	    addVModeToList(&defaultVModes[i]);
	}
}

UInt16 QemuVga_ReadEdidModes(void)
{
    UInt16 edidSize = GetEdidSize();
    UInt8 *edid;
    int i;

    if (EdidReadB(0x0) != 0x0 || EdidReadB(0x1) != 0xff) {
        lprintf("No valid EDID detected, using default modes\n");
        ParseDefaultVModes();
        return getVModeListSize();
    } 

    lprintf("EDID size: %d bytes, reading modes from EDID...\n", edidSize);
    
    edid = (UInt8 *)PoolAllocateResident(edidSize, true);
    if (!edid)
         return;

    for (i = 0; i < edidSize; i++) {
        edid[i] = EdidReadB(i);
    }

    ParseEdid(edid);

    PoolDeallocate(edid);
    
    lprintf("Final mode list:\n");
    dumpVModeList();

    return getVModeListSize();
}
