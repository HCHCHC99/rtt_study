/*
 * kt_event.c — 事件集验证: AND/OR/CLEAR/跨线程
 * 命令: kt_event
 */
#include <rtthread.h>
#define KT_MODULE_PRINT(fmt, ...) KT_EVENT_PRINT(fmt, ##__VA_ARGS__)
#include "kt_common.h"

#define KT_E_A  (1 << 0)
#define KT_E_B  (1 << 1)
#define KT_E_C  (1 << 2)

static struct kt_result g_r;

static struct rt_event e_evt;
static struct rt_semaphore e_go;    /* main -> child: 放行 */
static struct rt_semaphore e_done;  /* child -> main: 完成 */
static volatile rt_err_t    e_child_err = -1;
static volatile rt_uint32_t e_recved;

static void e_worker(void *p)
{
    rt_sem_take(&e_go, KT_TIMEOUT_2S);
    e_child_err = rt_event_recv((rt_event_t)p, KT_E_A | KT_E_B,
                                RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                KT_TIMEOUT_2S, (rt_uint32_t *)&e_recved);
    rt_sem_release(&e_done);
}

void kt_event_test(void)
{
    rt_thread_t tid;
    rt_err_t err;
    rt_uint32_t recved = 0;

    kt_result_init(&g_r, "event");

    err = rt_event_init(&e_evt, "kt_e1", RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_event_init");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv on empty timeout");

    err = rt_event_send(&e_evt, KT_E_A);
    KT_CHECK(&g_r, err == RT_EOK, "rt_event_send");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, (err == RT_EOK) && (recved == KT_E_A), "recv A");

    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == RT_EOK, "recv A again(no CLEAR)");

    err = rt_event_recv(&e_evt, KT_E_A | KT_E_B, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "AND(A|B) timeout(only A set)");

    /* OR|CLEAR: 收到即清 */
    err = rt_event_recv(&e_evt, KT_E_A | KT_E_B,
                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, (err == RT_EOK) && (recved == KT_E_A), "OR(A|B) ok");
    err = rt_event_recv(&e_evt, KT_E_A, RT_EVENT_FLAG_AND,
                        RT_WAITING_NO, &recved);
    KT_CHECK(&g_r, err == -RT_ETIMEOUT, "recv after CLEAR timeout");

    /* 跨线程: child 等 A|B 全到 */
    err  = rt_sem_init(&e_go,   "kt_eg", 0, RT_IPC_FLAG_FIFO);
    err |= rt_sem_init(&e_done, "kt_ed", 0, RT_IPC_FLAG_FIFO);
    KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

    tid = rt_thread_create("kt_ec", e_worker, &e_evt,
                           KT_STACK_SMALL, KT_PRIO_HIGH, 10);
    KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
    if (tid != RT_NULL)
    {
        e_child_err = -1;
        e_recved = 0xFFFFFFFFu;
        rt_thread_startup(tid);
        rt_thread_mdelay(20);           /* child 阻塞在 e_go */
        rt_sem_release(&e_go);
        rt_thread_mdelay(20);           /* child 阻塞在 event_recv */
        rt_event_send(&e_evt, KT_E_B);  /* 半个条件 */
        rt_thread_mdelay(20);
        KT_CHECK(&g_r, e_child_err == -1, "child still waiting(only B)");
        rt_event_send(&e_evt, KT_E_A);  /* 条件齐 */
        err = rt_sem_take(&e_done, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
        KT_CHECK(&g_r, e_child_err == RT_EOK, "child recv ok");
        KT_CHECK(&g_r, e_recved == (KT_E_A | KT_E_B), "child recved == A|B");
        KT_CHECK(&g_r, e_evt.set == 0, "event set cleared");
    }
    rt_thread_mdelay(10);

    rt_sem_detach(&e_go);
    rt_sem_detach(&e_done);
    rt_event_detach(&e_evt);

    /* 动态接口 */
    {
        rt_event_t dyn = rt_event_create("kt_e2", RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, dyn != RT_NULL, "rt_event_create != NULL");
        if (dyn != RT_NULL)
        {
            rt_event_send(dyn, KT_E_C);
            err = rt_event_recv(dyn, KT_E_C,
                                RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                RT_WAITING_NO, &recved);
            KT_CHECK(&g_r, err == RT_EOK, "dynamic send/recv");
            err = rt_event_delete(dyn);
            KT_CHECK(&g_r, err == RT_EOK, "rt_event_delete");
        }
    }

    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_event_test, kt_event, RT-Thread event test);
