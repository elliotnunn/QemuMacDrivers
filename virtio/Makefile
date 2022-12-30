# This is an MPW makefile
# Run: mps -c 'Make >MakeOut && MakeOut'

COptions = -i :polyfill -proto strict
PPCCOptions = {COptions}

::builds:qemu_vga.ndrv ƒ ∂
                         allocator-ppc.c.x ∂
                         atomic-ppc.c.x ∂
                         blit.c.x ∂
                         blit.s.x ∂
                         debugpollpatch.c.x ∂
                         dirtyrectpatch.c.x ∂
                         gammatables.c.x ∂
                         lateboothook.c.x ∂
                         lprintf.s.x ∂
                         transport-ppc.c.x ∂
                         virgl.c.x ∂
                         virtqueue.c.x ∂
                         wait.s.x ∂
                         device-gpu.c.x
	PPCLink -xm sharedlibrary ∂
		-export TheDriverDescription,DoDriverIO ∂
		-t ndrv -c '????' ∂
		-o {Targ} ∂
		-weaklib InterfaceLib -d ∂
		"{PPCLibraries}PPCCRuntime.o" ∂
		"{SharedLibraries}DriverServicesLib" ∂
		"{SharedLibraries}InterfaceLib" ∂
		"{SharedLibraries}MathLib" ∂
		"{SharedLibraries}NameRegistryLib" ∂
		"{SharedLibraries}PCILib" ∂
		"{SharedLibraries}StdCLib" ∂
		"{SharedLibraries}VideoServicesLib" ∂
		{Deps}

::builds:pci1af4,1059 ƒ ∂
                        allocator-ppc.c.x ∂
                        lprintf.s.x ∂
                        transport-ppc.c.x ∂
                        virtqueue.c.x ∂
                        device-snd.c.x
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

::builds:pci1af4,1009 ƒ 9p.c.x transport-ndrv.c.x lprintf.s.x
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
