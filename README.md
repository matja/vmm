A bare-minimum virtual machine monitor using KVM hardware support in Linux.

Build with `make`, which will compile the VMM and assemble the 'BIOS'.

This creates a VM with 16MB of memory, and loads the real-mode BIOS to
0xf0000 in guest memory, which is also mapped to 0xffff0000 to handle the
default x86 power-on BIST entry point.

Execution continues inside the virtual BIOS, setting up interrupt handlers
and structures for serial and parallel ports, before writing to an I/O port
to cause a vmexit to trap execution.

I wanted to create a basic VM host which I could use for testing and debugging,
and also for a learning experience.

I found that documentation on KVM interfaces was quite sparse and inaccurate,
so I followed the kernel source in order to work out the minimum required to
get a VM running.
