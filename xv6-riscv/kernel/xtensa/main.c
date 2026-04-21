#include "types.h"
#include "kernel/xtensa/port.h"

void
xtensa_handle_level1_interrupt(void)
{
  xtensa_timer_interrupt();
}

void
xtensa_handle_exception(void)
{
  uart_puts("\nwind: unhandled xtensa exception\n");
  for(;;)
    ;
}

void
xtensa_kernel_main(void)
{
  uint32 last_ticks;

  uart_init();
  uart_puts("\nwind: ESP32-S3 kernel bring-up\n");

  timer_init(XTENSA_CPU_HZ, XTENSA_TICK_HZ);
  uart_puts("wind: timer interrupt enabled\n");

  last_ticks = 0;
  for(;;){
    uint32 now = timer_ticks();
    if(now != last_ticks && (now % XTENSA_TICK_HZ) == 0){
      uart_puts("wind: timer tick\n");
      last_ticks = now;
    }
  }
}
