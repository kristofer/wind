#include "kernel/types.h"
#include "kernel/xtensa/port.h"
#include <stdio.h>

#ifdef WIND_ESP_IDF_APP
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
extern void kfree(void *);
#endif

#define WIND_SCHED_SLOTS 8U
#define WIND_WAIT_CHAN_BASE 0x80000000U

static struct xtensa_proc procs[WIND_SCHED_SLOTS];
static int current_index;
static uint32 runnable_count;
static uint32 sleeping_count;
static uint32 zombie_count;
static int last_scheduled_pid;
static int preferred_wakeup_pid;
static int next_pid;  /* monotonic counter for spawn-assigned pids */

static uint32
wait_chan_for_parent(int parent_pid)
{
  return WIND_WAIT_CHAN_BASE | ((uint32)parent_pid & ~WIND_WAIT_CHAN_BASE);
}

static void
sched_copy_cmdline(char *dst, const char *src)
{
  uint32 i = 0;
  if(dst == 0)
    return;
  if(src == 0){
    dst[0] = '\0';
    return;
  }
  while(i + 1U < WIND_PROC_CMDLINE_MAX && src[i] != '\0'){
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static int
sched_wakeup_chan_locked(uint32 chan)
{
  uint32 i;

  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == XTENSA_PROC_SLEEPING && procs[i].wait_chan == chan){
      procs[i].state = XTENSA_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      if(sleeping_count > 0)
        sleeping_count--;
      runnable_count++;
      preferred_wakeup_pid = procs[i].pid;
      return 0;
    }
  }
  return -1;
}

#ifdef WIND_ESP_IDF_APP
static portMUX_TYPE sched_lock = portMUX_INITIALIZER_UNLOCKED;

static inline void
sched_lock_enter(void)
{
  portENTER_CRITICAL(&sched_lock);
}

static inline void
sched_lock_exit(void)
{
  portEXIT_CRITICAL(&sched_lock);
}
#else
static inline void
sched_lock_enter(void)
{
}

static inline void
sched_lock_exit(void)
{
}
#endif

void
xtensa_sched_init(void)
{
  uint32 i;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    procs[i].pid = 0;
    procs[i].state = XTENSA_PROC_UNUSED;
    procs[i].wait_chan = 0;
    procs[i].kstack = 0;
    procs[i].trapframe = 0;
    procs[i].fn = 0;
    procs[i].fn_state = 0;
    procs[i].exit_code = 0;
    procs[i].parent_pid = -1;
    procs[i].killed = 0;
    procs[i].cmdline[0] = '\0';
    __builtin_memset(&procs[i].context, 0, sizeof(procs[i].context));
    __builtin_memset(&procs[i].name, 0, sizeof(procs[i].name));
  }
  current_index = -1;
  runnable_count = 0;
  sleeping_count = 0;
  zombie_count = 0;
  last_scheduled_pid = -1;
  preferred_wakeup_pid = -1;
  next_pid = 300;
  sched_lock_exit();
}

int
xtensa_sched_create_proc(int pid)
{
  uint32 i;
  void *kstack;
  struct xtensa_trapframe *tf;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == XTENSA_PROC_UNUSED){
      kstack = xtensa_page_alloc();
      if(kstack == 0)
      {
        sched_lock_exit();
        return -1;
      }
      tf = (struct xtensa_trapframe *)kstack;
      __builtin_memset(tf, 0, sizeof(*tf));
      
      procs[i].pid = pid;
      procs[i].state = XTENSA_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      procs[i].kstack = kstack;
      procs[i].trapframe = tf;
      procs[i].fn = 0;
      procs[i].fn_state = 0;
      procs[i].exit_code = 0;
      procs[i].parent_pid = -1;
      procs[i].killed = 0;
      procs[i].cmdline[0] = '\0';
      __builtin_memset(&procs[i].context, 0, sizeof(procs[i].context));
      snprintf(procs[i].name, sizeof(procs[i].name), "proc-%d", pid);
      runnable_count++;
      sched_lock_exit();
      return 0;
    }
  }

  sched_lock_exit();
  return -1;
}

int
xtensa_sched_create_proc_fn(int pid, void (*fn)(struct xtensa_proc *))
{
  return xtensa_sched_create_proc_fn_parent(pid, -1, fn);
}

int
xtensa_sched_create_proc_fn_parent(int pid, int parent_pid, void (*fn)(struct xtensa_proc *))
{
  int ret = xtensa_sched_create_proc(pid);
  if(ret != 0)
    return ret;

  sched_lock_enter();
  {
    uint32 i;
    for(i = 0; i < WIND_SCHED_SLOTS; i++){
      if(procs[i].pid == pid){
        procs[i].fn = fn;
        procs[i].parent_pid = parent_pid;
        break;
      }
    }
  }
  sched_lock_exit();
  return 0;
}

/*
 * xtensa_sched_create_child — spawn a child of the current proc.
 *
 * Auto-assigns a pid from the monotonic next_pid counter and creates
 * a new proc with parent = current proc's pid.  Returns the child's
 * pid on success, -1 on error (no free slot, no current proc, null fn).
 * This is the scheduling primitive underlying wind_spawn().
 */
int
xtensa_sched_create_child(void (*fn)(struct xtensa_proc *), const char *cmdline)
{
  int parent_pid;
  int child_pid;

  if(fn == 0)
    return -1;

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return -1;
  }
  parent_pid = procs[(uint32)current_index].pid;
  child_pid  = next_pid++;
  sched_lock_exit();

  if(xtensa_sched_create_proc_fn_parent(child_pid, parent_pid, fn) != 0)
    return -1;
  sched_lock_enter();
  {
    uint32 i;
    for(i = 0; i < WIND_SCHED_SLOTS; i++){
      if(procs[i].pid == child_pid && procs[i].state != XTENSA_PROC_UNUSED){
        sched_copy_cmdline(procs[i].cmdline, cmdline);
        break;
      }
    }
  }
  sched_lock_exit();
  kprintf("wind: sched spawn child_pid=%d parent=%d\n", child_pid, parent_pid);
  return child_pid;
}

void
xtensa_sched_run_current(void)
{
  struct xtensa_proc *p;
  void (*fn)(struct xtensa_proc *);

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return;
  }
  p = &procs[(uint32)current_index];
  fn = p->fn;
  sched_lock_exit();

  if(fn != 0)
    fn(p);
}

int
xtensa_sched_exit_current(int code)
{
  int exiting_pid;
  int parent_pid;
  int reparent_target;
  int wake_init_waiter;
  uint32 i;
  uint32 wake_chan;

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return -1;
  }
  exiting_pid = procs[(uint32)current_index].pid;
  if(exiting_pid == WIND_INIT_PID){
    sched_lock_exit();
    kprintf("wind: sched refusing exit for init pid=%d\n", WIND_INIT_PID);
    return -1;
  }

  parent_pid = procs[(uint32)current_index].parent_pid;
  reparent_target = WIND_INIT_PID;
  wake_init_waiter = 0;

  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if((int)i == current_index)
      continue;
    if(procs[i].state == XTENSA_PROC_UNUSED)
      continue;
    if(procs[i].parent_pid != exiting_pid)
      continue;
    procs[i].parent_pid = reparent_target;
    if(procs[i].state == XTENSA_PROC_ZOMBIE)
      wake_init_waiter = 1;
  }

  procs[(uint32)current_index].state = XTENSA_PROC_ZOMBIE;
  procs[(uint32)current_index].wait_chan = 0;
  procs[(uint32)current_index].fn = 0;
  procs[(uint32)current_index].exit_code = code;
  if(runnable_count > 0)
    runnable_count--;
  zombie_count++;
  current_index = -1;
  if(parent_pid > 0){
    wake_chan = wait_chan_for_parent(parent_pid);
    (void)sched_wakeup_chan_locked(wake_chan);
  }
  if(wake_init_waiter){
    wake_chan = wait_chan_for_parent(WIND_INIT_PID);
    (void)sched_wakeup_chan_locked(wake_chan);
  }
  sched_lock_exit();

  kprintf("wind: sched zombie pid=%d ppid=%d exit=%d zombie=%u\n",
          exiting_pid,
          parent_pid,
          code,
          xtensa_sched_zombie_count());

  return 0;
}

/*
 * xtensa_sched_exec_current — pseudo-exec.
 *
 * Replaces the calling proc's entry function with fn and resets fn_state
 * to 0, mirroring xv6 exec() semantics at the kernel-task level.  The
 * proc's user region (ubase/usz) is freed here so the new fn can allocate
 * a fresh one on its first step.
 *
 * The caller MUST return immediately after this call; the scheduler will
 * invoke fn(p) on the next scheduling round.  Does nothing and returns -1
 * if fn is null or no proc is current.
 */
int
xtensa_sched_exec_current(void (*fn)(struct xtensa_proc *))
{
  struct xtensa_proc *p;

  if(fn == 0)
    return -1;

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return -1;
  }
  p = &procs[(uint32)current_index];
  p->fn       = fn;
  p->fn_state = 0;
  sched_lock_exit();

  /* free uregion outside the lock */
  xtensa_user_free(p);

  kprintf("wind: sched exec pid=%d -> new fn\n", p->pid);
  return 0;
}

int
xtensa_sched_wait_current(int *wstatus)
{
  uint32 i;
  int parent_pid;
  int child_pid;
  int child_exit;
  int child_ppid;
  int have_children;
  void *kstack_to_free;
  uint32 ubase_to_free;
  uint32 usz_to_free;

  child_pid = -1;
  child_exit = 0;
  child_ppid = -1;
  have_children = 0;
  kstack_to_free = 0;
  ubase_to_free = 0;
  usz_to_free = 0;

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return -1;
  }

  parent_pid = procs[(uint32)current_index].pid;

  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == XTENSA_PROC_UNUSED)
      continue;
    if(procs[i].parent_pid != parent_pid)
      continue;

    have_children = 1;
    if(procs[i].state == XTENSA_PROC_ZOMBIE){
      child_pid = procs[i].pid;
      child_exit = procs[i].exit_code;
      child_ppid = procs[i].parent_pid;
      kstack_to_free = procs[i].kstack;
      ubase_to_free  = procs[i].ubase;
      usz_to_free    = procs[i].usz;
      procs[i].pid = 0;
      procs[i].state = XTENSA_PROC_UNUSED;
      procs[i].wait_chan = 0;
      procs[i].kstack = 0;
      procs[i].trapframe = 0;
      procs[i].fn = 0;
      procs[i].fn_state = 0;
      procs[i].exit_code = 0;
      procs[i].parent_pid = -1;
      procs[i].killed = 0;
      procs[i].ubase = 0;
      procs[i].usz   = 0;
      procs[i].cmdline[0] = '\0';
      __builtin_memset(&procs[i].context, 0, sizeof(procs[i].context));
      __builtin_memset(&procs[i].name, 0, sizeof(procs[i].name));
      if(zombie_count > 0)
        zombie_count--;
      break;
    }
  }

  if(child_pid >= 0){
    sched_lock_exit();
#ifdef WIND_ESP_IDF_APP
    if(kstack_to_free != 0)
      kfree(kstack_to_free);
    if(ubase_to_free != 0)
      heap_caps_free((void *)ubase_to_free);
    (void)usz_to_free;
#else
    if(kstack_to_free != 0)
      xtensa_page_free(kstack_to_free);
    (void)ubase_to_free;
    (void)usz_to_free;
#endif
    kprintf("wind: sched reap parent=%d child=%d status=%d zombie=%u\n",
            child_ppid,
            child_pid,
            child_exit,
            xtensa_sched_zombie_count());
    if(wstatus != 0)
      *wstatus = child_exit;
    return child_pid;
  }

  if(!have_children){
    sched_lock_exit();
    return -1;
  }

  if(procs[(uint32)current_index].killed != 0){
    sched_lock_exit();
    return -1;
  }

  if(procs[(uint32)current_index].state == XTENSA_PROC_RUNNING){
    procs[(uint32)current_index].state = XTENSA_PROC_SLEEPING;
    procs[(uint32)current_index].wait_chan = wait_chan_for_parent(parent_pid);
    if(runnable_count > 0)
      runnable_count--;
    sleeping_count++;
    current_index = -1;
  }
  sched_lock_exit();
  return -2;
}

void
xtensa_sched_step(void)
{
  uint32 scanned;
  uint32 idx;
  uint32 i;
  int fallback_index;

  sched_lock_enter();
  if(runnable_count == 0)
  {
    current_index = -1;
    sched_lock_exit();
    return;
  }

  if(current_index >= 0 && procs[(uint32)current_index].state == XTENSA_PROC_RUNNING)
    procs[(uint32)current_index].state = XTENSA_PROC_RUNNABLE;

  if(preferred_wakeup_pid >= 0){
    for(i = 0; i < WIND_SCHED_SLOTS; i++){
      if(procs[i].pid == preferred_wakeup_pid && procs[i].state == XTENSA_PROC_RUNNABLE){
        procs[i].state = XTENSA_PROC_RUNNING;
        current_index = (int)i;
        last_scheduled_pid = procs[i].pid;
        preferred_wakeup_pid = -1;
        sched_lock_exit();
        return;
      }
    }
    preferred_wakeup_pid = -1;
  }

  idx = (current_index < 0) ? 0U : (uint32)(current_index + 1);
  fallback_index = -1;
  for(scanned = 0; scanned < WIND_SCHED_SLOTS; scanned++){
    idx %= WIND_SCHED_SLOTS;
    if(procs[idx].state == XTENSA_PROC_RUNNABLE){
      if(fallback_index < 0)
        fallback_index = (int)idx;
      if(runnable_count > 1 && procs[idx].pid == last_scheduled_pid){
        idx++;
        continue;
      }
      procs[idx].state = XTENSA_PROC_RUNNING;
      current_index = (int)idx;
      last_scheduled_pid = procs[idx].pid;
      sched_lock_exit();
      return;
    }
    idx++;
  }

  if(fallback_index >= 0){
    procs[(uint32)fallback_index].state = XTENSA_PROC_RUNNING;
    current_index = fallback_index;
    last_scheduled_pid = procs[(uint32)fallback_index].pid;
    sched_lock_exit();
    return;
  }

  current_index = -1;
  sched_lock_exit();
}

int
xtensa_sched_current_pid(void)
{
  int pid;

  sched_lock_enter();
  if(current_index < 0)
  {
    sched_lock_exit();
    return -1;
  }

  pid = procs[(uint32)current_index].pid;
  sched_lock_exit();
  return pid;
}

struct xtensa_proc *
xtensa_sched_current_proc(void)
{
  struct xtensa_proc *p;

  sched_lock_enter();
  if(current_index < 0)
  {
    sched_lock_exit();
    return 0;
  }

  p = &procs[(uint32)current_index];
  sched_lock_exit();
  return p;
}

uint32
xtensa_sched_runnable_count(void)
{
  uint32 count;

  sched_lock_enter();
  count = runnable_count;
  sched_lock_exit();
  return count;
}

uint32
xtensa_sched_sleeping_count(void)
{
  uint32 count;

  sched_lock_enter();
  count = sleeping_count;
  sched_lock_exit();
  return count;
}

uint32
xtensa_sched_zombie_count(void)
{
  uint32 count;

  sched_lock_enter();
  count = zombie_count;
  sched_lock_exit();
  return count;
}

int
xtensa_sched_sleep_current(void)
{
  return xtensa_sched_sleep_current_on_chan(0);
}

int
xtensa_sched_sleep_current_on_chan(uint32 chan)
{
  int pid;

  sched_lock_enter();
  if(current_index < 0){
    sched_lock_exit();
    return -1;
  }
  if(procs[(uint32)current_index].state != XTENSA_PROC_RUNNING){
    sched_lock_exit();
    return -1;
  }

  pid = procs[(uint32)current_index].pid;
  procs[(uint32)current_index].state = XTENSA_PROC_SLEEPING;
  procs[(uint32)current_index].wait_chan = chan;
  current_index = -1;
  sleeping_count++;
  if(runnable_count > 0)
    runnable_count--;
  sched_lock_exit();
  return pid;
}

int
xtensa_sched_wakeup_pid(int pid)
{
  uint32 i;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].pid == pid && procs[i].state == XTENSA_PROC_SLEEPING){
      procs[i].state = XTENSA_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      sleeping_count--;
      runnable_count++;
      preferred_wakeup_pid = pid;
      sched_lock_exit();
      return 0;
    }
  }
  sched_lock_exit();
  return -1;
}

int
xtensa_sched_wakeup_chan(uint32 chan)
{
  int ret;

  sched_lock_enter();
  ret = sched_wakeup_chan_locked(chan);
  sched_lock_exit();
  return ret;
}

int
xtensa_sched_kill_pid(int pid)
{
  uint32 i;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == XTENSA_PROC_UNUSED)
      continue;
    if(procs[i].pid != pid)
      continue;

    procs[i].killed = 1;
    if(procs[i].state == XTENSA_PROC_SLEEPING){
      procs[i].state = XTENSA_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      if(sleeping_count > 0)
        sleeping_count--;
      runnable_count++;
      preferred_wakeup_pid = pid;
    }
    sched_lock_exit();
    return 0;
  }
  sched_lock_exit();
  return -1;
}

void
xtensa_sched_dump(void)
{
  uint32 i;
  int pid_snapshot[WIND_SCHED_SLOTS];
  int state_snapshot[WIND_SCHED_SLOTS];
  uint32 chan_snapshot[WIND_SCHED_SLOTS];
  int parent_snapshot[WIND_SCHED_SLOTS];
  int exit_snapshot[WIND_SCHED_SLOTS];
  int current_snapshot;
  int last_pid_snapshot;
  int preferred_pid_snapshot;
  uint32 runnable_snapshot;
  uint32 sleeping_snapshot;
  uint32 zombie_snapshot;
  const char *state_name;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    pid_snapshot[i] = procs[i].pid;
    state_snapshot[i] = (int)procs[i].state;
    chan_snapshot[i] = procs[i].wait_chan;
    parent_snapshot[i] = procs[i].parent_pid;
    exit_snapshot[i] = procs[i].exit_code;
  }
  current_snapshot = current_index;
  last_pid_snapshot = last_scheduled_pid;
  preferred_pid_snapshot = preferred_wakeup_pid;
  runnable_snapshot = runnable_count;
  sleeping_snapshot = sleeping_count;
  zombie_snapshot = zombie_count;
  sched_lock_exit();

  kprintf("wind: sched dump begin current_idx=%d last_pid=%d preferred_wakeup_pid=%d runnable=%u sleeping=%u zombie=%u\n",
          current_snapshot,
          last_pid_snapshot,
          preferred_pid_snapshot,
          runnable_snapshot,
          sleeping_snapshot,
          zombie_snapshot);

  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    switch(state_snapshot[i]){
    case XTENSA_PROC_UNUSED:
      state_name = "UNUSED";
      break;
    case XTENSA_PROC_RUNNABLE:
      state_name = "RUNNABLE";
      break;
    case XTENSA_PROC_RUNNING:
      state_name = "RUNNING";
      break;
    case XTENSA_PROC_SLEEPING:
      state_name = "SLEEPING";
      break;
    case XTENSA_PROC_ZOMBIE:
      state_name = "ZOMBIE";
      break;
    default:
      state_name = "UNKNOWN";
      break;
    }

    kprintf("wind: sched slot=%u pid=%d ppid=%d state=%s chan=%u exit=%d\n",
            i,
            pid_snapshot[i],
            parent_snapshot[i],
            state_name,
            chan_snapshot[i],
            exit_snapshot[i]);
  }
  kprintf("wind: sched dump end\n");
}
