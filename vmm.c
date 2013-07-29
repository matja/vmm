#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "apicdef.h"

#define FD_INVALID (-1)

typedef struct KVMHost_t KVMHost;
typedef struct KVMVM_t KVMVM;

struct KVMHost_t
{
	int kvm_fd;
	struct kvm_msr_list msr_list_temp, *msr_list;
};

struct KVMVM_t
{
	KVMHost *kvm;
	struct kvm_run *kvm_run;
	int vm_fd;
	int vcpu_fd;
	unsigned char *ram;
	unsigned ram_align_bits;
	unsigned ram_size;
};

KVMHost *KVMHost_create(void)
{
	KVMHost *self = (KVMHost *)calloc(1, sizeof(*self));

	self->kvm_fd = FD_INVALID;

	return self;
}

int KVMHost_init(KVMHost *self)
{
	static const char kvm_name[] = "/dev/kvm";
	int kvm_ioctl = 0;

	self->kvm_fd = open(kvm_name, O_RDWR);
	if (self->kvm_fd < 0) {
		perror("open kvm");
		return 0;
	}

	kvm_ioctl = ioctl(self->kvm_fd, KVM_GET_API_VERSION, 0);

	memset(&self->msr_list_temp, 0, sizeof(self->msr_list_temp));
	self->msr_list = &self->msr_list_temp;
	kvm_ioctl = ioctl(self->kvm_fd, KVM_GET_MSR_INDEX_LIST, &self->msr_list_temp);

	// if any MSR's, then ioctl will fail with E2BIG
	if (kvm_ioctl < 0 && self->msr_list_temp.nmsrs > 0) {
		// allocate enough memory for the full MSR list,
		// now that kvm has told us how many there are
		size_t msr_list_size = sizeof(*self->msr_list) +
			sizeof(self->msr_list->indices[0]) * self->msr_list->nmsrs;
		self->msr_list = calloc(1, msr_list_size);
		self->msr_list->nmsrs = self->msr_list_temp.nmsrs;

		// and try again
		kvm_ioctl = ioctl(self->kvm_fd, KVM_GET_MSR_INDEX_LIST, self->msr_list);
		if (kvm_ioctl < 0) {
			printf("failed to get msrs\n");
		}
	}
}

KVMVM *KVMHost_createVM(KVMHost *self)
{
	KVMVM *vm = (KVMVM *)calloc(1, sizeof(*vm));
	int kvm_ioctl = 0;
	int vm_ioctl = 0;
	struct kvm_userspace_memory_region umr;
	struct kvm_pit_config pit_config;
	int int_result;

	vm->kvm = self;
	vm->vm_fd = FD_INVALID;
	vm->vcpu_fd = FD_INVALID;
	vm->ram = NULL;
	vm->ram_size = 64*1024*1024;
	vm->ram_align_bits = 21;

	// create VM
	kvm_ioctl = ioctl(self->kvm_fd, KVM_CREATE_VM, 0);
	if (kvm_ioctl < 0) {
		perror("kvm create vm");
		return NULL;
	}

	// save VM FD
	vm->vm_fd = kvm_ioctl;

	// check if we need to set TSS (Intel bug)
	kvm_ioctl = ioctl(self->kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_SET_TSS_ADDR);
	if (kvm_ioctl) {
		vm_ioctl = ioctl(vm->vm_fd, KVM_SET_TSS_ADDR, 0xfffbd000);
		if (vm_ioctl < 0) {
			perror("KVM_SET_TSS_ADDR");
			return NULL;
		}
	}

	// create a PIT
	memset(&pit_config, 0, sizeof(pit_config));
	vm_ioctl = ioctl(vm->vm_fd, KVM_CREATE_PIT2, &pit_config);
	if (vm_ioctl < 0) {
		perror("KVM_CREATE_PIT2");
		return NULL;
	}

	// create in-kernel IRQ handler
	vm_ioctl = ioctl(vm->vm_fd, KVM_CREATE_IRQCHIP, 0);
	if (vm_ioctl < 0) {
		perror("KVM_CREATE_IRQCHIP");
		return NULL;
	}

	// setup vm memory space
	int_result = posix_memalign((void **)&vm->ram, 1 << vm->ram_align_bits, vm->ram_size);
	if (int_result != 0) {
		perror("posix_memalign");
		return NULL;
	}

	// clear memory
	memset(vm->ram, 0, vm->ram_size);

	memset(&umr, 0, sizeof(umr));
	umr.slot = 0;
	umr.flags = 0;
	umr.guest_phys_addr = 0x0;
	umr.memory_size = vm->ram_size;
	umr.userspace_addr = (unsigned long)vm->ram;

	vm_ioctl = ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &umr);
	if (vm_ioctl < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
		return NULL;
	}

	return vm;
}

int KVMVM_createVCPU(KVMVM *self)
{
	int vm_ioctl;
	int vcpu_ioctl;
	struct local_apic lapic;

	// create VCPU for this VM
	vm_ioctl = ioctl(self->vm_fd, KVM_CREATE_VCPU, 0);
	if (vm_ioctl < 0) {
		perror("KVM_CREATE_VCPU");
		return 0;
	}
	self->vcpu_fd = vm_ioctl;

	// get the existing LAPIC for the VCPU
	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_GET_LAPIC, &lapic);
	if (vm_ioctl < 0) {
		perror("KVM_GET_LAPIC");
		return 0;
	}

	// handle NMI and external interrupts in-kernel
	lapic.lvt_lint0.delivery_mode = APIC_MODE_EXTINT;
	lapic.lvt_lint1.delivery_mode = APIC_MODE_NMI;

	// set new LAPIC flags for this VCPU
	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_SET_LAPIC, &lapic);
	if (vm_ioctl < 0) {
		perror("KVM_SET_LAPIC");
		return 0;
	}

	return 1;
}

void KVMHost_destroy(KVMHost *self)
{
	if (self->kvm_fd != FD_INVALID) {
		close(self->kvm_fd);
		self->kvm_fd = FD_INVALID;
	}
}

void KVMVM_destroy(KVMVM *self)
{
	if (self->vm_fd != FD_INVALID) {
		close(self->vm_fd);
		self->vm_fd = FD_INVALID;
	}
}

int KVMVM_mapControl(KVMVM *self)
{
	int vm_ioctl = -1;
	unsigned mmap_size;

	// get the size of the vcpu control structure
	vm_ioctl = ioctl(self->kvm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vm_ioctl < 0) {
		perror("vm get vcpu mmap size");
		return 0;
	}
	mmap_size = vm_ioctl;

	// get a pointer to the vcpu control structure
	self->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, self->vcpu_fd, 0);
	if (self->kvm_run == MAP_FAILED) {
		perror("mmap kvm vcpu");
		return 0;
	}
}

static void dump_vcpu_regs(const struct kvm_regs *r)
{
	printf("rax:%016llx rbx:%016llx\n", r->rax, r->rbx);
	printf("rcx:%016llx rdx:%016llx\n", r->rcx, r->rdx);
	printf("rsi:%016llx rdi:%016llx\n", r->rsi, r->rdi);
	printf("rsp:%016llx rbp:%016llx\n", r->rsp, r->rbp);
	printf("r8 :%016llx r9 :%016llx\n", r->r8, r->r9);
	printf("r10:%016llx r11:%016llx\n", r->r10, r->r11);
	printf("r12:%016llx r13:%016llx\n", r->r12, r->r13);
	printf("r14:%016llx r15:%016llx\n", r->r14, r->r15);
	printf("rip:%016llx rflags:%016llx\n", r->rip, r->rflags);
}

static int kvm_segment_format(char *buffer, size_t size, const struct kvm_segment *s)
{
	return snprintf(buffer, size
		,"%016llx +%08x sel:%04x t:%02x"
		" p:%u dpl:%u db:%u s:%u l:%u g:%u a:%u"
		,(unsigned long long)s->base
		,(unsigned)s->limit
		,(unsigned)s->selector
		,(unsigned)s->type
		,(unsigned)s->present
		,(unsigned)s->dpl
		,(unsigned)s->db
		,(unsigned)s->s
		,(unsigned)s->l
		,(unsigned)s->g
		,(unsigned)s->avl
	);
}

static int kvm_dtable_format(char *buffer, size_t size, const struct kvm_dtable *dt)
{
	return snprintf(buffer, size
		,"base:%016llx limit:%08x"
		,dt->base
		,dt->limit
	);
}

static void dump_vcpu_sregs(const struct kvm_sregs *r)
{
	char buffer[256];

	kvm_segment_format(buffer, sizeof(buffer), &r->cs);
	printf("cs : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->ds);
	printf("ds : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->es);
	printf("es : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->fs);
	printf("fs : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->gs);
	printf("gs : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->ss);
	printf("ss : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->tr);
	printf("tr : %s\n", buffer);
	kvm_segment_format(buffer, sizeof(buffer), &r->ldt);
	printf("ldt: %s\n", buffer);

	kvm_dtable_format(buffer, sizeof(buffer), &r->gdt);
	printf("gdt: %s\n", buffer);
	kvm_dtable_format(buffer, sizeof(buffer), &r->idt);
	printf("idt: %s\n", buffer);

	printf("cr0:%016llx cr2:%016llx\n", r->cr0, r->cr2);
	printf("cr3:%016llx cr4:%016llx\n", r->cr3, r->cr4);
	printf("cr8:%016llx\n", r->cr8);

	printf("efer:%016llx apic_base:%016llx\n", r->efer, r->apic_base);
// r->interrupt_bitmap
}

int KVMVM_reset(KVMVM *self)
{
	struct kvm_regs r;
	struct kvm_sregs sr;
	int vm_ioctl;
	int vcpu_ioctl;

	// seems to not be needed, kvm sets a valid (?) initial state.
	return 1;

	memset(&r, 0, sizeof(r));
	memset(&sr, 0, sizeof(sr));

	r.rdx = 0x623; // CPUID
	r.rip = 0xfff0;

	sr.cs.base = 0xffff0000;
	sr.cs.limit = 0xffff;
	sr.cs.selector = 0xf000;
	sr.cs.type = 3;
	sr.cs.present = 1;
	sr.cs.dpl = 0;
	sr.cs.s = 0;
	sr.cs.l = 0;
	sr.cs.g = 1;
	sr.cs.avl = 1;

	sr.cr0 = 0x60000010;
	sr.apic_base = 0xfee00000;

	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_SET_REGS, &r);
	if (vcpu_ioctl < 0) {
		perror("ioctl");
		printf("%s: failed to set regs\n", __func__);
		return 0;	}

	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_SET_SREGS, &sr);
	if (vcpu_ioctl < 0) {
		perror("ioctl");
		printf("%s: failed to set sregs\n", __func__);
		return 0;
	}

	return 1;
}

int KVMVM_dumpRegisters(KVMVM *self)
{
	int vm_ioctl = -1;
	int vcpu_ioctl = -1;
	unsigned mmap_size = 0;
	struct kvm_regs kvm_regs;
	struct kvm_sregs kvm_sregs;

	// get the initial vcpu register state
	memset(&kvm_regs, 0, sizeof(kvm_regs));
	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_GET_REGS, &kvm_regs);
	if (vcpu_ioctl < 0) {
		perror("kvm regs");
		return 0;
	}
	dump_vcpu_regs(&kvm_regs);

	// get the initial vcpu special register state
	memset(&kvm_sregs, 0, sizeof(kvm_sregs));
	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_GET_SREGS, &kvm_sregs);
	if (vcpu_ioctl < 0) {
		perror("kvm sregs");
		return 0;
	}
	dump_vcpu_sregs(&kvm_sregs);

	return 1;
}

void KVMVM_handleExitIO(KVMVM *self)
{
	/* a KVM_EXIT_IO is returned for each byte which is output, least
	significant byte first, with increasing port number for the number of bytes
	which have been output, same as real hardware.

	io.count     : number of bytes in data (1 on x86)
	io.size      : depending on opcode (1/2/4/8)
	io.port      : IO port
	io.data      : data (1 on x86)
	io.direction : IN (0) or OUT (1)
	*/

	unsigned char *data =
		(unsigned char *)self->kvm_run + self->kvm_run->io.data_offset;

	const char *direction_string;

	switch (self->kvm_run->io.direction) {
		case KVM_EXIT_IO_IN  : direction_string = "IN";  break;
		case KVM_EXIT_IO_OUT : direction_string = "OUT"; break;
		default : direction_string = "<unknown>"; break;
	}

	printf(
		"KVM_EXIT_IO."
		" direction:%u (%s) size:%u port:%u count:%u data:%02x\n"
		,(unsigned)self->kvm_run->io.direction
		,direction_string
		,(unsigned)self->kvm_run->io.size
		,(unsigned)self->kvm_run->io.port
		,(unsigned)self->kvm_run->io.count
		,(unsigned)data[0]
	);
}

int KVMVM_loadBIOS(KVMVM *self, const char *bios_filename)
{
	FILE *file = fopen(bios_filename, "r");

	if (!file) {
		printf("failed to load BIOS [%s]\n", bios_filename);
		return 0;
	}

	fread(((char *)self->ram) + 0xf0000, 0x10000, 1, file);
	fclose(file);

	return 1;
}

int KVMVM_run(KVMVM *self)
{
	int vm_ioctl;
	int vcpu_ioctl;

	vcpu_ioctl = ioctl(self->vcpu_fd, KVM_RUN, 0);

	printf("KVM_run = %d\n", vm_ioctl);
	printf("kvm_run { exit_reason:%d }\n", self->kvm_run->exit_reason);

	switch (self->kvm_run->exit_reason) {
		case KVM_EXIT_UNKNOWN : {
			printf(
				"kvm_run exited with KVM_EXIT_UNKNOWN."
				" hardware_exit_reason:%llu\n"
				,self->kvm_run->hw.hardware_exit_reason
			);
			break;
		}
		case KVM_EXIT_INTERNAL_ERROR : {
			printf(
				"kvm_run exited with KVM_EXIT_INTERNAL_ERROR."
				" hardware_exit_reason:%llu\n"
				,self->kvm_run->hw.hardware_exit_reason
			);
			break;
		}
		case KVM_EXIT_FAIL_ENTRY : {
			printf(
				"kvm_run exited with KVM_EXIT_FAIL_ENTRY."
				" hardware_entry_failure_reason:%llu\n"
				,self->kvm_run->fail_entry.hardware_entry_failure_reason
			);
			break;
		}
		case KVM_EXIT_EXCEPTION : {
			printf(
				"kvm_run exited with KVM_EXIT_EXCEPTION."
				" exception:%u error_code:%u\n"
				,self->kvm_run->ex.exception
				,self->kvm_run->ex.error_code
			);
			break;
		}
		case KVM_EXIT_IO : {
			KVMVM_handleExitIO(self);
			break;
		}
		default : {
			printf("unhandled kvm_run exit (%d)\n"
				,self->kvm_run->exit_reason
			);
			break;
		}
	}

	return 0;
}

int main(void)
{
	KVMHost *kvm = NULL;
	KVMVM *vm = NULL;
	int result = 0;

	// create KVM context
	kvm = KVMHost_create();
	if (!kvm) {
		printf("cannot create kvm context\n");
		result = EXIT_FAILURE;
		goto cleanup;
	}

	KVMHost_init(kvm);

	// create VM
	vm = KVMHost_createVM(kvm);
	if (!vm) {
		printf("cannot create vm\n");
		result = EXIT_FAILURE;
		goto cleanup;
	}

	KVMVM_loadBIOS(vm, "bios.bin");

	KVMVM_createVCPU(vm);
	KVMVM_mapControl(vm);

	//KVMVM_reset(vm);
	KVMVM_dumpRegisters(vm);

	KVMVM_run(vm);
	KVMVM_dumpRegisters(vm);

	result = EXIT_SUCCESS;

	{
		unsigned addr;
		for (addr=0; addr<0xa0000; addr++) {
			if (vm->ram[addr]) {
				printf("%08x: %02x\n", addr, vm->ram[addr]);
			}
		}
	}

cleanup:
	if (vm) {
		KVMVM_destroy(vm);
	}
	if (kvm) {
		KVMHost_destroy(kvm);
	}

	return result;
}

