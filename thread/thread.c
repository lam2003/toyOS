#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "list.h"
#include "print.h"
#include "process.h"
#include "sync.h"


struct task_struct *main_thread; // 主线程的pcb
struct list thread_ready_list; // 就绪队列
struct list thread_all_list; // 所有任务队列
static struct list_elem *thread_tag;

extern void switch_to(struct task_struct *cur, struct task_struct *next);
extern void init();

struct task_struct *idle_thread; // 空载线程

// 空载任务
static void idle(void *arg) {
    while(1) {
        thread_block(TASK_BLOCKED);
        asm volatile ("sti; hlt" : : : "memory");
    }
}

// 获取当前线程的pcb
struct task_struct *running_thread() {
    uint32_t esp;
    asm ("mov %%esp, %0" : "=g" (esp));
    return (struct task_struct *)(esp & 0xfffff000);
}

// 线程中由kernel_thread去执行线程函数
static void kernel_thread(thread_func function, void *func_arg) {
    // 进时钟中断后，中断已经被关了，所以这里要打开，否则后面没法再调度了
    intr_enable();
    function(func_arg);
}

// 初始化线程栈
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg) {
    // 栈顶预留中断栈的位置
    pthread->self_kstack -= sizeof(struct intr_stack);
    // 栈顶预留线程栈的位置
    pthread->self_kstack -= sizeof(struct thread_stack);
    // 初始化线程栈
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

struct lock pid_lock;

static pid_t allocate_pid() {
    static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

// 初始化线程基本信息
void init_thread(struct task_struct *pthread, char *name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);
    if (pthread == main_thread) {
        // 刚开始就是在主线程上初始化主线程pcb，所以状态一定是running
        pthread->status = TASK_RUNNING;
    } else {
        pthread->status = TASK_READY;
    }
    pthread->priority = prio;
    // 初始化自己的内核栈顶地址到PCB所在页的顶部
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    // 线程没有页表，而是共享进程的页表，所以为NULL
    pthread->pgdir = NULL;
    // 初始化文件描述符表
    pthread->fd_table[0] = 0; // 标准输入
    pthread->fd_table[1] = 1; // 标准输出
    pthread->fd_table[2] = 2; // 标准错误
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
    pthread->cwd_inode_nr = 0; // 默认工作目录为根目录
    pthread->stack_magic = 0x19920928; // 自定义魔数
}

// 创建线程
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg) {
    // 在内核空间中申请1页，存放PCB
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);
    // 加入到就绪队列和全部队列中
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    return thread;
}

// 构造主线程的pcb
static void make_main_thread() {
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

// 任务调度
void schedule() {
    ASSERT(intr_get_status() == INTR_OFF);
    struct task_struct *cur = running_thread();
    if (cur->status == TASK_RUNNING) {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    } else {
        // 出现了阻塞等情况，就不要将当前线程再加入到就绪队列里
        // 而是应该唤醒的时候再加入
    }
    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    // 切换页表，如果是下一个是进程，还要更换TSS
    process_activate(next);
    switch_to(cur, next);
}

// 开启多线程环境
void thread_init() {
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    process_execute(init, "init");
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init_done\n");
}

pid_t fork_pid(void) {
    return allocate_pid();
}

// 主动让出CPU，与阻塞不同的地方在于当前线程的状态是READY，而不是BLOCK
void thread_yield() {
    struct task_struct *cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

// 阻塞线程
void thread_block(enum task_status stat) {
    ASSERT((stat == TASK_BLOCKED || stat == TASK_HANGING || stat == TASK_WAITING));
    enum intr_status old_status = intr_disable();
    struct task_struct *cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();
    intr_set_status(old_status);
}

// 唤醒目标线程
void thread_unblock(struct task_struct *pthread) {
    enum intr_status old_status = intr_disable();
    ASSERT((pthread->status == TASK_BLOCKED || pthread->status == TASK_HANGING || pthread->status == TASK_WAITING));
    if (pthread->status != TASK_READY) {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("thread_unlock: blocked thread in ready_list");
        }
        // 放到队列最前端以求最快得到调度
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}
