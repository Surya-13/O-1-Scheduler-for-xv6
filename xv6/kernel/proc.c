#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "queue.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void wakeup1(struct proc *chan);

extern char trampoline[]; // trampoline.S

//################################################################################################
struct queue_list active_runQ;    // Active queue list
struct queue_list expired_runQ;   // Expired queue list

// Flag needed for test.c
int flag;

// For test2.c, uncomment the line below.
//int check[40];

// Return the max of two integers.
int max(int x,int y){
  if(x<y)return y;
  return x;                       
}

// Return the min of two integers.
int min(int x,int y){
  if(x<y)return x;
  return y;                       
}

// Function to initialize the queue lists.
void initQ(){                     
  for(int i=0;i<40;i++){
    // Set the front,rear and size variabes of each queue.
    createQueue(&active_runQ.Q[i]);     
    createQueue(&expired_runQ.Q[i]);    

    // For test2.c, uncomment the line below.
    //check[i]=0;
  }
  // Bitmask = 0.
  active_runQ.bitmask=0;          
  expired_runQ.bitmask=0;         
  return;
}

// This function will insert a process according to its
// static priority. Used when we are about to schedule 
// a process for the first time.
void
insertQ(struct queue_list* q,struct proc* p){     
  
  acquire(&q->lock);     

  // Insert into the corresponding index which is 
  // the priority - 100.
  insert(&(q->Q[p->spriority-100]),p);    
        
  // Set that bit of the bitmask as 1, which is done
  // by bitwise or with bit 1 at that position.                                  
  q->bitmask = q->bitmask | ((uint64)1L << (p->spriority-100));

  // Finally release the lock for that queue list.
  release(&q->lock);
  return;
}

// This function will calculate the dynamic priority
// of the process, and then insert according to that
// priority. Used when the process has already been 
// scheeduled atleast once
void
insert_expQ(struct queue_list* q,struct proc* p){

  // Acquire the lock for the queue list.
  acquire(&q->lock);

  // Bonus value for each process.
  int bonus=-1;

  // Avg Sleep time calculated as the total
  // sleep time divided by the number of sleeps.
  int AvgSleepTime = p->stime/p->s_no;

  // Find the bonus value for the correspnding 
  // Avg sleep time.
  for(int i=1000000;i<10000000;){
    if(AvgSleepTime < i){
      bonus=(int)(i/1000000)-1;
      break;
    }
    i+=1000000;
  }
  if(bonus==-1)bonus=10;

  // Assign the dynamic priority according to the 
  // static priority and the bonus value.
  p->dpriority = max(100,min(p->spriority-bonus+5,139));

  // Insert into the corresponding index which is 
  // the dynamic priority - 100.  
  insert(&(q->Q[p->dpriority-100]),p);

  // Set that bit of the bitmask as 1, which is done
  // by bitwise or with bit 1 at that position.  
  q->bitmask = q->bitmask | ((uint64)1L << (p->dpriority-100));

  // Finally release the lock for that queue list.
  release(&q->lock);
}

// Helper function to print the queue.
void 
printQ(struct queue_list* q){
  printf("Queue is: \n");
  for(int i=0; i < 40; i++){
    if(!isEmpty(&(q->Q[i]))){
      printf("At index %d\n",100+i);
      for(int j=0; j < (q->Q[i]).size; j++){
        printf("Priority: %d,\t Name: %s\n" , 100+i, (q->Q[i]).array[j]->name);
      }
    }
  }
  return;
}

// Function to print all the processes 
// and their status and priority.
uint64
cps(void)
{
  struct proc *p;
  intr_on();
  printf("Name \t pid \t State \t \t Priority \n");
  for(p=proc ; p < &proc[NPROC] ; p++)
  {
    acquire(&p->lock);
    if(p->state == SLEEPING)
      printf("%s \t %d \t SLEEPING \t %d \n",p->name,p->pid,p->dpriority);

    else if(p->state == RUNNING){
      printf("%s \t %d \t RUNNING \t %d \n",p->name,p->pid,p->dpriority);
    }

    else if(p->state == RUNNABLE)
      printf("%s \t %d \t RUNNABLE \t %d \n",p->name,p->pid,p->dpriority);
    release(&p->lock);
  }
  return 0;
}

uint64
set_flag_0(void)
{
  flag = 0;
  return 1;
}
uint64
set_flag_1(void)
{
  flag = 1;
  return 1;
}
//##################################################################################################################

void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;

      //############################################################################################################
      // Initialize the variables inside the proc structure. 
          p->spriority=p->dpriority=120;
          p->stime=p->s_no=0;
          p->timeslice=p->last_time=0;
  }
  kvminithart();
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();

  // Allocate a trapframe page.
  if((p->tf = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof p->context);
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->tf)
    kfree((void*)p->tf);
  p->tf = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  //############################################################################################################
  // Initialize the variables inside the proc structure. 
      p->spriority=p->dpriority=120;
      p->stime=p->s_no=0;
      p->timeslice=p->last_time=0;
}

// Create a page table for a given process,
// with no user pages, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  mappages(pagetable, TRAMPOLINE, PGSIZE,
           (uint64)trampoline, PTE_R | PTE_X);

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  mappages(pagetable, TRAPFRAME, PGSIZE,
           (uint64)(p->tf), PTE_R | PTE_W);

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, PGSIZE, 0);
  uvmunmap(pagetable, TRAPFRAME, PGSIZE, 0);
  if(sz > 0)
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x05, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x05, 0x02,
  0x9d, 0x48, 0x73, 0x00, 0x00, 0x00, 0x89, 0x48,
  0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0xbf, 0xff,
  0x2f, 0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x01,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->tf->epc = 0;      // user program counter
  p->tf->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  //##########################################################################################################################
  // Insert according to static priority.
        insertQ(&expired_runQ,p);
  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  np->parent = p;

  // copy saved user registers.
  *(np->tf) = *(p->tf);

  // Cause fork to return 0 in the child.
  np->tf->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  //##########################################################################################################################
  // Insert according to static priority.
      insertQ(&expired_runQ,np);

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    // this code uses pp->parent without holding pp->lock.
    // acquiring the lock first could cause a deadlock
    // if pp or a child of pp were also in exit()
    // and about to try to lock p.
    if(pp->parent == p){
      // pp->parent can't change between the check and the acquire()
      // because only the parent changes it, and we're the parent.
      acquire(&pp->lock);
      pp->parent = initproc;
      // we should wake up init here, but that would require
      // initproc->lock, which would be a deadlock, since we hold
      // the lock on one of init's children (pp). this is why
      // exit() always wakes init (before acquiring any locks).
      release(&pp->lock);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // we might re-parent a child to init. we can't be precise about
  // waking up init, since we can't acquire its lock once we've
  // acquired any other proc lock. so wake up init whether that's
  // necessary or not. init may miss this wakeup, but that seems
  // harmless.
  acquire(&initproc->lock);
  wakeup1(initproc);
  release(&initproc->lock);

  // grab a copy of p->parent, to ensure that we unlock the same
  // parent we locked. in case our parent gives us away to init while
  // we're waiting for the parent lock. we may then race with an
  // exiting parent, but the result will be a harmless spurious wakeup
  // to a dead or wrong process; proc structs are never re-allocated
  // as anything else.
  acquire(&p->lock);
  struct proc *original_parent = p->parent;
  release(&p->lock);
  
  // we need the parent's lock in order to wake it up from wait().
  // the parent-then-child rule says we have to lock it first.
  acquire(&original_parent->lock);

  acquire(&p->lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup1(original_parent);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&original_parent->lock);


  // For test2, uncomment the loop below
  // if(check[p->nice+20]==0 && p->nice!=0){
  //   printf("Process with nice value = %d exiting\n",p->nice);
  //   check[p->nice+20]=1;
  // }


  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  // hold p->lock for the whole time to avoid lost
  // wakeups from a child's exit().
  acquire(&p->lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      // this code uses np->parent without holding np->lock.
      // acquiring the lock first would cause a deadlock,
      // since np might be an ancestor, and we already hold p->lock.
      if(np->parent == p){
        // np->parent can't change between the check and the acquire()
        // because only the parent changes it, and we're the parent.
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&p->lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &p->lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.

//################################################################################################################
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int i;
  
  // Uncomment for test.c
  //uint64 start_time=0;
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    // Uncomment for test.c
    //start_time=r_time();
    
    // Acquire the locks for both the queue lists.
    acquire(&(active_runQ.lock));
    acquire(&(expired_runQ.lock));

    // If the active run queue is empty, and the expired run queue
    // is non-empty, then swap the queue lists.
    if(active_runQ.bitmask==0 && expired_runQ.bitmask!=0){
      struct Queue tempq;
      for(int i=0;i<40;i++){
        tempq=active_runQ.Q[i];
        active_runQ.Q[i]=expired_runQ.Q[i];
        expired_runQ.Q[i]=tempq;
      }
      uint64 temp=active_runQ.bitmask;
      active_runQ.bitmask=expired_runQ.bitmask;
      expired_runQ.bitmask=temp;
    }

    // If both are empty, then continue through the infinite loop
    else if(active_runQ.bitmask==0 && expired_runQ.bitmask==0){
      release(&(active_runQ.lock));
      release(&(expired_runQ.lock));
      continue;
    }

    // We come here only when the active queue list is non empty,
    // hence find the first index which has non-empty queue.
    for( i=0; i < 40; i++){
      if(((uint64)1L << i) & active_runQ.bitmask){
        break;
      }
    }

    // Get the first process from the queue at that index.
    p = del(&(active_runQ.Q[i]));

    // If the queue at index 'i' is empty, then unset the bit
    // for that index 
    if(isEmpty(&(active_runQ.Q[i]))){
      active_runQ.bitmask = active_runQ.bitmask & (~((uint64)1L << i));
    }

    // Release the locks as we dont need them anymore.
    release(&(active_runQ.lock));
    release(&(expired_runQ.lock));

    // Acquire the lock for the process to be scheduled.
    acquire(&p->lock);

    // IF p is Runnable :
    if(p->state == RUNNABLE){

      // Calculating the timeslice for the process this time
      // based on its current dynamic priority. 
      // NOTE :- 1ms == 10000 units.
      if(p->dpriority < 120){
        p->timeslice=(uint64)((140-p->dpriority)*200000); // 20ms
      }
      else p->timeslice=(uint64)((140-p->dpriority)*50000); // 5ms.

      // Get the CPU id to increment MTIME_CMP field.
      // MTIMT_CMP = MTIME + timeslice essentially means that
      // a timer interrupt will be called after timeslice 
      // amount of time since then MTIME will exceed MTIME_CMP.
      int id = cpuid();
      *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + p->timeslice;

      // Uncomment for test.c
      //if(flag)printf("Time taken : %d\n", r_time()-start_time);

      // Set proc as RUNNING and switch context.
      p->state = RUNNING;
      c->proc = p;
      swtch(&c->scheduler, &p->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&p->lock);
  }
}

//################################################################################################################

// The old-naive scheduler


// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   uint64 start_time=0;
//   c->proc = 0;
//   for(;;){
//     // Avoid deadlock by ensuring that devices can interrupt.
//     intr_on();
//     start_time=r_time();
//     for(p = proc; p < &proc[NPROC]; p++) {
//       acquire(&p->lock);
//       if(p->state == RUNNABLE) {
//         // Switch to chosen process.  It is the process's job
//         // to release its lock and then reacquire it
//         // before jumping back to us.        
//         p->state = RUNNING;
//         c->proc = p;
//         if(flag)printf(" Time taken : %d\n", r_time()-start_time);
//         swtch(&c->scheduler, &p->context);

//         // Process is done running for now.
//         // It should have changed its p->state before coming back.
//         c->proc = 0;
//       }
//       release(&p->lock);
//     }
//   }
// }

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);

  //######################################################################################################################
  // Insert according to the processes
  // dynamic priority.
      insert_expQ(&expired_runQ,p);

  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  if(lk != &p->lock){  //DOC: sleeplock0
    acquire(&p->lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  p->chan = chan;

  //######################################################################################################################
      // Set the last_time as the time the process
      // goes to sleep.
        p->last_time = (uint64)r_time();
  
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &p->lock){
    release(&p->lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {

    //######################################################################################################################

        // Get the sleep time by subtracting the sleep
        // start time from the current time, and then
        // Insert according to the processes dynamic priority.
        p->stime += r_time() - p->last_time;
        p->s_no ++;  // Increment the no of sleeps
        insert_expQ(&expired_runQ,p);
    
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
}

// Wake up p if it is sleeping in wait(); used by exit().
// Caller must hold p->lock.
static void
wakeup1(struct proc *p)
{
  if(!holding(&p->lock))
    panic("wakeup1");
  if(p->chan == p && p->state == SLEEPING) {

    //######################################################################################################################

        // Get the sleep time by subtracting the sleep
        // start time from the current time, and then
        // Insert according to the processes dynamic priority.
        p->stime += r_time() - p->last_time;
        p->s_no ++;  // Increment the no of sleeps
        insert_expQ(&expired_runQ,p);
    
    p->state = RUNNABLE;
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().

        //######################################################################################################################

          // Insert according to the processes dynamic priority.
            insert_expQ(&expired_runQ,p);
    
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
