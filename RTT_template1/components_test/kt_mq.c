/*
 * kt_mq.c — 消息队列验证: 定长消息/urgent/满队/动态
 * 命令: kt_mq
 */
#include <rtthread.h>
#include "kt_common.h"

struct kt_msg
{
    rt_uint32_t id;
    rt_uint32_t data;
};

#define MQ_POOL_MSGS 4

static struct kt_result g_r;

static struct rt_messagequeue q_mq;
static rt_uint8_t q_pool[MQ_POOL_MSGS * sizeof(struct kt_msg)];

static rt_err_t q_put(struct rt_messagequeue *mq,
                      rt_uint32_t id, rt_uint32_t data)
{
    struct kt_msg m;

    m.id = id;
    m.data = data;
    return rt_mq_send(mq, &m, sizeof(m));
}

static int q_get_eq(struct rt_messagequeue *mq,
                    rt_uint32_t id, rt_uint32_t data)
{
    struct kt_msg m;

    if (rt_mq_recv(mq, &m, sizeof(m), RT_WAITING_NO) != RT_EOK)
        return 0;
    return (m.id == id) && (m.data == data);
}

void kt_mq_test(void)
{
    rt_err_t err;
    int i;

    kt_result_init(&g_r, "mq");

    err = rt_mq_init(&q_mq, "kt_mq1", q_pool, sizeof(struct kt_msg),
                     sizeof(q_pool), RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mq_init");

    err = q_put(&q_mq, 1, 100);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mq_send");
    KT_CHECK(&g_r, q_get_eq(&q_mq, 1, 100) == 1, "recv match");

    /* urgent 插队 */
    q_put(&q_mq, 2, 0xAAAA);
    {
        struct kt_msg m;

        m.id = 3;
        m.data = 0xBBBB;
        err = rt_mq_urgent(&q_mq, &m, sizeof(m));
        KT_CHECK(&g_r, err == RT_EOK, "rt_mq_urgent");
    }
    KT_CHECK(&g_r, q_get_eq(&q_mq, 3, 0xBBBB) == 1, "urgent first");
    KT_CHECK(&g_r, q_get_eq(&q_mq, 2, 0xAAAA) == 1, "normal next");

    /* 满队: 4 条后拒绝 */
    for (i = 0; i < MQ_POOL_MSGS; i++)
        q_put(&q_mq, (rt_uint32_t)i, (rt_uint32_t)i);
    err = q_put(&q_mq, 0xDEAD, 0);
    KT_CHECK(&g_r, err == -RT_EFULL, "send on full queue");
    KT_CHECK(&g_r, q_mq.entry == MQ_POOL_MSGS, "entry == max_msgs");

    /* FIFO 排空 */
    err = RT_EOK;
    for (i = 0; i < MQ_POOL_MSGS; i++)
    {
        if (q_get_eq(&q_mq, (rt_uint32_t)i, (rt_uint32_t)i) != 1)
            err = -RT_ERROR;
    }
    KT_CHECK(&g_r, err == RT_EOK, "FIFO order");

    /* 空队超时 */
    {
        struct kt_msg m;

        err = rt_mq_recv(&q_mq, &m, sizeof(m), 10);
        KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");
    }

    rt_mq_detach(&q_mq);

    /* 动态接口 */
    {
        rt_mq_t dyn = rt_mq_create("kt_mq2", sizeof(struct kt_msg), 2,
                                   RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_mq_create != NULL");
        if (dyn != RT_NULL)
        {
            KT_CHECK(&g_r, q_put(dyn, 9, 9) == RT_EOK, "dynamic send");
            KT_CHECK(&g_r, q_get_eq(dyn, 9, 9) == 1, "dynamic recv");
            err = rt_mq_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_mq_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mq_test, kt_mq, RT-Thread message queue test);
