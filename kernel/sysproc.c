#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
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

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

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

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//新增sys_trace()函数
//实现新的系统调用
uint64 sys_trace(void) {
  int mask; //存储系统调用参数

  //获取整数类型的系统调用参数
  //argint的第一个参数是参数的索引，这里是0，表示第一个参数
  //argint的第二个参数是用于存储参数值的变量的地址
  //如果获取参数失败，argint会返回小于0的值
  if (argint(0, &mask) < 0) 
    return -1;
  
  //调用myproc函数获取当前进程的proc结构体指针
  //然后将获取到的mask值存入proc结构体的mask字段中
  myproc()->mask = mask;
  return 0;
} 

//新增sys_sysinfo()函数
uint64 sys_sysinfo(void) 
{
  uint64 addr; //存储用户空间传递的地址参数
  
  //调用argaddr函数，获取系统调用第一个参数的地址，并将其存储在addr变量中
  if(argaddr(0, &addr)<0){
    return -1; //获取失败
  }

  struct sysinfo info; //sysinfo结构体变量info 存储系统信息
  
  //调用getfreemem函数获取当前可用的内存大小
  info.freemem = getfreemem();
  
  //调用getnproc函数获取当前活跃的进程数
  info.nproc = getnproc();
  
  //调用copyout函数，将info结构体中的数据复制到用户空间的addr地址处
  if(copyout(myproc()->pagetable, addr, 
            (char *)&info, sizeof(info)) < 0)
    return -1; //复制失败 返回-1

  return 0;
}