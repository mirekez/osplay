INCLUDE kernel/arch/riscv32/linker.ld
SECTIONS {
  .tohost 0x83000000 : ALIGN(64) { KEEP(*(.tohost)) }
  .rocket_disk 0x84000000 : ALIGN(4096) {
    __rocket_disk_begin = .;
    KEEP(*(.rocket_disk))
    __rocket_disk_end = .;
  }
}
