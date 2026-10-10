/*
 * main.c — components_test 验证固件入口
 * 原业务 main 已备份至 main.c.bak_orig。本工程仅用于 RT-Thread 组件功能验证，
 * Dev/Task/Utils 业务代码保留编译但不再被调用。
 *
 * 启动即自动跑批次①全部内核对象组件测试，结论走 RTT（RTT Viewer 查看）：
 *  - 各组件明细: [KT][xxx] PASS/FAIL ...（rtt_manager.h 开关区 KT_XXX_PRINT_EN 可关）
 *  - 结论总表:   [KT][RESULT] <组件> PASS/FAIL n/n + [KT][SUMMARY]（MAIN_D_SYNC 防丢）
 * msh 命令（MSH_RTT_EN=1 时经 RTT Viewer Terminal，=0 时经 uart4）：
 * kt_list / kt_<组件> / kt_all / kt_diag。
 */
#include <rtthread.h>
#include "rtt_manager.h"
/* 相对路径引号包含：Studio 生成的 applications 编现行没有 components_test 的 -I，
   引号包含按引用者目录解析，不依赖构建配置 */
#include "../components_test/kt_common.h"

int main(void)
{
    MAIN_D_SYNC("=== RT-Thread components_test fw (%s %s) ===", __DATE__, __TIME__);
    MAIN_D_SYNC("log+cmd -> RTT Viewer (msh on RTT ch0), type 'kt_list' to re-run");

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

    kt_summary_print();     /* 组件成功与否总表（防丢打印） */

    /* 开机自动转储调度器/全线程：msh 未通期间收集 tshell 证据（stat/mask/栈哨兵）；
       msh 调通后可用 kt_diag 命令复跑，此处可删 */
    kt_diag_dump("boot-end");

    return 0;
}
