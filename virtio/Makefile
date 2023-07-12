CC = ${HOME}/Documents/mac/Retro68-build/toolchain/bin/powerpc-apple-macos-gcc
CFLAGS = -O3

SYSLIBS = \
	-lStdCLib \
	-lDriverServicesLib \
	-lMathLib \
	-lNameRegistryLib \
	-lPCILib \
	-lVideoServicesLib \
	-lInterfaceLib \

COMMON_PPC = \
	transport-ppc.o \
	virtqueue.o \
	allocator-ppc.o \
	atomic-ppc.o \
	lprintf.o \
	patch68k.o \
	scc.o \

.PHONY: ../bootstrap/systemfolder/tbxi.src/Parcels.src/9p.ndrv
../bootstrap/systemfolder/tbxi.src/Parcels.src/9p.ndrv: device-9p.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^
	cd ../bootstrap/systemfolder && tbxi build tbxi.src

../builds/qemu_vga.ndrv: device-gpu.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^

device-gpu.so: device-gpu.o blit.o virgl.o dirtyrectpatch.o gammatables.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

device-9p.so: device-9p.o rpc9p.o hashtab.o paramblkprint.o unicode.o extfs.o universalfcb.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

clean:
	rm -f *.o *.so
