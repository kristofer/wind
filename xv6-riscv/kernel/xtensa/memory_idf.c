#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#ifdef WIND_ESP_IDF_APP

#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#define XTENSA_PAGE_SIZE 4096U
#define XTENSA_PAGE_COUNT 32U

struct xtensa_page_run {
  struct xtensa_page_run *next;
};

static struct xtensa_page_run *free_list;
static void *page_pool;
static uint32 total_pages;
static uint32 free_pages;
static portMUX_TYPE allocator_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8 page_state[XTENSA_PAGE_COUNT];

enum {
  PAGE_STATE_ALLOCATED = 0,
  PAGE_STATE_FREE = 1,
};

static void
allocator_panic(const char *msg)
{
  kprintf("wind: allocator panic: %s\n", msg);
  esp_system_abort(msg);
}

static int
page_index_from_ptr(void *page, uint32 *out_index)
{
  uint64 pool_start;
  uint64 pool_end;
  uint64 addr;
  uint64 offset;

  if(page_pool == 0)
    return 0;

  pool_start = (uint64)page_pool;
  pool_end = pool_start + (uint64)XTENSA_PAGE_SIZE * (uint64)XTENSA_PAGE_COUNT;
  addr = (uint64)page;
  if(addr < pool_start || addr >= pool_end)
    return 0;

  offset = addr - pool_start;
  if((offset % XTENSA_PAGE_SIZE) != 0)
    return 0;

  *out_index = (uint32)(offset / XTENSA_PAGE_SIZE);
  return 1;
}

static inline void
allocator_lock_enter(void)
{
  portENTER_CRITICAL(&allocator_lock);
}

static inline void
allocator_lock_exit(void)
{
  portEXIT_CRITICAL(&allocator_lock);
}

void *
xtensa_page_alloc(void)
{
  struct xtensa_page_run *page;

  allocator_lock_enter();
  if(free_list == 0)
    page = 0;
  else {
    page = free_list;
    free_list = page->next;
    page_state[((uint64)(void *)page - (uint64)page_pool) / XTENSA_PAGE_SIZE] = PAGE_STATE_ALLOCATED;
    free_pages--;
    memset((void *)page, 5, XTENSA_PAGE_SIZE);
  }
  allocator_lock_exit();

  return (void *)page;
}

void
xtensa_page_free(void *page)
{
  uint32 index;
  struct xtensa_page_run *run;

  if(page == 0)
    return;

  allocator_lock_enter();
  if(!page_index_from_ptr(page, &index)){
    allocator_lock_exit();
    allocator_panic("kfree invalid pointer");
  }
  if(page_state[index] == PAGE_STATE_FREE){
    allocator_lock_exit();
    allocator_panic("kfree double free");
  }

  memset(page, 1, XTENSA_PAGE_SIZE);
  run = (struct xtensa_page_run *)page;
  run->next = free_list;
  free_list = run;
  page_state[index] = PAGE_STATE_FREE;
  free_pages++;
  allocator_lock_exit();
}

void
xtensa_memory_init(void)
{
  uint32 index;
  char *cursor;

  if(page_pool != 0)
    return;

  page_pool = heap_caps_aligned_alloc(XTENSA_PAGE_SIZE,
                                      XTENSA_PAGE_SIZE * XTENSA_PAGE_COUNT,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if(page_pool == 0){
    total_pages = 0;
    free_pages = 0;
    return;
  }

  free_list = 0;
  total_pages = XTENSA_PAGE_COUNT;
  free_pages = XTENSA_PAGE_COUNT;
  cursor = (char *)page_pool;
  allocator_lock_enter();
  for(index = 0; index < XTENSA_PAGE_COUNT; index++){
    struct xtensa_page_run *page = (struct xtensa_page_run *)(cursor + index * XTENSA_PAGE_SIZE);
    page->next = free_list;
    free_list = page;
    page_state[index] = PAGE_STATE_FREE;
  }
  allocator_lock_exit();
}

uint32
xtensa_memory_total_pages(void)
{
  return total_pages;
}

uint32
xtensa_memory_free_pages(void)
{
  uint32 count;

  allocator_lock_enter();
  count = free_pages;
  allocator_lock_exit();
  return count;
}

#else

void
xtensa_memory_init(void)
{
}

void *
xtensa_page_alloc(void)
{
  return 0;
}

void
xtensa_page_free(void *page)
{
  (void)page;
}

uint32
xtensa_memory_total_pages(void)
{
  return 0;
}

uint32
xtensa_memory_free_pages(void)
{
  return 0;
}

#endif