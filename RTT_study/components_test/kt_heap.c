/*
 * kt_heap.c — 堆内存验证: malloc/calloc/realloc/free/统计
 * 命令: kt_heap
 */
#include <rtthread.h>
#include <string.h>
#define KT_MODULE_PRINT(fmt, ...) KT_HEAP_PRINT(fmt, ##__VA_ARGS__)
#include "kt_common.h"

static struct kt_result g_r;

void kt_heap_test(void)
{
    rt_uint8_t *p1, *p2, *p3;
    rt_size_t total = 0, used0, used1, used2, used3, max_used = 0;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "heap");

    rt_memory_info(&total, &used0, &max_used);
    KT_CHECK(&g_r, total > 0, "rt_memory_info total > 0");
    KT_CHECK(&g_r, max_used >= used0, "max_used >= used");
    KT_HEAP_PRINT("total=%uKB used=%uKB max_used=%uKB",
                  (unsigned)(total >> 10), (unsigned)(used0 >> 10),
                  (unsigned)(max_used >> 10));

    p1 = (rt_uint8_t *)rt_malloc(512);
    KT_CHECK(&g_r, p1 != RT_NULL, "rt_malloc(512)");
    used1 = used0;
    if (p1 != RT_NULL)
    {
        memset(p1, 0xA5, 512);
        rt_memory_info(&total, &used1, &max_used);
        KT_CHECK(&g_r, used1 > used0, "used grows after malloc");
    }

    p2 = (rt_uint8_t *)rt_calloc(4, 32);
    KT_CHECK(&g_r, p2 != RT_NULL, "rt_calloc(4,32)");
    err = RT_EOK;
    if (p2 != RT_NULL)
    {
        for (i = 0; i < 128; i++)
        {
            if (p2[i] != 0)
            {
                err = -RT_ERROR;
                break;
            }
        }
    }
    KT_CHECK(&g_r, err == RT_EOK, "calloc zeroed");

    p3 = RT_NULL;
    err = RT_EOK;
    if (p1 != RT_NULL)
    {
        p1[0]   = 0x11;
        p1[511] = 0x22;
        p3 = (rt_uint8_t *)rt_realloc(p1, 2048);
        KT_CHECK(&g_r, p3 != RT_NULL, "rt_realloc(512->2048)");
        if (p3 != RT_NULL)
        {
            if ((p3[0] != 0x11) || (p3[511] != 0x22))
                err = -RT_ERROR;
        }
        else
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "realloc keeps data");

    rt_memory_info(&total, &used2, &max_used);
    if (p3 != RT_NULL)
        rt_free(p3);
    if (p2 != RT_NULL)
        rt_free(p2);
    rt_memory_info(&total, &used3, &max_used);
    KT_CHECK(&g_r, used3 < used2, "used shrinks after free");

    /* 小对象压力: 32 块 64B, 写入并校验后全部释放 */
    {
        rt_uint8_t *arr[32];

        err = RT_EOK;
        for (i = 0; i < 32; i++)
        {
            arr[i] = (rt_uint8_t *)rt_malloc(64);
            if (arr[i] == RT_NULL)
            {
                err = -RT_ERROR;
                break;
            }
            arr[i][0]  = (rt_uint8_t)i;
            arr[i][63] = (rt_uint8_t)~i;
        }
        KT_CHECK(&g_r, err == RT_EOK, "stress alloc 32x64B");

        err = RT_EOK;
        for (i = 0; i < 32; i++)
        {
            if (arr[i] == RT_NULL)
                continue;
            if ((arr[i][0] != (rt_uint8_t)i) ||
                (arr[i][63] != (rt_uint8_t)~i))
                err = -RT_ERROR;
            rt_free(arr[i]);
        }
        KT_CHECK(&g_r, err == RT_EOK, "stress data intact");
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_heap_test, kt_heap, RT-Thread heap test);
