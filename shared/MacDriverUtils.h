#ifndef __MAC_DRIVER_UTILS_H__
#define __MAC_DRIVER_UTILS_H__

#include <NameRegistry.h>
#include <DriverServices.h>
#include <PCI.h>

void *DTGetProp(RegEntryIDPtr		 dtNode,
				RegPropertyNamePtr	 name,
				RegPropertyValueSize *outSize);

void DTFreeProp(void *);

LogicalAddress GetDeviceBARAddress(RegEntryIDPtr		dtNode,
								   PCIRegisterNumber	barOffset,
								   ByteCount			*size,
								   Boolean				*isIO);

OSStatus EnablePCIMemorySpace(RegEntryIDPtr dtNode);

typedef struct IRQInfo {
  InterruptSetMember	  interruptSetMember;
  void*                   refCon;
  InterruptHandler        handlerFunction;
  InterruptEnabler        enableFunction;
  InterruptDisabler       disableFunction;	
} IRQInfo;

OSStatus SetupPCIInterrupt(RegEntryID *dtNode, IRQInfo *info,
						   InterruptHandler handler, void *refCon);

#endif /* __MAC_DRIVER_UTILS_H__ */
