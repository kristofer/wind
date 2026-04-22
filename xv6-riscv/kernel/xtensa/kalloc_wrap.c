#include "kernel/types.h"
#include "kernel/xtensa/port.h"

void
kinit(void)
{
  xtensa_memory_init();
}

void *
kalloc(void)
{
  return xtensa_page_alloc();
}

void
kfree(void *pa)
{
  xtensa_page_free(pa);
}
