#ifndef XV6_XTENSA_PORT_H
#define XV6_XTENSA_PORT_H

#include "kernel/types.h"

#define XTENSA_CPU_HZ 80000000U
#define XTENSA_TICK_HZ 1000U

struct xtensa_context {
  uint32 a0;
  uint32 a1;
  uint32 a12;
  uint32 a13;
  uint32 a14;
  uint32 a15;
};

void xtensa_context_switch(struct xtensa_context *old, struct xtensa_context *new);
void xtensa_kernel_init(void);
void xtensa_kernel_poll(void);
void xtensa_kernel_main(void);

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

void timer_init(uint32 cpu_hz, uint32 tick_hz);
uint32 timer_ticks(void);
void xtensa_timer_interrupt(void);

#endif // XV6_XTENSA_PORT_H
