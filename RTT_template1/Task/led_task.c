/**
 * @file    led_task.c
 * @brief   LED 任务实现（LED_PIN 见 led_task.h）：欠压指示灯
 * @note    实时点亮/熄灭由电压检测 ISR 每 1ms 直控（低于欠压阈值亮/高于灭），
 *          本线程 1s 轮询同状态做兜底同步（语义一致，不会打架）。
 *          独立线程（C 模式），优先级取低值，栈 1024。
 */
#include "led_task.h"
#include "hc32_drv_gpio.h"
#include "Dev/dev_power/dev_bus_voltage.h"   /* BusVoltage_IsBelowUnderTh：轮询兜底 */
#include <rtthread.h>

/* LED 亮灭控制（ISR/线程上下文均可调，单脚写原子）：on=1 亮(置低) / on=0 灭(置高) */
void Led_SetOn(uint8_t on)
{
    if (on != 0U) {
        Hc32_Gpio_Reset(LED_PORT, LED_PIN);     /* 亮 = 置低 */
    } else {
        Hc32_Gpio_Set(LED_PORT, LED_PIN);       /* 灭 = 置高 */
    }
}

static void led_thread_entry(void *param)
{
    (void)param;
    while (1) {
        /* 轮询兜底：跟随实时阈值跟踪状态（实时点亮/熄灭在电压 ISR 每 1ms，此处 1s 同步一次防漏） */
        Led_SetOn(BusVoltage_IsBelowUnderTh());
        Task_Set_Beat();
        rt_thread_mdelay(LED_POLL_MS);
    }
}

void Led_Task_Start(void)
{
    /* 初始化 LED GPIO：推挽输出，初始高电平（上电常灭） */
    Output_GPIO_Init(LED_PORT, LED_PIN, GPIO_INIT_HIGH);

    (void)Task_Set_Create("led", led_thread_entry, RT_NULL,
                          LED_THREAD_STACK, LED_THREAD_PRIO, 2000U);
}

/* EOF */
