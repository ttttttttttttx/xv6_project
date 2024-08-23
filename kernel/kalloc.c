// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

int useReference[PHYSTOP/PGSIZE]; //存储对应物理页的引用计数
struct spinlock ref_count_lock;   //确保在修改引用计数时不会有其他进程或线程同时进行修改

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
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
// 回收对应页
void kfree(void *pa)
{
  struct run *r;
  int tmp; 

  //检查传入的物理地址pa是否对齐到页面大小PGSIZE的边界，以及是否在物理内存的有效范围内
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //新增修改
  //获取ref_count_lock锁 保护useReference数组 防止并发修改
  acquire(&ref_count_lock);
  //减少对应物理页的引用计数
  useReference[(uint64)pa/PGSIZE] -= 1;
  //暂存引用计数的值
  tmp = useReference[(uint64)pa/PGSIZE];
  //释放ref_count_lock锁
  release(&ref_count_lock);

  //引用计数大于0 表示该页仍然被其他地方引用
  if (tmp > 0)
    return; //不释放 直接返回

  memset(pa, 1, PGSIZE); //将pa指向的内存块填充为1 防止后续的引用

  r = (struct run*)pa; //将pa强制转换为run结构体指针

  acquire(&kmem.lock);

  r->next = kmem.freelist;
  kmem.freelist = r;

  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 处理 COW 页面故障
void* kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock); //获取名为kmem.lock的锁 防止并发修改

  r = kmem.freelist;
  //获取kmem结构体的freelist指针 r指向第一个空闲内存块

  if(r) { 
    kmem.freelist = r->next; //将下一个空闲内存块的地址赋给freelist

    acquire(&ref_count_lock);
    //用于保护引用计数数组useReference 防止并发修改

    useReference[(uint64)r / PGSIZE] = 1;
    //将r指向的内存块的引用计数初始化为1
    //(uint64)r / PGSIZE计算r指向的内存块的页号

    release(&ref_count_lock); //释放ref_count_lock锁
  }
  release(&kmem.lock); //释放kmem.lock锁

  if(r) //r不为空
    memset((char*)r, 5, PGSIZE); //将r指向的内存块的前PGSIZE字节设置为5

  return (void*)r; //返回r指向的内存块的地址
}