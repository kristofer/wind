#ifndef XV6_XTENSA_PORT_H
#define XV6_XTENSA_PORT_H

#include "kernel/types.h"

#define XTENSA_CPU_HZ 80000000U
#define XTENSA_TICK_HZ 1000U
#define WIND_INIT_PID 100
#define WIND_SYSCALL_YIELD 1U
#define WIND_SYSCALL_SLEEP_ON_CHAN 2U
#define WIND_SYSCALL_WAKEUP_CHAN 3U
#define WIND_SYSCALL_GETPID 4U
#define WIND_SYSCALL_EXIT 5U
#define WIND_SYSCALL_WAIT 6U
#define WIND_SYSCALL_KILL 7U

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

enum xtensa_proc_state {
  XTENSA_PROC_UNUSED = 0,
  XTENSA_PROC_RUNNABLE = 1,
  XTENSA_PROC_RUNNING = 2,
  XTENSA_PROC_SLEEPING = 3,
  XTENSA_PROC_ZOMBIE = 4,
};

struct xtensa_proc {
  int pid;
  enum xtensa_proc_state state;
  uint32 wait_chan;
  void *kstack;
  struct xtensa_trapframe *trapframe;
  struct xtensa_context context;
  char name[16];
  void (*fn)(struct xtensa_proc *);  /* kernel entry function */
  uint32 fn_state;                   /* proc-owned continuation counter */
  int exit_code;
  int parent_pid;
  int killed;
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
void xtensa_sched_init(void);
int xtensa_sched_create_proc(int pid);
int xtensa_sched_create_proc_fn(int pid, void (*fn)(struct xtensa_proc *));
int xtensa_sched_create_proc_fn_parent(int pid, int parent_pid, void (*fn)(struct xtensa_proc *));
void xtensa_sched_run_current(void);
int xtensa_sched_exit_current(int code);
int xtensa_sched_wait_current(int *wstatus);
void xtensa_sched_step(void);
int xtensa_sched_current_pid(void);
struct xtensa_proc *xtensa_sched_current_proc(void);
uint32 xtensa_sched_runnable_count(void);
uint32 xtensa_sched_sleeping_count(void);
uint32 xtensa_sched_zombie_count(void);
int xtensa_sched_sleep_current(void);
int xtensa_sched_wakeup_pid(int pid);
int xtensa_sched_sleep_current_on_chan(uint32 chan);
int xtensa_sched_wakeup_chan(uint32 chan);
int xtensa_sched_kill_pid(int pid);
void xtensa_sched_dump(void);
void xtensa_trap_init(void);
void xtensa_trap_handle_syscall(struct xtensa_trapframe *tf);

/* kernel proc API — call from within a proc's fn */
void wind_yield(void);
void wind_sleep_on_chan(uint32 chan);
void wind_wakeup_chan(uint32 chan);
void wind_exit(int code);
int  wind_getpid(void);
int  wind_wait(int *wstatus);
int  wind_kill(int pid);
void consputc(int c);
int kprintf(const char *fmt, ...);

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

void timer_init(uint32 cpu_hz, uint32 tick_hz);
uint32 timer_ticks(void);
void xtensa_timer_interrupt(void);

#endif // XV6_XTENSA_PORT_H
