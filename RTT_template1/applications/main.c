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
