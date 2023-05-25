CC = ${HOME}/Documents/mac/Retro68-build/toolchain/bin/powerpc-apple-macos-gcc
CFLAGS =

../builds/qemu_vga.ndrv: all.so
	${HOME}/Documents/mac/Retro68/PEFTools/MakePEF -v $^ -o $@
	~/Documents/mac/tbxi-patches/cfmtool.py $@ /tmp/dmp

all.so: allocator-ppc.o atomic-ppc.o blit.o debugpollpatch.o dirtyrectpatch.o gammatables.o lateboothook.o transport-ppc.o virgl.o virtqueue.o device-gpu.o lprintf.o
	${CC} -fdce -shared    -Wl,-bE:library.exp    $^ -o $@      -lDriverServicesLib -lMathLib -lNameRegistryLib -lPCILib -lVideoServicesLib -lStdCLib

clean:
	rm -f *.o
