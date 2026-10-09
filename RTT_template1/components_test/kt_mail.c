/*
 * kt_mail.c — 邮箱验证: 值语义/满箱/urgent 插队/跨线程
 * 命令: kt_mail
 */
#include <rtthread.h>
#include "kt_common.h"

#define MB_POOL_SIZE 8

static struct kt_result g_r;

static struct rt_mailbox b_mb;
static rt_ubase_t b_pool[MB_POOL_SIZE];
static struct rt_semaphore b_go;    /* main -> child: 放行 */
static struct rt_semaphore b_done;  /* child -> main: 完成 */
static volatile rt_err_t    b_child_err = -1;
static volatile rt_ubase_t  b_child_val;

static void b_worker(void *p)
{
    rt_ubase_t v = 0;

    rt_sem_take(&b_go, KT_TIMEOUT_2S);
    b_child_err = rt_mb_recv((rt_mailbox_t)p, &v, KT_TIMEOUT_2S);
    b_child_val = v;
    rt_sem_release(&b_done);
}

void kt_mail_test(void)
{
    rt_thread_t tid;
    rt_ubase_t v = 0;
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mail");

    err = rt_mb_init(&b_mb, "kt_mb1", b_pool, MB_POOL_SIZE, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mb_init");

    err = rt_mb_send(&b_mb, 0x1111);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mb_send");
    err = rt_mb_recv(&b_mb, &v, KT_TIMEOUT_2S);
    KT_CHECK(&g_r, (err == RT_EOK) && (v == 0x1111), "recv value == 0x1111");

    /* 满箱: 8 条后拒绝 */
    for (i = 0; i < MB_POOL_SIZE; i++)
        rt_mb_send(&b_mb, (rt_ubase_t)i);
    err = rt_mb_send(&b_mb, 0xDEAD);
    KT_CHECK(&g_r, err == -RT_EFULL, "send on full mailbox");
    KT_CHECK(&g_r, b_mb.entry == MB_POOL_SIZE, "entry == pool size");

    /* FIFO 顺序排空 */
    err = RT_EOK;
    for (i = 0; i < MB_POOL_SIZE; i++)
    {
        rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
        if (v != (rt_ubase_t)i)
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "FIFO order 0..7");

    /* urgent 插队 */
    rt_mb_send(&b_mb, 0xAAAA);
    rt_mb_urgent(&b_mb, 0xBBBB);
    rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
    KT_CHECK(&g_r, v == 0xBBBB, "urgent first");
    rt_mb_recv(&b_mb, &v, RT_WAITING_NO);
    KT_CHECK(&g_r, v == 0xAAAA, "normal next");

    /* 跨线程: child 阻塞 recv, main send 唤醒 */
    err  = rt_sem_init(&b_go,   "kt_bg", 0, RT_IPC_FLAG_FIFO);
    err |= rt_sem_init(&b_done, "kt_bd", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

    tid = rt_thread_create("kt_bc", b_worker, &b_mb,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        b_child_err = -1;
        b_child_val = 0;
        rt_thread_startup(tid);
        rt_thread_mdelay(20);           /* child 阻塞在 b_go */
        rt_sem_release(&b_go);
        rt_thread_mdelay(20);           /* child 阻塞在 mb_recv */
        rt_mb_send(&b_mb, 0x33);
        err = rt_sem_take(&b_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, b_child_err == RT_EOK, "child recv ok");
        KT_CHECK(&g_r, b_child_val == 0x33, "child value == 0x33");
    }
    rt_thread_mdelay(10);

    err = rt_mb_recv(&b_mb, &v, 10);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");

    rt_sem_detach(&b_go);
    rt_sem_detach(&b_done);
    rt_mb_detach(&b_mb);

    /* 动态接口 */
    {
        rt_mailbox_t dyn = rt_mb_create("kt_mb2", 4, RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mb_create != NULL");
        if (dyn != RT_NULL)
        {
            rt_mb_send(dyn, 0x55);
            err = rt_mb_recv(dyn, &v, RT_WAITING_NO);
            KT_CHECK(&g_r, (err == RT_EOK) && (v == 0x55),
                     "dynamic send/recv");
            err = rt_mb_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mb_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mail_test, kt_mail, RT-Thread mailbox test);
