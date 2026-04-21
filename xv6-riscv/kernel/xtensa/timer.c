#include "types.h"
#include "kernel/xtensa/port.h"
#include "kernel/xtensa/xtensa.h"

static volatile uint32 ticks;
static uint32 tick_interval_cycles;

void
xtensa_timer_interrupt(void)
{
  xtensa_write_ccompare0(xtensa_read_ccompare0() + tick_interval_cycles);
  ticks++;
}

void
timer_init(uint32 cpu_hz, uint32 tick_hz)
{
  tick_interval_cycles = cpu_hz / tick_hz;
  ticks = 0;
  xtensa_write_ccompare0(xtensa_read_ccount() + tick_interval_cycles);
  xtensa_enable_interrupts();
}

uint32
timer_ticks(void)
{
  return ticks;
}
