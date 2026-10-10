/*
 * msh_cmd.c — 自定义 msh 命令模块（示例命令 demo）
 *
 * 用法（RTT Viewer Terminal 输入，回车执行）：
 *   demo            -> 打印用法
 *   demo info       -> 运行时长 / 堆水位
 *   demo add 3 4    -> 参数解析示例，打印 3 + 4 = 7
 *
 * 机制备注（本树已核实）：
 *  - MSH_CMD_EXPORT_ALIAS 把函数挂进 finsh 符号命令表，msh 按名查找执行；
 *  - 命令在 tshell 线程上下文执行；输入链路：RTT 下行 ch0 键入 ->
 *    rtt_rx 20ms 轮询（applications/msh_rtt.c:68-77）-> rx_indicate ->
 *    tshell 收字节（rt-thread/components/finsh/shell.c:172-184）。
 */
#include <stdlib.h>
#include <rtthread.h>
#include "msh_cmd.h"

/* 示例命令：无参 = 用法；子命令 info/add 演示无参、有参两种形态 */
static void cmd_demo(int argc, char **argv)
{
    if ((argc >= 2) && (rt_strcmp(argv[1], "info") == 0))
    {
        rt_uint32_t tick = rt_tick_get();
        rt_size_t total = 0, used = 0, max_used = 0;

        rt_memory_info(&total, &used, &max_used);
        rt_kprintf("uptime : %u tick (%u.%u s)\n",
                   (unsigned)tick,
                   (unsigned)(tick / RT_TICK_PER_SECOND),
                   (unsigned)((tick % RT_TICK_PER_SECOND) * 10U / RT_TICK_PER_SECOND));
        rt_kprintf("heap   : total %u, used %u, max %u\n",
                   (unsigned)total, (unsigned)used, (unsigned)max_used);
        return;
    }

    if ((argc >= 4) && (rt_strcmp(argv[1], "add") == 0))
    {
        int a = atoi(argv[2]);
        int b = atoi(argv[3]);

        rt_kprintf("%d + %d = %d\n", a, b, a + b);
        return;
    }

    rt_kprintf("usage: demo info        - uptime & heap info\n");
    rt_kprintf("       demo add <a> <b> - integer add\n");
}
/* 命令名用别名 demo；说明文字不能带逗号（宏参数分隔符） */
MSH_CMD_EXPORT_ALIAS(cmd_demo, demo, demo command: demo info | demo add a b);

/* EOF */
