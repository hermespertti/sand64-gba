/* minimal GBA crt0: entry @0x00, IRQ vector @0x18, title @0xA0.
 * NOTE: arm7tdmi = ARMv4T: no cps instruction (use msr), no .org tricks
 * misaligning code. Nintendo logo skipped (BIOS-less boot). */
	.cpu	arm7tdmi
	.arm
	.section .crt0, "ax"
	.global _start
	.balign 4

_start:
	b	reset

	.org 0x18
	ldr	pc, =IRQvector

	.org 0xA0
	.ascii "SANDFALL GBA"	/* 12-byte game title */
	.org 0xB2
	.byte 0x96			/* fixed byte (BIOS check, cosmetic without BIOS) */

	.balign 4
reset:
	/* supervisor mode, IRQ+FIQ disabled */
	msr	cpsr_c, #0xDF

	/* supervisor stack at top of IWRAM */
	ldr	sp, =0x03007FE0

	/* copy .data LMA->VMA */
	ldr	r0, =__data_lma
	ldr	r1, =__data_vma
	ldr	r2, =__data_end
1:	cmp	r1, r2
	ldrlo	r3, [r0], #4
	strlo	r3, [r1], #4
	blo	1b

	/* clear .bss */
	ldr	r1, =__bss_start
	ldr	r2, =__bss_end
	mov	r3, #0
2:	cmp	r1, r2
	strlo	r3, [r1], #4
	blo	2b

	b	main

	.balign 4
IRQvector:
	/* stackless ack: read IF, write back, return */
	ldr	r0, =0x04000208
	ldr	r1, [r0]
	str	r1, [r0]
	subs	pc, lr, #4
