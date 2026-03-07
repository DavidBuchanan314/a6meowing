ENTRY(start)

MEMORY {
    MEM (rwx) : ORIGIN = 0x10000000, LENGTH = 0x10000
}

SECTIONS {
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        *(.bss)
        *(COMMON)
    } > MEM

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
