//切换上下文：被调用者保存寄存器
struct context {
	uint edi;   // 目标索引寄存器
	uint esi;   // 源索引寄存器
	uint ebx;   // 基地址寄存器
	uint ebp;   // 基址指针
	uint eip;   // 指令寄存器，返回地址
};

//进程状态
enum procstate {
	UNUSED,       // 未使用，空闲状态
	NEW,          // 分配PCB，但未分配足够资源
	WAITING,      // 进程休眠，等待IO或其他事件完成
	READY,        // 进程获得足够资源，可以上CPU执行
	RUNNING,      // 进程正在CPU上执行
	TERMINATED    // 进程执行结束，等待回收资源
};

struct proc {
	uint sz;                     // 进程所占内存大小 (bytes)
	pde_t* pgdir;                // 页表
	char* kstack;                // 内核栈位置
	enum procstate state;        // 进程状态
	int pid;                     // Process ID
	struct proc* parent;         // 父进程指针
	struct trapframe* tf;        // 中断栈帧指针
	struct context* context;     // 上下文指针
	void* chan;                  // 进程在channel上睡眠
	int killed;                  // 进程是否被杀死
	struct file* ofile[NOFILE];  // 打开文件描述符表
	struct inode* cwd;           // 当前工作目录
	char name[16];               // 进程名
	int priority;
	int createTime;
	int readyTime;
	int runTime;
	int finishTime;
	int runTotal;
	int waitTotal;
	int currentWait;
	int currentRun;
	int time;                    // 提前设置好的总的运行时间 
	int remainTime;              // 剩余运行时间 
};

struct cpu {
	uchar apicid;                // Local APIC ID
	struct context* scheduler;   // 调度器上下文
	struct taskstate ts;         // 任务状态栈
	struct segdesc gdt[NSEGS];   // 全局描述符
	volatile uint started;       // CPU是否启动
	int ncli;                    // 关中断深度
	int intena;                  // CPU是否允许中断
	struct proc* proc;           // 运行在该CPU上的进程指针
};

extern struct cpu cpus[NCPU];
extern int ncpu;