// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// bucket 实现缓冲区的哈希分桶机制
// 每个分桶包含一个链表 存储缓冲区
struct bucket {
  struct spinlock lock; //自旋锁
  struct buf head;      //缓冲区指针
};

// buffer cache
// 缓冲区管理的主容器
struct {
  struct buf buf[NBUF];          //缓冲区数组
  struct bucket bucket[NBUCKET]; //哈希桶数组
} bcache;

// hash_v()函数 
// 将不同的块号分散到不同的分桶中
static uint hash_v(uint key) 
{
  return key % NBUCKET;
}

// initbucket()函数
// 对每个分桶初始化
static void initbucket(struct bucket* b) 
{
    //初始化锁 同步访问bucket结构体
    initlock(&b->lock, "bcache.bucket");
    
    //初始化 bucket 链表的头节点
    b->head.prev = &b->head;
    b->head.next = &b->head;
}

// binit()函数
void binit(void)
{
  //初始化缓冲区的睡眠锁
  for (int i = 0; i < NBUF; i++) 
    initsleeplock(&bcache.buf[i].lock, "buffer");
  
  //初始化分桶
  for (int i = 0; i < NBUCKET; i++) 
    initbucket(&bcache.bucket[i]);
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// bget()函数
// 在缓冲区缓存中查找或分配一个缓冲区
static struct buf* bget(uint dev, uint blockno)
{
  uint v = hash_v(blockno); // hash 值
  struct bucket* bucket = &bcache.bucket[v]; //对应的桶
 
  acquire(&bucket->lock); //获取自旋锁

  //检查块是否已经在缓存中
  for (struct buf *buf = bucket->head.next; buf != &bucket->head;
       buf = buf->next) {
    //块已经在缓存中 增加引用计数 返回缓冲区
    if(buf->dev == dev && buf->blockno == blockno){
      buf->refcnt++;
      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }

  //块不在缓存中 需要分配一个新的缓冲区
  //查找最不常使用的未使用的缓冲区
  for (int i = 0; i < NBUF; ++i) {
    if (!bcache.buf[i].used &&
        !__atomic_test_and_set(&bcache.buf[i].used, __ATOMIC_ACQUIRE)) {
      struct buf *buf = &bcache.buf[i];
      //初始化新缓冲区的属性
      buf->dev = dev;
      buf->blockno = blockno;
      buf->valid = 0;  //缓冲区内容无效
      buf->refcnt = 1; //初始引用计数为 1

      //将新缓冲区添加到链表中 使其成为最近使用的缓冲区
      buf->next = bucket->head.next;
      buf->prev = &bucket->head;
      bucket->head.next->prev = buf;
      bucket->head.next = buf;
      
      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }
 
  panic("bget: no buffers"); //没有可用的缓冲区
}


// Return a locked buf with the contents of the indicated block.
// 从磁盘读取指定块的内容并返回一个已加锁的缓冲区
struct buf* bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// 将一个已加锁的缓冲区的内容写入磁盘
void bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// brelse()函数
// 释放一个已加锁的缓冲区 并将它移动到最近使用的列表头部
void brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock)) //检查是否有睡眠锁
    panic("brelse");
  releasesleep(&b->lock); //释放睡眠锁

  uint v = hash_v(b->blockno); //计算缓冲区b的块号哈希值
  struct bucket* bucket = &bcache.bucket[v]; //获取对应的桶
 
  acquire(&bucket->lock); //获取桶的自旋锁

  b->refcnt--; //缓冲区引用计数
  //缓冲区引用计数为零 则从链表中移除缓冲区
  if (b->refcnt == 0) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
    __atomic_clear(&b->used, __ATOMIC_RELEASE);
    //清除b->used标志 表示缓冲区不再被使用
  }
  
  release(&bucket->lock); //释放桶的自旋锁
}

// bpin()函数
// 增加缓冲区 b 的引用计数
void bpin(struct buf *b) {
  uint v = hash_v(b->blockno); //hash值
  struct bucket* bucket = &bcache.bucket[v]; //桶

  acquire(&bucket->lock);
  b->refcnt++; //缓冲区引用计数+1
  release(&bucket->lock);
}

// bunpin()函数
// 减少缓冲区 b 的引用计数
void bunpin(struct buf *b) {
  uint v = hash_v(b->blockno); //hash值
  struct bucket* bucket = &bcache.bucket[v]; //桶
 
  acquire(&bucket->lock);
  b->refcnt--; //缓冲区引用计数-1
  release(&bucket->lock);
}