A bare-minimum virtual machine monitor using KVM hardware support in Linux.

Build with `make`, which will compile the VMM and assemble the 'BIOS'.

I wanted to create a basic VM host which I could use for testing and debugging, and also for a learning experience.

I found that documentation on KVM interfaces was quite sparse and inaccurate, so I followed the kernel source in order to work out the minimum required to get a VM running.

