#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sleeplock.h" 
#include "fs.h"         
#include "file.h"      
#include "fcntl.h"  

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
// 处理来自用户空间的中断、异常或系统调用
void
usertrap(void)
{
  int which_dev = 0; //记录中断的设备号

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode"); 

  w_stvec((uint64)kernelvec);

  struct proc *p = myproc(); //获取当前进程的指针
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8) 
  {
    if(p->killed)
      exit(-1); 
    p->trapframe->epc += 4;
    intr_on(); 
    syscall(); 
  } 
  // mmap页面错误 
  else if (r_scause() == 12 || r_scause() == 13 || r_scause() == 15) 
  { 
    char *pa; //物理地址
    uint64 va = PGROUNDDOWN(r_stval()); //将触发错误的虚拟地址向下取整到页边界
    struct vm_area *vma = 0; //虚拟内存区域指针
    int flags = PTE_U; //用户可访问
    int i;

    //查找对应的VMA
    for (i = 0; i < NVMA; i++) 
      if (p->vma[i].addr && va >= p->vma[i].addr
          && va < p->vma[i].addr + p->vma[i].len) {
        vma = &p->vma[i]; //找到对应的VMA
        break;
      }
    //没有找到VMA
    if (!vma) 
      goto err; //跳转到错误处理

    //存储错误 Store Page Fault
    if (r_scause() == 15 && (vma->prot & PROT_WRITE)
        && walkaddr(p->pagetable, va)) {
      if (uvmsetdirtywrite(p->pagetable, va)) //设置脏页标志位PTE_D
        goto err; //设置失败
    } 
    else {
      if ((pa = kalloc()) == 0) //分配物理内存
        goto err;
      memset(pa, 0, PGSIZE); //清空分配的物理内存

      ilock(vma->f->ip); //锁定文件
      //使用readi()从文件中读取数据到物理⻚
      if (readi(vma->f->ip, 0, (uint64) pa, va - vma->addr + vma->offset, PGSIZE) < 0) {
        iunlock(vma->f->ip); 
        goto err;
      }
      iunlock(vma->f->ip); //解锁文件

      if ((vma->prot & PROT_READ)) //允许读
        flags |= PTE_R; //设置读标志
      
      //只有在存储页面错误时才设置PTE的写标志和脏标志
      if (r_scause() == 15 && (vma->prot & PROT_WRITE)) 
        flags |= PTE_W | PTE_D; //如果允许写 设置写和脏标志
      //允许执行 设置执行标志
      if ((vma->prot & PROT_EXEC)) 
        flags |= PTE_X; 
      //将物理页映射到用户进程的虚拟地址
      if (mappages(p->pagetable, va, PGSIZE, (uint64) pa, flags) != 0) {
        kfree(pa); //映射失败
        goto err;
      }
    }
  } 
  else if((which_dev = devintr()) != 0){
    // ok
  } else {
err:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1; 
  }

  if(p->killed)
    exit(-1); 

  if(which_dev == 2)
    yield(); //让出CPU

  usertrapret(); //准备返回用户空间
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

