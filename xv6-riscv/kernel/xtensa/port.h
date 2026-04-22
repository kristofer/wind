#ifndef XV6_XTENSA_PORT_H
#define XV6_XTENSA_PORT_H

#include "kernel/types.h"

#define XTENSA_CPU_HZ 80000000U
#define XTENSA_TICK_HZ 1000U
#define WIND_SYSCALL_YIELD 1U

struct xtensa_trapframe {
  uint32 syscall_no;
  uint32 arg0;
  uint32 retval;
};

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
void xtensa_memory_init(void);
void *xtensa_page_alloc(void);
void xtensa_page_free(void *page);
uint32 xtensa_memory_total_pages(void);
uint32 xtensa_memory_free_pages(void);
void xtensa_proc_service_init(void);
int xtensa_proc_service_create(int pid);
void xtensa_proc_service_destroy_all(void);
uint32 xtensa_proc_service_active_count(void);
void xtensa_sched_init(void);
int xtensa_sched_create_proc(int pid);
void xtensa_sched_step(void);
int xtensa_sched_current_pid(void);
uint32 xtensa_sched_runnable_count(void);
uint32 xtensa_sched_sleeping_count(void);
int xtensa_sched_sleep_current(void);
int xtensa_sched_wakeup_pid(int pid);
int xtensa_sched_sleep_current_on_chan(uint32 chan);
int xtensa_sched_wakeup_chan(uint32 chan);
void xtensa_sched_dump(void);
void xtensa_trap_init(void);
void xtensa_trap_handle_syscall(struct xtensa_trapframe *tf);
void consputc(int c);
int kprintf(const char *fmt, ...);

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

void timer_init(uint32 cpu_hz, uint32 tick_hz);
uint32 timer_ticks(void);
void xtensa_timer_interrupt(void);

#endif // XV6_XTENSA_PORT_H
