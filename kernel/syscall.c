#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
    return -1;
  if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if (err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n)
  {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(int n, char *buf, int max)
{
  uint64 addr;
  if (argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);    //lab 2.1
extern uint64 sys_sysinfo(void);  //lab 2.2

static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,
    [SYS_exit] sys_exit,
    [SYS_wait] sys_wait,
    [SYS_pipe] sys_pipe,
    [SYS_read] sys_read,
    [SYS_kill] sys_kill,
    [SYS_exec] sys_exec,
    [SYS_fstat] sys_fstat,
    [SYS_chdir] sys_chdir,
    [SYS_dup] sys_dup,
    [SYS_getpid] sys_getpid,
    [SYS_sbrk] sys_sbrk,
    [SYS_sleep] sys_sleep,
    [SYS_uptime] sys_uptime,
    [SYS_open] sys_open,
    [SYS_write] sys_write,
    [SYS_mknod] sys_mknod,
    [SYS_unlink] sys_unlink,
    [SYS_link] sys_link,
    [SYS_mkdir] sys_mkdir,
    [SYS_close] sys_close,
    [SYS_trace] sys_trace,      // lab 2.1
    [SYS_sysinfo] sys_sysinfo,  // lab 2.2
};

//syscalls_name 数组
static char *syscalls_name[] = {
        "",
        "fork",
        "exit",
        "wait",
        "pipe",
        "read",
        "kill",
        "exec",
        "fstat",
        "chdir",
        "dup",
        "getpid",
        "sbrk",
        "sleep",
        "uptime",
        "open",
        "write",
        "mknod",
        "unlink",
        "link",
        "mkdir",
        "close",
        "trace",    //lab 2.1
        "sysinfo"   //lab 2.2
};

//syscall函数 操作系统处理系统调用的核心部分
void syscall(void) 
{
  int num; //系统调用编号
  struct proc *p = myproc(); //获取当前进程的进程控制块（PCB）结构体指针

  //从当前进程的陷阱帧（trapframe）中获取系统调用编号，并存储在变量num中
  //陷阱帧是在发生中断或系统调用时保存的处理器状态
  num = p->trapframe->a7;

  //判断系统调用编号是否有效
  //num < NELEM(syscalls) 确保编号不超过系统调用表的长度
  //syscalls[num] 检查系统调用表中对应编号的函数指针是否非空
  if (num > 0 && num < NELEM(syscalls) && syscalls[num])
  {
    //系统调用编号有效 调用系统调用表中对应的函数 返回值存储在陷阱帧的a0寄存器中
    p->trapframe->a0 = syscalls[num]();

    //打印跟踪输出，用于调试或跟踪系统调用的执行
    if ((1 << num) & p->mask) //判断当前系统调用的编号是否在进程的跟踪掩码中设置
      //打印当前进程的PID、系统调用的名称和返回值
      printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], p->trapframe->a0);
  }
  else
  {
    //系统调用编号无效，打印错误信息
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    //将陷阱帧的a0寄存器设置为-1，表示系统调用失败
    p->trapframe->a0 = -1;
  }
}