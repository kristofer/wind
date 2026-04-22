#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP
extern void *kalloc(void);
extern void kfree(void *);
#endif

#define WIND_PROC_SLOTS 4U

struct wind_proc_slot {
  int pid;
  void *kstack_page;
};

static struct wind_proc_slot slots[WIND_PROC_SLOTS];
static uint32 active_count;

void
xtensa_proc_service_init(void)
{
  uint32 i;

  for(i = 0; i < WIND_PROC_SLOTS; i++){
    slots[i].pid = 0;
    slots[i].kstack_page = 0;
  }
  active_count = 0;
}

int
xtensa_proc_service_create(int pid)
{
  uint32 i;

  for(i = 0; i < WIND_PROC_SLOTS; i++){
    if(slots[i].kstack_page == 0){
#ifdef WIND_ESP_IDF_APP
      slots[i].kstack_page = kalloc();
#else
      slots[i].kstack_page = xtensa_page_alloc();
#endif
      if(slots[i].kstack_page == 0)
        return -1;
      slots[i].pid = pid;
      active_count++;
      return 0;
    }
  }

  return -1;
}

void
xtensa_proc_service_destroy_all(void)
{
  uint32 i;

  for(i = 0; i < WIND_PROC_SLOTS; i++){
    if(slots[i].kstack_page != 0){
#ifdef WIND_ESP_IDF_APP
      kfree(slots[i].kstack_page);
#else
      xtensa_page_free(slots[i].kstack_page);
#endif
      slots[i].kstack_page = 0;
      slots[i].pid = 0;
    }
  }
  active_count = 0;
}

uint32
xtensa_proc_service_active_count(void)
{
  return active_count;
}
