/*
 * usb_0xA1_2 handler for s5l8950x (ARMv7 / Apple A6)
 *
 * Based on usb_0xA1_2_armv7.S from ipwndfu by axi0mX
 *
 * USB callback handler that intercepts 0xA1 control requests
 * to provide exec/memcpy/memset functionality over USB.
 * Non-0xA1 requests are forwarded to the original ROM handler
 * via the Thumb trampoline at offset 0.
 */

.text

.syntax unified
.thumb

@ s5l8950x constants
.set LOAD_ADDRESS,   0x10020000
.set EXEC_MAGIC,     0x65786563
.set DONE_MAGIC,     0x646F6E65
.set MEMC_MAGIC,     0x6D656D63
.set MEMS_MAGIC,     0x6D656D73
.set USB_CORE_DO_IO, 0x7621

.global _start
_start:

@ 16-byte trampoline, filled at runtime by install_usb_0xA1_2_handler().
@
@ Bytes  0..7:  saved original instructions from handle_interface_request
@               (push {r4,r5,r7,lr}; add r7,sp,#8; ldrb r3,[r0,#7]; ldrb r2,[r0,#6])
@ Bytes  8..15: LDR.W PC,[PC] + resume address (0x8169) to branch past
@               the 8-byte hook and continue in the original ROM function.
@
@ Non-0xA1 requests take BNE here, replay the overwritten prologue,
@ then jump back into ROM at handle_interface_request+8.
trampoline:
  .space 16

@ Handler entry point (offset +16, Thumb entry = offset +0x11)
  LDRH R2, [R0]
  MOVW R3, #0x2A1
  CMP  R2, R3
  BNE  trampoline

  PUSH {R4-R7,LR}
  ADD  R7, SP, #0xC
  SUB  SP, SP, #0x10

  MOV  R4, R0
  LDR  R5, =LOAD_ADDRESS

  MOVW R1, #0xFFFF
  LDRH R2, [R4,#2]
  CMP  R1, R2
  BNE  request_done

  LDRD  R0, R1, [R5]

  LDR  R2, =EXEC_MAGIC
  CMP  R0, R2
  BNE  not_exec
  CMP  R1, R2
  BNE  not_exec

  MOV  R1, #0
  STRD R1, R1, [R5]

  LDRD R0, R1, [R5, #0x20]
  LDRD R2, R3, [R5, #0x28]
  STRD R0, R1, [SP]
  STRD R2, R3, [SP, #0x8]

  LDRD R0, R1, [R5, #0x10]
  LDRD R2, R3, [R5, #0x18]

  LDR  R6, [R5, #0x8]
  BLX  R6

  LDR  R2, =DONE_MAGIC
  STRD R0, R1, [R5,#0x8]
  STRD R2, R2, [R5]

not_exec:
  LDR  R2, =MEMC_MAGIC
  CMP  R0, R2
  BNE  not_memc
  CMP  R1, R2
  BNE  not_memc

  MOV  R1, #0
  STRD R1, R1, [R5]

  LDRD R0, R1, [R5, #0x10]
  LDR  R2, [R5, #0x18]
  BL   memcpy

  LDR  R2, =DONE_MAGIC
  STRD R2, R2, [R5]
  B    request_done

not_memc:
  LDR  R2, =MEMS_MAGIC
  CMP  R0, R2
  BNE  request_done
  CMP  R1, R2
  BNE  request_done

  MOV  R1, #0
  STRD R1, R1, [R5]

  LDRD R0, R1, [R5, #0x10]
  LDR  R2, [R5, #0x18]
  BL   memset

  LDR  R2, =DONE_MAGIC
  STRD R2, R2, [R5]

request_done:
  MOV  R0, #0x80
  MOV  R1, R5
  LDRH R2, [R4,#6]
  MOV  R3, #0
  LDR  R4, =USB_CORE_DO_IO
  BLX  R4

  MOV  R0, #0
  ADD  SP, SP, #0x10
  POP  {R4-R7,PC}

@ ---------------------------------------------------------------------------
@ Local memcpy: R0=dest, R1=src, R2=len
@ ---------------------------------------------------------------------------
memcpy:
  CMP  R2, #4
  BCC  memcpy_2

  LDR  R3, [R1]
  STR  R3, [R0]
  ADD  R0, R0, #4
  ADD  R1, R1, #4
  SUB  R2, R2, #4
  B    memcpy

memcpy_2:
  CMP  R2, #2
  BCC  memcpy_1

  LDRH R3, [R1]
  STRH R3, [R0]
  ADD  R0, R0, #2
  ADD  R1, R1, #2
  SUB  R2, R2, #2

memcpy_1:
  CBZ  R2, memcpy_done

  LDRB R3, [R1]
  STRB R3, [R0]
  ADD  R0, R0, #1
  ADD  R1, R1, #1
  SUB  R2, R2, #1

memcpy_done:
  BX   LR

@ ---------------------------------------------------------------------------
@ Local memset: R0=dest, R1=val, R2=len
@ ---------------------------------------------------------------------------
memset:
  MOV  R3, #0xFF
  AND  R1, R1, R3
  LSL  R3, R1, #8
  ORR  R1, R1, R3
  LSL  R3, R1, #16
  ORR  R1, R1, R3

memset_4:
  CMP  R2, #4
  BCC  memset_2

  STR  R1, [R0]
  ADD  R0, R0, #4
  SUB  R2, R2, #4
  B    memset_4

memset_2:
  CMP  R2, #2
  BCC  memset_1

  STRH R1, [R0]
  ADD  R0, R0, #2
  SUB  R2, R2, #2

memset_1:
  CBZ  R2, memset_done

  STRB R1, [R0]
  ADD  R0, R0, #1
  SUB  R2, R2, #1

memset_done:
  BX   LR

.pool
