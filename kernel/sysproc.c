#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void)
{
  return myproc()->pid;
}

uint64 sys_fork(void)
{
  return fork();
}

uint64 sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64 sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64 sys_sleep(void)
{
  int n;
  uint ticks0;

  backtrace(); //调用backtrace()函数

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//sys_sigreturn()函数
uint64 sys_sigreturn(void)
{
  //获取当前进程的进程控制块 PCB 结构体指针
  struct proc* proc = myproc();
  
  //将之前保存的中断帧 trapframe 恢复到当前的 trapframe 中
  *proc->trapframe = proc->saved_trapframe; 
  
  //设置进程的have_return标志为1 表示已经准备好返回
  proc->have_return = 1; 

  //返回中断帧中a0寄存器的值 通常是系统调用的返回值
  return proc->trapframe->a0; 
}

//sys_sigalarm()函数
uint64 sys_sigalarm(void)
{
  int ticks;
  uint64 handler_va;

  argint(0, &ticks);       //获取系统调用参数中的第一个参数 ticks
  argaddr(1, &handler_va); //获取系统调用参数中的第二个参数 信号处理函数的虚拟地址

  struct proc* proc = myproc(); //获取当前进程的 PCB 结构体指针

  proc->alarm_interval = ticks;  //设置进程的定时器间隔
  proc->handler_va = handler_va; //设置进程的信号处理函数地址
  proc->have_return = 1;         //处理函数已经返回
  return 0; 
}