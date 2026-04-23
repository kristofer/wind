#include "kernel/types.h"
#include "kernel/xtensa/port.h"

#include <string.h>

static uint32 syscall_count;

static int
xtensa_sys_yield(struct xtensa_trapframe *tf)
{
  (void)tf;
  xtensa_sched_step();
  return xtensa_sched_current_pid();
}

static int
xtensa_sys_sleep_on_chan(struct xtensa_trapframe *tf)
{
  uint32 chan = tf->arg0;
  int pid = xtensa_sched_sleep_current_on_chan(chan);
  return pid;
}

static int
xtensa_sys_wakeup_chan(struct xtensa_trapframe *tf)
{
  uint32 chan = tf->arg0;
  int ret = xtensa_sched_wakeup_chan(chan);
  return ret;
}

static int
xtensa_sys_getpid(struct xtensa_trapframe *tf)
{
  (void)tf;
  return xtensa_sched_current_pid();
}

static int
xtensa_sys_exit(struct xtensa_trapframe *tf)
{
  int code = (int)tf->arg0;
  xtensa_sched_exit_current(code);
  return 0;
}

static int
xtensa_sys_wait(struct xtensa_trapframe *tf)
{
  int wstatus = 0;
  int child_pid = xtensa_sched_wait_current(&wstatus);
  /* internal -2 means the caller was put to sleep waiting for a child */
  if(child_pid == -2)
    child_pid = -1;
  tf->arg0 = (uint32)wstatus;
  return child_pid;
}

static int
xtensa_sys_kill(struct xtensa_trapframe *tf)
{
  int pid = (int)tf->arg0;
  return xtensa_sched_kill_pid(pid);
}
/*
 * xtensa_sys_write — WIND_SYSCALL_WRITE
 *
 * Writes a null-terminated string from the calling proc's uregion,
 * starting at byte offset tf->arg0, to the console.  This is the
 * Phase 5 analogue of xv6 sys_write(fd=1, buf, len): the proc
 * supplies a "user virtual address" (uregion offset), and the kernel
 * resolves it to a physical pointer via wind_uaddr_to_kaddr before
 * touching the bytes — the same translation step a real sys_write
 * would perform on a paged system.
 *
 * Returns the number of bytes written, or -1 on error.
 */
static int
xtensa_sys_write(struct xtensa_trapframe *tf)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  uint32 uoffset = tf->arg0;
  const char *s;
  uint32 n;

  if(p == 0 || p->ubase == 0)
    return -1;
  if(uoffset >= p->usz)
    return -1;

  s = (const char *)wind_uaddr_to_kaddr(p, uoffset);
  /* write until null byte or end of region */
  for(n = 0; (uoffset + n) < p->usz && s[n] != '\0'; n++)
    consputc((int)(unsigned char)s[n]);
  return (int)n;
}

static int
xtensa_sys_read(struct xtensa_trapframe *tf)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  uint32 uoffset = tf->arg0;
  uint32 maxlen = tf->arg1;
  uint32 avail;
  int n;

  if(p == 0 || p->ubase == 0 || maxlen == 0)
    return -1;
  if(uoffset >= p->usz)
    return -1;

  avail = p->usz - uoffset;
  if(maxlen > avail)
    maxlen = avail;

  n = xtensa_console_read((char *)wind_uaddr_to_kaddr(p, uoffset), maxlen);
  if(n >= 0)
    return n;

  /* No committed line yet: sleep and let caller retry after wakeup. */
  (void)xtensa_sched_sleep_current_on_chan(xtensa_console_line_chan());
  return -1;
}

/*
 * xtensa_sys_exec — WIND_SYSCALL_EXEC
 *
 * Pseudo-exec: replaces the calling proc's entry fn with the function
 * pointer in tf->arg0.  fn_state is reset and uregion freed so the new
 * fn starts fresh on its first invocation.  Caller must return.
 */
static int
xtensa_sys_exec(struct xtensa_trapframe *tf)
{
  void (*fn)(struct xtensa_proc *) =
    (void (*)(struct xtensa_proc *))(uint32)tf->arg0;
  return xtensa_sched_exec_current(fn);
}
/*
 * ROMFS catalog — set once at boot by xtensa_romfs_catalog_set.
 * Paths are looked up for spawn/exec and read-only opens.
 */
#define WIND_ROMFS_FD_MAX 8U
#define WIND_ROMFS_PATH_MAX 64U
#define WIND_ROMFS_MIN_PATH_BUFSZ 2U

struct wind_romfs_fd_state {
  const struct wind_romfs_entry *entry;
  uint32 off;
};

static const struct wind_romfs_entry *romfs_table;
static uint32 romfs_table_count;
static struct wind_romfs_fd_state romfs_fds[WIND_ROMFS_FD_MAX];

static const struct wind_romfs_entry *
xtensa_romfs_lookup(const char *path)
{
  uint32 i;

  if(path == 0 || romfs_table == 0)
    return 0;

  for(i = 0; i < romfs_table_count; i++){
    if(strcmp(romfs_table[i].path, path) == 0)
      return &romfs_table[i];
  }
  return 0;
}

static int
xtensa_romfs_resolve_exec_path(const char *name_or_path, char *dst, uint32 dst_len)
{
  const char *prefix = "/bin/";
  uint32 i = 0;
  uint32 j = 0;

  if(name_or_path == 0 || dst == 0 || dst_len < WIND_ROMFS_MIN_PATH_BUFSZ)
    return -1;

  if(name_or_path[0] == '/'){
    for(i = 0; i + 1U < dst_len && name_or_path[i] != '\0'; i++)
      dst[i] = name_or_path[i];
    dst[i] = '\0';
    if(name_or_path[i] != '\0')
      return -1;
    return 0;
  }

  for(i = 0; i + 1U < dst_len && prefix[i] != '\0'; i++)
    dst[i] = prefix[i];
  if(prefix[i] != '\0')
    return -1;
  j = i;
  for(i = 0; j + 1U < dst_len && name_or_path[i] != '\0'; i++, j++)
    dst[j] = name_or_path[i];
  dst[j] = '\0';
  if(name_or_path[i] != '\0')
    return -1;
  return 0;
}

void
xtensa_romfs_catalog_set(const struct wind_romfs_entry *table, uint32 count)
{
  romfs_table = table;
  romfs_table_count = count;
  memset(romfs_fds, 0, sizeof(romfs_fds));
  kprintf("wind: romfs catalog registered count=%u\n", count);
}

int
xtensa_romfs_open(const char *path)
{
  const struct wind_romfs_entry *entry;
  uint32 i;

  entry = xtensa_romfs_lookup(path);
  if(entry == 0)
    return -1;

  for(i = 0; i < WIND_ROMFS_FD_MAX; i++){
    if(romfs_fds[i].entry == 0){
      romfs_fds[i].entry = entry;
      romfs_fds[i].off = 0;
      return (int)i;
    }
  }
  return -1;
}

int
xtensa_romfs_read(int fd, void *dst_void, uint32 maxlen)
{
  const struct wind_romfs_entry *entry;
  char *dst = (char *)dst_void;
  const char *src;
  uint32 fdu;
  uint32 n;

  if(fd < 0 || dst == 0)
    return -1;
  fdu = (uint32)fd;
  if(fdu >= WIND_ROMFS_FD_MAX)
    return -1;
  if(maxlen == 0)
    return 0;

  entry = romfs_fds[fdu].entry;
  if(entry == 0)
    return -1;

  if(entry->kind == WIND_ROMFS_DEV){
    if(strcmp(entry->path, WIND_ROMFS_DEV_CONSOLE_PATH) == 0)
      return xtensa_console_read(dst, maxlen);
    return -1;
  }

  if(entry->kind != WIND_ROMFS_DATA || entry->data == 0)
    return -1;

  if(romfs_fds[fdu].off >= entry->data_len){
    romfs_fds[fdu].entry = 0;
    romfs_fds[fdu].off = 0;
    return 0;
  }

  n = entry->data_len - romfs_fds[fdu].off;
  if(n > maxlen)
    n = maxlen;

  src = entry->data + romfs_fds[fdu].off;
  memcpy(dst, src, n);
  romfs_fds[fdu].off += n;
  return (int)n;
}

int
xtensa_romfs_close(int fd)
{
  uint32 fdu;

  if(fd < 0)
    return -1;
  fdu = (uint32)fd;
  if(fdu >= WIND_ROMFS_FD_MAX)
    return -1;

  romfs_fds[fdu].entry = 0;
  romfs_fds[fdu].off = 0;
  return 0;
}

int
xtensa_romfs_exec_path(const char *path)
{
  const struct wind_romfs_entry *entry;

  entry = xtensa_romfs_lookup(path);
  if(entry == 0 || entry->kind != WIND_ROMFS_EXEC || entry->fn == 0)
    return -1;

  return xtensa_sched_exec_current(entry->fn);
}

/*
 * xtensa_sys_exec_by_name — WIND_SYSCALL_EXEC_BY_NAME
 *
 * arg0 is a uregion byte offset containing the null-terminated program
 * name.  The kernel resolves it to a kernel pointer, looks up the
 * program table, and calls xtensa_sched_exec_current with the matching
 * entry function.  This is the exec(path,...) system call analogue:
 * the proc supplies a "user virtual address" for the path string, and
 * the kernel translates it before touching the bytes — exactly the
 * copyinstr() step in xv6's sys_exec.
 */
static int
xtensa_sys_exec_by_name(struct xtensa_trapframe *tf)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  uint32 uoffset = tf->arg0;
  const char *name;
  char path[WIND_ROMFS_PATH_MAX];

  if(p == 0 || p->ubase == 0 || uoffset >= p->usz)
    return -1;

  name = (const char *)wind_uaddr_to_kaddr(p, uoffset);
  if(xtensa_romfs_resolve_exec_path(name, path, sizeof(path)) != 0)
    return -1;
  if(xtensa_romfs_exec_path(path) == 0)
    return 0;
  kprintf("wind: exec_by_name: path '%s' not found\n", path);
  return -1;
}

/*
 * xtensa_sys_spawn — WIND_SYSCALL_SPAWN
 *
 * Resolves a program name from the calling proc's uregion at offset
 * tf->arg0, looks it up in the program table, and creates a new child
 * proc running the matching entry function via xtensa_sched_create_child.
 * Returns the child's pid on success, -1 on error.
 *
 * Unlike exec_by_name the calling proc is NOT replaced; it continues
 * running and should call wind_wait() to reap the child.
 * This is the fork()+exec() primitive for the flat model.
 */
static int
xtensa_sys_spawn(struct xtensa_trapframe *tf)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  uint32 uoffset = tf->arg0;
  const char *line;
  const char *cmd_start;
  const char *cmd_args;
  uint32 cmd_len;
  uint32 i;
  const struct wind_romfs_entry *entry;
  char name[WIND_ROMFS_PATH_MAX];
  char cmdline[WIND_PROC_CMDLINE_MAX];
  char path[WIND_ROMFS_PATH_MAX];

  if(p == 0 || p->ubase == 0 || uoffset >= p->usz)
    return -1;

  line = (const char *)wind_uaddr_to_kaddr(p, uoffset);
  while(*line == ' ' || *line == '\t')
    line++;
  if(*line == '\0')
    return -1;

  cmd_start = line;
  while(*line != '\0' && *line != ' ' && *line != '\t')
    line++;
  cmd_len = (uint32)(line - cmd_start);
  if(cmd_len == 0 || cmd_len + 1U > sizeof(name))
    return -1;
  memcpy(name, cmd_start, cmd_len);
  name[cmd_len] = '\0';

  if(xtensa_romfs_resolve_exec_path(name, path, sizeof(path)) != 0)
    return -1;

  entry = xtensa_romfs_lookup(path);
  if(entry != 0 && entry->kind == WIND_ROMFS_EXEC && entry->fn != 0)
  {
    cmd_args = cmd_start;
    for(i = 0; i + 1U < sizeof(cmdline) && cmd_args[i] != '\0'; i++)
      cmdline[i] = cmd_args[i];
    cmdline[i] = '\0';
    return xtensa_sched_create_child(entry->fn, cmdline);
  }

  kprintf("wind: spawn: path '%s' not found\n", path);
  return -1;
}
void
xtensa_trap_init(void)
{
  syscall_count = 0;
  kprintf("wind: trap/syscall scaffold ready\n");
}

void
xtensa_trap_handle_syscall(struct xtensa_trapframe *tf)
{
  if(tf == 0)
    return;

  syscall_count++;
  switch(tf->syscall_no){
  case WIND_SYSCALL_YIELD:
    tf->retval = xtensa_sys_yield(tf);
    kprintf("wind: syscall yield ret_pid=%d count=%u\n", tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_SLEEP_ON_CHAN:
    tf->retval = xtensa_sys_sleep_on_chan(tf);
    kprintf("wind: syscall sleep_on_chan(chan=%u) ret_pid=%d count=%u\n", 
            tf->arg0, tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_WAKEUP_CHAN:
    tf->retval = xtensa_sys_wakeup_chan(tf);
    kprintf("wind: syscall wakeup_chan(chan=%u) ret=%d count=%u\n", 
            tf->arg0, tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_GETPID:
    tf->retval = xtensa_sys_getpid(tf);
    break;
  case WIND_SYSCALL_EXIT:
    xtensa_sys_exit(tf);
    tf->retval = 0;
    kprintf("wind: syscall exit(code=%d) count=%u\n", (int)tf->arg0, syscall_count);
    break;
  case WIND_SYSCALL_WAIT:
    tf->retval = xtensa_sys_wait(tf);
    if((int)tf->retval >= 0)
      kprintf("wind: syscall wait child=%d status=%d count=%u\n", tf->retval, (int)tf->arg0, syscall_count);
    break;
  case WIND_SYSCALL_KILL:
    tf->retval = xtensa_sys_kill(tf);
    kprintf("wind: syscall kill(pid=%d) ret=%d count=%u\n", (int)tf->arg0, (int)tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_WRITE:
    tf->retval = xtensa_sys_write(tf);
    kprintf("wind: syscall write(uoff=%u) ret=%d count=%u\n", tf->arg0, (int)tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_READ:
    tf->retval = xtensa_sys_read(tf);
    if((int)tf->retval >= 0)
      kprintf("wind: syscall read(uoff=%u,max=%u) ret=%d count=%u\n",
              tf->arg0, tf->arg1, (int)tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_EXEC:
    tf->retval = xtensa_sys_exec(tf);
    kprintf("wind: syscall exec ret=%d count=%u\n", (int)tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_EXEC_BY_NAME:
    tf->retval = xtensa_sys_exec_by_name(tf);
    kprintf("wind: syscall exec_by_name ret=%d count=%u\n", (int)tf->retval, syscall_count);
    break;
  case WIND_SYSCALL_SPAWN:
    tf->retval = xtensa_sys_spawn(tf);
    kprintf("wind: syscall spawn ret=%d count=%u\n", (int)tf->retval, syscall_count);
    break;
  default:
    tf->retval = -1;
    kprintf("wind: syscall unknown no=%u count=%u\n", tf->syscall_no, syscall_count);
    break;
  }
}

/* ---- kernel proc API: call from within a proc's fn ---- */

void
wind_yield(void)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_YIELD;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_sleep_on_chan(uint32 chan)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_SLEEP_ON_CHAN;
  tf.arg0 = chan;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_wakeup_chan(uint32 chan)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_WAKEUP_CHAN;
  tf.arg0 = chan;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
}

void
wind_exit(int code)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_EXIT;
  tf.arg0 = (uint32)code;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  /* proc is now UNUSED; caller must return immediately */
}

int
wind_getpid(void)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_GETPID;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  return tf.retval;
}

int
wind_wait(int *wstatus)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_WAIT;
  tf.arg0 = 0;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  if(wstatus != 0)
    *wstatus = (int)tf.arg0;
  return tf.retval;
}

int
wind_kill(int pid)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_KILL;
  tf.arg0 = (uint32)pid;
  tf.retval = -1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}

/*
 * Flat memory model helpers.
 *
 * wind_proc_uregion_alloc(sz):
 *   Allocates a contiguous sz-byte user region for the calling proc.
 *   Returns 0 on success, -1 on failure or if already allocated.
 *   After return, p->ubase and p->usz are valid; use wind_uaddr_to_kaddr
 *   to convert user offsets to kernel pointers.
 *
 * wind_proc_uregion_free():
 *   Frees the calling proc's user region.  Idempotent; safe to call when
 *   no region is allocated.  The reap path also frees the region
 *   automatically, so an explicit call before wind_exit is optional but
 *   good practice when the region is no longer needed.
 */
int
wind_proc_uregion_alloc(uint32 sz)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  if(p == 0)
    return -1;
  return xtensa_user_alloc(p, sz);
}

void
wind_proc_uregion_free(void)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  if(p == 0)
    return;
  xtensa_user_free(p);
}

int
wind_write(uint32 uoffset)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_WRITE;
  tf.arg0 = uoffset;
  tf.retval = (uint32)-1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}

int
wind_read(uint32 uoffset, uint32 maxlen)
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_READ;
  tf.arg0 = uoffset;
  tf.arg1 = maxlen;
  tf.retval = (uint32)-1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}

/*
 * wind_exec — pseudo-exec; replaces the calling proc's entry fn.
 * Caller MUST return immediately after this call.
 */
int
wind_exec(void (*fn)(struct xtensa_proc *))
{
  struct xtensa_trapframe tf;
  tf.syscall_no = WIND_SYSCALL_EXEC;
  tf.arg0 = (uint32)fn;
  tf.retval = (uint32)-1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}

/*
 * wind_exec_by_name — exec named program from the program table.
 *
 * Writes name into the calling proc's uregion (allocating one if absent),
 * then issues WIND_SYSCALL_EXEC_BY_NAME so the kernel copies the name
 * from the proc's flat region via wind_uaddr_to_kaddr, exactly as
 * sys_exec would use copyinstr().  Caller MUST return immediately.
 *
 * If the proc has no uregion a temporary 64-byte one is allocated for
 * the duration of the syscall.  It is freed by exec_current before the
 * new fn's first invocation.
 */
int
wind_exec_by_name(const char *name)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  struct xtensa_trapframe tf;
  uint8 *buf;
  uint32 i;
  int free_after = 0;

  if(p == 0 || name == 0)
    return -1;

  if(p->ubase == 0){
    if(xtensa_user_alloc(p, 64) != 0)
      return -1;
    free_after = 1;
  }

  buf = (uint8 *)wind_uaddr_to_kaddr(p, 0);
  for(i = 0; name[i] != '\0' && i < (p->usz - 1U); i++)
    buf[i] = (uint8)name[i];
  buf[i] = '\0';

  tf.syscall_no = WIND_SYSCALL_EXEC_BY_NAME;
  tf.arg0 = 0;  /* uregion offset 0 */
  tf.retval = (uint32)-1;
  xtensa_trap_handle_syscall(&tf);

  /* exec_current frees the uregion; no need to free_after on success */
  if((int)tf.retval < 0 && free_after)
    xtensa_user_free(p);

  return (int)tf.retval;
}

/*
 * wind_spawn — create a child process running the named program.
 *
 * Writes name into the calling proc's uregion (which must already be
 * allocated), issues WIND_SYSCALL_SPAWN, and returns the child's pid on
 * success or -1 on failure.  The calling proc continues running after
 * spawn returns; it should call wind_wait() to reap the child when it
 * exits.  This is the xv6 fork()+exec() equivalent for the flat model.
 */
int
wind_spawn(const char *name)
{
  struct xtensa_proc *p = xtensa_sched_current_proc();
  struct xtensa_trapframe tf;
  uint8 *buf;
  uint32 i;

  if(p == 0 || p->ubase == 0 || name == 0)
    return -1;

  buf = (uint8 *)wind_uaddr_to_kaddr(p, 0);
  for(i = 0; name[i] != '\0' && i < (p->usz - 1U); i++)
    buf[i] = (uint8)name[i];
  buf[i] = '\0';

  tf.syscall_no = WIND_SYSCALL_SPAWN;
  tf.arg0 = 0;
  tf.retval = (uint32)-1;
  xtensa_trap_handle_syscall(&tf);
  return (int)tf.retval;
}
