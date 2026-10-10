/*
 * kt_diag.c — 调度器/线程饿死诊断
 * 命令: kt_diag（kt_thread/kt_sem 超时路径自动调用；main.c 开机自检后自动调用 tag=boot-end）
 *
 * 一次转储回答四件事（判定树见文件尾注）：
 *  1. rt_thread_ready_priority_group 位图：目标 prio 的位到底置没置；
 *  2. __rt_ffs 对若干单 bit 的返回值（位图健全但 ffs 返回错 → FFS 表/实现坏）；
 *  3. 全线程 TCB：名字可读性、prio 对照设计值、stat、sp；
 *  4. 线程栈哨兵 '#'(0x23) 连续字节数：=stack_size 表示从未运行过。
 * 焦点线程（tshell/main/rtt_rx/idle）紧跟头部行置顶打印（行尾 <<）。
 * 全部打印必须走 MAIN_D_SYNC（写后 200µs 忙等）：本块 ~1.5KB 若用裸 MAIN_D
 * 会在微秒级突发写完，撑爆 RTT 环形缓冲后被 NO_BLOCK_SKIP 整条静默丢弃——
 * 这就是日志尾部三连丢的根因；12 行 summary 块（同款 SYNC）次次完整存活是反证。
 */
#include <rtthread.h>
#include <rthw.h>       /* rt_hw_interrupt_disable/enable（与 Task/task_set.c:10 同因） */
#include "rtt_manager.h"
#include "kt_common.h"

/* scheduler.c 全局（非 static），本树调度唯一依据 */
extern rt_uint32_t rt_thread_ready_priority_group;
/* kservice.c：选线程用的最低有效位索引 */
extern int __rt_ffs(int value);

/* 栈初始化哨兵：线程栈自 stack_addr 起被填 '#'，从未用过的前缀保持 '#' */
#define KT_STACK_FILL   '#'

static void diag_name_copy(char dst[RT_NAME_MAX + 1], const char src[RT_NAME_MAX])
{
    rt_memcpy(dst, src, RT_NAME_MAX);
    dst[RT_NAME_MAX] = '\0';
}

/* 单线程行：名字/stat/prio/mask/sp/栈哨兵；mark 非 NULL 时追加行尾（焦点标记） */
static void diag_show(struct rt_thread *t, const char *mark)
{
    char name[RT_NAME_MAX + 1];
    rt_uint8_t *p;
    rt_uint32_t free_bytes = 0;

    diag_name_copy(name, t->name);

    /* 哨兵扫描：连续 '#' 前缀长度；==stack_size 即该线程从未运行 */
    if ((t->stack_addr != RT_NULL) && (t->stack_size != 0U))
    {
        p = (rt_uint8_t *)t->stack_addr;
        while (((rt_uint32_t)(p - (rt_uint8_t *)t->stack_addr) < (rt_uint32_t)t->stack_size)
               && (*p == KT_STACK_FILL))
        {
            p++;
        }
        free_bytes = (rt_uint32_t)(p - (rt_uint8_t *)t->stack_addr);
    }

    /* stat 状态码（rtdef.h:563-567 本树实值）：0 INIT/1 READY/2 SUSPEND/3 RUNNING/4 CLOSE
       —— 与常见文档相反，READY=1、SUSPEND=2！stat=02+free<size 即睡在等事件，健康态；
       prio 对照设计值：main=10 kt=17~19 rtt_rx=19 tshell=20 workq=23 idle=31 */
    MAIN_D_SYNC("[KT][DIAG] th %-8s stat=%02x prio=%02d mask=0x%08x sp=0x%08x stk=0x%08x+%u free=%u%s",
           name, (unsigned)t->stat,
           (int)t->current_priority,
           (unsigned)t->number_mask,
           (unsigned)(rt_base_t)t->sp,
           (unsigned)(rt_base_t)t->stack_addr,
           (unsigned)t->stack_size, free_bytes,
           (mark != RT_NULL) ? mark : "");
}

/* 按名字找线程并打印（找不到也明说）——焦点置顶用 */
static void diag_show_named(const char *name)
{
    struct rt_object_information *info;
    struct rt_list_node *node;
    char nm[RT_NAME_MAX + 1];

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL)
    {
        return;
    }
    for (node = info->object_list.next; node != &(info->object_list); node = node->next)
    {
        struct rt_thread *t = (struct rt_thread *)rt_list_entry(node, struct rt_object, list);

        diag_name_copy(nm, t->name);
        if (rt_strcmp(nm, name) == 0)
        {
            diag_show(t, "  <<");
            return;
        }
    }
    MAIN_D_SYNC("[KT][DIAG] th %-8s NOT FOUND", name);
}

void kt_diag_dump(const char *tag)
{
    struct rt_object_information *info;
    struct rt_list_node *node;
    char self_name[RT_NAME_MAX + 1];
    rt_thread_t self;
    rt_size_t total = 0, used = 0, max_used = 0;
    rt_base_t level;

    MAIN_D_SYNC("[KT][DIAG][%s] === scheduler/thread dump ===", tag);
    MAIN_D_SYNC("[KT][DIAG] ready_group=0x%08x  ffs(0x00100000)=%d(expect21) ffs(0x01000000)=%d(expect25) ffs(0x02000000)=%d(expect26) ffs(0x80000000)=%d(expect32)",
           rt_thread_ready_priority_group,
           __rt_ffs(0x00100000), __rt_ffs(0x01000000), __rt_ffs(0x02000000), __rt_ffs(0x80000000));
    rt_memory_info(&total, &used, &max_used);
    MAIN_D_SYNC("[KT][DIAG] heap total=%u used=%u max=%u", (unsigned)total, (unsigned)used, (unsigned)max_used);

    /* 焦点线程置顶（此刻仅 main 在运行，无锁快照足够） */
    diag_show_named("tshell");
    diag_show_named("main");
    diag_show_named("rtt_rx");
    diag_show_named("idle");

    level = rt_hw_interrupt_disable();
    self = rt_thread_self();
    if (self != RT_NULL)
    {
        diag_name_copy(self_name, self->name);
        MAIN_D_SYNC("[KT][DIAG] dumper thread: %s", self_name);
    }

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info != RT_NULL)
    {
        for (node = info->object_list.next; node != &(info->object_list); node = node->next)
        {
            diag_show((struct rt_thread *)rt_list_entry(node, struct rt_object, list), RT_NULL);
        }
    }
    rt_hw_interrupt_enable(level);

    MAIN_D_SYNC("[KT][DIAG][%s] === dump end (bit17=0x%08x) ===", tag,
           (unsigned)(rt_thread_ready_priority_group & (1UL << KT_PRIO_HIGH)));
}

/* 判定树（本树 rtdef.h:563-567：INIT=0 READY=1 SUSPEND=2 RUNNING=3 CLOSE=4）：
 *  - tshell stat=02(SUSPEND) free<1024 → 运行过、睡在 rx_sem 等输入：msh 待命（健康态，
 *    2026-10-10 实测 free=752）；
 *  - tshell stat=00(INIT) free=1024    → 已创建但从未运行：查 startup 链；
 *  - tshell stat=01(READY) 持续不消    → EOF 紧忙转（shell.c:167 裸 return -1，入口
 *    shell.c:492 continue）→ 查设备绑定：finsh_set_device 失败路径是静默的
 *    （shell.c:225-241 open 非 EOK 时无 else 无报错）；
 *  - 名字乱码/prio 变小               → TCB 被踩（查相邻对象溢出）；
 *  - idle free=256                    → idle 从未运行，佐证系统性饿死。 */

static void kt_diag_cmd(void)
{
    kt_diag_dump("msh");
}
MSH_CMD_EXPORT_ALIAS(kt_diag_cmd, kt_diag, dump scheduler ready bitmap and all thread TCBs);

/* EOF */
