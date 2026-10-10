/*
 * msh_cmd.h — 自定义 msh 命令模块（示例模板）
 *
 * 新增一条命令三步：
 *   1. 在 msh_cmd.c 里写 void cmd_xxx(int argc, char **argv)；
 *   2. 文件尾加 MSH_CMD_EXPORT(cmd_xxx, 一句话说明)
 *      （或 MSH_CMD_EXPORT_ALIAS(cmd_xxx, 别名, 说明) 自定命令名）；
 *   3. 重编译烧录，msh 里敲 help 可见、可直接执行。
 *
 * 注意（本树已核实）：
 *   - 说明文字里不要带逗号（宏参数分隔符）；
 *   - 命令在 tshell 线程（prio 20）上下文执行，可阻塞但别长睡；
 *   - 输出走 rt_kprintf（本工程已被 rtt_console.c 强覆盖重定向 RTT ch0，
 *     RTT Viewer Terminal 直接可见）；
 *   - argc/argv 与 main 同义：argv[0] 是命令名本身。
 */
#ifndef __MSH_CMD_H__
#define __MSH_CMD_H__

#include <rtthread.h>

#endif
