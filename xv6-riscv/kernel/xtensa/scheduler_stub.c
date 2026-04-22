#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
#include "freertos/FreeRTOS.h"
#endif

#define WIND_SCHED_SLOTS 4U

enum wind_proc_state {
  WIND_PROC_UNUSED = 0,
  WIND_PROC_RUNNABLE = 1,
  WIND_PROC_RUNNING = 2,
  WIND_PROC_SLEEPING = 3,
};

struct wind_sched_proc {
  int pid;
  enum wind_proc_state state;
  uint32 wait_chan;
};

static struct wind_sched_proc procs[WIND_SCHED_SLOTS];
static int current_index;
static uint32 runnable_count;
static uint32 sleeping_count;
static int last_scheduled_pid;
static int preferred_wakeup_pid;

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

  xtensa_proc_service_init();
  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    procs[i].pid = 0;
    procs[i].state = WIND_PROC_UNUSED;
    procs[i].wait_chan = 0;
  }
  current_index = -1;
  runnable_count = 0;
  sleeping_count = 0;
  last_scheduled_pid = -1;
  preferred_wakeup_pid = -1;
  sched_lock_exit();
}

int
xtensa_sched_create_proc(int pid)
{
  uint32 i;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == WIND_PROC_UNUSED){
      if(xtensa_proc_service_create(pid) != 0)
      {
        sched_lock_exit();
        return -1;
      }
      procs[i].pid = pid;
      procs[i].state = WIND_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      runnable_count++;
      sched_lock_exit();
      return 0;
    }
  }

  sched_lock_exit();
  return -1;
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

  if(current_index >= 0 && procs[(uint32)current_index].state == WIND_PROC_RUNNING)
    procs[(uint32)current_index].state = WIND_PROC_RUNNABLE;

  if(preferred_wakeup_pid >= 0){
    for(i = 0; i < WIND_SCHED_SLOTS; i++){
      if(procs[i].pid == preferred_wakeup_pid && procs[i].state == WIND_PROC_RUNNABLE){
        procs[i].state = WIND_PROC_RUNNING;
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
    if(procs[idx].state == WIND_PROC_RUNNABLE){
      if(fallback_index < 0)
        fallback_index = (int)idx;
      if(runnable_count > 1 && procs[idx].pid == last_scheduled_pid){
        idx++;
        continue;
      }
      procs[idx].state = WIND_PROC_RUNNING;
      current_index = (int)idx;
      last_scheduled_pid = procs[idx].pid;
      sched_lock_exit();
      return;
    }
    idx++;
  }

  if(fallback_index >= 0){
    procs[(uint32)fallback_index].state = WIND_PROC_RUNNING;
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
  if(procs[(uint32)current_index].state != WIND_PROC_RUNNING){
    sched_lock_exit();
    return -1;
  }

  pid = procs[(uint32)current_index].pid;
  procs[(uint32)current_index].state = WIND_PROC_SLEEPING;
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
    if(procs[i].pid == pid && procs[i].state == WIND_PROC_SLEEPING){
      procs[i].state = WIND_PROC_RUNNABLE;
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
  uint32 i;
  int woke = 0;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    if(procs[i].state == WIND_PROC_SLEEPING && procs[i].wait_chan == chan){
      procs[i].state = WIND_PROC_RUNNABLE;
      procs[i].wait_chan = 0;
      sleeping_count--;
      runnable_count++;
      preferred_wakeup_pid = procs[i].pid;
      woke = 1;
      break;
    }
  }
  sched_lock_exit();
  return woke ? 0 : -1;
}

void
xtensa_sched_dump(void)
{
  uint32 i;
  int pid_snapshot[WIND_SCHED_SLOTS];
  int state_snapshot[WIND_SCHED_SLOTS];
  uint32 chan_snapshot[WIND_SCHED_SLOTS];
  int current_snapshot;
  int last_pid_snapshot;
  int preferred_pid_snapshot;
  uint32 runnable_snapshot;
  uint32 sleeping_snapshot;
  const char *state_name;

  sched_lock_enter();
  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    pid_snapshot[i] = procs[i].pid;
    state_snapshot[i] = (int)procs[i].state;
    chan_snapshot[i] = procs[i].wait_chan;
  }
  current_snapshot = current_index;
  last_pid_snapshot = last_scheduled_pid;
    preferred_pid_snapshot = preferred_wakeup_pid;
  runnable_snapshot = runnable_count;
  sleeping_snapshot = sleeping_count;
  sched_lock_exit();

    kprintf("wind: sched dump begin current_idx=%d last_pid=%d preferred_wakeup_pid=%d runnable=%u sleeping=%u\n",
          current_snapshot,
          last_pid_snapshot,
      preferred_pid_snapshot,
          runnable_snapshot,
          sleeping_snapshot);

  for(i = 0; i < WIND_SCHED_SLOTS; i++){
    switch(state_snapshot[i]){
    case WIND_PROC_UNUSED:
      state_name = "UNUSED";
      break;
    case WIND_PROC_RUNNABLE:
      state_name = "RUNNABLE";
      break;
    case WIND_PROC_RUNNING:
      state_name = "RUNNING";
      break;
    case WIND_PROC_SLEEPING:
      state_name = "SLEEPING";
      break;
    default:
      state_name = "UNKNOWN";
      break;
    }

    kprintf("wind: sched slot=%u pid=%d state=%s chan=%u\n",
            i,
            pid_snapshot[i],
            state_name,
            chan_snapshot[i]);
  }
  kprintf("wind: sched dump end\n");
}
