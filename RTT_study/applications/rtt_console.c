/**
 * @file    rtt_console.c
 * @brief   将 RT-Thread rt_kprintf（含栈溢出/断言消息）重定向到 RTT（RTT_PRINTF）
 * @note    根因：board.c 已 rt_console_set_device("uart4")，_console_device 非 NULL，
 *          默认 rt_kprintf 会走 rt_device_write 绕过 rt_hw_console_output；
 *          故这里强定义覆盖 rt_kprintf 本身（RT_WEAK），无论 console 设没设都进 RTT。
 */
#include <rtthread.h>
#include <stdarg.h>
#include "rtt_manager.h"

/* 强定义覆盖 rt_kprintf（RT_WEAK） */
int rt_kprintf(const char *fmt, ...)
{
    va_list args;
    char buf[RT_CONSOLEBUF_SIZE];
    rt_size_t len;

    va_start(args, fmt);
    len = rt_vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    RTT_PRINTF("%s", buf);
    return (int)len;
}

/* 强定义覆盖 rt_hw_console_output（RT_WEAK）：_console_device 为 NULL 时（rt_kputs 等）兜底 */
void rt_hw_console_output(const char *str)
{
    RTT_PRINTF("%s", str);
}

/* EOF */
