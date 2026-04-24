#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/xtensa/port.h"

void wind_net_console_start(void);

void
app_main(void)
{
  wind_net_console_start();
  xtensa_kernel_init();
  for(;;){
    xtensa_kernel_poll();
    vTaskDelay(1);  /* 1 FreeRTOS tick (10ms at 100 Hz) — lets IDLE reset the WDT */
  }
}