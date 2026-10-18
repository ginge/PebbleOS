/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

.syntax unified
.cpu cortex-m33
.thumb

.equ UART0_BASE, 0x44820000
.equ UART_FIFO_STATUS, 0x18
.equ UART_FIFO_PORT, 0x1c
.equ UART_TX_WR_READY, (1 << 20)
.equ MBOX_BASE, 0x41000000
.equ MBOX_CH0_FIFO_STATUS, 0x60
.equ MBOX_CH1_TX_DATA0, 0x88
.equ MBOX_CH1_TX_DATA1, 0x8c
.equ MBOX_CH1_TX_ID, 0x90
.equ MBOX_FIFO_FULL, 1
.equ CP_CONTROL_MESSAGE, 0x28010000
.equ CP_HEARTBEAT_PARAMETER, 0x28010020
.equ CP_POWER_UP_HEADER, 0x10010001
.equ CP_HEARTBEAT_HEADER, 0x10020002
.equ SCB_DCCMVAC, 0xe000ef68
.equ IO_TIMEOUT, 12000000

.section .isr_vector, "a", %progbits
.align 7
.global vector_table
vector_table:
  .word 0x28066000
  .word reset_handler
  .rept 62
  .word fault_handler
  .endr

.section .text.reset_handler, "ax", %progbits
.align 2
.global reset_handler
.type reset_handler, %function
.thumb_func
reset_handler:
  cpsid i
  ldr r4, =CP_POWER_UP_HEADER
  bl send_cp_control
  ldr r4, =UART0_BASE

1:
  adr r5, marker
2:
  ldrb r6, [r5], #1
  cbz r6, 4f
  ldr r0, =IO_TIMEOUT
3:
  ldr r7, [r4, #UART_FIFO_STATUS]
  tst r7, #UART_TX_WR_READY
  bne 6f
  subs r0, r0, #1
  bne 3b
  b 4f
6:
  str r6, [r4, #UART_FIFO_PORT]
  b 2b
4:
  ldr r0, =IO_TIMEOUT
5:
  subs r0, r0, #1
  bne 5b
  ldr r4, =CP_HEARTBEAT_HEADER
  bl send_cp_control
  ldr r4, =UART0_BASE
  b 1b

send_cp_control:
  ldr r0, =CP_CONTROL_MESSAGE
  str r4, [r0]
  movs r1, #0
  str r1, [r0, #4]
  str r1, [r0, #8]
  str r1, [r0, #12]
  ldr r2, =CP_HEARTBEAT_HEADER
  cmp r4, r2
  bne 7f
  ldr r2, =CP_HEARTBEAT_PARAMETER
  str r1, [r2]
  str r2, [r0, #4]
  movs r1, #4
  str r1, [r0, #8]
  ldr r3, =SCB_DCCMVAC
  str r2, [r3]
7:
  dsb sy
  ldr r3, =SCB_DCCMVAC
  str r0, [r3]
  dsb sy

  ldr r2, =MBOX_BASE
  ldr r3, =IO_TIMEOUT
8:
  ldr r1, [r2, #MBOX_CH0_FIFO_STATUS]
  tst r1, #MBOX_FIFO_FULL
  beq 9f
  subs r3, r3, #1
  bne 8b
  bx lr
9:
  str r0, [r2, #MBOX_CH1_TX_DATA0]
  movs r1, #16
  str r1, [r2, #MBOX_CH1_TX_DATA1]
  movs r1, #0
  str r1, [r2, #MBOX_CH1_TX_ID]
  dsb sy
  bx lr

.align 2
marker:
  .asciz "\r\nPBL-CPU1-RESET\r\n"
.size reset_handler, . - reset_handler

.type fault_handler, %function
.thumb_func
fault_handler:
  b fault_handler
.size fault_handler, . - fault_handler
