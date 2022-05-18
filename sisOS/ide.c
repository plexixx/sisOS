// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE   512     // 扇区大小512字节
#define IDE_BSY       0x80    // 1000 0000状态寄存器的第8位表硬盘是否繁忙
#define IDE_DRDY      0x40    // 0100 0000状态寄存器的第7位表硬盘是否就绪，可继续执行命令
#define IDE_DF        0x20    // 0010 0000写出错
#define IDE_ERR       0x01    // 0001 0000出错

#define IDE_CMD_READ  0x20    //读扇区命令
#define IDE_CMD_WRITE 0x30    //写扇区命令
#define IDE_CMD_RDMUL 0xc4    //读多个扇区
#define IDE_CMD_WRMUL 0xc5    //写多个扇区

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static void idestart(struct buf*);

// 等待磁盘就绪
static int
idewait(int checkerr)
{
  int r;

  // 从端口0x1f7读出状态，若硬盘忙，空循环等待
  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
    ;
  // checkerr为0则不检查错误，若为1则检查错误
  // 若需要检查错误，且出现了写出错或者错误，则进行错误退出
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

// 初始化磁盘，被main.c中的main函数锁调用，作为启动时建立环境的一项。
void
ideinit(void) 
{
  int i;

  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE, ncpu - 1);  //让CPU来处理硬盘中断
  idewait(0); //等磁盘就绪,直接以0来实现更快返回

  // 往状态寄存器写数据
  outb(0x1f6, 0xe0 | (1<<4));
  // 是因为切换磁盘得需要一定时间
  for(i=0; i<1000; i++){
    // 如果有数据那说明从盘存在，反之从盘不存在
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }

  // 切换到主盘
  outb(0x1f6, 0xe0 | (0<<4));
}

// 开始缓冲块b的请求
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  if(b->blockno >= FSSIZE)  //块号超过了文件系统支持的块数
    panic("incorrect blockno");
  int sector_per_block =  BSIZE/SECTOR_SIZE; //每块的扇区数
  int sector = b->blockno * sector_per_block;//扇区数
  //一块包含多个扇区的话就用读多个块的命令
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  //每个块不能大于7个扇区
  if (sector_per_block > 7) panic("idestart");

  idewait(0); //等待磁盘就绪
  outb(0x3f6, 0);  //用来产生磁盘中断
  outb(0x1f2, sector_per_block);  // number of sectors
  /*像0x1f3~6写入扇区地址*/
  outb(0x1f3, sector & 0xff);
  outb(0x1f4, (sector >> 8) & 0xff);
  outb(0x1f5, (sector >> 16) & 0xff);
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));

  // 如果数据脏，则写到磁盘
  if(b->flags & B_DIRTY){
    outb(0x1f7, write_cmd); //向0x1f7发送写命令
    outsl(0x1f0, b->data, BSIZE/4); //向磁盘写数据
  } else {
    outb(0x1f7, read_cmd); //否则发送读命令
  }
}

void
ideintr(void)
{
  struct buf *b;

  acquire(&idelock);

  if((b = idequeue) == 0){  //如果磁盘请求队列为空
    release(&idelock);
    return;
  }
  idequeue = b->qnext;

  // 如果此次请求磁盘的操作是读且磁盘已经就绪
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    //从0x1f0端口读取数据到b->data
    insl(0x1f0, b->data, BSIZE/4); 

  b->flags |= B_VALID; //将缓存块数据设置为有效
  b->flags &= ~B_DIRTY;//此时缓存块数据不脏
  wakeup(b);  //唤醒等待在缓存块b上的进程 

  // 此时队列还不空，则处理下一个
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

void
iderw(struct buf *b)
{
  struct buf **pp;

  //要同步该块到磁盘，那前面应该是已经拿到了这个块的锁
  if(!holdingsleep(&b->lock))
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // 将这个块放进请求队列
  b->qnext = 0;
  // 插入到队尾
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  
    ;
  *pp = b;

  // 当前块是唯一请求磁盘的块
  if(idequeue == b)
    idestart(b);

  // 该缓存块数据无效
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock); //将等待在该缓存块上的进程进行休眠
  }


  release(&idelock);
}
