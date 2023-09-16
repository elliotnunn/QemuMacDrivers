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
	panic.o \
	scc.o \

all: ../builds/qemu_vga.ndrv ../builds/9p.ndrv ../builds/bootstrap.img

../builds/bootstrap.img: ../builds/9p.ndrv ../mk-9p-bootstrapper.py
	../mk-9p-bootstrapper.py ~/Documents/mac/MacROMan/TestImages/New\ World\ ROM/2003-04-03\ -\ Mac\ OS\ ROM\ 10.2.1.rom $< $@

../builds/9p.ndrv: device-9p.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^
	cp $@ ../os9test/System\ Folder/Extensions/9p.ndrv || :

../builds/qemu_vga.ndrv: device-gpu.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -o $@ $^

device-gpu.so: device-gpu.o blit.o virgl.o dirtyrectpatch.o gammatables.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

device-9p.so: device-9p.o 9p.o hashtab.o paramblkprint.o unicode.o universalfcb.o ${COMMON_PPC}
	${CC} ${CFLAGS} -shared -Wl,-bE:ndrv.exp -o $@ $^ ${SYSLIBS}

clean:
	rm -f *.o *.so
