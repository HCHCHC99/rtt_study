/*
 * kt_sem.c — 信号量验证: 计数/超时/跨线程唤醒
 * 命令: kt_sem
 */
#include <rtthread.h>
#define KT_MODULE_PRINT(fmt, ...) KT_SEM_PRINT(fmt, ##__VA_ARGS__)
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
