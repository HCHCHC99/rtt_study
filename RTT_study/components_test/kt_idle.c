/*
 * kt_idle.c — 空闲钩子验证: sethook/自动执行/delhook
 * 命令: kt_idle
 * 注: hook 在 idle 线程上下文执行, 必须极短, 禁止打印/阻塞。
 */
#include <rtthread.h>
#define KT_MODULE_PRINT(fmt, ...) KT_IDLE_PRINT(fmt, ##__VA_ARGS__)
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
    KT_IDLE_PRINT("hook count in 100ms: %u", (unsigned)n);

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
