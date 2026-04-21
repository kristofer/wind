#ifndef XV6_XTENSA_H
#define XV6_XTENSA_H

#include "kernel/types.h"

static inline uint32
xtensa_read_ccount(void)
{
  uint32 value;
  asm volatile("rsr.ccount %0" : "=a"(value));
  return value;
}

static inline uint32
xtensa_read_ccompare0(void)
{
  uint32 value;
  asm volatile("rsr.ccompare0 %0" : "=a"(value));
  return value;
}

static inline void
xtensa_write_ccompare0(uint32 value)
{
  asm volatile("wsr.ccompare0 %0\n"
               "rsync"
               :
               : "a"(value)
               : "memory");
}

static inline void
xtensa_enable_interrupts(void)
{
  asm volatile("rsil a2, 0" : : : "a2", "memory");
}

static inline void
xtensa_disable_interrupts(void)
{
  asm volatile("rsil a2, 15" : : : "a2", "memory");
}

static inline uint32
xtensa_mmio_read32(uint32 addr)
{
  return *(volatile uint32 *)addr;
}

static inline void
xtensa_mmio_write32(uint32 addr, uint32 value)
{
  *(volatile uint32 *)addr = value;
}

#endif // XV6_XTENSA_H
