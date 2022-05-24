#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
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

int
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

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_changePriority(void) {
    int pid, priority;
    if(argint(0, &pid) < 0) return -1;
    if(argint(1, &priority) < 0) return -1;
    return changePriority(pid, priority);
}

int sys_showProcess(void) {
    int op;
    if(argint(0, &op) < 0) return -1;
    return showProcess(op);
}

int sys_changeTime(void) {
    int pid, time;
    if (argint(0, &pid) < 0) return -1;
    if (argint(1, &time) < 0) return -1;
    return changeTime(pid, time);
}

int sys_changeSche(void) {
    int tempSche;
    if (argint(0, &tempSche) < 0) return -1;
    return changeSche(tempSche);
}