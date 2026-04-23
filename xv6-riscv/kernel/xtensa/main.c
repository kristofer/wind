#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
#include "sdkconfig.h"
#include "esp_system.h"
#include <stdio.h>
extern void kinit(void);
extern void *kalloc(void);
extern void kfree(void *);
#endif

static uint32 last_ticks;
#ifdef WIND_ESP_IDF_APP
static uint32 last_logged_second;
#define WIND_SELFTEST_MAX_PAGES 64U
static void *selftest_pages[WIND_SELFTEST_MAX_PAGES];
static uint32 selftest_allocated;
static int selftest_ok;
static int sched_service_ok;
#define WIND_ROMFS_IO_BUFSZ 64U

/* ---- kernel proc entry functions ---- */

/*
 * proc100: parent/waiter — waits for zombie children and reaps them.
 * If killed flag is set, wait returns -1 immediately; log and yield.
 */
static void
proc100_fn(struct xtensa_proc *p)
{
  int status;
  int child;

  p->fn_state++;
  child = wind_wait(&status);
  if(child >= 0){
    kprintf("wind: proc100 wait reaped child=%d status=%d\n", child, status);
  } else if(p->killed){
    kprintf("wind: proc100 wait killed return step=%u\n", p->fn_state);
    wind_yield();
  }

  if((p->fn_state % 4U) == 0U)
    wind_yield();
}

static int
wind_copy_cstr_to_uregion(struct xtensa_proc *p, uint32 uoff, const char *s)
{
  uint8 *dst;
  uint32 i;
  uint32 avail;

  if(p == 0 || s == 0 || p->ubase == 0 || uoff >= p->usz)
    return -1;

  dst = (uint8 *)wind_uaddr_to_kaddr(p, uoff);
  avail = p->usz - uoff;
  if(avail == 0)
    return -1;

  for(i = 0; i + 1U < avail && s[i] != '\0'; i++)
    dst[i] = (uint8)s[i];
  dst[i] = '\0';
  return 0;
}

static void
wind_write_cstr(struct xtensa_proc *p, uint32 uoff, const char *s)
{
  if(wind_copy_cstr_to_uregion(p, uoff, s) == 0)
    (void)wind_write(uoff);
}

static const char *
wind_cmd_args(const struct xtensa_proc *p)
{
  const char *s;

  if(p == 0)
    return "";
  s = p->cmdline;
  while(*s == ' ' || *s == '\t')
    s++;
  while(*s != '\0' && *s != ' ' && *s != '\t')
    s++;
  while(*s == ' ' || *s == '\t')
    s++;
  return s;
}

static int
wind_cmd_arg0(const struct xtensa_proc *p, char *dst, uint32 dst_len)
{
  const char *s = wind_cmd_args(p);
  uint32 n = 0;

  if(dst == 0 || dst_len == 0)
    return -1;
  if(*s == '\0'){
    dst[0] = '\0';
    return -1;
  }
  while(s[n] != '\0' && s[n] != ' ' && s[n] != '\t' && n + 1U < dst_len){
    dst[n] = s[n];
    n++;
  }
  dst[n] = '\0';
  if(s[n] != '\0' && s[n] != ' ' && s[n] != '\t')
    return -1;
  return 0;
}

static void
user_echo_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    if(wind_proc_uregion_alloc(128) == 0){
      char *out = (char *)wind_uaddr_to_kaddr(p, 0);
      const char *args = wind_cmd_args(p);
      uint32 i = 0;
      /* Need room for at least '\n' + '\0'. */
      if(p->usz < 2U){
        wind_exit(1);
        return;
      }
      while(args[i] != '\0' && i + 2U < p->usz){
        out[i] = args[i];
        i++;
      }
      out[i++] = '\n';
      out[i] = '\0';
      (void)wind_write(0);
    }
    wind_exit(0);
  }
}

static void
user_ls_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    if(wind_proc_uregion_alloc(64) == 0)
      wind_write_cstr(p, 0, "shell\necho\nls\ncat\nwc\n");
    wind_exit(0);
  }
}

static void
user_cat_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    if(wind_proc_uregion_alloc(128) == 0){
      char path[64];
      int fd;
      int n;
      char *out = (char *)wind_uaddr_to_kaddr(p, 0);
      uint32 output_buf_cap = (p->usz > 0U) ? (p->usz - 1U) : 0U;

      if(wind_cmd_arg0(p, path, sizeof(path)) != 0)
        snprintf(path, sizeof(path), "/etc/motd");
      if((fd = xtensa_romfs_open(path)) < 0)
        wind_write_cstr(p, 0, "cat: file not found\n");
      else{
        while(output_buf_cap > 0U && (n = xtensa_romfs_read(fd, out, output_buf_cap)) > 0){
          out[(uint32)n] = '\0';
          (void)wind_write(0);
        }
        (void)xtensa_romfs_close(fd);
      }
    }
    wind_exit(0);
  }
}

static void
user_wc_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    if(wind_proc_uregion_alloc(128) == 0){
      char path[64];
      int fd;
      int n;
      uint32 lines = 0;
      uint32 words = 0;
      uint32 bytes = 0;
      int in_word = 0;
      char *buf = (char *)wind_uaddr_to_kaddr(p, 0);
      char *out = (char *)wind_uaddr_to_kaddr(p, WIND_ROMFS_IO_BUFSZ);
      uint32 i;

      if(wind_cmd_arg0(p, path, sizeof(path)) != 0)
        snprintf(path, sizeof(path), "/etc/motd");
      if((fd = xtensa_romfs_open(path)) < 0)
        wind_write_cstr(p, 0, "wc: file not found\n");
      else{
        while((n = xtensa_romfs_read(fd, buf, WIND_ROMFS_IO_BUFSZ - 1U)) > 0){
          bytes += (uint32)n;
          for(i = 0; i < (uint32)n; i++){
            char c = buf[i];
            if(c == '\n')
              lines++;
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
              in_word = 0;
            else if(!in_word){
              words++;
              in_word = 1;
            }
          }
        }
        (void)xtensa_romfs_close(fd);
        snprintf(out, WIND_ROMFS_IO_BUFSZ, "%u %u %u %.30s\n", lines, words, bytes, path);
        (void)wind_write(WIND_ROMFS_IO_BUFSZ);
      }
    }
    wind_exit(0);
  }
}

static void
user_shell_fn(struct xtensa_proc *p)
{
  char *line;
  char *cmd;
  int n;
  int status;
  int child;

  if(p->fn_state == 0){
    if(wind_proc_uregion_alloc(128) != 0){
      kprintf("wind: user_shell uregion alloc FAILED\n");
      wind_exit(1);
      return;
    }
    p->fn_state = 1;
  }

  if(p->fn_state == 1){
    wind_write_cstr(p, 0, "$ ");
    p->fn_state = 2;
    return;
  }

  if(p->fn_state == 2){
    n = wind_read(32, 63);
    if(n < 0)
      return;
    if(n == 0){
      p->fn_state = 1;
      return;
    }

    line = (char *)wind_uaddr_to_kaddr(p, 32);
    if(line[n - 1] != '\n'){
      wind_write_cstr(p, 0, "sh: line too long\n");
      p->fn_state = 1;
      return;
    }
    line[n - 1] = '\0';

    cmd = line;
    while(*cmd == ' ' || *cmd == '\t')
      cmd++;

    if(*cmd == '\0'){
      p->fn_state = 1;
      return;
    }

    child = wind_spawn(cmd);
    if(child < 0){
      wind_write_cstr(p, 0, "sh: command not found\n");
      p->fn_state = 1;
      return;
    }
    p->fn_state = 3;
    return;
  }

  if(p->fn_state == 3){
    child = wind_wait(&status);
    if(child < 0)
      return;
    kprintf("wind: shell command pid=%d status=%d\n", child, status);
    p->fn_state = 1;
  }
}

static const char wind_romfs_motd[] =
  "wind: romfs bootstrap\n";

/*
 * Read-only ROMFS catalog in app flash.
 * Entries are path-addressable and include executable paths, a sample
 * data file, and a simple device node.
 */
static const struct wind_romfs_entry wind_romfs_catalog[] = {
  { "/bin/shell", WIND_ROMFS_EXEC, 0, 0, user_shell_fn },
  { "/bin/echo",  WIND_ROMFS_EXEC, 0, 0, user_echo_fn  },
  { "/bin/ls",    WIND_ROMFS_EXEC, 0, 0, user_ls_fn    },
  { "/bin/cat",   WIND_ROMFS_EXEC, 0, 0, user_cat_fn   },
  { "/bin/wc",    WIND_ROMFS_EXEC, 0, 0, user_wc_fn    },
  { "/etc/motd",  WIND_ROMFS_DATA, wind_romfs_motd, sizeof(wind_romfs_motd) - 1U /* exclude NUL terminator from data_len */, 0 },
  { WIND_ROMFS_DEV_CONSOLE_PATH, WIND_ROMFS_DEV, 0, 0, 0 },
};

/*
 * user_init_fn — real init analogue for Phase 7.
 *
 * Mirrors xv6 init(8): allocates a user region once, then enters an
 * infinite spawn→wait→respawn loop.  "shell" is spawned as a child;
 * when it exits, init reaps it and immediately spawns a fresh copy.
 * Init never exits: it is the root process and the parent of all
 * dynamically-created procs.
 *
 * Phase 7 vs Phase 6: exec_by_name replaced init itself with the shell
 * (init disappeared).  Now init stays alive and supervises the shell,
 * restarting it on each exit — the canonical xv6 init behaviour.
 */
static void
user_init_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    const char *msg = "hello from user_init\n";
    uint32 i;
    uint8 *buf;

    if(wind_proc_uregion_alloc(64) != 0){
      kprintf("wind: user_init uregion alloc FAILED\n");
      wind_exit(1);
      return;
    }
    buf = (uint8 *)wind_uaddr_to_kaddr(p, 0);
    for(i = 0; msg[i] != '\0'; i++)
      buf[i] = (uint8)msg[i];
    buf[i] = '\0';

    kprintf("wind: user_init step=1 calling wind_write\n");
    wind_write(0);
  }
  if(p->fn_state == 3){
    int child;
    kprintf("wind: user_init step=3 spawning shell\n");
    child = wind_spawn("shell");
    if(child < 0)
      kprintf("wind: user_init spawn FAILED\n");
    else
      kprintf("wind: user_init spawned shell pid=%d\n", child);
  }
  if(p->fn_state >= 5){
    int status;
    int child = wind_wait(&status);
    if(child >= 0){
      kprintf("wind: user_init reaped child=%d status=%d respawning\n",
              child, status);
      p->fn_state = 2;  /* reset: next step → 3 → spawn again */
    }
  }
}

/*
 * proc200: worker — on step 1 verifies flat uregion read/write, then on
 * step 2 execs into user_init_fn to exercise the pseudo-exec path.
 */
static void
proc200_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 1){
    int rc;
    uint8 *buf;
    uint32 j;
    kprintf("wind: proc200 start pid=%d\n", wind_getpid());
    rc = wind_proc_uregion_alloc(64);
    if(rc == 0){
      buf = (uint8 *)wind_uaddr_to_kaddr(p, 0);
      for(j = 0; j < 64; j++)
        buf[j] = (uint8)(j & 0xFFU);
      kprintf("wind: proc200 uregion write check buf[0]=%u buf[63]=%u\n",
              (unsigned)buf[0], (unsigned)buf[63]);
    } else {
      kprintf("wind: proc200 uregion alloc FAILED\n");
    }
  }
  if(p->fn_state == 2){
    kprintf("wind: proc200 step=2 exec -> user_init_fn\n");
    wind_exec(user_init_fn);
    return;  /* must return; scheduler calls user_init_fn next round */
  }
}

/*
 * proc201: sleeper — sleeps on chan=1 every 5 steps; proc202 wakes it.
 */
static void
proc201_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if((p->fn_state % 5) == 0){
    kprintf("wind: proc201 step=%u sleep chan=1\n", p->fn_state);
    wind_sleep_on_chan(1);
    return;
  }
}

/*
 * proc202: waker — wakes chan=1 every 3 steps, exercising the wakeup
 * path even when nothing is sleeping (wakeup_chan returns -1 = no-op).
 */
static void
proc202_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if((p->fn_state % 3) == 0){
    kprintf("wind: proc202 step=%u wakeup chan=1\n", p->fn_state);
    wind_wakeup_chan(1);
  }
}

/*
 * proc203: killer — sends kill to proc100 (the waiter/parent) at step 35
 * to exercise kill-safe wait behavior. Exits itself after step 40.
 */
static void
proc203_fn(struct xtensa_proc *p)
{
  p->fn_state++;
  if(p->fn_state == 8){
    kprintf("wind: proc203 step=%u killing proc100\n", p->fn_state);
    wind_kill(100);
  }
  if(p->fn_state >= 12){
    kprintf("wind: proc203 step=%u exiting\n", p->fn_state);
    wind_exit(0);
    return;
  }
}

static void
xtensa_allocator_selftest(void)
{
  uint32 i;
  uint32 target;
  void *extra;

  kinit();
  target = xtensa_memory_total_pages();
  if(target > WIND_SELFTEST_MAX_PAGES)
    target = WIND_SELFTEST_MAX_PAGES;

  selftest_ok = 1;
  selftest_allocated = 0;
  for(i = 0; i < target; i++){
    selftest_pages[i] = kalloc();
    if(selftest_pages[i] == 0){
      selftest_ok = 0;
      break;
    }
    selftest_allocated++;
  }

  extra = kalloc();
  if(extra != 0)
    selftest_ok = 0;

  for(i = 0; i < selftest_allocated; i++){
    kfree(selftest_pages[i]);
    selftest_pages[i] = 0;
  }

  if(xtensa_memory_free_pages() != xtensa_memory_total_pages())
    selftest_ok = 0;
}


static void
xtensa_sched_service_bootstrap(void)
{
  struct {
    int pid;
    int ppid;
    void (*fn)(struct xtensa_proc *);
  } boot_procs[] = {
    { 100, -1, proc100_fn },
    { 200, 100, proc200_fn },
    { 201, 100, proc201_fn },
    { 202, 100, proc202_fn },    { 203, 100, proc203_fn },  };
  uint32 i;

  sched_service_ok = 1;
  xtensa_sched_init();
  xtensa_romfs_catalog_set(wind_romfs_catalog,
                           sizeof(wind_romfs_catalog) / sizeof(wind_romfs_catalog[0]));
  for(i = 0; i < (sizeof(boot_procs) / sizeof(boot_procs[0])); i++){
    if(xtensa_sched_create_proc_fn_parent(boot_procs[i].pid,
                                          boot_procs[i].ppid,
                                          boot_procs[i].fn) != 0){
      sched_service_ok = 0;
      break;
    }
  }
  xtensa_sched_step();
  if(xtensa_sched_current_pid() < 0)
    sched_service_ok = 0;

  kprintf("wind: scheduler bootstrap %s (runnable=%u current_pid=%d free_pages=%u/%u)\n",
          sched_service_ok ? "PASS" : "FAIL",
          xtensa_sched_runnable_count(),
          xtensa_sched_current_pid(),
          xtensa_memory_free_pages(),
          xtensa_memory_total_pages());
}

#ifdef CONFIG_WIND_ALLOC_NEGATIVE_SELFTEST
static void
xtensa_allocator_negative_selftest(void)
{
  void *page;

  kprintf("wind: allocator negative selftest START (expect panic after double-free)\n");
  page = kalloc();
  if(page == 0){
    kprintf("wind: allocator negative selftest FAIL (initial kalloc returned null)\n");
    return;
  }

  kfree(page);
  kprintf("wind: allocator negative selftest: first kfree ok, issuing second kfree\n");
  kfree(page);

  // We should never get here; memory_idf.c panics on double free.
  kprintf("wind: allocator negative selftest FAIL (double-free did not panic)\n");
}
#endif

#ifdef CONFIG_WIND_ALLOC_NEGATIVE_INVALID_FREE_SELFTEST
static void
xtensa_allocator_negative_invalid_free_selftest(void)
{
  int bogus;

  kprintf("wind: allocator negative invalid-free selftest START (expect panic on out-of-pool free)\n");
  bogus = 0;
  kfree(&bogus);
  kprintf("wind: allocator negative invalid-free selftest FAIL (invalid free did not panic)\n");
}
#endif
#endif

void
xtensa_handle_level1_interrupt(void)
{
  xtensa_timer_interrupt();
}

void
xtensa_handle_exception(void)
{
  kprintf("wind: unhandled xtensa exception\n");
#ifdef WIND_ESP_IDF_APP
  esp_system_abort("wind: unhandled xtensa exception");
#else
  for(;;)
    ;
#endif
}

void
xtensa_kernel_init(void)
{
  uart_init();
  xtensa_memory_init();

#ifdef WIND_ESP_IDF_APP
  kprintf("wind: ESP32-S3 kernel bring-up\n");
  kprintf("wind: ESP-IDF wrapper active\n");
  kprintf("wind: page allocator ready: %u free pages\n", xtensa_memory_free_pages());
  xtensa_allocator_selftest();
  kprintf("wind: allocator selftest %s (allocated=%u free_pages=%u/%u)\n",
          selftest_ok ? "PASS" : "FAIL",
          selftest_allocated,
          xtensa_memory_free_pages(),
          xtensa_memory_total_pages());
  xtensa_sched_service_bootstrap();
  xtensa_trap_init();
#ifdef CONFIG_WIND_ALLOC_NEGATIVE_SELFTEST
  xtensa_allocator_negative_selftest();
#endif
#ifdef CONFIG_WIND_ALLOC_NEGATIVE_INVALID_FREE_SELFTEST
  xtensa_allocator_negative_invalid_free_selftest();
#endif
  timer_init(XTENSA_CPU_HZ, XTENSA_TICK_HZ);
  last_ticks = 0;
  last_logged_second = (uint32)-1;
#else
  uart_puts("\nwind: ESP32-S3 kernel bring-up\n");
  timer_init(XTENSA_CPU_HZ, XTENSA_TICK_HZ);
  uart_puts("wind: timer interrupt enabled\n");
  last_ticks = 0;
#endif
}

void
xtensa_kernel_poll(void)
{
#ifdef WIND_ESP_IDF_APP
  xtensa_console_poll_input();
  uint32 now = timer_ticks();
  while(last_ticks != now){
    uint32 next_tick = last_ticks + 1U;
    uint32 seconds = next_tick / XTENSA_TICK_HZ;

    /* One scheduling quantum per timer tick: preemptive round-robin. */
    xtensa_sched_step();
    xtensa_sched_run_current();
    /* If proc slept/exited during its quantum, select a replacement now. */
    if(xtensa_sched_current_pid() < 0)
      xtensa_sched_step();

    if(seconds != last_logged_second){
      kprintf("wind: tick=%u seconds=%u current_pid=%d runnable=%u sleeping=%u zombie=%u free_pages=%u/%u\n",
              next_tick,
              seconds,
              xtensa_sched_current_pid(),
              xtensa_sched_runnable_count(),
              xtensa_sched_sleeping_count(),
              xtensa_sched_zombie_count(),
              xtensa_memory_free_pages(),
              xtensa_memory_total_pages());

      if((seconds % 10U) == 0U){
        kprintf("wind: sched dump cmd\n");
        xtensa_sched_dump();
      }
      last_logged_second = seconds;
    }

    last_ticks = next_tick;
  }
#else
  uint32 now = timer_ticks();
  if(now != last_ticks && (now % XTENSA_TICK_HZ) == 0){
    uart_puts("wind: timer tick\n");
    last_ticks = now;
  }
#endif
}

void
xtensa_kernel_main(void)
{
  xtensa_kernel_init();
  for(;;)
    xtensa_kernel_poll();
}
