#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
#include <stdarg.h>

#include "esp_rom_sys.h"

#define BACKSPACE 0x100
#define CONSOLE_LINE_BUFSZ 256
#define CONSOLE_INPUT_BUFSZ 256
#define CONSOLE_LINE_CHAN 0x434F4E53U

static char console_line[CONSOLE_LINE_BUFSZ];
static uint32 console_len;
static struct {
  char buf[CONSOLE_INPUT_BUFSZ];
  uint32 r;
  uint32 w;
  uint32 e;
} console_input;
static char digits[] = "0123456789abcdef";

static void
console_flush(void)
{
  if(console_len == 0)
    return;

  console_line[console_len] = '\0';
  esp_rom_printf("%s\n", console_line);
  console_len = 0;
}

void
consputc(int c)
{
  if(c == '\r')
    return;

  if(c == BACKSPACE){
    if(console_len > 0)
      console_len--;
    return;
  }

  if(c == '\n'){
    console_flush();
    return;
  }

  if(console_len >= CONSOLE_LINE_BUFSZ - 1)
    console_flush();

  console_line[console_len++] = (char)c;
}

uint32
xtensa_console_line_chan(void)
{
  return CONSOLE_LINE_CHAN;
}

static void
console_input_echo_backspace(void)
{
  uart_putc('\b');
  uart_putc(' ');
  uart_putc('\b');
}

static void
console_input_commit_line(void)
{
  if(console_input.w != console_input.e){
    console_input.w = console_input.e;
    xtensa_sched_wakeup_chan(CONSOLE_LINE_CHAN);
  }
}

static void
console_input_ingest_char(int c)
{
  if(c < 0)
    return;

  if(c == '\r')
    c = '\n';

  if(c == '\b' || c == 0x7f){
    if(console_input.e != console_input.w){
      console_input.e--;
      console_input_echo_backspace();
    }
    return;
  }

  /* e/r are monotonic counters; this prevents ring overwrite of unread bytes. */
  if((console_input.e - console_input.r) >= CONSOLE_INPUT_BUFSZ)
    return;

  if(c == '\n'){
    uart_putc('\r');
    uart_putc('\n');
  } else if(c >= 0x20 && c < 0x7f){
    uart_putc((char)c);
  } else {
    return;
  }

  console_input.buf[console_input.e % CONSOLE_INPUT_BUFSZ] = (char)c;
  console_input.e++;

  if(c == '\n' || (console_input.e - console_input.r) == CONSOLE_INPUT_BUFSZ)
    console_input_commit_line();
}

void
xtensa_console_poll_input(void)
{
  int c;
  while((c = uart_getc_nonblock()) >= 0)
    console_input_ingest_char(c);
}

int
xtensa_console_read(char *dst, uint32 maxlen)
{
  uint32 n = 0;

  if(dst == 0 || maxlen == 0)
    return -1;
  if(console_input.r == console_input.w)
    return -1;

  while(n < maxlen && console_input.r != console_input.w){
    char c = console_input.buf[console_input.r % CONSOLE_INPUT_BUFSZ];
    console_input.r++;
    dst[n++] = c;
    if(c == '\n')
      break;
  }

  /*
   * Once all committed bytes are consumed, collapse edit to write so the next
   * line starts at the same logical point.
   */
  if(console_input.r == console_input.w)
    console_input.e = console_input.w;

  return (int)n;
}

static void
printint(long long xx, int base, int sign)
{
  char buf[20];
  int i;
  unsigned long long x;

  if(sign && xx < 0)
    x = (unsigned long long)(-xx);
  else
    x = (unsigned long long)xx;

  i = 0;
  do {
    buf[i++] = digits[x % (unsigned)base];
  } while((x /= (unsigned)base) != 0);

  if(sign && xx < 0)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;

  consputc('0');
  consputc('x');
  for(i = 0; i < (int)(sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

int
kprintf(const char *fmt, ...)
{
  int i, cx, c0, c1, c2;
  const char *s;
  va_list ap;

  va_start(ap, fmt);
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      consputc(cx);
      continue;
    }

    i++;
    c0 = fmt[i + 0] & 0xff;
    c1 = c2 = 0;
    if(c0)
      c1 = fmt[i + 1] & 0xff;
    if(c1)
      c2 = fmt[i + 2] & 0xff;

    if(c0 == 'd'){
      printint(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      printint(va_arg(ap, long), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      printint(va_arg(ap, long long), 10, 1);
      i += 2;
    } else if(c0 == 'u'){
      printint(va_arg(ap, unsigned int), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){
      printint(va_arg(ap, unsigned long), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      printint(va_arg(ap, unsigned long long), 10, 0);
      i += 2;
    } else if(c0 == 'x'){
      printint(va_arg(ap, unsigned int), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){
      printint(va_arg(ap, unsigned long), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      printint(va_arg(ap, unsigned long long), 16, 0);
      i += 2;
    } else if(c0 == 'p'){
      printptr((uint64)va_arg(ap, void *));
    } else if(c0 == 'c'){
      consputc(va_arg(ap, int));
    } else if(c0 == 's'){
      s = va_arg(ap, const char *);
      if(s == 0)
        s = "(null)";
      while(*s)
        consputc(*s++);
    } else if(c0 == '%'){
      consputc('%');
    } else if(c0 == 0){
      break;
    } else {
      consputc('%');
      consputc(c0);
    }
  }
  va_end(ap);
  return 0;
}

#else

uint32
xtensa_console_line_chan(void)
{
  return 0;
}

void
xtensa_console_poll_input(void)
{
}

int
xtensa_console_read(char *dst, uint32 maxlen)
{
  (void)dst;
  (void)maxlen;
  return -1;
}

void
consputc(int c)
{
  uart_putc((char)c);
}

int
kprintf(const char *fmt, ...)
{
  uart_puts((const char *)fmt);
  return 0;
}

#endif
