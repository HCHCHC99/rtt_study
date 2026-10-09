/**
 * @file    dev_registry.c
 * @brief   设备注册管理：SysModule_t 注册表 + RT-Thread 设备管理线程
 * @note    参考源工程 sys_sched 的注册语义（注册即 init）；
 *          电源链（adc/cur/vm）已改为 ISR 驱动（TMR0_1 采样 + TMR0_2 1ms 检测），
 *          task 传 RT_NULL 仅保留初始化与注册打印；Dev_Start 线程留给未来线程型设备。
 */
#include "dev_registry.h"
#include "dev_config.h"
#include "rtt_manager.h"
#include "dev_adc.h"
#include "dev_cur_sensor.h"
#include "dev_bus_voltage.h"
#include "dev_polarity.h"
#include "dev_monitor.h"
#include "dev_act.h"
#include "dev_gpio_motor.h"
#include "dev_hall_motor.h"
#include <rtthread.h>


static SysModule_t s_modules[MAX_REG_MODULES];
static uint16_t    s_module_num = 0U;

int Dev_Registry_Add(const SysModule_t *module)
{
    if (module == RT_NULL || s_module_num >= MAX_REG_MODULES) {
        return -1;
    }
    for (uint16_t i = 0U; i < s_module_num; i++) {
        if (rt_strcmp(s_modules[i].name, module->name) == 0) {
            return -1;   /* 重复注册 */
        }
    }
    s_modules[s_module_num++] = *module;
    DEV_REG_PRINT("register %s", module->name);

    if (module->init != RT_NULL) {
        module->init();
    }

    if (module->thread_entry != RT_NULL) {
        if (Task_Set_Create(module->name, module->thread_entry, RT_NULL,
                            module->thread_stack, module->prio, 0U) == RT_NULL) {
            MAIN_D("[DEV_REG] thread create failed: %s", module->name);
        }
    }

    return 0;
}

void Dev_Registry_InitAll(void)
{
    /* 统一初始化/复位所有已注册模块：sys_enter_idle 每次进 IDLE 调用（上电一次 + 恢复时清业务状态） */
    for (uint16_t i = 0U; i < s_module_num; i++) {
        if (s_modules[i].init != RT_NULL) {
            s_modules[i].init();
        }
    }
}

void Dev_Registry_UpdateAll(void)
{
    uint32_t tick = rt_tick_get();
    for (uint16_t i = 0U; i < s_module_num; i++) {
        SysModule_t *m = &s_modules[i];
        if (!m->enabled || m->task == RT_NULL) {
            continue;
        }
        if (m->period_ms == 0U || (tick - m->last_tick) >= m->period_ms) {
            m->task();
            m->last_tick = tick;
        }
    }
}

void Dev_Thread_Entry(void *param)
{
    (void)param;
    MAIN_D_SYNC("[DEV_REG] dev thread entry");
    while (1) {
        Dev_Registry_UpdateAll();
        Task_Set_Beat();
        rt_thread_mdelay(1);
    }
}

void Dev_RegisterAll(void)
{
#if DEV_ENABLE_ADC
    /* ISR 驱动：TMR0_1 硬件触发 200us 采样；Dev_Adc_Start() 启动 */
    static const SysModule_t s_adc_module =
        SYS_MODULE_REGISTER("adc", Dev_Adc_Init, RT_NULL, DEV_PRIO_HIGH, 0);
    Dev_Registry_Add(&s_adc_module);
#endif
#if DEV_ENABLE_CUR_SENSOR
    /* ISR 驱动：1ms 心跳 CurrentSensor_Isr1ms（经 Dev_Power_Isr1ms） */
    static const SysModule_t s_cur_module =
        SYS_MODULE_REGISTER("cur", CurrentSensor_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_cur_module);
#endif
#if DEV_ENABLE_BUS_VOLTAGE
    /* ISR 驱动：1ms 心跳 BusVoltage_Isr1ms（经 Dev_Power_Isr1ms） */
    static const SysModule_t s_vm_module =
        SYS_MODULE_REGISTER("vm", BusVoltage_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_vm_module);
#endif
#if DEV_ENABLE_POLARITY
    /* 执行：Task/di_task 每 10ms 调 Polarity_Scan，事件发轴事件组 */
    static const SysModule_t s_pol_module =
        SYS_MODULE_REGISTER("pol", Polarity_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_pol_module);

#endif
#if DEV_ENABLE_MONITOR
    /* B 模式：registry 线程按 period 调 Monitor_Task 刷新 g_monitor（Watch 用） */
    static const SysModule_t s_monitor_module =
        SYS_MODULE_REGISTER("monitor", Monitor_Init, Monitor_Task, DEV_PRIO_MONITOR, 100);
    Dev_Registry_Add(&s_monitor_module);
#endif
#if DEV_ENABLE_ACT_ARB
    /* C 模式：仲裁线程（rt_mq + 每轴互斥量），线程由 registry 创建命名 "act" */
    static const SysModule_t s_act_module =
        SYS_MODULE_REGISTER_THREAD(act, Arb_Module_Init, Arb_ThreadEntry,
                                   ARB_THREAD_PRIORITY, ARB_THREAD_STACK_SIZE);
    Dev_Registry_Add(&s_act_module);
#endif
#if DEV_ENABLE_MOTOR_GPIO
    /* GPIO 输出设备（新板）：init 双脚输出低 + 绑定仲裁输出 ops（fwd/rev/stop，无线程） */
    static const SysModule_t s_motg_module =
        SYS_MODULE_REGISTER(motg, Dev_MotorGpio_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_motg_module);
#endif
#if DEV_ENABLE_HALL_MOTOR
    /* 电机霍尔：init 首次注册 EXTI（IDLE 重入仅复位业务态）；Task 由 rod_task 10ms 调 */
    static const SysModule_t s_hall_motor_module =
        SYS_MODULE_REGISTER(hall_mot, MotorHall_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_hall_motor_module);
#endif

} 

int Dev_Start(void)
{
    if (Task_Set_Create("dev", Dev_Thread_Entry, RT_NULL,
                        DEV_THREAD_STACK_SIZE, DEV_THREAD_PRIORITY, 500U) != RT_NULL) {
        return 0;
    }
    return -1;
}

/* EOF */










