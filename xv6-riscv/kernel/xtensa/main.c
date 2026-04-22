#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
#include "sdkconfig.h"
#include "esp_system.h"
extern void kinit(void);
extern void *kalloc(void);
extern void kfree(void *);
#endif

static uint32 last_ticks;
static uint32 last_logged_second;
static uint32 last_yield_second;

#ifdef WIND_ESP_IDF_APP
#define WIND_SELFTEST_MAX_PAGES 64U
#define WIND_TICK_REASON_YIELD 0x1U
#define WIND_TICK_REASON_WAKEUP_RESCHED 0x2U
#define WIND_TICK_REASON_SLEEP_RESCHED 0x4U
static void *selftest_pages[WIND_SELFTEST_MAX_PAGES];
static uint32 selftest_allocated;
static int selftest_ok;
static int proc_service_ok;
static int sched_service_ok;

#define WIND_DEMO_SLEEP_CHAN 1U

static const char *
xtensa_tick_reason_string(uint32 reason_flags)
{
  switch(reason_flags){
  case 0:
    return "normal";
  case WIND_TICK_REASON_YIELD:
    return "yield";
  case WIND_TICK_REASON_WAKEUP_RESCHED:
    return "wakeup-resched";
  case WIND_TICK_REASON_SLEEP_RESCHED:
    return "sleep-resched";
  case WIND_TICK_REASON_YIELD | WIND_TICK_REASON_WAKEUP_RESCHED:
    return "yield+wakeup-resched";
  case WIND_TICK_REASON_YIELD | WIND_TICK_REASON_SLEEP_RESCHED:
    return "yield+sleep-resched";
  case WIND_TICK_REASON_WAKEUP_RESCHED | WIND_TICK_REASON_SLEEP_RESCHED:
    return "wakeup+sleep-resched";
  case WIND_TICK_REASON_YIELD | WIND_TICK_REASON_WAKEUP_RESCHED | WIND_TICK_REASON_SLEEP_RESCHED:
    return "yield+wakeup+sleep-resched";
  default:
    return "unknown";
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
xtensa_proc_service_selftest(void)
{
  int i;

  proc_service_ok = 1;
  xtensa_proc_service_init();
  for(i = 0; i < 3; i++){
    if(xtensa_proc_service_create(100 + i) != 0){
      proc_service_ok = 0;
      break;
    }
  }

  kprintf("wind: proc service state active=%u free_pages=%u/%u\n",
          xtensa_proc_service_active_count(),
          xtensa_memory_free_pages(),
          xtensa_memory_total_pages());

  xtensa_proc_service_destroy_all();
  if(xtensa_proc_service_active_count() != 0)
    proc_service_ok = 0;
  if(xtensa_memory_free_pages() != xtensa_memory_total_pages())
    proc_service_ok = 0;
}

static void
xtensa_sched_service_bootstrap(void)
{
  int i;

  sched_service_ok = 1;
  xtensa_sched_init();
  for(i = 0; i < 3; i++){
    if(xtensa_sched_create_proc(200 + i) != 0){
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
  xtensa_proc_service_selftest();
  kprintf("wind: proc service selftest %s (free_pages=%u/%u)\n",
          proc_service_ok ? "PASS" : "FAIL",
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
  last_yield_second = (uint32)-1;
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
  uint32 now = timer_ticks();
  uint32 reason_flags;
  uint32 seconds = now / XTENSA_TICK_HZ;
  int need_resched;
  int yielded_this_tick;

  if(now != last_ticks && seconds != last_logged_second){
    need_resched = 0;
    reason_flags = 0;
    yielded_this_tick = 0;
    if((seconds % 7U) == 0U && seconds != last_yield_second){
      struct xtensa_trapframe tf;

      tf.syscall_no = WIND_SYSCALL_YIELD;
      tf.arg0 = 0;
      tf.retval = -1;
      xtensa_trap_handle_syscall(&tf);
      kprintf("wind: fake trap/syscall yield dispatched ret=%d\n", tf.retval);
      last_yield_second = seconds;
      yielded_this_tick = 1;
      reason_flags |= WIND_TICK_REASON_YIELD;
    }

    if(!yielded_this_tick)
      xtensa_sched_step();

    if((seconds % 5U) == 0U){
      if(xtensa_sched_wakeup_chan(WIND_DEMO_SLEEP_CHAN) == 0){
        kprintf("wind: wakeup chan=%u\n", WIND_DEMO_SLEEP_CHAN);
        need_resched = 1;
        reason_flags |= WIND_TICK_REASON_WAKEUP_RESCHED;
      }
    }
    if((seconds % 5U) == 4U){
      int slept_pid = xtensa_sched_sleep_current_on_chan(WIND_DEMO_SLEEP_CHAN);
      if(slept_pid >= 0){
        kprintf("wind: sleep pid=%d chan=%u\n", slept_pid, WIND_DEMO_SLEEP_CHAN);
        need_resched = 1;
        reason_flags |= WIND_TICK_REASON_SLEEP_RESCHED;
      }
    }

    if(need_resched)
      xtensa_sched_step();

        kprintf("wind: tick=%u seconds=%u reason=%s current_pid=%d runnable=%u sleeping=%u free_pages=%u/%u\n",
            now,
            seconds,
          xtensa_tick_reason_string(reason_flags),
            xtensa_sched_current_pid(),
            xtensa_sched_runnable_count(),
            xtensa_sched_sleeping_count(),
            xtensa_memory_free_pages(),
            xtensa_memory_total_pages());

    if((seconds % 10U) == 0U){
      kprintf("wind: sched dump cmd\n");
      xtensa_sched_dump();
    }

    last_ticks = now;
    last_logged_second = seconds;
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
