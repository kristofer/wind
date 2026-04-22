#include "kernel/types.h"
#include "kernel/xtensa/port.h"

static uint32 syscall_count;

static int
xtensa_sys_yield(struct xtensa_trapframe *tf)
{
  (void)tf;
  xtensa_sched_step();
  return xtensa_sched_current_pid();
}

void
xtensa_trap_init(void)
{
  syscall_count = 0;
  kprintf("wind: trap/syscall scaffold ready\n");
}

void
xtensa_trap_handle_syscall(struct xtensa_trapframe *tf)
{
  if(tf == 0)
    return;

  syscall_count++;
  switch(tf->syscall_no){
  case WIND_SYSCALL_YIELD:
    tf->retval = xtensa_sys_yield(tf);
    kprintf("wind: syscall yield ret_pid=%d count=%u\n", tf->retval, syscall_count);
    break;
  default:
    tf->retval = -1;
    kprintf("wind: syscall unknown no=%u count=%u\n", tf->syscall_no, syscall_count);
    break;
  }
}
