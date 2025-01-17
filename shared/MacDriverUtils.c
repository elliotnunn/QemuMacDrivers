/*
 * Various utilities for writing a MacOS "ndrv" driver, such as device-tree
 * helpers. These replace the various Apple sample codes whose licences
 * are dubious and probably not suitable for GPL code
 */

//#include "VideoDriverPrivate.h"
//#include "VideoDriverPrototypes.h"
#include "logger.h"
#include "MacDriverUtils.h"

/* Simplified DT properties accessors */ 
void *DTGetProp(RegEntryIDPtr		 dtNode,
				RegPropertyNamePtr	 name,
				RegPropertyValueSize *outSize)
{
	OSStatus err;
	RegPropertyValue *v;

	if (!outSize)
		return NULL;

	/* Grab size so we can allocate some memory */
	err = RegistryPropertyGetSize(dtNode, name, outSize);
	if (err)
		return NULL;
	
	/* Allocate */
	v = PoolAllocateResident(*outSize, FALSE);
	if (!v)
		return NULL;
	
	err = RegistryPropertyGet(dtNode, name, v, outSize);
	if (err)
		return NULL;
	
	return v;
}

void DTFreeProp(void *v)
{
	if (v)
		PoolDeallocate(v);
}

/* Find a BAR logical address and size */ 
LogicalAddress GetDeviceBARAddress(RegEntryIDPtr		dtNode,
								   PCIRegisterNumber	barOffset,
								   ByteCount			*size,
								   Boolean				*isIO)
{
		RegPropertyValueSize	aasize, lasize;
		LogicalAddress			*las = NULL;
		PCIAssignedAddress		*aas = NULL;
		LogicalAddress			result = 0;
		UInt32 					i;

		/* First grab assigned-addresses to find the BAR */
		aas = DTGetProp(dtNode, kPCIAssignedAddressProperty, &aasize);
		if (!aas)
			return NULL;
		aasize /= sizeof(*aas);

		/* Then grab AAPL,addresses to get the corresponding logical addresses */
		las = DTGetProp(dtNode, kAAPLDeviceLogicalAddress, &lasize);
		if (!las)
			goto bail;
		lasize /= sizeof(LogicalAddress);

		/* Lookup BAR */
		for (i = 0; i < aasize; i++) {
			struct PCIAssignedAddress *aa = aas + i;

			/* Skip config space */
			if (GetPCIAddressSpaceType(aa) == kPCIConfigSpace)
				continue;

			/* Check BAR offset */
			if (GetPCIRegisterNumber(aa) != barOffset)
				continue;
			
			/* Found it, check that it was assigned */
			if (aa->size.hi == 0 && aa->size.lo == 0) {
				lprintf("BAR %02x not assigned !\n");
				goto bail;	
			}

			/* Check we have a logical for it */
			if (i >= lasize) {
				lprintf("BAR %02x missing AAPL,address property !\n");
				goto bail;
			}

			/* We don't do 64-bit, sorry... */
			if (aa->size.hi) {
				lprintf("BAR %02x too big !\n");
				goto bail;
			}

			/* Gotcha ! */
			if (size)
				*size = aa->size.lo;
			if (isIO)
				*isIO = GetPCIAddressSpaceType(aa) == kPCIIOSpace;
			result = las[i];
			break;
		}
		
bail:
		DTFreeProp(aas);
		DTFreeProp(las);
		return result;
}


/* PCI "Command" config register address */
#define kPCIConfigCommandAddress	0x04
#define cwCommandEnableMemorySpace	0x0002

/* Enable access to PCI memory space */
OSStatus EnablePCIMemorySpace(RegEntryIDPtr dtNode)
{
	OSStatus status;
	UInt16 cmd;

	lprintf("Reading cmd word...\n");
	status = ExpMgrConfigReadWord(dtNode, (LogicalAddress)kPCIConfigCommandAddress, &cmd);
	if( status )
		return status;

	lprintf("cmad word is: %04x, writing update...\n", cmd);
	cmd |= cwCommandEnableMemorySpace;
 
	return ExpMgrConfigWriteWord(dtNode, (LogicalAddress)kPCIConfigCommandAddress, cmd);
}

OSStatus SetupPCIInterrupt(RegEntryID *dtNode, IRQInfo *info,
						   InterruptHandler handler, void *refCon)
{
	ISTProperty *ist;
	RegPropertyValueSize istSize;
	OSStatus err;

	ist = DTGetProp(dtNode, kISTPropertyName, &istSize);
	if (!ist) {
		lprintf("SetupPCIInterrupt: No %s property\n", kISTPropertyName);
		return paramErr;
	}
	info->interruptSetMember = (*ist)[kISTChipInterruptSource];
	DTFreeProp(ist);

	err = GetInterruptFunctions(info->interruptSetMember.setID,
							    info->interruptSetMember.member,
								&info->refCon,
								&info->handlerFunction,
								&info->enableFunction,
								&info->disableFunction);
	if (err) {
		lprintf("SetupPCIInterrupt: Error %d getting interrupt functions\n");
		return err;
	}
	err = InstallInterruptFunctions(info->interruptSetMember.setID, 
									info->interruptSetMember.member, 
									refCon, handler, NULL, NULL);
	if (err) {
		lprintf("SetupPCIInterrupt: Error %d setting interrupt functions\n");
		return err;
	}
	return noErr;
}