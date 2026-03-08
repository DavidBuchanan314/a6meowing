ENTRY(_start)

MEMORY {
    MEM (rwx) : ORIGIN = 0x0, LENGTH = 0x200
}

SECTIONS {
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
    } > MEM

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
