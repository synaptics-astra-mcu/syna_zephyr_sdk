SECTION_PROLOGUE(.npu_noinit (NOLOAD),,)
{
    __npu_noinit_start = .;
    KEEP(*(.noinit.*/subsys/net/ip/net_pkt*))
    __npu_noinit_end = .;
} GROUP_LINK_IN(NPU)
