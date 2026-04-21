.section .text, "ax"
.align 4
.global xtensa_context_switch
.type xtensa_context_switch, @function

# a2 = old context pointer
# a3 = new context pointer
xtensa_context_switch:
  s32i a0,  a2, 0
  s32i a1,  a2, 4
  s32i a12, a2, 8
  s32i a13, a2, 12
  s32i a14, a2, 16
  s32i a15, a2, 20

  l32i a0,  a3, 0
  l32i a1,  a3, 4
  l32i a12, a3, 8
  l32i a13, a3, 12
  l32i a14, a3, 16
  l32i a15, a3, 20

  ret
