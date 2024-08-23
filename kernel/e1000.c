#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;
struct spinlock e1000_txlock; //发送锁
struct spinlock e1000_rxlock; //接受锁

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.

// e1000_init()函数 
// 初始化并配置 e1000 寄存器供数据的发送与接收
void e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000"); //初始化 e1000_lock 自旋锁

  regs = xregs; //寄存器映射

  // 重置 e1000 设备 (禁用中断)
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  // 发送环初始化
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  // 接受环初始化
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // 设置 MAC 地址过滤
  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // 传输控制位设置
  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // 接收控制位设置
  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // 接收中断设置
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

// e1000_transmit()函数 
// 发送数据包 mbuf包含要发送的以太网帧
int e1000_transmit(struct mbuf *m) 
{
  // Your code here.
  
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.

  acquire(&e1000_txlock); //获取自旋锁

  uint64 tdt = regs[E1000_TDT]; //获取当前应该使用的发送描述符索引
  uint64 index = tdt % TX_RING_SIZE;
  //计算索引 发送描述符环是循环的 使用模运算确保索引在环的大小范围内

  struct tx_desc send_desc = tx_ring[index];
  //从发送描述符环中获取当前索引位置的描述符
  
  //检查描述符的DD位
  if(!(send_desc.status & E1000_TXD_STAT_DD)) {
    //没有设置 表示发送尚未完成
    release(&e1000_lock); //释放自旋锁
    return -1; //发送失败 前一个包尚未发送完成
  }

  if(tx_mbufs[index] != 0) //当前索引位置的mbuf指针不为空 表示有未释放的数据包
    mbuffree(tx_mbufs[index]); //释放之前的数据包

  tx_mbufs[index] = m; //mbuf指针保存到tx_mbufs数组中
  
  //设置发送描述符
  tx_ring[index].addr = (uint64)tx_mbufs[index]->head; //设置地址字段
  tx_ring[index].length = (uint16)tx_mbufs[index]->len; //设置长度字段
  tx_ring[index].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP; //设置命令字段 RS 和EOP 表示一个完整的包
  tx_ring[index].status = 0; //清除状态字段 准备发送新数据包

  tdt = (tdt + 1) % TX_RING_SIZE; //更新发送描述符尾指针 指向下一个发送描述符
  regs[E1000_TDT] = tdt; //将更新后的发送描述符尾指针写回e1000的TDT寄存器

  __sync_synchronize(); //确保所有之前的写入操作都已完成

  release(&e1000_txlock); //释放自旋锁
  
  return 0;
}

// e1000_recv()函数 
// 接收数据包
static void e1000_recv(void)
{
  // Your code here.
  
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).

  uint64 rdt = regs[E1000_RDT]; //获取下一个应该处理的接收描述符索引
  uint64 index = (rdt + 1) % RX_RING_SIZE;
  //计算索引 接收描述符环是循环的  使用模运算确保索引在范围内

  acquire(&e1000_rxlock); //获取自旋锁
  
  //描述符是否包含一个完整的包
  if(!(rx_ring[index].status & E1000_RXD_STAT_DD))
    return; //没有新的数据包到达 直接返回
  
  //有新的数据包到达 循环用于处理同一位置多个数据包到达
  while(rx_ring[index].status & E1000_RXD_STAT_DD){

    struct mbuf* buf = rx_mbufs[index]; //获取与当前接收描述符关联的mbuf
    mbufput(buf, rx_ring[index].length); //更新mbuf的长度为接收数据包的实际长度

    rx_mbufs[index] = mbufalloc(0); //分配一个新的mbuf 用于接收下一个数据包
    rx_ring[index].addr = (uint64)rx_mbufs[index]->head; //更新地址字段为新的mbuf的头部地址
    rx_ring[index].status = 0; //清除接收描述符的状态字段 准备接收新的数据包

    rdt = index; //更新接收描述符尾指针
    regs[E1000_RDT] = rdt; //将更新后的接收描述符尾指针写回e1000的RDT寄存器

    __sync_synchronize(); //确保所有之前的写入操作都已完成

    net_rx(buf); //调用net_rx()函数处理接收到的数据包

    index = (regs[E1000_RDT] + 1) % RX_RING_SIZE; //计算并更新下一个接收描述符的索引
  }

  release(&e1000_rxlock); //释放锁

  return;
}

// e1000_intr()函数
// 中断处理
void e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}