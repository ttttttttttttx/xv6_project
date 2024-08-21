#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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

#ifdef LAB_PGTBL
pte_t * walk(pagetable_t pagetable, uint64 va, int alloc);
int sys_pgaccess(void)
{
  uint64 va;        //变量va 存储用户提供的虚拟地址
  int pagenum;      //变量pagenum 存储要检查的页数
  uint64 abitsaddr; //变量abitsaddr 存储位掩码的用户空间缓冲区地址

  argaddr(0, &va);        //argaddr函数 解析系统调用的第一个参数 虚拟地址 
  argint(1, &pagenum);    //argint函数  解析系统调用的第二个参数 要检查的页数
  argaddr(2, &abitsaddr); //argaddr函数 解析系统调用的第三个参数 位掩码缓冲区地址

  uint64 maskbits = 0; //用于存储位掩码
  struct proc *proc = myproc(); //调用myproc函数获取当前进程的proc结构体指针

  //遍历指定的页数
  for (int i = 0; i < pagenum; i++) {
    //调用walk函数获取虚拟地址va+i*PGSIZE对应的页表项pte
    pte_t *pte = walk(proc->pagetable, va + i*PGSIZE, 0); 

    if (pte == 0)
      panic("page not exist."); //页面不存在，调用panic函数终止程序

    if (PTE_FLAGS(*pte) & PTE_A)  //检查页表项的访问位PTE_A是否被设置
      maskbits = maskbits | (1L << i); //将位掩码的第i位设置为1
    
    *pte = ((*pte&PTE_A) ^ *pte) ^ 0 ; //清除页表项的访问位PTE_A，通过异或操作实现
  }

  //将位掩码复制到用户空间
  if (copyout(proc->pagetable, abitsaddr, (char *)&maskbits, sizeof(maskbits)) < 0)
    panic("sys_pgacess copyout error"); //如果copyout函数返回负值，表示复制失败，调用panic函数终止程序

  return 0;
}
#endif

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
