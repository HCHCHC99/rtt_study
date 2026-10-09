/**
 * @file    us_timer.c
 * @brief   通用 us 计时器实现（经接口表转发到 Adp 底层驱动）
 */
#include "us_timer.h"
#include <stddef.h>

static const struct us_timer_ops *s_ops = NULL;

void UsTimer_Bind(const struct us_timer_ops *ops)
{
    s_ops = ops;
}

void UsTimer_Init(void)
{
    if (s_ops != NULL && s_ops->init != NULL) {
        (void)s_ops->init();
    }
}

void UsTimer_Start(void)
{
    if (s_ops != NULL && s_ops->start != NULL) {
        s_ops->start();
    }
}

void UsTimer_Stop(void)
{
    if (s_ops != NULL && s_ops->stop != NULL) {
        s_ops->stop();
    }
}

void UsTimer_UpdateTimestamp(void)
{
    if (s_ops != NULL && s_ops->update_timestamp != NULL) {
        s_ops->update_timestamp();
    }
}

uint64_t UsTimer_GetTimestampUs(void)
{
    if (s_ops != NULL && s_ops->get_timestamp != NULL) {
        return s_ops->get_timestamp();
    }
    return 0;
}

uint32_t UsTimer_GetCounter(void)
{
    if (s_ops != NULL && s_ops->get_counter != NULL) {
        return s_ops->get_counter();
    }
    return 0;
}

uint32_t UsTimer_DeltaToUs(uint32_t cnt)
{
    if (s_ops != NULL && s_ops->delta_to_us != NULL) {
        return s_ops->delta_to_us(cnt);
    }
    return 0;
}

uint32_t UsTimer_GetDelta(void)
{
    if (s_ops != NULL && s_ops->get_delta != NULL) {
        return s_ops->get_delta();
    }
    return 0;
}

/* EOF */

