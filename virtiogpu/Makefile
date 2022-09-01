# This is an MPW makefile
# Run: mps -c 'Make >MakeOut && MakeOut'

COptions = -i :stdint -proto strict
PPCCOptions = {COptions}

::builds:qemu_vga.ndrv ƒ viotransport-ndrv.c.x viodevice-gpu.c.x lprintf.s.x
	PPCLink -xm sharedlibrary ∂
		-export TheDriverDescription,DoDriverIO ∂
		-t ndrv -c '????' ∂
		-o {Targ} ∂
		"{SharedLibraries}StdCLib" ∂
		"{SharedLibraries}DriverServicesLib" ∂
		"{SharedLibraries}VideoServicesLib" ∂
		"{SharedLibraries}NameRegistryLib" ∂
		"{SharedLibraries}PCILib" ∂
		"{PPCLibraries}PPCCRuntime.o" ∂
		{Deps}
