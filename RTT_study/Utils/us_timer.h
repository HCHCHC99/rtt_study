/**
 * @file    us_timer.h
 * @brief   通用微秒计时器接口（芯片无关）
 * @note    实现由 Adp 提供（如 hc_us_timer_ops），换芯片只换实现；
 *          UsTimer_Bind 绑定后即可使用。
 */
#ifndef __US_TIMER_H__
#define __US_TIMER_H__

#include <stdint.h>

/* 通用 us 计时器接口表（rt_pin_ops 风格） */
struct us_timer_ops {
    int      (*init)(void);              /* 初始化 */
    void     (*start)(void);             /* 启动计数 */
    void     (*stop)(void);              /* 停止计数 */
    void     (*update_timestamp)(void);  /* 更新累计 us 时间戳（采样点调用） */
    uint64_t (*get_timestamp)(void);     /* 全局累计 us */
    uint32_t (*get_counter)(void);       /* 原始计数值 */
    uint32_t (*delta_to_us)(uint32_t cnt); /* 计数差 -> us */
    uint32_t (*get_delta)(void);         /* 距上次调用的计数差（底层维护 last 计数，处理回绕） */
};

/* 绑定底层实现（如 hc_us_timer_ops） */
void UsTimer_Bind(const struct us_timer_ops *ops);

void     UsTimer_Init(void);
void     UsTimer_Start(void);
void     UsTimer_Stop(void);
void     UsTimer_UpdateTimestamp(void);
uint64_t UsTimer_GetTimestampUs(void);
uint32_t UsTimer_GetCounter(void);
uint32_t UsTimer_DeltaToUs(uint32_t cnt);
uint32_t UsTimer_GetDelta(void);

#endif /* __US_TIMER_H__ */
