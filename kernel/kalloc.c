// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
}; //链表结构 用于管理空闲内存块

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; //为每个CPU单独设置一个freelist

// kinit()函数
// 初始化内存分配器
void kinit() { 
  char buf[10]; //存储自旋锁的名称

  //遍历每个CPU
  for (int i = 0; i < NCPU; i++) {
    snprintf(buf, 10, "kmem_CPU%d", i); //使用 snprintf 创建自旋锁名称
    initlock(&kmem[i].lock, buf); //初始化第i个CPU对应的自旋锁
  } 

  //初始化空闲内存块链表 
  freerange(end, (void*)PHYSTOP); //参数是内核末尾地址和物理内存末尾地址
} 

// freerange()函数
// 初始化空闲内存块链表
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// kfree()函数
// 释放指向物理内存页的指针pa
void kfree(void *pa) 
{ 
  struct run *r; 

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree"); 

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE); //将整个内存页填充为 1 以捕获可能的悬挂引用

  r = (struct run*)pa; //将 pa 转换为 struct run* 类型 就可以将其添加到空闲链表中

  push_off();        //禁用中断
  int cpu = cpuid(); //获取当前 CPU 的 ID
  pop_off();         //恢复中断

  acquire(&kmem[cpu].lock);     //获取当前 CPU 的自旋锁
  r->next = kmem[cpu].freelist; //将新释放的内存页 r 插入到当前 CPU 的空闲链表头部
  kmem[cpu].freelist = r;       //更新当前 CPU 的空闲链表头部为新释放的内存页
  release(&kmem[cpu].lock);     //释放当前 CPU 的自旋锁
} 

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// kalloc()函数
// 分配一个物理内存页 返回指向该页的指针
void * kalloc(void)
{ 
  struct run *r; 

  push_off();        //禁用中断
  int cpu = cpuid(); //当前 CPU 的 ID
  pop_off();         //恢复中断

  acquire(&kmem[cpu].lock); //获取当前 CPU 的自旋锁

  r = kmem[cpu].freelist; //当前 CPU 的空闲链表的第一个空闲页

  //找到了空闲页
  if(r) { 
    kmem[cpu].freelist = r->next; //更新当前 CPU 的空闲链表头部
  }
  //当前 CPU 的空闲链表为空 
  else {
    struct run* tmp; 

    //遍历所有CPU
    for (int i = 0; i < NCPU; ++i) {
      if (i == cpu) //跳过当前CPU
        continue;

      acquire(&kmem[i].lock); //获取其他 CPU 的自旋锁
      tmp = kmem[i].freelist; //获取其他 CPU 的空闲链表头部

      if (tmp == 0) { 
        release(&kmem[i].lock);
        continue;
      } 
      else {
        //尝试拿取最多 1024 个页
        for (int j = 0; j < 1024; j++) { 
          if (tmp->next) //还有下一页
            tmp = tmp->next;
          else //没有更多的页
            break; 
        }

        kmem[cpu].freelist = kmem[i].freelist;
        kmem[i].freelist = tmp->next; //更新 CPU 的空闲链表头部
        tmp->next = 0; 

        release(&kmem[i].lock); //释放其他 CPU 的自旋锁
        break; 
      }
    }

    r = kmem[cpu].freelist; //再次检查当前 CPU 的空闲链表
    if (r)
      kmem[cpu].freelist = r->next; 
  } 
  
  release(&kmem[cpu].lock); //释放当前 CPU 的自旋锁

  if(r)
    memset((char*)r, 5, PGSIZE); //分配成功，用 5 填充页以捕获悬挂引用

  return (void*)r; //返回指向分配的内存页的指针
} 