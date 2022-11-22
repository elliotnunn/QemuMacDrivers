# This is an MPW makefile
# Run: mps -c 'Make >MakeOut && MakeOut'

COptions = -i :polyfill -proto strict
PPCCOptions = {COptions}

::builds:qemu_vga.ndrv ƒ gpu.c.x viotransport-ndrv.c.x lprintf.s.x hardwarecursor.c.x debugpollpatch.c.x dirtyrectpatch.c.x gammatables.c.x
	PPCLink -xm sharedlibrary ∂
		-export TheDriverDescription,DoDriverIO ∂
		-t ndrv -c '????' ∂
		-o {Targ} ∂
		"{SharedLibraries}StdCLib" ∂
		"{SharedLibraries}InterfaceLib" ∂
		"{SharedLibraries}MathLib" ∂
		"{SharedLibraries}DriverServicesLib" ∂
		"{SharedLibraries}VideoServicesLib" ∂
		"{SharedLibraries}NameRegistryLib" ∂
		"{SharedLibraries}PCILib" ∂
		"{PPCLibraries}PPCCRuntime.o" ∂
		{Deps}

::builds:pci1af4,1009 ƒ 9p.c.x viotransport-ndrv.c.x lprintf.s.x
	PPCLink -xm sharedlibrary ∂
		-export TheDriverDescription,DoDriverIO ∂
		-t ndrv -c '????' ∂
		-o {Targ} ∂
		"{SharedLibraries}StdCLib" ∂
		"{SharedLibraries}DriverServicesLib" ∂
		"{SharedLibraries}NameRegistryLib" ∂
		"{SharedLibraries}PCILib" ∂
		"{PPCLibraries}PPCCRuntime.o" ∂
		{Deps}
