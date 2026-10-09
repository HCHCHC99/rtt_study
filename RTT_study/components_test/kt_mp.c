/*
 * kt_mp.c — 内存池验证: 定块分配/耗尽/阻塞唤醒/动态
 * 命令: kt_mp
 */
#include <rtthread.h>
#include <string.h>
#define KT_MODULE_PRINT(fmt, ...) KT_MP_PRINT(fmt, ##__VA_ARGS__)
#include "kt_common.h"

#define MP_BLOCKS       16
#define MP_BLOCK_SIZE   32

static struct kt_result g_r;

static struct rt_mempool p_mp;
static rt_uint32_t p_pool[MP_BLOCKS * MP_BLOCK_SIZE / sizeof(rt_uint32_t)];
static struct rt_semaphore p_done;  /* child -> main: 完成 */
static volatile rt_err_t p_child_ok = 0;
static void *p_child_block = RT_NULL;

static void p_worker(void *p)
{
    /* 池已耗尽, child 阻塞在 alloc, 等 main free 一块唤醒 */
    p_child_block = rt_mp_alloc((rt_mp_t)p, KT_TIMEOUT_2S);
    p_child_ok = (p_child_block != RT_NULL);
    if (p_child_block != RT_NULL)
        rt_mp_free(p_child_block);
    rt_sem_release(&p_done);
}

void kt_mp_test(void)
{
    void *blk[MP_BLOCKS];
    rt_thread_t tid;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mp");

    err = rt_mp_init(&p_mp, "kt_mp1", p_pool, MP_BLOCKS, MP_BLOCK_SIZE);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mp_init");
    KT_CHECK(&g_r, p_mp.block_total_count == MP_BLOCKS, "block_total == 16");
    KT_CHECK(&g_r, p_mp.block_free_count == MP_BLOCKS, "block_free == 16");

    for (i = 0; i < MP_BLOCKS; i++)
        blk[i] = rt_mp_alloc(&p_mp, RT_WAITING_NO);
    err = RT_EOK;
    for (i = 0; i < MP_BLOCKS; i++)
    {
        if (blk[i] == RT_NULL)
        {
            err = -RT_ERROR;
            break;
        }
        memset(blk[i], (int)(i & 0xFF), MP_BLOCK_SIZE);
    }
    KT_CHECK(&g_r, err == RT_EOK, "alloc all 16 blocks");
    KT_CHECK(&g_r, p_mp.block_free_count == 0, "pool exhausted");
    KT_CHECK(&g_r, rt_mp_alloc(&p_mp, RT_WAITING_NO) == RT_NULL,
             "alloc on empty(NOBLOCK) NULL");

    rt_mp_free(blk[0]);
    KT_CHECK(&g_r, p_mp.block_free_count == 1, "free one -> free == 1");
    blk[0] = rt_mp_alloc(&p_mp, RT_WAITING_NO);
    KT_CHECK(&g_r, blk[0] != RT_NULL, "realloc after free");

    /* 跨线程: child 阻塞 alloc, main free 唤醒 */
    err = rt_sem_init(&p_done, "kt_pd", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init done sem");

    p_child_ok = 0;
    p_child_block = RT_NULL;
    tid = rt_thread_create("kt_pc", p_worker, &p_mp,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        rt_thread_mdelay(50);           /* child 已阻塞在 rt_mp_alloc */
        rt_mp_free(blk[1]);             /* 唤醒 child; 该块归 child,
                                           blk[1] 之后不得由 main 再释放 */
        blk[1] = RT_NULL;
        err = rt_sem_take(&p_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, p_child_ok == 1, "child alloc ok");
        KT_CHECK(&g_r, p_mp.block_free_count == 1,
                 "free == 1 after child free");
    }
    rt_thread_mdelay(10);

    /* 归还其余全部块, 验证块数守恒 */
    for (i = 0; i < MP_BLOCKS; i++)
    {
        if (blk[i] != RT_NULL)
            rt_mp_free(blk[i]);
    }
    KT_CHECK(&g_r, p_mp.block_free_count == MP_BLOCKS, "all blocks back");

    rt_sem_detach(&p_done);
    rt_mp_detach(&p_mp);

    /* 动态接口 */
    {
        rt_mp_t dyn = rt_mp_create("kt_mp2", 4, 16);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mp_create != NULL");
        if (dyn != RT_NULL)
        {
            void *b = rt_mp_alloc(dyn, RT_WAITING_NO);

            KT_CHECK(&g_r, b != RT_NULL, "dynamic alloc");
            if (b != RT_NULL)
                rt_mp_free(b);
            err = rt_mp_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mp_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mp_test, kt_mp, RT-Thread memory pool test);
