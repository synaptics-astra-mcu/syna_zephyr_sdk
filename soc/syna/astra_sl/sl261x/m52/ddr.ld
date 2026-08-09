SECTION_PROLOGUE(.ddr_noinit (NOLOAD),,)
{
    __ddr_noinit_start = .;
    KEEP(*(.noinit.*/subsys/net/ip/net_pkt*))
    __ddr_noinit_end = .;
} GROUP_LINK_IN(DDR)
