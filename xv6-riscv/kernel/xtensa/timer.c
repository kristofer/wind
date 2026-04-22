#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
#include "esp_timer.h"
#else
#include "kernel/xtensa/xtensa.h"
#endif

// Phase-1 bring-up runs on a single core; interrupt masking is sufficient here.
static volatile uint32 ticks;
static uint32 tick_interval_cycles;

#ifdef WIND_ESP_IDF_APP
static esp_timer_handle_t tick_timer;

static void
xtensa_tick_timer_cb(void *arg)
{
  (void)arg;
  ticks++;
}
#endif

void
xtensa_timer_interrupt(void)
{
#ifdef WIND_ESP_IDF_APP
  ticks++;
#else
  xtensa_write_ccompare0(xtensa_read_ccompare0() + tick_interval_cycles);
  ticks++;
#endif
}

void
timer_init(uint32 cpu_hz, uint32 tick_hz)
{
#ifdef WIND_ESP_IDF_APP
  const esp_timer_create_args_t timer_args = {
    .callback = xtensa_tick_timer_cb,
    .name = "wind_tick",
  };

  (void)cpu_hz;
  tick_interval_cycles = tick_hz;
  ticks = 0;
  if(tick_timer == 0){
    esp_timer_create(&timer_args, &tick_timer);
  }
  esp_timer_stop(tick_timer);
  esp_timer_start_periodic(tick_timer, 1000000ULL / tick_hz);
#else
  tick_interval_cycles = cpu_hz / tick_hz;
  ticks = 0;
  xtensa_write_ccompare0(xtensa_read_ccount() + tick_interval_cycles);
  xtensa_enable_interrupts();
#endif
}

uint32
timer_ticks(void)
{
#ifdef WIND_ESP_IDF_APP
  return ticks;
#else
  uint32 snapshot;

  xtensa_disable_interrupts();
  snapshot = ticks;
  xtensa_enable_interrupts();
  return snapshot;
#endif
}
