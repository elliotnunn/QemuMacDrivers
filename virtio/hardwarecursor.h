#ifndef HARDWARECURSOR_H
#define HARDWARECURSOR_H

#include <Types.h>
#include <Video.h>

void InitHardwareCursor(void);

// Control/Status calls
OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec);
OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec);
OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec);
OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec);

#endif
