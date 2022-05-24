#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct proc rq[NPROC]; // Queue of RUNNABLE processes.
  struct proc sq[NPROC]; // Queue of SLEEPING processes.
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

//进程调度算法：0优先级；1短作业优先；2轮询
int sche_method = 2;
//是否更换过进程调度算法
int sche_change = 1;

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

void addRunnable(struct proc *np) {
  struct proc *p;
  //cprintf("addRunnalbe!\n");
  for(p = ptable.rq; p < &ptable.rq[NPROC]; p++) {
    if(p->state == UNUSED) {
      //cprintf("addRunnalbe success!\n");
      p = np;
      return;
    }
  }
}

void addSleep(struct proc *np) {
  struct proc *p;
  for(p = ptable.sq; p < &ptable.sq[NPROC]; p++) {
    if(p->state == UNUSED) {
      //cprintf("addSleep success!\n");
      p = np;
      return;
    }
  }
}

void delRunnable(struct proc *p) {
  struct proc *rp;
  //cprintf("delRunnalbe!\n");
  for(rp = ptable.rq; rp < &ptable.rq[NPROC]; rp++) {
    if(rp == p) {
      //cprintf("delRunnalbe success!\n");
      rp->state = UNUSED;
      break;
    }
  }
}

void delSleep(struct proc *p) {
  struct proc *sp;
  for(sp = ptable.sq; sp < &ptable.sq[NPROC]; sp++) {
    if(sp == p) {
      //cprintf("delSleep success!\n");
      sp->state = UNUSED;
      break;
    }
  }
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to ALLOCATED and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = ALLOCATED;
  p->pid = nextpid++;
  p->priority = 15;
  p->waitTime = 0;
  p->runTime = 0;
  p->turnAroundTime = 0;
  p->currentWait = 0;
  p->currentRun = 0;

  p->time = 100;
  p->remainTime = 100;


  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  addRunnable(p);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  addRunnable(np);

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
  
	struct proc * tempProcess = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (sche_method == 0) {
            //优先级调度
            if (sche_change == 1) {
                cprintf("Priority scheduling.\n");
                sche_change = 0;
            }
            if (p->state != RUNNABLE)
                continue;

            //遍历一次进程数组，找出优先级最高的进程
            for (tempProcess = ptable.proc; tempProcess < &ptable.proc[NPROC]; tempProcess++)
            {
                //遍历等待或者运行状态的进程，如果选中等待状态的，就进行进程切换；如果选中运行状态的，当前运行进程继续执行
                if (tempProcess->state != RUNNABLE || tempProcess->state != RUNNING)
                    continue;
                //priority越小，优先级越高
                if (tempProcess->priority < p->priority)
                {
                    p = tempProcess;
                }
            }

            //p进程被选中，下一轮将被执行，因此运行次数加一
            p->runTime++;
            p->currentRun++;
            p->turnAroundTime++;
            //如果p进程运行次数达到了一定程度，则优先级降低
            if (p->currentRun > 100 && p->priority < 31) {
                p->priority++;
                p->currentRun = 0;
                cprintf("%d priority ++, turn into %d\n", p->pid, p->priority);
            }

            //p进程被选中执行，则其他进程将要等待，遍历进程数组找出除p进程之外在运行或等待的进程，等待次数加一
            for (tempProcess = ptable.proc; tempProcess < &ptable.proc[NPROC]; tempProcess++) {
                //选出除p进程之外在等待或运行的进程
                if ((tempProcess->state != RUNNABLE && tempProcess->state != RUNNING)
                    || tempProcess->pid == p->pid)
                    continue;
                tempProcess->waitTime++;
                tempProcess->currentWait++;
                tempProcess->turnAroundTime++;
                //如果等待次数达到一定程度，优先级升高
                if (tempProcess->currentWait > 220 && tempProcess->priority > 0)
                {
                    tempProcess->priority--;
                    tempProcess->currentWait = 0;
                    cprintf("%d priority --, turn into %d\n", tempProcess->pid, tempProcess->priority);
                }
            }

            //如果p进程本身就是运行状态且优先级最高，则直接跳过本轮循环，不需要进程上下文切换
            if (p->state == RUNNING)
                continue;
        }
        else if (sche_method == 1) {
            //短作业优先
            if (sche_change == 1) {
                cprintf("SF scheduling.\n");
                sche_change = 0;
            }
            if (p->state != RUNNABLE || !p->remainTime)
                continue;

            //遍历一次进程数组，找出剩余运行时间最短的进程
            for (tempProcess = ptable.proc; tempProcess < &ptable.proc[NPROC]; tempProcess++)
            {
                //遍历等待或者运行状态的进程，如果选中等待状态的，就进行进程切换；如果选中运行状态的，当前运行进程继续执行
                if (tempProcess->state != RUNNABLE || tempProcess->state != RUNNING || !tempProcess->remainTime)
                    continue;
                //time越小，越先运行
                if (tempProcess->remainTime < p->remainTime)
                {
                    p = tempProcess;
                }
            }

            //init/sh进程单独考虑，运行时间不能减少
            if (p->pid < 3)
            {
                cprintf("%d is running.\n", p->pid);
            }
            else
            {
                cprintf("%d is running.\t time is %d.\t remainTime is %d.\n", p->pid, p->time, p->remainTime);
                p->remainTime--;
            }

            //如果p进程本身就是运行状态且剩余运行时间最短，则直接跳过本轮循环，不需要进程上下文切换
            if (p->state == RUNNING)
                continue;
        }
        else if (sche_method == 2) {
            //时间片轮转
            if (sche_change == 1) {
                cprintf("RR scheduling.\n");
                sche_change = 0;
            }
            if (p->state != RUNNABLE)
                continue;
            //cprintf("%d is running.\n", p->pid);
            //p->remainTime--;
        }
        

        //p进程是等待状态，此时进行进程上下文切换
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
    }

    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  struct proc *p = myproc();
  p->state = RUNNABLE;
  addRunnable(p);
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  delRunnable(p);
  addSleep(p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  //cprintf("wakeup!\n");

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan){
      delSleep(p);
      addRunnable(p);
      p->state = RUNNABLE;
    }
  }
  

}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        delSleep(p);
        addRunnable(p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [ALLOCATED] "allocated",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int 
changePriority(int tempPid, int tempPriority){
    struct proc * p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == tempPid){
    		p->priority = tempPriority;
			break;
		}
    }
    release(&ptable.lock);
    return tempPid;
}

int 
showProcess(int op)
{
    struct proc* p;
    sti();
    acquire(&ptable.lock);
    cprintf("name \t pid \t state    \t priority \t time \t remainTime \n");

    if(op == 0) {
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
          if (p->state == SLEEPING)
          {
              cprintf("%s \t %d \t SLEEPING \t %d \t\t %d \t\t %d\n", p->name, p->pid, p->priority, p->time, p->remainTime);
              cprintf("turn:%d\trun:%d\twait:%d\n", p->turnAroundTime, p->runTime, p->waitTime);
          }
          else if (p->state == RUNNING)
          {
              cprintf("%s \t %d \t RUNNING  \t %d \t\t %d \t\t %d\n", p->name, p->pid, p->priority, p->time, p->remainTime);
              cprintf("turn:%d\trun:%d\twait:%d\n", p->turnAroundTime, p->runTime, p->waitTime);
          }
          else if (p->state == RUNNABLE)
          {
              cprintf("%s \t %d \t RUNNABLE \t %d \t\t %d \t\t %d\n", p->name, p->pid, p->priority, p->time, p->remainTime);
              cprintf("turn:%d\trun:%d\twait:%d\n", p->turnAroundTime, p->runTime, p->waitTime);
          }
      }
    }

    else if(op == 1) {
      for(p = ptable.rq; p < &ptable.rq[NPROC]; p++) {
        cprintf("%s \t %d \t RUNNABLE \t %d \t\t %d \t\t %d\n", p->name, p->pid, p->priority, p->time, p->remainTime);
        cprintf("turn:%d\trun:%d\twait:%d\n", p->turnAroundTime, p->runTime, p->waitTime);
      }
    }

    else if(op == 2) {
      for(p = ptable.rq; p < &ptable.rq[NPROC]; p++) {
        cprintf("%s \t %d \t SLEEPING \t %d \t\t %d \t\t %d\n", p->name, p->pid, p->priority, p->time, p->remainTime);
        cprintf("turn:%d\trun:%d\twait:%d\n", p->turnAroundTime, p->runTime, p->waitTime);
      }
    }
    
    release(&ptable.lock);
    return 1;
}

int
changeTime(int tempPid, int tempTime)
{
    struct proc* p;
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->pid == tempPid)
        {
            cprintf("prcoss %d changes time from %d to %d", p->pid, p->time, tempTime);
            p->time = tempTime;
            p->remainTime = tempTime;
            break;
        }
    }
    release(&ptable.lock);
    return tempPid;
}

int
changeSche(int tempSche)
{
    acquire(&ptable.lock);
    cprintf("!!!!!!!!111\n");
    sche_method = tempSche;
    sche_change = 1;
    release(&ptable.lock);
    return sche_method;
}