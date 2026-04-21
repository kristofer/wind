.section .text.start, "ax"
.align 4
.global _start
.type _start, @function

_start:
  movi a1, _stack_top
  call0 xtensa_kernel_main
1:
  j 1b
