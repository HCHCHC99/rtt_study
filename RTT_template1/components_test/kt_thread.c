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
