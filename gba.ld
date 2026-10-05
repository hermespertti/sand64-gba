/* GBA linker: ROM at 0x08000000, code+data in IWRAM (fast), .crt0 first */
MEMORY
{
	EWRAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
	IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
	ROM   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
}

SECTIONS
{
	. = ORIGIN(ROM);
	.crt0 : { *(.crt0) } > ROM

	.text : {
		. = ALIGN(4);
		*(.text .text.*)
		. = ALIGN(4);
	} > ROM

	.rodata : {
		. = ALIGN(4);
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > ROM

	__data_lma = LOADADDR(.data);

	.data : {
		. = ALIGN(4);
		__data_vma = .;
		*(.data .data.*)
		. = ALIGN(4);
		__data_end = .;
	} > IWRAM AT > ROM

	.bss (NOLOAD) : {
		. = ALIGN(4);
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > IWRAM

	/* big buffers -> EWRAM */
	.ewram (NOLOAD) : {
		. = ALIGN(4);
		*(.ewram .ewram.*)
		. = ALIGN(4);
	} > EWRAM

	DISCARD : { *(.note .note.* .comment) }
}
