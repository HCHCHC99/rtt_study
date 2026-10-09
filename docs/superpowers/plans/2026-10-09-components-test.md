# 实现计划：RT-Thread 内核对象组件验证套件（批次①）

设计文档：`docs/superpowers/specs/2026-10-09-components-test-design.md`（已获用户批准）
日期：2026-10-09 ｜ 状态：待执行

## 目标

在 `D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\` 新建 13 个文件（SConscript + kt_common.h + 10 个组件测试 + kt_menu.c），并把 `applications\main.c` 换成最小骨架（原文备份为 `main.c.bak_orig`）。本工程今后仅用于验证 RT-Thread 组件功能；业务代码保留编译但不再被调用。

## 环境约束（执行时必须遵守）

- **pwsh 工具在本工作区不可用**（沙箱 ACL 错误 `SetNamedSecurityInfoW failed (Win32 5)`，已验证重试无效）。只用 read/write/edit/glob/grep 工具。
- **本地无法编译**（无可用命令行）：编译、烧录、硬件验证全部由用户在 RT-Thread Studio + J-Link 完成。因此"每任务跑测试"的 TDD 循环退化为：写文件 → 按下方 API 事实表逐行自查 → 用户统一编译验证。这是对 writing-plans 验证步骤的**有意偏离**，已在任务内注明替代验证手段。
- 本目录**不是 git 仓库**：无 commit 步骤。
- 不修改 rtconfig.h、.cproject、uvprojx。`.cproject` sourceEntries 全工程收录 → `components_test/` 会被 RT-Thread Studio 自动编译；顶层 `SConscript` 自动扫描带 SConscript 的子目录。
- 代码风格：C99、4 空格缩进、声明放块首（与工程现有风格一致）；注释中文；每文件注释头写明验证的组件与对应 msh 命令。

## 已验证的 API 事实（RT-Thread 4.1.1，写代码时以此为准，勿凭记忆）

| 事实 | 出处 |
|---|---|
| `rt_sem_take(rt_sem_t, rt_int32_t timeout)`；`RT_WAITING_NO=0`，`RT_WAITING_FOREVER=-1`，超时返回 `-RT_ETIMEOUT` | rtthread.h:355, rtdef.h:726-727 |
| `rt_sem_release/trytake/init(sem,name,value,flag)/detach/delete` | rtthread.h:345-357 |
| `struct rt_semaphore` 字段：`value`(rt_uint16_t) | rtdef.h:743-749 |
| `rt_mutex_take` 递归支持：owner==self 时 `hold++`（上限 `RT_MUTEX_HOLD_MAX`，超过返回 `-RT_EFULL`） | ipc.c:942-953 |
| `struct rt_mutex` 字段：`value`、`original_priority`、`hold`(rt_uint8_t)、`owner`(rt_thread*) | rtdef.h:757-767 |
| 优先级继承：低 prio 线程持锁、高 urgency 线程等待 → owner 的 `current_priority` 被抬升；release 且 hold==0 时恢复 `original_priority` | ipc.c（mutex 实现） |
| `rt_event_recv(event,set,opt,timeout,recved*)`；`RT_EVENT_FLAG_AND=0x01 / OR=0x02 / CLEAR=0x04` | rtthread.h:390-394, rtdef.h:775-777 |
| `rt_mb_send(mb, rt_ubase_t value)` 值语义（4 字节）；`rt_mb_urgent` 插队首；满箱 `-RT_EFULL`；`struct rt_mailbox` 有 `size/entry` 字段 | rtthread.h:402-419, rtdef.h:795-808 |
| `rt_mq_create(name,msg_size,max_msgs,flag)`；`rt_mq_recv(mq,buf,size,timeout)`；`rt_mq_urgent`；`struct rt_messagequeue` 有 `msg_size/max_msgs/entry` | rtthread.h:426-451, rtdef.h:816-832 |
| `rt_mp_init(mp,name,pool,block_count,block_size)`；`rt_mp_alloc(mp, time)` 可带超时阻塞；`rt_mp_free(void*)`（只需指针）；`struct rt_mempool` 字段：`block_size/block_total_count/block_free_count` | rtthread.h:237-255, rtdef.h:919-933 |
| `rt_malloc/rt_calloc/rt_realloc/rt_free`；`rt_memory_info(rt_size_t *total, *used, *max_used)` | rtthread.h:266-273 |
| `rt_timer_create(name,timeout_fn,parameter,tick,flag)`；`RT_TIMER_FLAG_ONE_SHOT=0x0 / PERIODIC=0x2 / HARD=0x0 / SOFT=0x4`；`RT_TIMER_CTRL_SET_TIME=0x0 / GET_TIME=0x1`（GET_TIME 返回 `init_tick`） | rtthread.h:96-113, rtdef.h:494-507 |
| `rt_thread_create(name,entry,param,stack_size,prio,tick)`；栈大小单位=字节 | rtthread.h:144-149 |
| `rt_thread_control(thread, cmd, arg)`；`RT_THREAD_CTRL_CHANGE_PRIORITY=0x02`，arg 为 `rt_uint8_t*`：`thread->current_priority = *(rt_uint8_t*)arg` | thread.c:718-748, rtdef.h:582-586 |
| `rt_thread_idle_sethook/delhook` 存在（`RT_IDLE_HOOK_LIST_SIZE=4`，rtconfig.h 已开 RT_USING_IDLE_HOOK） | idle.c:82,114 |
| `MSH_CMD_EXPORT_ALIAS(fn, name, desc)` / `MSH_CMD_EXPORT(fn, desc)`，desc 不带引号 | finsh_api.h；applications/main.c 已有用例 |
| `RT_TICK_PER_SECOND=1000`；tshell prio 20；互斥量唤醒按 RT_IPC_FLAG_PRIO | rtconfig.h |
| 动态线程 entry 返回后对象由 idle 线程回收；`rt_thread_find` 返回句柄，回收后返回 RT_NULL | thread.c/idle.c |

数值约定（kt_common.h 统一定义）：`KT_PRIO_HIGH=24 / MID=26 / LOW=28`（都比 tshell 20 低，避免干扰 msh），栈 `KT_STACK_SMALL=1024`，超时上限 `KT_TIMEOUT_2S = RT_TICK_PER_SECOND*2`。

---

## Task 1：新建 components_test 目录与构建脚本

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\SConscript`

```python
from building import *

cwd = GetCurrentDir()
src = Glob('*.c')
CPPPATH = [cwd]

group = DefineGroup('components_test', src, depend = ['RT_USING_THREAD'], CPPPATH = CPPPATH)

Return('group')
```

**步骤**：用 write 工具创建。**验证**：glob 确认文件存在；内容与上文逐字一致（SCons 模板与 RTT_template1/applications/SConscript 同款）。

## Task 2：main.c 备份并换成最小骨架

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\applications\main.c`（重写）
**新建**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\applications\main.c.bak_orig`（原文逐字备份，`.bak_orig` 后缀不会被 CDT/SCons 当源码编译）

**步骤**：
1. read 原 main.c 全文；
2. write 到 main.c.bak_orig（一字不改）；
3. write 新 main.c：

```c
/*
 * main.c — 最小骨架
 * 原业务 main 已备份至 main.c.bak_orig。本工程仅用于 RT-Thread 组件功能验证，
 * Dev/Task/Utils 业务代码保留编译但不再被调用。
 */
#include <rtthread.h>

int main(void)
{
    rt_kprintf("\n=== RT-Thread components_test template ===\n");
    rt_kprintf("build: %s %s\n", __DATE__, __TIME__);
    rt_kprintf("log -> SEGGER RTT Viewer, cmd -> uart4 msh\n");
    rt_kprintf("type 'kt_list' for test commands.\n");

    return 0;
}
```

**验证**：glob 确认两个文件；read main.c.bak_orig 与原内容一致；新 main.c 不再 include 任何业务头（不得出现 `#include "xxx.h"` 业务头）。旧 main.c 里的 list_thread/list_sem 等 msh 命令随骨架移除属预期（finsh 自带 list 系列命令仍在）。

## Task 3：公共头 kt_common.h

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_common.h`

```c
/*
 * kt_common.h — components_test 公共定义
 *
 * 约定（所有 kt_*.c 必须遵守）：
 *  - 一组件一文件 kt_<组件>.c，测试入口 void kt_<组件>_test(void)（非 static），
 *    并用 MSH_CMD_EXPORT_ALIAS 导出同名 msh 命令；
 *  - 所有阻塞调用必须带超时（<= KT_TIMEOUT_2S），防止挂死 tshell；
 *  - 每个测试自清理（delete/detach），可重复执行；
 *  - 定时器/空闲钩子回调内禁止 rt_kprintf，只允许置计数或放信号量；
 *  - 校验一律用 KT_CHECK，结束一律 kt_result_report 打印 PASS/FAIL 汇总。
 */
#ifndef __KT_COMMON_H__
#define __KT_COMMON_H__

#include <rtthread.h>

/* ---------- 统一常量 ---------- */
#define KT_PRIO_HIGH        24      /* 数值比 20(tshell) 大 = 更低 urgency，不抢 msh */
#define KT_PRIO_MID         26
#define KT_PRIO_LOW         28
#define KT_STACK_SMALL      1024    /* 字节 */
#define KT_TIMEOUT_2S       (RT_TICK_PER_SECOND * 2)

/* ---------- 结果统计 ---------- */
struct kt_result
{
    const char *name;           /* 组件名，如 "sem" */
    rt_uint32_t total;
    rt_uint32_t fail;
};

static inline void kt_result_init(struct kt_result *r, const char *name)
{
    r->name  = name;
    r->total = 0;
    r->fail  = 0;
}

static inline void kt_result_report(const struct kt_result *r)
{
    rt_kprintf("[KT][%s] ============ %s : %u/%u ============\n",
               r->name,
               (r->fail == 0) ? "PASS" : "FAIL",
               (unsigned)(r->total - r->fail), (unsigned)r->total);
}

/* 校验一步：cond 真记 PASS，假记 FAIL 并打印文件:行号。不中断流程。 */
#define KT_CHECK(res, cond, desc)                                   \
    do                                                              \
    {                                                               \
        (res)->total++;                                             \
        if (cond)                                                   \
            rt_kprintf("[KT][%s]   PASS %s\n", (res)->name, desc);  \
        else                                                        \
        {                                                           \
            (res)->fail++;                                          \
            rt_kprintf("[KT][%s]   FAIL %s @%s:%d\n",               \
                       (res)->name, desc, __FILE__, __LINE__);      \
        }                                                           \
    } while (0)

/* ---------- 各组件测试入口（kt_menu.c 汇总调用） ---------- */
void kt_thread_test(void);
void kt_sem_test(void);
void kt_mutex_test(void);
void kt_event_test(void);
void kt_mail_test(void);
void kt_mq_test(void);
void kt_mp_test(void);
void kt_heap_test(void);
void kt_timer_test(void);
void kt_idle_test(void);

#endif /* __KT_COMMON_H__ */
```

**验证**：read 回读；确认 10 个入口声明与后续文件定义的函数名逐一对应。

## Task 4：kt_thread.c（线程）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_thread.c`
**验证点**：动态创建/启动/自清理回收、参数传递、`rt_thread_self`/`rt_thread_find`、优先级、`rt_thread_yield`、`rt_thread_mdelay` 实测时长。

```c
/*
 * kt_thread.c — 线程基础功能验证
 * 命令: kt_thread
 */
#include <rtthread.h>
#include "kt_common.h"

static struct kt_result g_r;

static struct rt_semaphore t_done;      /* worker -> main: 结束通知 */
static volatile rt_uint32_t t_param;        /* worker 收到的参数 */
static volatile rt_uint32_t t_prio;         /* worker 自报优先级 */
static volatile rt_err_t    t_yield_err = -1;
static rt_thread_t t_self_handle = RT_NULL; /* worker 自报句柄 */

static void t_worker(void *p)
{
    rt_thread_t self = rt_thread_self();

    t_param = (rt_uint32_t)(rt_base_t)p;
    if (self != RT_NULL)
    {
        t_self_handle = self;
        t_prio = self->current_priority;
    }
    t_yield_err = rt_thread_yield();

    rt_sem_release(&t_done);
}

void kt_thread_test(void)
{
    rt_thread_t tid;
    rt_err_t err;
    rt_uint32_t tick0, tick1, elapsed;

    kt_result_init(&g_r, "thread");

    err = rt_sem_init(&t_done, "kt_td", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_sem_init(t_done)");

    tid = rt_thread_create("kt_t1", t_worker, (void *)(rt_base_t)0x1234,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "rt_thread_create != NULL");
    if (tid == RT_NULL)
    {
        rt_sem_detach(&t_done);
        kt_result_report(&g_r);
        return;
    }

    KT_CHECK(&g_r, rt_thread_find("kt_t1") == tid, "rt_thread_find == tid");

    err = rt_thread_startup(tid);
    KT_CHECK(&g_r, err == RT_EOK, "rt_thread_startup");

    /* worker prio 24 低于 main, main 阻塞等信号量时 worker 才运行 */
    err = rt_sem_take(&t_done, KT_TIMEOUT_2S);
    KT_CHECK(&g_r, err == RT_EOK, "worker done sem in 2s");
    KT_CHECK(&g_r, t_param == 0x1234, "worker param == 0x1234");
    KT_CHECK(&g_r, t_self_handle == tid, "rt_thread_self in worker");
    KT_CHECK(&g_r, t_prio == KT_PRIO_HIGH, "worker priority == 24");
    KT_CHECK(&g_r, t_yield_err == RT_EOK, "rt_thread_yield in worker");

    /* mdelay 实测: 期望 ~50 tick, 容差 [40,100] */
    tick0 = rt_tick_get();
    rt_thread_mdelay(50);
    tick1 = rt_tick_get();
    elapsed = tick1 - tick0;
    KT_CHECK(&g_r, (elapsed >= 40) && (elapsed <= 100),
             "mdelay(50) elapsed 40~100 ticks");

    /* worker 已退出, 对象由 idle 线程回收 */
    rt_thread_mdelay(10);
    KT_CHECK(&g_r, rt_thread_find("kt_t1") == RT_NULL,
             "thread recycled by idle");

    rt_sem_detach(&t_done);
    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_thread_test, kt_thread, RT-Thread thread test);
```

## Task 5：kt_sem.c（信号量）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_sem.c`
**验证点**：初值消费、阻塞超时及实测时长、release 计数语义、跨线程 release 唤醒、动态 create/delete、`rt_sem_trytake`。

```c
/*
 * kt_sem.c — 信号量验证: 计数/超时/跨线程唤醒
 * 命令: kt_sem
 */
#include <rtthread.h>
#include "kt_common.h"

static struct kt_result g_r;

static struct rt_semaphore s_sem;   /* 被测静态信号量 */
static struct rt_semaphore s_go;    /* main -> child: 放行 */
static struct rt_semaphore s_done;  /* child -> main: 完成 */
static rt_sem_t s_dyn = RT_NULL;
static volatile rt_err_t s_child_err = -1;

static void s_worker(void *p)
{
    rt_sem_take(&s_go, KT_TIMEOUT_2S);              /* 等 main 放行 */
    s_child_err = rt_sem_take((rt_sem_t)p, KT_TIMEOUT_2S);
    rt_sem_release(&s_done);
}

void kt_sem_test(void)
{
    rt_thread_t tid;
    rt_tick_t start, elapsed;
    rt_err_t err;

    kt_result_init(&g_r, "sem");

    err = rt_sem_init(&s_sem, "kt_s1", 1, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_sem_init(value=1)");

    err = rt_sem_take(&s_sem, RT_WAITING_NO);
    KT_CHECK(&g_r, err == RT_EOK, "take consume init value");

    err = rt_sem_take(&s_sem, RT_WAITING_NO);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "take on empty(NOBLOCK) timeout");

    /* 空信号量带超时 take, 实测等待 ~200ms, 容差 [150,350] */
    start = rt_tick_get();
    err = rt_sem_take(&s_sem, RT_TICK_PER_SECOND / 5);
    elapsed = rt_tick_get() - start;
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "take timeout err");
    KT_CHECK(&g_r, (elapsed >= RT_TICK_PER_SECOND * 3 / 20) &&
                   (elapsed <= RT_TICK_PER_SECOND * 7 / 20),
             "timeout elapsed ~200ms(150~350)");

    err = rt_sem_release(&s_sem);
    KT_CHECK(&g_r, err == RT_EOK, "rt_sem_release");
    err = rt_sem_take(&s_sem, RT_WAITING_NO);
    KT_CHECK(&g_r, err == RT_EOK, "take after release");

    /* 计数语义: 连续 release 不封顶 */
    err  = rt_sem_release(&s_sem);
    err |= rt_sem_release(&s_sem);
    KT_CHECK(&g_r, err == RT_EOK, "release twice ok");
    err  = rt_sem_take(&s_sem, RT_WAITING_NO);
    err |= rt_sem_take(&s_sem, RT_WAITING_NO);
    KT_CHECK(&g_r, err == RT_EOK, "take twice after release");

    /* 跨线程: child 阻塞 take, main release 唤醒 */
    err  = rt_sem_init(&s_go,   "kt_sg", 0, RT_IPC_FLAG_FIFO);
    err |= rt_sem_init(&s_done, "kt_sd", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

    tid = rt_thread_create("kt_sc", s_worker, &s_sem,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        s_child_err = -1;
        rt_thread_startup(tid);
        rt_thread_mdelay(20);           /* child 已阻塞在 s_go */
        rt_sem_release(&s_go);          /* 放行 */
        rt_thread_mdelay(50);           /* child 已阻塞在 s_sem */
        rt_sem_release(&s_sem);         /* 唤醒 child */
        err = rt_sem_take(&s_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, s_child_err == RT_EOK,
                 "child take ok(wake by release)");
    }
    rt_thread_mdelay(10);               /* child 退出并被 idle 回收 */

    s_dyn = rt_sem_create("kt_s2", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, s_dyn != RT_NULL, "rt_sem_create != NULL");
    if (s_dyn != RT_NULL)
    {
        rt_sem_release(s_dyn);
        err = rt_sem_trytake(s_dyn);
        KT_CHECK(&g_r, err == RT_EOK, "trytake on dynamic sem");
        err = rt_sem_delete(s_dyn);
        KT_CHECK(&g_r, err == RT_EOK, "rt_sem_delete");
    }

    rt_sem_detach(&s_go);
    rt_sem_detach(&s_done);
    rt_sem_detach(&s_sem);
    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_sem_test, kt_sem, RT-Thread semaphore test);
```

## Task 6：kt_mutex.c（互斥量）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_mutex.c`
**验证点**：owner/hold 字段、递归持有、优先级继承（main 降到 28，child 26 抢锁 → main 被抬到 26）与释放后恢复、静态 init/trytake/detach、动态 create/delete。
**关键机制**：测试开头用 `rt_thread_control(RT_THREAD_CTRL_CHANGE_PRIORITY)` 把 main 降到 KT_PRIO_LOW(28)（否则 main 默认优先级比 child 更 urgent，继承不可观测），结束恢复。child 抢到锁后立即释放并通知。

```c
/*
 * kt_mutex.c — 互斥量验证: 递归/优先级继承/恢复/静态动态
 * 命令: kt_mutex
 */
#include <rtthread.h>
#include "kt_common.h"

static struct kt_result g_r;

static rt_mutex_t m_mux = RT_NULL;
static struct rt_semaphore m_go;    /* main -> child: 放行 */
static struct rt_semaphore m_done;  /* child -> main: 完成 */
static volatile rt_err_t    m_child_err = -1;
static volatile rt_uint32_t m_prio_before, m_prio_during, m_prio_after;
static rt_uint8_t m_saved_prio;

static void m_worker(void *p)
{
    rt_sem_take(&m_go, KT_TIMEOUT_2S);              /* 等 main 放行 */
    m_child_err = rt_mutex_take((rt_mutex_t)p, KT_TIMEOUT_2S);
    if (m_child_err == RT_EOK)
        rt_mutex_release((rt_mutex_t)p);            /* 立即归还, 触发优先级恢复 */
    rt_sem_release(&m_done);
}

void kt_mutex_test(void)
{
    rt_thread_t tid;
    rt_err_t err;
    rt_uint8_t prio_tmp;
    struct rt_mutex m_sta;

    kt_result_init(&g_r, "mutex");

    /* 0. 把 main 降到 KT_PRIO_LOW, 让"child(26) 抢 main(28) 的锁"可观测继承 */
    m_saved_prio = rt_thread_self()->current_priority;
    prio_tmp = KT_PRIO_LOW;
    err = rt_thread_control(rt_thread_self(),
                            RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_tmp);
    KT_CHECK(&g_r, err == RT_EOK, "control change own priority");
    KT_CHECK(&g_r, rt_thread_self()->current_priority == KT_PRIO_LOW,
             "priority now 28");

    m_mux = rt_mutex_create("kt_m1", RT_IPC_FLAG_PRIO);
    KT_CHECK(&g_r, m_mux != RT_NULL, "rt_mutex_create != NULL");
    if (m_mux != RT_NULL)
    {
        err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "main take");
        KT_CHECK(&g_r, m_mux->owner == rt_thread_self(), "owner == self");
        KT_CHECK(&g_r, m_mux->hold == 1, "hold == 1");

        err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "recursive take");
        KT_CHECK(&g_r, m_mux->hold == 2, "hold == 2");

        err  = rt_mutex_release(m_mux);
        err |= rt_mutex_release(m_mux);
        KT_CHECK(&g_r, err == RT_EOK, "release twice");
        KT_CHECK(&g_r, m_mux->owner == RT_NULL, "owner back to NULL");

        /* 优先级继承: child(26) 阻塞在 main(28) 持有的锁上 */
        err  = rt_sem_init(&m_go,   "kt_mg", 0, RT_IPC_FLAG_FIFO);
        err |= rt_sem_init(&m_done, "kt_md", 0, RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

        tid = rt_thread_create("kt_mc", m_worker, m_mux,
                               KT_STACK_SMALL, KT_PRIO_MID, 10);
        KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
        if (tid != RT_NULL)
        {
            m_child_err = -1;
            err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
            KT_CHECK(&g_r, err == RT_EOK, "main take again");
            m_prio_before = rt_thread_self()->current_priority;

            rt_thread_startup(tid);
            rt_thread_mdelay(20);           /* child 阻塞在 m_go */
            rt_sem_release(&m_go);          /* 放行, child 转为阻塞在 m_mux */
            rt_thread_mdelay(50);           /* 继承生效 */
            m_prio_during = rt_thread_self()->current_priority;
            KT_CHECK(&g_r, m_prio_during == KT_PRIO_MID,
                     "priority inherited to child prio(26)");

            rt_mutex_release(m_mux);        /* 归还, child(26) 抢到 */
            err = rt_sem_take(&m_done, KT_TIMEOUT_2S);
            KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
            KT_CHECK(&g_r, m_child_err == RT_EOK, "child take ok");

            rt_thread_mdelay(10);           /* 等优先级恢复 */
            m_prio_after = rt_thread_self()->current_priority;
            KT_CHECK(&g_r, m_prio_after == m_prio_before,
                     "priority restored");
        }
        rt_thread_mdelay(10);

        rt_sem_detach(&m_go);
        rt_sem_detach(&m_done);

        err = rt_mutex_delete(m_mux);
        KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_delete");
    }

    /* 静态接口 */
    err = rt_mutex_init(&m_sta, "kt_m2", RT_IPC_FLAG_PRIO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_init");
    err = rt_mutex_trytake(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "trytake first");
    err = rt_mutex_trytake(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "trytake again(recursive)");
    err  = rt_mutex_release(&m_sta);
    err |= rt_mutex_release(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "static release twice");
    err = rt_mutex_detach(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_detach");

    /* 恢复 main 原优先级 */
    prio_tmp = m_saved_prio;
    rt_thread_control(rt_thread_self(),
                      RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_tmp);
    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mutex_test, kt_mutex, RT-Thread mutex test);
```

## Task 7：kt_event.c（事件集）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_event.c`
**验证点**：空事件超时、send/recv、不清除可重复收、AND 条件不齐、OR|CLEAR 收到即清、`e_evt.set` 清零、跨线程 AND 等待、动态 create/delete。

```c
/*
 * kt_event.c — 事件集验证: AND/OR/CLEAR/跨线程
 * 命令: kt_event
 */
#include <rtthread.h>
#include "kt_common.h"

#define KT_E_A  (1 << 0)
#define KT_E_B  (1 << 1)
#define KT_E_C  (1 << 2)

static struct kt_result g_r;

static struct rt_event e_evt;
static struct rt_semaphore e_go;    /* main -> child: 放行 */
static struct rt_semaphore e_done;  /* child -> main: 完成 */
static volatile rt_err_t    e_child_err = -1;
static volatile rt_uint32_t e_recved;

static void e_worker(void *p)
{
    rt_sem_take(&e_go, KT_TIMEOUT_2S);
    e_child_err = rt_event_recv((rt_event_t)p, KT_E_A | KT_E_B,
                                RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                KT_TIMEOUT_2S, (rt_uint32_t *)&e_recved);
    rt_sem_release(&e_done);
}

void kt_event_test(void)
{
    rt_thread_t tid;
    rt_err_t err;
    rt_uint32_t recved = 0;

    kt_result_init(&g_r, "event");

    err = rt_event_init(&e_evt, "kt_e1", RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_event_init");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");

    err = rt_event_send(&e_evt, KT_E_A);
    KT_CHECK(&g_r, err == RT_EOK, "rt_event_send");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, (err == RT_EOK) && (recved == KT_E_A), "recv A");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == RT_EOK, "recv A again(no CLEAR)");

    err = rt_event_recv(&e_evt, KT_E_A | KT_E_B, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "AND(A|B) timeout(only A set)");

    /* OR|CLEAR: 收到即清 */
    err = rt_event_recv(&e_evt, KT_E_A | KT_E_B,
                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, (err == RT_EOK) && (recved == KT_E_A), "OR(A|B) ok");
    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv after CLEAR timeout");

    /* 跨线程: child 等 A|B 全到 */
    err  = rt_sem_init(&e_go,   "kt_eg", 0, RT_IPC_FLAG_FIFO);
    err |= rt_sem_init(&e_done, "kt_ed", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

    tid = rt_thread_create("kt_ec", e_worker, &e_evt,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        e_child_err = -1;
        e_recved = 0xFFFFFFFFu;
        rt_thread_startup(tid);
        rt_thread_mdelay(20);           /* child 阻塞在 e_go */
        rt_sem_release(&e_go);
        rt_thread_mdelay(20);           /* child 阻塞在 event_recv */
        rt_event_send(&e_evt, KT_E_B);  /* 半个条件 */
        rt_thread_mdelay(20);
        KT_CHECK(&g_r, e_child_err == -1, "child still waiting(only B)");
        rt_event_send(&e_evt, KT_E_A);  /* 条件齐 */
        err = rt_sem_take(&e_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, e_child_err == RT_EOK, "child recv ok");
        KT_CHECK(&g_r, e_recved == (KT_E_A | KT_E_B), "child recved == A|B");
        KT_CHECK(&g_r, e_evt.set == 0, "event set cleared");
    }
    rt_thread_mdelay(10);

    rt_sem_detach(&e_go);
    rt_sem_detach(&e_done);
    rt_event_detach(&e_evt);

    /* 动态接口 */
    {
        rt_event_t dyn = rt_event_create("kt_e2", RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_event_create != NULL");
        if (dyn != RT_NULL)
        {
            rt_event_send(dyn, KT_E_C);
            err = rt_event_recv(dyn, KT_E_C,
                                RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                RT_WAITING_NO, &recved);
            KT_CHECK(&g_r, err == RT_EOK, "dynamic send/recv");
            err = rt_event_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_event_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_event_test, kt_event, RT-Thread event test);
```

## Task 8：kt_mail.c（邮箱）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_mail.c`
**验证点**：值语义收发、满箱 `-RT_EFULL` 与 `entry==size`、FIFO 顺序、`rt_mb_urgent` 插队、空箱超时、跨线程阻塞 recv 被唤醒、动态 create/delete。

```c
/*
 * kt_mail.c — 邮箱验证: 值语义/满箱/urgent 插队/跨线程
 * 命令: kt_mail
 */
#include <rtthread.h>
#include "kt_common.h"

#define MB_POOL_SIZE 8

static struct kt_result g_r;

static struct rt_mailbox b_mb;
static rt_ubase_t b_pool[MB_POOL_SIZE];
static struct rt_semaphore b_go;    /* main -> child: 放行 */
static struct rt_semaphore b_done;  /* child -> main: 完成 */
static volatile rt_err_t    b_child_err = -1;
static volatile rt_ubase_t  b_child_val;

static void b_worker(void *p)
{
    rt_ubase_t v = 0;

    rt_sem_take(&b_go, KT_TIMEOUT_2S);
    b_child_err = rt_mb_recv((rt_mailbox_t)p, &v, KT_TIMEOUT_2S);
    b_child_val = v;
    rt_sem_release(&b_done);
}

void kt_mail_test(void)
{
    rt_thread_t tid;
    rt_ubase_t v = 0;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mail");

    err = rt_mb_init(&b_mb, "kt_mb1", b_pool, MB_POOL_SIZE, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mb_init");

    err = rt_mb_send(&b_mb, 0x1111);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mb_send");
    err = rt_mb_recv(&b_mb, &v, KT_TIMEOUT_2S);
    KT_CHECK(&g_r, (err == RT_EOK) && (v == 0x1111), "recv value == 0x1111");

    /* 满箱: 8 条后拒绝 */
    for (i = 0; i < MB_POOL_SIZE; i++)
        rt_mb_send(&b_mb, (rt_ubase_t)i);
    err = rt_mb_send(&b_mb, 0xDEAD);
    KT_CHECK(&g_r, err == -RT_EFULL, "send on full mailbox");
    KT_CHECK(&g_r, b_mb.entry == MB_POOL_SIZE, "entry == pool size");

    /* FIFO 顺序排空 */
    err = RT_EOK;
    for (i = 0; i < MB_POOL_SIZE; i++)
    {
        rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
        if (v != (rt_ubase_t)i)
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "FIFO order 0..7");

    /* urgent 插队 */
    rt_mb_send(&b_mb, 0xAAAA);
    rt_mb_urgent(&b_mb, 0xBBBB);
    rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
    KT_CHECK(&g_r, v == 0xBBBB, "urgent first");
    rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
    KT_CHECK(&g_r, v == 0xAAAA, "normal next");

    /* 跨线程: child 阻塞 recv, main send 唤醒 */
    err  = rt_sem_init(&b_go,   "kt_bg", 0, RT_IPC_FLAG_FIFO);
    err |= rt_sem_init(&b_done, "kt_bd", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

    tid = rt_thread_create("kt_bc", b_worker, &b_mb,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        b_child_err = -1;
        b_child_val = 0;
        rt_thread_startup(tid);
        rt_thread_mdelay(20);           /* child 阻塞在 b_go */
        rt_sem_release(&b_go);
        rt_thread_mdelay(20);           /* child 阻塞在 mb_recv */
        rt_mb_send(&b_mb, 0x33);
        err = rt_sem_take(&b_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, b_child_err == RT_EOK, "child recv ok");
        KT_CHECK(&g_r, b_child_val == 0x33, "child value == 0x33");
    }
    rt_thread_mdelay(10);

    err = rt_mb_recv(&b_mb, &v, 10);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");

    rt_sem_detach(&b_go);
    rt_sem_detach(&b_done);
    rt_mb_detach(&b_mb);

    /* 动态接口 */
    {
        rt_mailbox_t dyn = rt_mb_create("kt_mb2", 4, RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mb_create != NULL");
        if (dyn != RT_NULL)
        {
            rt_mb_send(dyn, 0x55);
            err = rt_mb_recv(dyn, &v, RT_WAITING_NO);
            KT_CHECK(&g_r, (err == RT_EOK) && (v == 0x55),
                     "dynamic send/recv");
            err = rt_mb_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mb_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mail_test, kt_mail, RT-Thread mailbox test);
```

## Task 9：kt_mq.c（消息队列）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_mq.c`
**验证点**：结构化消息收发、urgent 插队、满队 `-RT_EFULL` 与 `entry==max_msgs`、FIFO、空队超时、动态 create/delete。

```c
/*
 * kt_mq.c — 消息队列验证: 定长消息/urgent/满队/动态
 * 命令: kt_mq
 */
#include <rtthread.h>
#include "kt_common.h"

struct kt_msg
{
    rt_uint32_t id;
    rt_uint32_t data;
};

#define MQ_POOL_MSGS 4

static struct kt_result g_r;

static struct rt_messagequeue q_mq;
static rt_uint8_t q_pool[MQ_POOL_MSGS * sizeof(struct kt_msg)];

static rt_err_t q_put(struct rt_messagequeue *mq,
                      rt_uint32_t id, rt_uint32_t data)
{
    struct kt_msg m;

    m.id = id;
    m.data = data;
    return rt_mq_send(mq, &m, sizeof(m));
}

static int q_get_eq(struct rt_messagequeue *mq,
                    rt_uint32_t id, rt_uint32_t data)
{
    struct kt_msg m;

    if (rt_mq_recv(mq, &m, sizeof(m), RT_WAITING_NO) != RT_EOK)
        return 0;
    return (m.id == id) && (m.data == data);
}

void kt_mq_test(void)
{
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mq");

    err = rt_mq_init(&q_mq, "kt_mq1", q_pool, sizeof(struct kt_msg),
                     sizeof(q_pool), RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mq_init");

    err = q_put(&q_mq, 1, 100);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mq_send");
    KT_CHECK(&g_r, q_get_eq(&q_mq, 1, 100) == 1, "recv match");

    /* urgent 插队 */
    q_put(&q_mq, 2, 0xAAAA);
    {
        struct kt_msg m;

        m.id = 3;
        m.data = 0xBBBB;
        err = rt_mq_urgent(&q_mq, &m, sizeof(m));
        KT_CHECK(&g_r, err == RT_EOK, "rt_mq_urgent");
    }
    KT_CHECK(&g_r, q_get_eq(&q_mq, 3, 0xBBBB) == 1, "urgent first");
    KT_CHECK(&g_r, q_get_eq(&q_mq, 2, 0xAAAA) == 1, "normal next");

    /* 满队: 4 条后拒绝 */
    for (i = 0; i < MQ_POOL_MSGS; i++)
        q_put(&q_mq, (rt_uint32_t)i, (rt_uint32_t)i);
    err = q_put(&q_mq, 0xDEAD, 0);
    KT_CHECK(&g_r, err == -RT_EFULL, "send on full queue");
    KT_CHECK(&g_r, q_mq.entry == MQ_POOL_MSGS, "entry == max_msgs");

    /* FIFO 排空 */
    err = RT_EOK;
    for (i = 0; i < MQ_POOL_MSGS; i++)
    {
        if (q_get_eq(&q_mq, (rt_uint32_t)i, (rt_uint32_t)i) != 1)
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "FIFO order");

    /* 空队超时 */
    {
        struct kt_msg m;

        err = rt_mq_recv(&q_mq, &m, sizeof(m), 10);
        KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");
    }

    rt_mq_detach(&q_mq);

    /* 动态接口 */
    {
        rt_mq_t dyn = rt_mq_create("kt_mq2", sizeof(struct kt_msg), 2,
                                   RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mq_create != NULL");
        if (dyn != RT_NULL)
        {
            KT_CHECK(&g_r, q_put(dyn, 9, 9) == RT_EOK, "dynamic send");
            KT_CHECK(&g_r, q_get_eq(dyn, 9, 9) == 1, "dynamic recv");
            err = rt_mq_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mq_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mq_test, kt_mq, RT-Thread message queue test);
```

## Task 10：kt_mp.c（内存池）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_mp.c`
**验证点**：init 与 `block_total_count/block_free_count`、16 块全部分配成功、耗尽后非阻塞 alloc 返回 NULL、释放后复用、**跨线程阻塞 alloc 被 free 唤醒**、块数守恒、动态 create/delete。
**注意**：child 拿走的块很可能就是 main 释放的那块（`blk[1]`），所以 child 结束后 main **不得**再 free `blk[1]`。

```c
/*
 * kt_mp.c — 内存池验证: 定块分配/耗尽/阻塞唤醒/动态
 * 命令: kt_mp
 */
#include <rtthread.h>
#include <string.h>
#include "kt_common.h"

#define MP_BLOCKS       16
#define MP_BLOCK_SIZE   32

static struct kt_result g_r;

static struct rt_mempool p_mp;
static rt_uint32_t p_pool[MP_BLOCKS * MP_BLOCK_SIZE / sizeof(rt_uint32_t)];
static struct rt_semaphore p_done;  /* child -> main: 完成 */
static volatile rt_err_t p_child_ok = 0;
static void *p_child_block = RT_NULL;

static void p_worker(void *p)
{
    /* 池已耗尽, child 阻塞在 alloc, 等 main free 一块唤醒 */
    p_child_block = rt_mp_alloc((rt_mp_t)p, KT_TIMEOUT_2S);
    p_child_ok = (p_child_block != RT_NULL);
    if (p_child_block != RT_NULL)
        rt_mp_free(p_child_block);
    rt_sem_release(&p_done);
}

void kt_mp_test(void)
{
    void *blk[MP_BLOCKS];
    rt_thread_t tid;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mp");

    err = rt_mp_init(&p_mp, "kt_mp1", p_pool, MP_BLOCKS, MP_BLOCK_SIZE);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mp_init");
    KT_CHECK(&g_r, p_mp.block_total_count == MP_BLOCKS, "block_total == 16");
    KT_CHECK(&g_r, p_mp.block_free_count == MP_BLOCKS, "block_free == 16");

    for (i = 0; i < MP_BLOCKS; i++)
        blk[i] = rt_mp_alloc(&p_mp, RT_WAITING_NO);
    err = RT_EOK;
    for (i = 0; i < MP_BLOCKS; i++)
    {
        if (blk[i] == RT_NULL)
        {
            err = -RT_ERROR;
            break;
        }
        memset(blk[i], (int)(i & 0xFF), MP_BLOCK_SIZE);
    }
    KT_CHECK(&g_r, err == RT_EOK, "alloc all 16 blocks");
    KT_CHECK(&g_r, p_mp.block_free_count == 0, "pool exhausted");
    KT_CHECK(&g_r, rt_mp_alloc(&p_mp, RT_WAITING_NO) == RT_NULL,
             "alloc on empty(NOBLOCK) NULL");

    rt_mp_free(blk[0]);
    KT_CHECK(&g_r, p_mp.block_free_count == 1, "free one -> free == 1");
    blk[0] = rt_mp_alloc(&p_mp, RT_WAITING_NO);
    KT_CHECK(&g_r, blk[0] != RT_NULL, "realloc after free");

    /* 跨线程: child 阻塞 alloc, main free 唤醒 */
    err = rt_sem_init(&p_done, "kt_pd", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init done sem");

    p_child_ok = 0;
    p_child_block = RT_NULL;
    tid = rt_thread_create("kt_pc", p_worker, &p_mp,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        rt_thread_mdelay(50);           /* child 已阻塞在 rt_mp_alloc */
        rt_mp_free(blk[1]);             /* 唤醒 child; 该块归 child,
                                           blk[1] 之后不得由 main 再释放 */
        blk[1] = RT_NULL;
        err = rt_sem_take(&p_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, p_child_ok == 1, "child alloc ok");
        KT_CHECK(&g_r, p_mp.block_free_count == 1,
                 "free == 1 after child free");
    }
    rt_thread_mdelay(10);

    /* 归还其余全部块, 验证块数守恒 */
    for (i = 0; i < MP_BLOCKS; i++)
    {
        if (blk[i] != RT_NULL)
            rt_mp_free(blk[i]);
    }
    KT_CHECK(&g_r, p_mp.block_free_count == MP_BLOCKS, "all blocks back");

    rt_sem_detach(&p_done);
    rt_mp_detach(&p_mp);

    /* 动态接口 */
    {
        rt_mp_t dyn = rt_mp_create("kt_mp2", 4, 16);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mp_create != NULL");
        if (dyn != RT_NULL)
        {
            void *b = rt_mp_alloc(dyn, RT_WAITING_NO);

            KT_CHECK(&g_r, b != RT_NULL, "dynamic alloc");
            if (b != RT_NULL)
                rt_mp_free(b);
            err = rt_mp_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mp_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mp_test, kt_mp, RT-Thread memory pool test);
```

## Task 11：kt_heap.c（堆内存）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_heap.c`
**验证点**：`rt_memory_info` 统计合理、malloc 后 used 增长、calloc 清零、realloc 扩容且数据保留、free 后 used 回落、32×64B 小对象压力循环数据完好。

```c
/*
 * kt_heap.c — 堆内存验证: malloc/calloc/realloc/free/统计
 * 命令: kt_heap
 */
#include <rtthread.h>
#include <string.h>
#include "kt_common.h"

static struct kt_result g_r;

void kt_heap_test(void)
{
    rt_uint8_t *p1, *p2, *p3;
    rt_size_t total = 0, used0, used1, used2, used3, max_used = 0;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "heap");

    rt_memory_info(&total, &used0, &max_used);
    KT_CHECK(&g_r, total > 0, "rt_memory_info total > 0");
    KT_CHECK(&g_r, max_used >= used0, "max_used >= used");
    rt_kprintf("[KT][heap]   total=%uKB used=%uKB max_used=%uKB\n",
               (unsigned)(total >> 10), (unsigned)(used0 >> 10),
               (unsigned)(max_used >> 10));

    p1 = (rt_uint8_t *)rt_malloc(512);
    KT_CHECK(&g_r, p1 != RT_NULL, "rt_malloc(512)");
    used1 = used0;
    if (p1 != RT_NULL)
    {
        memset(p1, 0xA5, 512);
        rt_memory_info(&total, &used1, &max_used);
        KT_CHECK(&g_r, used1 > used0, "used grows after malloc");
    }

    p2 = (rt_uint8_t *)rt_calloc(4, 32);
    KT_CHECK(&g_r, p2 != RT_NULL, "rt_calloc(4,32)");
    err = RT_EOK;
    if (p2 != RT_NULL)
    {
        for (i = 0; i < 128; i++)
        {
            if (p2[i] != 0)
            {
                err = -RT_ERROR;
                break;
            }
        }
    }
    KT_CHECK(&g_r, err == RT_EOK, "calloc zeroed");

    p3 = RT_NULL;
    err = RT_EOK;
    if (p1 != RT_NULL)
    {
        p1[0]   = 0x11;
        p1[511] = 0x22;
        p3 = (rt_uint8_t *)rt_realloc(p1, 2048);
        KT_CHECK(&g_r, p3 != RT_NULL, "rt_realloc(512->2048)");
        if (p3 != RT_NULL)
        {
            if ((p3[0] != 0x11) || (p3[511] != 0x22))
                err = -RT_ERROR;
        }
        else
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "realloc keeps data");

    rt_memory_info(&total, &used2, &max_used);
    if (p3 != RT_NULL)
        rt_free(p3);
    if (p2 != RT_NULL)
        rt_free(p2);
    rt_memory_info(&total, &used3, &max_used);
    KT_CHECK(&g_r, used3 < used2, "used shrinks after free");

    /* 小对象压力: 32 块 64B, 写入并校验后全部释放 */
    {
        rt_uint8_t *arr[32];

        err = RT_EOK;
        for (i = 0; i < 32; i++)
        {
            arr[i] = (rt_uint8_t *)rt_malloc(64);
            if (arr[i] == RT_NULL)
            {
                err = -RT_ERROR;
                break;
            }
            arr[i][0]  = (rt_uint8_t)i;
            arr[i][63] = (rt_uint8_t)~i;
        }
        KT_CHECK(&g_r, err == RT_EOK, "stress alloc 32x64B");

        err = RT_EOK;
        for (i = 0; i < 32; i++)
        {
            if (arr[i] == RT_NULL)
                continue;
            if ((arr[i][0] != (rt_uint8_t)i) ||
                (arr[i][63] != (rt_uint8_t)~i))
                err = -RT_ERROR;
            rt_free(arr[i]);
        }
        KT_CHECK(&g_r, err == RT_EOK, "stress data intact");
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_heap_test, kt_heap, RT-Thread heap test);
```

## Task 12：kt_timer.c（硬件定时器）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_timer.c`
**验证点**：单次（触发一次不重复）、周期（180ms 内 2~5 次，stop 后停）、`rt_timer_control` SET/GET_TIME 后按新周期触发、静态 init/start/detach。
**注意**：`RT_USING_TIMER_SOFT` 未开，全部用 HARD timer；回调在 tick 中断上下文，只允许 `t_cnt++` / `rt_sem_release`，禁止打印（kt_common.h 约定）。

```c
/*
 * kt_timer.c — 硬件定时器验证: 单次/周期/stop/control/静态
 * 命令: kt_timer
 * 注: 回调运行于 tick 中断上下文, 只置计数/放信号量, 禁止打印。
 *     软定时器(RT_USING_TIMER_SOFT)未开启, 留批次③。
 */
#include <rtthread.h>
#include "kt_common.h"

static struct kt_result g_r;

static struct rt_semaphore t_sem;   /* 回调 -> main */
static volatile rt_uint32_t t_cnt1, t_cnt2, t_cnt3;

static void t_cb1(void *p)
{
    t_cnt1++;
    rt_sem_release((rt_sem_t)p);
}

static void t_cb2(void *p)
{
    t_cnt2++;
}

static void t_cb3(void *p)
{
    t_cnt3++;
}

void kt_timer_test(void)
{
    rt_timer_t t1, t2;
    rt_err_t err;
    rt_uint32_t n;
    rt_tick_t period;

    kt_result_init(&g_r, "timer");

    err = rt_sem_init(&t_sem, "kt_ts", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_sem_init");

    /* 1. 单次 100ms */
    t_cnt1 = 0;
    t1 = rt_timer_create("kt_t1", t_cb1, &t_sem,
                         rt_tick_from_millisecond(100),
                         RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_HARD_TIMER);
    KT_CHECK(&g_r, t1 != RT_NULL, "rt_timer_create(one shot)");
    if (t1 != RT_NULL)
    {
        err  = rt_timer_start(t1);
        err |= rt_sem_take(&t_sem, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "one shot fired in 2s");
        KT_CHECK(&g_r, t_cnt1 == 1, "cb count == 1");
        rt_thread_mdelay(150);
        KT_CHECK(&g_r, t_cnt1 == 1, "one shot not repeated");
        err = rt_timer_delete(t1);
        KT_CHECK(&g_r, err == RT_EOK, "rt_timer_delete(t1)");
    }

    /* 2. 周期 50ms */
    t_cnt2 = 0;
    t2 = rt_timer_create("kt_t2", t_cb2, RT_NULL,
                         rt_tick_from_millisecond(50),
                         RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_HARD_TIMER);
    KT_CHECK(&g_r, t2 != RT_NULL, "rt_timer_create(periodic)");
    if (t2 != RT_NULL)
    {
        rt_timer_start(t2);
        rt_thread_mdelay(180);
        n = t_cnt2;
        KT_CHECK(&g_r, (n >= 2) && (n <= 5),
                 "periodic fired 2~5 in 180ms");
        rt_timer_stop(t2);
        rt_thread_mdelay(150);
        KT_CHECK(&g_r, t_cnt2 == n, "stopped, no more fires");

        /* 3. stop 后改周期 200ms 再启动 */
        period = rt_tick_from_millisecond(200);
        err = rt_timer_control(t2, RT_TIMER_CTRL_SET_TIME, &period);
        KT_CHECK(&g_r, err == RT_EOK, "rt_timer_control(SET_TIME)");
        {
            rt_tick_t getv = 0;

            err = rt_timer_control(t2, RT_TIMER_CTRL_GET_TIME, &getv);
            KT_CHECK(&g_r, (err == RT_EOK) && (getv == period),
                     "GET_TIME == 200ms");
        }
        rt_timer_start(t2);
        rt_thread_mdelay(120);
        n = t_cnt2;
        KT_CHECK(&g_r, t_cnt2 == n, "no fire within 120ms(200ms period)");
        rt_thread_mdelay(150);          /* 累计 270ms > 200ms */
        KT_CHECK(&g_r, t_cnt2 > n, "fired after new 200ms period");
        err = rt_timer_delete(t2);
        KT_CHECK(&g_r, err == RT_EOK, "rt_timer_delete(t2)");
    }

    /* 4. 静态 init/start/detach, 50ms 单次 */
    {
        struct rt_timer t_sta;

        t_cnt3 = 0;
        rt_timer_init(&t_sta, "kt_t3", t_cb3, RT_NULL,
                      rt_tick_from_millisecond(50),
                      RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_HARD_TIMER);
        rt_timer_start(&t_sta);
        rt_thread_mdelay(120);
        KT_CHECK(&g_r, t_cnt3 == 1, "static timer fired once");
        err = rt_timer_detach(&t_sta);
        KT_CHECK(&g_r, err == RT_EOK, "rt_timer_detach");
    }

    rt_sem_detach(&t_sem);
    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_timer_test, kt_timer, RT-Thread hard timer test);
```

## Task 13：kt_idle.c（空闲钩子）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_idle.c`
**验证点**：sethook 返回 EOK、main 睡眠期间 hook 持续被调用（计数增长）、delhook 后停止。

```c
/*
 * kt_idle.c — 空闲钩子验证: sethook/自动执行/delhook
 * 命令: kt_idle
 * 注: hook 在 idle 线程上下文执行, 必须极短, 禁止打印/阻塞。
 */
#include <rtthread.h>
#include "kt_common.h"

static struct kt_result g_r;

static volatile rt_uint32_t i_count;

static void i_hook(void)
{
    i_count++;
}

void kt_idle_test(void)
{
    rt_err_t err;
    rt_uint32_t n;

    kt_result_init(&g_r, "idle");

    i_count = 0;
    err = rt_thread_idle_sethook(i_hook);
    KT_CHECK(&g_r, err == RT_EOK, "rt_thread_idle_sethook");

    /* main 睡 100ms, 无其他就绪任务时 idle 必然运行 */
    rt_thread_mdelay(100);
    n = i_count;
    KT_CHECK(&g_r, n > 0, "hook ran while idle (count>0)");
    rt_kprintf("[KT][idle]   hook count in 100ms: %u\n", (unsigned)n);

    rt_thread_mdelay(100);
    KT_CHECK(&g_r, i_count > n, "hook keeps counting");

    err = rt_thread_idle_delhook(i_hook);
    KT_CHECK(&g_r, err == RT_EOK, "rt_thread_idle_delhook");
    n = i_count;
    rt_thread_mdelay(100);
    KT_CHECK(&g_r, i_count == n, "hook stopped after delhook");

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_idle_test, kt_idle, RT-Thread idle hook test);
```

## Task 14：kt_menu.c（汇总入口）

**文件**：`D:\RT_HC32F460_v.1.0.1\RTT_template1\components_test\kt_menu.c`
**命令**：`kt_list`（列命令）、`kt_all`（按序全部执行）。

```c
/*
 * kt_menu.c — components_test 汇总入口
 * 命令: kt_list 列出全部测试命令, kt_all 按序执行全部测试
 */
#include <rtthread.h>
#include "kt_common.h"

static const struct
{
    const char *cmd;
    const char *desc;
} kt_table[] =
{
    { "kt_thread", "thread create/start/find/yield/mdelay" },
    { "kt_sem",    "semaphore count/timeout/wakeup" },
    { "kt_mutex",  "mutex recursive/priority inherit" },
    { "kt_event",  "event AND/OR/CLEAR/cross-thread" },
    { "kt_mail",   "mailbox value/full/urgent/wakeup" },
    { "kt_mq",     "message queue urgent/full/FIFO" },
    { "kt_mp",     "memory pool fixed-block/blocking alloc" },
    { "kt_heap",   "heap malloc/calloc/realloc/free" },
    { "kt_timer",  "hard timer one-shot/periodic/control" },
    { "kt_idle",   "idle hook set/run/del" },
};

static void kt_print_header(void)
{
    rt_kprintf("\nRT-Thread components_test (batch1: kernel objects)\n");
    rt_kprintf("tick=%dHz, log->RTT Viewer, cmd->uart4 msh\n",
               RT_TICK_PER_SECOND);
}

static void kt_list(void)
{
    int i;

    kt_print_header();
    for (i = 0; i < sizeof(kt_table) / sizeof(kt_table[0]); i++)
        rt_kprintf("  %-10s %s\n", kt_table[i].cmd, kt_table[i].desc);
    rt_kprintf("  %-10s run all tests in order\n", "kt_all");
    rt_kprintf("hint: run 'list_thread' before/after a test to check leak.\n");
}
MSH_CMD_EXPORT_ALIAS(kt_list, kt_list, list components_test commands);

static void kt_all(void)
{
    kt_print_header();
    kt_thread_test();
    kt_sem_test();
    kt_mutex_test();
    kt_event_test();
    kt_mail_test();
    kt_mq_test();
    kt_mp_test();
    kt_heap_test();
    kt_timer_test();
    kt_idle_test();
    rt_kprintf("\n[KT] ALL DONE. Expect every block ends with PASS.\n");
}
MSH_CMD_EXPORT_ALIAS(kt_all, kt_all, run all components tests);
```

## Task 15：全局自查 + 交付验证清单

**步骤**（执行者做）：
1. glob `RTT_template1/components_test/*` 应有 13 个文件；read 每个文件与计划逐字比对；
2. grep 检查：所有 `KT_CHECK` 目标文件都 include 了 kt_common.h；所有测试入口名与 kt_common.h 声明一致；`rt_sem_detach/rt_mutex_detach/rt_event_detach/rt_mb_detach/rt_mq_detach/rt_mp_detach` 与 create/delete 成对出现；
3. grep 确认 components_test 下无 `printf(`、无 `while(1)` 死循环、无 `RT_WAITING_FOREVER`；
4. 向用户输出下方"用户验证清单"。

**用户验证清单**（交付时连同文件一起给出）：
1. RT-Thread Studio 中刷新工程 → Build（预期 0 Error；若 kt_common.h 的 `static inline` 报编译器版本问题，改 `static __inline` 后重编）；
2. J-Link 下载复位；RTT Viewer 看 banner："=== RT-Thread components_test template ==="；uart4 终端敲 `kt_list`；
3. 逐个执行 `kt_thread` / `kt_sem` / `kt_mutex` / `kt_event` / `kt_mail` / `kt_mq` / `kt_mp` / `kt_heap` / `kt_timer` / `kt_idle`，每条末尾应出现 `[KT][xxx] ============ PASS : n/n ============`；任何 FAIL 行自带 `@文件:行号`；
4. `kt_all` 一键全跑；跑前后各执行一次 `list_thread`，不应残留 kt_ 开头线程（idle 回收后 find==NULL 已在各测试内断言）；
5. `kt_mutex`、`kt_timer`、`kt_sem`、`kt_thread` 含毫秒级时序断言，若个别时序项边缘 FAIL，把 FAIL 行号报回来调容差，不改机制。

## 成功标准

- 15 个磁盘产物就位：components_test 13 文件 + main.c 重写 + main.c.bak_orig 备份；
- 用户 Studio 编译 0 Error 0 Warning（新目录）；
- 硬件上 `kt_all` 十个组件块全部以 PASS 结尾；重复执行 `kt_all` 结果一致（无泄漏、无残留对象）；
- 业务代码仍编译但不再执行（旧 main 已备份）。

## 执行方式（计划完成后向用户提供选择）

- **本会话内联执行**（推荐）：15 个任务全是一次性文件写入，代码已在计划中定稿，内联顺序写完 + 逐文件自查即可，无需子代理往返；
- **子代理驱动开发**：按 Task 派子代理 + 两阶段评审。因各文件强共享 kt_common.h 约定且本地无编译器、无并行收益，收益低。
