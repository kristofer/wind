#include "kernel/types.h"
#include "kernel/xtensa/xtensa.h"

#ifdef WIND_ESP_IDF_APP
#include <stdio.h>
#endif

#define UART0_BASE 0x60000000U
#define UART_FIFO_REG (UART0_BASE + 0x0U)
#define UART_STATUS_REG (UART0_BASE + 0x1CU)
#define UART_TXFIFO_CNT_MASK (0xFFU << 16)
#define UART_TXFIFO_CNT_SHIFT 16
#define UART_TXFIFO_SIZE 128U

static int
uart_txfifo_full(void)
{
  uint32 txfifo_cnt = (xtensa_mmio_read32(UART_STATUS_REG) & UART_TXFIFO_CNT_MASK) >>
                      UART_TXFIFO_CNT_SHIFT;
  return txfifo_cnt >= (UART_TXFIFO_SIZE - 1U);
}

void
uart_init(void)
{
}

void
uart_putc(char c)
{
#ifdef WIND_ESP_IDF_APP
  putchar((int)(unsigned char)c);
  fflush(stdout);
#else
  while(uart_txfifo_full())
    ;

  xtensa_mmio_write32(UART_FIFO_REG, (uint32)(uchar)c);
#endif
}

void
uart_puts(const char *s)
{
  while(*s != '\0'){
    if(*s == '\n')
      uart_putc('\r');

    uart_putc(*s);
    s++;
  }
}
