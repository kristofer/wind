#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/xtensa/port.h"

void
app_main(void)
{
  xtensa_kernel_init();
  for(;;){
    xtensa_kernel_poll();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}