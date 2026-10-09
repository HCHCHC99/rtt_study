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
