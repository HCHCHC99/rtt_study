/*
 * kt_result.c — components_test 结果登记表
 * 各组件测试结束时经 kt_result_report（kt_common.h）登记；
 * main 启动自跑 / kt_all 结束后调 kt_summary_print 打印总表（RTT 防丢）。
 */
#include "kt_common.h"

#define KT_RESULT_SLOTS     12      /* 10 组件 + 余量 */

static struct kt_result g_kt_results[KT_RESULT_SLOTS];
static int g_kt_result_cnt = 0;

/* 同名覆盖登记：同一测试重复执行时刷新旧记录，不无限增长 */
void kt_result_publish(const struct kt_result *r)
{
    int i;

    for (i = 0; i < g_kt_result_cnt; i++)
    {
        if (g_kt_results[i].name == r->name)
        {
            g_kt_results[i] = *r;
            return;
        }
    }
    if (g_kt_result_cnt < KT_RESULT_SLOTS)
        g_kt_results[g_kt_result_cnt++] = *r;
}

const struct kt_result *kt_result_get(int index)
{
    if ((index < 0) || (index >= g_kt_result_cnt))
        return RT_NULL;
    return &g_kt_results[index];
}

int kt_result_count(void)
{
    return g_kt_result_cnt;
}

/* 总表：结论行属启动段/关键输出，按 md_record/RTT打印不丢失优化.md 用 SYNC 版防丢 */
void kt_summary_print(void)
{
    int i;
    int fail_all = 0;

    MAIN_D_SYNC("---- components_test summary ----");
    for (i = 0; i < g_kt_result_cnt; i++)
    {
        const struct kt_result *r = &g_kt_results[i];

        fail_all += (int)r->fail;
        MAIN_D_SYNC("[KT][RESULT] %-8s %s %u/%u", r->name,
                    (r->fail == 0U) ? "PASS" : "FAIL",
                    (unsigned)(r->total - r->fail), (unsigned)r->total);
    }
    MAIN_D_SYNC("[KT][SUMMARY] %s (%d components, %d check(s) failed)",
                (fail_all == 0) ? "ALL PASS" : "HAS FAIL",
                g_kt_result_cnt, fail_all);
}
