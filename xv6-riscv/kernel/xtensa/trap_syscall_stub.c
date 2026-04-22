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

static int
xtensa_sys_sleep_on_chan(struct xtensa_trapframe *tf)
{
  uint32 chan = tf->arg0;
  int pid = xtensa_sched_sleep_current_on_chan(chan);
  return pid;
}

static int
xtensa_sys_wakeup_chan(struct xtensa_trapframe *tf)
{
  uint32 chan = tf->arg0;
  int ret = xtensa_sched_wakeup_chan(chan);
  return ret;
}

static int
xtensa_sys_getpid(struct xtensa_trapframe *tf)
{
  (void)tf;
  return xtensa_sched_current_pid();
}

static int
xtensa_sys_exit(struct xtensa_trapframe *tf)
{
  int code = (int)tf->arg0;
  xtensa_sched_exit_current(code);
  return 0;
}

static int
xtensa_sys_wait(struct xtensa_trapframe *tf)
{
  int wstatus = 0;
  int child_pid = xtensa_sched_wait_current(&wstatus);
  /* internal -2 means the caller was put to sleep waiting for a child */
  if(child_pid == -2)
    child_pid = -1;
  tf->arg0 = (uint32)wstatus;
  return child_pid;
}

static int
xtensa_sys_kill(struct xtensa_trapframe *tf)
{
  int pid = (int)tf->arg0;
  return xtensa_sched_kill_pid(pid);
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
  case WIND_SYSCALL_SLEEP_ON_CHAN:
    tf->retval = xtensa_sys_sleep_on_chan(tf);
    kprintf("wind: syscall sleep_on_chan(chan=%u) ret_pid=%d count=%u\n", 
            tf->arg0, tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_WAKEUP_CHAN:
    tf->retval = xtensa_sys_wakeup_chan(tf);
    kprintf("wind: syscall wakeup_chan(chan=%u) ret=%d count=%u\n", 
            tf->arg0, tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_GETPID:
    tf->retval = xtensa_sys_getpid(tf);
    break;
  case WIND_SYSCALL_EXIT:
    xtensa_sys_exit(tf);
    tf->retval = 0;
    kprintf("wind: syscall exit(code=%d) count=%u\n", (int)tf->arg0, syscall_count);
    break;
  case WIND_SYSCALL_WAIT:
    tf->retval = xtensa_sys_wait(tf);
    if((int)tf->retval >= 0)
      kprintf("wind: syscall wait child=%d status=%d count=%u\n", tf->retval, (int)tf->arg0, syscall_count);
    break;
  case WIND_SYSCALL_KILL:
    tf->retval = xtensa_sys_kill(tf);
    kprintf("wind: syscall kill(pid=%d) ret=%d count=%u\n", (int)tf->arg0, (int)tf->retval, syscall_count);
    break;
  default:
    tf->retval = -1;
    kprintf("wind: syscall unknown no=%u count=%u\n", tf->syscall_no, syscall_count);
    break;
  }
}

/* ---- kernel proc API: call from within a proc's fn ---- */

void
wind_yield(void)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_YIELD;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_sleep_on_chan(uint32 chan)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_SLEEP_ON_CHAN;
  tf.arg0 = chan;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_wakeup_chan(uint32 chan)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_WAKEUP_CHAN;
  tf.arg0 = chan;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_exit(int code)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_EXIT;
  tf.arg0 = (uint32)code;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  /* proc is now UNUSED; caller must return immediately */
}

int
wind_getpid(void)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_GETPID;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  return tf.retval;
}

int
wind_wait(int *wstatus)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_WAIT;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  if(wstatus != 0)
    *wstatus = (int)tf.arg0;
  return tf.retval;
}

int
wind_kill(int pid)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_KILL;
  tf.arg0 = (uint32)pid;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}
