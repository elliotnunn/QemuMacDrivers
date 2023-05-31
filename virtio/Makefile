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
	scc.o \

../os9test/systemfolder/Extensions/9p.ndrv: device-9p.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^

../builds/qemu_vga.ndrv: device-gpu.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^

device-gpu.so: device-gpu.o blit.o virgl.o debugpollpatch.o dirtyrectpatch.o \
gammatables.o lateboothook.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

device-9p.so: device-9p.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

clean:
	rm -f *.o *.so
