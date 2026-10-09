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
