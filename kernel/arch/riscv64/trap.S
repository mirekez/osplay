.equ FRAME_SIZE, 560

.section .text, "ax"
.align 4
.global trap_entry
.type trap_entry, @function
trap_entry:
  csrrw sp, mscratch, sp
  addi sp, sp, -FRAME_SIZE

  sd zero, 0(sp)
  sd ra, 8(sp)
  sd t0, 40(sp)
  csrr t0, mscratch
  sd t0, 16(sp)
  sd gp, 24(sp)
  sd tp, 32(sp)
  sd t1, 48(sp)
  sd t2, 56(sp)
  sd s0, 64(sp)
  sd s1, 72(sp)
  sd a0, 80(sp)
  sd a1, 88(sp)
  sd a2, 96(sp)
  sd a3, 104(sp)
  sd a4, 112(sp)
  sd a5, 120(sp)
  sd a6, 128(sp)
  sd a7, 136(sp)
  sd s2, 144(sp)
  sd s3, 152(sp)
  sd s4, 160(sp)
  sd s5, 168(sp)
  sd s6, 176(sp)
  sd s7, 184(sp)
  sd s8, 192(sp)
  sd s9, 200(sp)
  sd s10, 208(sp)
  sd s11, 216(sp)
  sd t3, 224(sp)
  sd t4, 232(sp)
  sd t5, 240(sp)
  sd t6, 248(sp)

  csrr t0, mepc
  sd t0, 256(sp)
  csrr t0, mstatus
  sd t0, 264(sp)
  csrr t0, mcause
  sd t0, 272(sp)
  csrr t0, mtval
  sd t0, 280(sp)

  fsd f0, 288(sp)
  fsd f1, 296(sp)
  fsd f2, 304(sp)
  fsd f3, 312(sp)
  fsd f4, 320(sp)
  fsd f5, 328(sp)
  fsd f6, 336(sp)
  fsd f7, 344(sp)
  fsd f8, 352(sp)
  fsd f9, 360(sp)
  fsd f10, 368(sp)
  fsd f11, 376(sp)
  fsd f12, 384(sp)
  fsd f13, 392(sp)
  fsd f14, 400(sp)
  fsd f15, 408(sp)
  fsd f16, 416(sp)
  fsd f17, 424(sp)
  fsd f18, 432(sp)
  fsd f19, 440(sp)
  fsd f20, 448(sp)
  fsd f21, 456(sp)
  fsd f22, 464(sp)
  fsd f23, 472(sp)
  fsd f24, 480(sp)
  fsd f25, 488(sp)
  fsd f26, 496(sp)
  fsd f27, 504(sp)
  fsd f28, 512(sp)
  fsd f29, 520(sp)
  fsd f30, 528(sp)
  fsd f31, 536(sp)
  csrr t0, fcsr
  sd t0, 544(sp)

  mv a0, sp
  call trap_handler
#ifdef OSPLAY_ROCKET
  /* exec and signal trampolines can have written instruction memory. */
  fence.i
#endif

  fld f0, 288(sp)
  fld f1, 296(sp)
  fld f2, 304(sp)
  fld f3, 312(sp)
  fld f4, 320(sp)
  fld f5, 328(sp)
  fld f6, 336(sp)
  fld f7, 344(sp)
  fld f8, 352(sp)
  fld f9, 360(sp)
  fld f10, 368(sp)
  fld f11, 376(sp)
  fld f12, 384(sp)
  fld f13, 392(sp)
  fld f14, 400(sp)
  fld f15, 408(sp)
  fld f16, 416(sp)
  fld f17, 424(sp)
  fld f18, 432(sp)
  fld f19, 440(sp)
  fld f20, 448(sp)
  fld f21, 456(sp)
  fld f22, 464(sp)
  fld f23, 472(sp)
  fld f24, 480(sp)
  fld f25, 488(sp)
  fld f26, 496(sp)
  fld f27, 504(sp)
  fld f28, 512(sp)
  fld f29, 520(sp)
  fld f30, 528(sp)
  fld f31, 536(sp)
  ld t0, 544(sp)
  csrw fcsr, t0

  ld t0, 256(sp)
  csrw mepc, t0
  ld t0, 264(sp)
  csrw mstatus, t0

  ld t0, 16(sp)
  csrw mscratch, t0

  ld ra, 8(sp)
  ld gp, 24(sp)
  ld tp, 32(sp)
  ld t1, 48(sp)
  ld t2, 56(sp)
  ld s0, 64(sp)
  ld s1, 72(sp)
  ld a0, 80(sp)
  ld a1, 88(sp)
  ld a2, 96(sp)
  ld a3, 104(sp)
  ld a4, 112(sp)
  ld a5, 120(sp)
  ld a6, 128(sp)
  ld a7, 136(sp)
  ld s2, 144(sp)
  ld s3, 152(sp)
  ld s4, 160(sp)
  ld s5, 168(sp)
  ld s6, 176(sp)
  ld s7, 184(sp)
  ld s8, 192(sp)
  ld s9, 200(sp)
  ld s10, 208(sp)
  ld s11, 216(sp)
  ld t3, 224(sp)
  ld t4, 232(sp)
  ld t5, 240(sp)
  ld t6, 248(sp)
  ld t0, 40(sp)

  addi sp, sp, FRAME_SIZE
  csrrw sp, mscratch, sp
  mret
.size trap_entry, . - trap_entry
