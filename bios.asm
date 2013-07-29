bits 16
org 0

main:
	; setup stack
	mov ax,0x9000
	mov ss,ax
	mov sp,0xfffe

	call ints_init
	call serial_init
	call parallel_init

	call boot

	call break

boot:
	ret

ints_init:
	xor ax,ax
	mov es,ax

	; point all ints to unhandled
	mov cx,0x100
	mov bx,0xf000
	xor di,di
	mov ax,int_unhandled
ints_init_loop:
	mov [di],bx
	add di,2
	stosw
	dec cx
	loop ints_init_loop

	; setup int 0x10
	mov di,0x10*4
	mov word [di],0xf000
	mov word [di+2],int_10

	; setup int 0x13
	mov di,0x13*4
	mov word [di],0xf000
	mov word [di+2],int_13

	; setup int 0x15
	mov di,0x15*4
	mov word [di],0xf000
	mov word [di+2],int_15

	; setup int 0x16
	mov di,0x16*4
	mov word [di],0xf000
	mov word [di+2],int_16

	ret


serial_init:
	; set base address of serial ports
	mov di,0x0400

	mov ax,0x3f8
	mov [di],ax

	xor ax,ax
	mov [di+2],ax ; 2f8
	mov [di+4],ax ; 3e8
	mov [di+6],ax ; 2e8

	ret


parallel_init:
	; set base address of parallel ports
	mov di,0x0408

	mov ax,0x3bc
	mov [di],ax

	xor ax,ax
	mov [di+2],ax ; ?
	mov [di+4],ax ; ?
	mov [di+6],ax ; ?

	ret


int_unhandled:
	call break
	ret


int_10:
	cmp ah,0x00
	jz int_10_00

	cmp ah,0x0e
	jz int_10_0e

	; fallthrough
	ret

int_13:
	cmp ah,0x00
	jz int_13_00

	cmp ah,0x01
	jz int_13_01

	cmp ah,0x02
	jz int_13_02

	; fallthrough
	ret

int_15:
	cmp ah,0x24
	jz int_15_24

	; fallthrough
	ret

int_16:
	cmp ah,0x00
	jz int_16_00

	cmp ah,0x01
	jz int_16_01

	cmp ah,0x02
	jz int_16_02

	cmp ah,0x03
	jz int_16_03

	; fallthrough
	ret


; set video mode
int_10_00:
	ret

; putchar
; AL = char
int_10_0e:
	ret


; DISK - RESET DISK SYSTEM
; DL - drive (if bit 7 is set both hard disks and floppy disks reset)
; return:
; CF clear if success (AH=0)
; CF set on error
int_13_00:
	ret


; DISK - GET STATUS OF LAST OPERATION
; DL - drive (if bit 7 is set both hard disks and floppy disks reset)
; return:
; CF set on error
; AH = status of previous operation
int_13_01:
	ret


; DISK - READ SECTOR(S) INTO MEMORY
; AL = number of sectors to read (must be nonzero)
; CH = low eight bits of cylinder number
; CL = sector number 1-63 (bits 0-5)
;  high two bits of cylinder (bits 6-7, hard disk only)
; DH = head number
; DL = drive number (bit 7 set for hard disk)
; ES:BX -> data buffer
; Return: CF set on error
; if AH = 11h (corrected ECC error), AL = burst length
; CF clear if successful
; AH = status
; AL = number of sectors transferred
int_13_02:
	ret

; set A20 line
; AL = A20 line state
int_15_24:
	ret

; query IST (Intel SpeedStep) support
int_15_e980:
	cmp edx, 0x47534943
	jnz int_15_e980_exit
	;mov eax, signature
	;mov ebx, command
	;mov ecx, event
	;mov edx, perf_level
int_15_e980_exit:
	ret


; get keyboard char
int_16_00:
	ret

; get keyboard char available
int_16_01:
	ret

; get keyboard state
int_16_02:
	ret

; set keyboard repeat rate
int_16_03:
	ret


break:
	out dx,ax

padding_entry:
	times 0xfff0-($-$$) db 0

entry:
	jmp 0xf000:0

date:
	db "20130128"

correct_checksum:
	db 0

machine_type:
	db 0xfc

unknown:
	db 0
