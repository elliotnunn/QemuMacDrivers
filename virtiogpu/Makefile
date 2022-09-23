# This is an MPW makefile
# Run: mps -c 'Make >MakeOut && MakeOut'

COptions = -i :polyfill -proto strict
PPCCOptions = {COptions}

::builds:qemu_vga.ndrv ƒ gpu.c.x viotransport-ndrv.c.x lprintf.s.x debugpollpatch.c.x
	PPCLink -xm sharedlibrary ∂
		-export TheDriverDescription,DoDriverIO ∂
		-t ndrv -c '????' ∂
		-o {Targ} ∂
		"{SharedLibraries}StdCLib" ∂
		"{SharedLibraries}InterfaceLib" ∂
		"{SharedLibraries}DriverServicesLib" ∂
		"{SharedLibraries}VideoServicesLib" ∂
		"{SharedLibraries}NameRegistryLib" ∂
		"{SharedLibraries}PCILib" ∂
		"{PPCLibraries}PPCCRuntime.o" ∂
		{Deps}
