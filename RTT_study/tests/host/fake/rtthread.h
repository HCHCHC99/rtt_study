#ifndef FAKE_RTTHREAD_H
#define FAKE_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

#define RT_NULL NULL
typedef int rt_err_t;
typedef unsigned long rt_base_t;
typedef uint8_t rt_bool_t;
struct rt_mutex { int reserved; };
typedef struct rt_mutex rt_mutex_t;
struct rt_mq { int reserved; };
typedef struct rt_mq *rt_mq_t;

#define RT_EOK                 0
#define RT_EINVAL              10
#define RT_FALSE               0U
#define RT_TRUE                1U
#define RT_IPC_FLAG_PRIO       0
#define RT_IPC_FLAG_FIFO       1
#define RT_WAITING_FOREVER     (-1)

rt_err_t rt_mutex_init(rt_mutex_t *mutex, const char *name, uint8_t flag);
rt_err_t rt_mutex_take(rt_mutex_t *mutex, int timeout);
rt_err_t rt_mutex_release(rt_mutex_t *mutex);
rt_mq_t rt_mq_create(const char *name, uint32_t msg_size, uint32_t max_msgs, uint8_t flag);
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, uint32_t size);
rt_err_t rt_mq_urgent(rt_mq_t mq, const void *buffer, uint32_t size);
rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, uint32_t size, int timeout);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
uint32_t rt_tick_get_millisecond(void);
int rt_snprintf(char *buf, uint32_t size, const char *fmt, ...);

uint32_t rt_tick_get_millisecond(void);

#endif
