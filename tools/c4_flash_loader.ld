ENTRY(loader_entry)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0xF00 }
SECTIONS {
    .text : { KEEP(*(.entry)) *(.text*) *(.rodata*) } > RAM
    .data : { *(.data*) } > RAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) }
}
