/**
 * @file    dev_hall_motor.c
 * @brief   电机霍尔设备实现（完整移植参考 Motor_hall.c，框架适配见设计文档）
 * @note    us 时基走 Utils/us_timer 通用接口（UsTimer_*，实现在 Adp/hc32_drv_timer，
 *          已在 main.c 经 UsTimer_Init/Start 初始化）；ISR 内只做计数/判向，
 *          测速滤波/状态检测在 MotorHall_Task（对应参考 motor_hall_update）。
 */
#include "dev_hall_motor.h"
#include "Utils/us_timer.h"               /* UsTimer_* us 时基（GetDelta/GetTimestampUs/DeltaToUs） */
#include "applications/rtt_manager.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>

/* ============ 实例内部结构（对应参考 motor_hall_instance_t，逐字段保留） ============ */
typedef struct {
    uint8_t  valid;
    uint8_t  id;

    volatile uint32_t last_pulse_interval;
    volatile uint64_t last_pulse_time_us;
    volatile uint32_t pulse_counter;
    volatile uint8_t  speed_data_ready;
    volatile float    current_rpm;
    volatile float    filtered_rpm;

    volatile uint32_t hall_pulse_counts[2];
    volatile uint8_t  active_hall_count;

    volatile MotorHallDir_t current_direction;
    volatile MotorHallDir_t last_valid_direction;
    volatile uint8_t  direction_confidence;
    volatile uint8_t  direction_confirm_count;
    volatile uint8_t  direction_data_ready;
    volatile uint8_t  direction_changed;

    volatile uint32_t hall_a_last_second_count;
    volatile uint32_t hall_b_last_second_count;
    volatile MotorHallStatus_t hall_status;

    volatile uint8_t  stalled;
    volatile uint8_t  is_running;
    volatile uint8_t  measurement_valid;
    volatile uint32_t total_measurements;
    volatile uint32_t error_count;

    float    rpm_window[MOTOR_HALL_RPM_WINDOW_SIZE];
    uint8_t  write_index;
    uint8_t  valid_count;

    uint32_t interval_history[6];
    uint8_t  interval_idx;
    uint8_t  interval_valid_count;

    uint32_t rpm_last_ms;        /* 替代参考 NonBlockingDelay rpm_timer(10ms) */
    uint32_t check_last_ms;      /* 替代参考 hall_check_timer(1000ms) */

    uint32_t last_total;         /* 上次脉冲总数（增量累积用） */
    uint8_t  first_run;
    int32_t  pulse_accum;        /* 带符号累积（对应参考 g_s32HallPulseAccum） */
} MotorHallInst_t;

static MotorHallInst_t s_inst[MOTOR_HALL_MAX_AXIS_NUM];
static uint8_t s_hw_ready = 0U;              /* EXTI 只登录一次 */
volatile uint8_t g_mothall_invert_dir = MOTOR_HALL_DIRECTION_INVERT_DEFAULT;

/* ISR 实例映射：单实体电机 → axis0（多轴扩展时改这里） */
#define MOTOR_HALL_ISR_AXIS   (0U)
static MotorHallInst_t * volatile s_isr_a = RT_NULL;
static MotorHallInst_t * volatile s_isr_b = RT_NULL;

/* Watch 观测量（Task 周期刷新） */
volatile int32_t  g_mothall_dbg_delta   = 0;
volatile uint32_t g_mothall_dbg_rpm_x10 = 0U;
volatile uint8_t  g_mothall_dbg_dir     = 0U;
volatile uint8_t  g_mothall_dbg_status  = 0U;

/* ============ ISR 回调（对应参考 hall_a_irq_callback / hall_b_irq_callback） ============ */

static void Hall_A_IrqCallback(void)
{
    MotorHallInst_t *inst = s_isr_a;
    uint32_t interval_counter;
    uint32_t interval_us;

    if ((inst == RT_NULL) || (inst->valid == 0U)) {
        return;
    }

    EXTINT_ClearExtIntStatus(MOTOR_HALL_A_EIRQ_CH);

    inst->hall_pulse_counts[0]++;

    interval_counter = UsTimer_GetDelta();
    interval_us = UsTimer_DeltaToUs(interval_counter);

    if ((interval_us >= MOTOR_HALL_MIN_PULSE_INTERVAL_US) &&
        (interval_us <= MOTOR_HALL_MAX_PULSE_INTERVAL_US)) {
        inst->last_pulse_interval = interval_us;
        inst->last_pulse_time_us = UsTimer_GetTimestampUs();
        inst->pulse_counter++;
        inst->stalled = 0U;
        inst->speed_data_ready = 1U;
    }
}

static void Hall_B_IrqCallback(void)
{
    MotorHallInst_t *inst = s_isr_b;
    uint32_t interval_counter;
    uint32_t interval_us;
    /* 判向参考：per-instance 下降沿记忆（0xFF=未初始化） */
    static uint8_t hall_b_last[MOTOR_HALL_MAX_AXIS_NUM] = {0xFFU, 0xFFU};

    if ((inst == RT_NULL) || (inst->valid == 0U)) {
        return;
    }

    EXTINT_ClearExtIntStatus(MOTOR_HALL_B_EIRQ_CH);

    inst->hall_pulse_counts[1]++;

    interval_counter = UsTimer_GetDelta();
    interval_us = UsTimer_DeltaToUs(interval_counter);

    if ((interval_us >= MOTOR_HALL_MIN_PULSE_INTERVAL_US) &&
        (interval_us <= MOTOR_HALL_MAX_PULSE_INTERVAL_US)) {
        inst->last_pulse_interval = interval_us;
        inst->last_pulse_time_us = UsTimer_GetTimestampUs();
        inst->pulse_counter++;
        inst->stalled = 0U;
        inst->speed_data_ready = 1U;
    }

    /* ---- 判向：B 下降沿采样 A 电平（与参考逻辑完全一致） ---- */
    {
        uint8_t hall_b_current = (GPIO_ReadInputPins(MOTOR_HALL_B_PORT, MOTOR_HALL_B_PIN) == PIN_SET) ? 1U : 0U;

        inst->direction_data_ready = 1U;

        if (hall_b_last[inst->id] == 0xFFU) {
            hall_b_last[inst->id] = hall_b_current;
            return;
        }

        if ((hall_b_last[inst->id] == 1U) && (hall_b_current == 0U)) {
            if (inst->current_rpm == 0.0f) {          /* 参考同款：rpm=0 跳过判向 */
                hall_b_last[inst->id] = hall_b_current;
                return;
            }

            uint8_t hall_a_state = (GPIO_ReadInputPins(MOTOR_HALL_A_PORT, MOTOR_HALL_A_PIN) == PIN_SET) ? 1U : 0U;
            MotorHallDir_t detected_direction;

            if (g_mothall_invert_dir != 0U) {
                detected_direction = (hall_a_state != 0U) ? MOTOR_HALL_DIR_REVERSE : MOTOR_HALL_DIR_FORWARD;
            } else {
                detected_direction = (hall_a_state != 0U) ? MOTOR_HALL_DIR_FORWARD : MOTOR_HALL_DIR_REVERSE;
            }

            if (inst->current_direction != detected_direction) {
                inst->direction_confirm_count++;
                if (inst->direction_confirm_count >= MOTOR_HALL_DIRECTION_CONFIRM_CNT) {
                    MotorHallDir_t old_direction = inst->current_direction;
                    inst->current_direction = detected_direction;
                    inst->direction_confidence = 100U;
                    inst->last_valid_direction = detected_direction;
                    inst->direction_confirm_count = 0U;

                    if (old_direction != detected_direction) {
                        inst->direction_changed = 1U;
                    }
                }
            } else {
                inst->direction_confirm_count = 0U;
            }
        }

        hall_b_last[inst->id] = hall_b_current;
    }
}

/* ============ EXTI/GPIO/INTC 注册（对应参考 register_hall_irq，1:1） ============ */

static void MotorHall_RegisterIrq(uint8_t is_hall_a)
{
    stc_extint_init_t stcExtiConfig;
    stc_irq_signin_config_t stcIrqRegiConf;
    stc_gpio_init_t stcPortInit;

    uint32_t eirq_ch;
    IRQn_Type irqn;
    en_int_src_t irq_src;
    uint8_t port;
    uint16_t pin;

    if (is_hall_a != 0U) {
        eirq_ch  = MOTOR_HALL_A_EIRQ_CH;
        irqn     = (IRQn_Type)MOTOR_HALL_A_IRQN;
        irq_src  = (en_int_src_t)MOTOR_HALL_A_IRQ_SRC;
        port     = (uint8_t)MOTOR_HALL_A_PORT;
        pin      = MOTOR_HALL_A_PIN;
        s_isr_a  = &s_inst[MOTOR_HALL_ISR_AXIS];
    } else {
        eirq_ch  = MOTOR_HALL_B_EIRQ_CH;
        irqn     = (IRQn_Type)MOTOR_HALL_B_IRQN;
        irq_src  = (en_int_src_t)MOTOR_HALL_B_IRQ_SRC;
        port     = (uint8_t)MOTOR_HALL_B_PORT;
        pin      = MOTOR_HALL_B_PIN;
        s_isr_b  = &s_inst[MOTOR_HALL_ISR_AXIS];
    }

    memset(&stcExtiConfig, 0, sizeof(stcExtiConfig));
    memset(&stcIrqRegiConf, 0, sizeof(stcIrqRegiConf));
    memset(&stcPortInit, 0, sizeof(stcPortInit));

    stcExtiConfig.u32Filter      = EXTINT_FILTER_OFF;
    stcExtiConfig.u32FilterClock = EXTINT_FCLK_DIV1;
    stcExtiConfig.u32Edge        = EXTINT_TRIG_BOTH;
    (void)EXTINT_Init(eirq_ch, &stcExtiConfig);

    GPIO_StructInit(&stcPortInit);
    stcPortInit.u16PinDir  = PIN_DIR_IN;
    stcPortInit.u16PinAttr = PIN_ATTR_DIGITAL;
    stcPortInit.u16PullUp  = PIN_PU_ON;
    stcPortInit.u16ExtInt  = PIN_EXTINT_ON;       /* 本工程 DDL：GPIO_Init 内置 INTE 位（等价参考 GPIO_ExtIntCmd） */
    LL_PERIPH_WE(LL_PERIPH_GPIO);                 /* 规范 §12：先解锁 */
    (void)GPIO_Init(port, pin, &stcPortInit);
    LL_PERIPH_WP(LL_PERIPH_GPIO);                 /* 写完上锁 */

    stcIrqRegiConf.enIntSrc = irq_src;
    stcIrqRegiConf.enIRQn   = irqn;
    stcIrqRegiConf.pfnCallback = (is_hall_a != 0U) ? Hall_A_IrqCallback : Hall_B_IrqCallback;
    (void)INTC_IrqSignIn(&stcIrqRegiConf);

    EXTINT_ClearExtIntStatus(eirq_ch);
    NVIC_ClearPendingIRQ(irqn);
    NVIC_SetPriority(irqn, MOTOR_HALL_IRQ_PRIORITY);
    NVIC_EnableIRQ(irqn);
}

/* ============ 测速链（对应参考 calculate_rpm / interval_to_rpm / update_rpm_filter 等，1:1） ============ */

static uint8_t MotorHall_CalcActiveHalls(const MotorHallInst_t *inst)
{
    uint8_t count = 0U;
    if (MOTOR_HALL_A_EIRQ_CH != 0U) { count++; }
    if (MOTOR_HALL_B_EIRQ_CH != 0U) { count++; }
    (void)inst;
    return count;
}

static void MotorHall_UpdateRpmFilter(MotorHallInst_t *inst, float new_rpm)
{
    uint8_t i;
    float sum = 0.0f;

    inst->rpm_window[inst->write_index] = new_rpm;
    inst->write_index = (uint8_t)((inst->write_index + 1U) % MOTOR_HALL_RPM_WINDOW_SIZE);

    if (inst->valid_count < MOTOR_HALL_RPM_WINDOW_SIZE) {
        inst->valid_count++;
    }

    for (i = 0U; i < inst->valid_count; i++) {
        sum += inst->rpm_window[i];
    }
    inst->filtered_rpm = sum / inst->valid_count;
}

static float MotorHall_AverageInterval(MotorHallInst_t *inst)
{
    uint8_t i;
    uint32_t sum = 0U;

    if ((inst->last_pulse_interval >= MOTOR_HALL_MIN_PULSE_INTERVAL_US) &&
        (inst->last_pulse_interval <= MOTOR_HALL_MAX_PULSE_INTERVAL_US)) {
        inst->interval_history[inst->interval_idx] = inst->last_pulse_interval;
        inst->interval_idx = (uint8_t)((inst->interval_idx + 1U) % 6U);
        if (inst->interval_valid_count < 6U) {
            inst->interval_valid_count++;
        }
        inst->measurement_valid = 1U;
    }

    if (inst->interval_valid_count < 2U) {
        return 0.0f;
    }

    for (i = 0U; i < inst->interval_valid_count; i++) {
        sum += inst->interval_history[i];
    }
    return (float)sum / inst->interval_valid_count;
}

static float MotorHall_IntervalToRpm(const MotorHallInst_t *inst, uint32_t interval_us)
{
    uint16_t pulses_per_rev;

    if ((interval_us == 0U) || (interval_us > MOTOR_HALL_MAX_PULSE_INTERVAL_US)) {
        return 0.0f;
    }

    if (MOTOR_HALL_CUSTOM_PULSES_PER_REV > 0U) {
        pulses_per_rev = MOTOR_HALL_CUSTOM_PULSES_PER_REV;
    } else {
        uint8_t active_halls = MotorHall_CalcActiveHalls(inst);
        if (active_halls == 0U) {
            return 0.0f;
        }
        pulses_per_rev = (uint16_t)(MOTOR_HALL_POLE_PAIRS * 2U * active_halls);
    }

    {
        float rpm = 60000000.0f / ((float)interval_us * pulses_per_rev);
        if (rpm > 1000000.0f) {   /* 仅显示限幅（不阻断增量传递）；参考 100000 上限已拉高 */
            rpm = 1000000.0f;
        }
        return rpm;
    }
}

/* 对应参考 calculate_rpm */
static void MotorHall_CalculateRpm(MotorHallInst_t *inst)
{
    float avg_interval = MotorHall_AverageInterval(inst);

    if (avg_interval == 0.0f) {
        inst->current_rpm = 0.0f;
        inst->filtered_rpm = 0.0f;
        return;
    }

    {
        float raw_rpm = MotorHall_IntervalToRpm(inst, (uint32_t)avg_interval);
        if (raw_rpm > 0.0f) {
            inst->current_rpm = raw_rpm;
            MotorHall_UpdateRpmFilter(inst, raw_rpm);
            inst->total_measurements++;
        } else {
            inst->current_rpm = 0.0f;
            inst->filtered_rpm = 0.0f;
        }
    }
}

/* 对应参考 perform_stall_detection（1:1；仅观测，不接错误链路） */
static void MotorHall_PerformStallDetection(MotorHallInst_t *inst)
{
    uint64_t current_time_us = UsTimer_GetTimestampUs();
    uint64_t time_since_last_pulse = current_time_us - inst->last_pulse_time_us;

    if ((inst->current_rpm > 0.0f) &&
        (time_since_last_pulse > ((uint64_t)MOTOR_HALL_STOP_DETECTION_MS * 1000UL))) {
        if (inst->current_direction != MOTOR_HALL_DIR_STOP) {
            inst->current_direction = MOTOR_HALL_DIR_STOP;
            inst->direction_confidence = 0U;
            inst->direction_confirm_count = 0U;
            inst->direction_changed = 1U;
        }
        inst->is_running = 0U;
    } else if (inst->current_rpm > 0.0f) {
        inst->is_running = 1U;
    }

    if (time_since_last_pulse > ((uint64_t)MOTOR_HALL_STALL_DETECTION_MS * 1000UL)) {
        inst->stalled = 1U;
        inst->current_rpm = 0.0f;
        inst->filtered_rpm = 0.0f;
        inst->is_running = 0U;
    }
}

/* 对应参考 check_hall_status（1:1；tickTimer → rt_tick_get_millisecond） */
static void MotorHall_CheckHallStatus(MotorHallInst_t *inst)
{
    static uint32_t last_check_time[MOTOR_HALL_MAX_AXIS_NUM] = {0U};
    uint32_t current_time = rt_tick_get_millisecond();

    if ((current_time - last_check_time[inst->id]) < MOTOR_HALL_CHECK_INTERVAL_MS) {
        return;
    }
    last_check_time[inst->id] = current_time;

    inst->hall_a_last_second_count = inst->hall_pulse_counts[0];
    inst->hall_b_last_second_count = inst->hall_pulse_counts[1];

    if ((inst->hall_pulse_counts[0] == 0U) && (inst->hall_pulse_counts[1] == 0U)) {
        inst->hall_status = MOTOR_HALL_STATUS_NONE;
    } else if ((inst->hall_pulse_counts[0] > 0U) && (inst->hall_pulse_counts[1] == 0U)) {
        inst->hall_status = MOTOR_HALL_STATUS_A_ONLY;
        inst->active_hall_count = 1U;
    } else if ((inst->hall_pulse_counts[0] == 0U) && (inst->hall_pulse_counts[1] > 0U)) {
        inst->hall_status = MOTOR_HALL_STATUS_B_ONLY;
        inst->active_hall_count = 1U;
    } else {
        uint32_t diff;
        uint32_t total = inst->hall_pulse_counts[0] + inst->hall_pulse_counts[1];

        if (inst->hall_pulse_counts[0] > inst->hall_pulse_counts[1]) {
            diff = inst->hall_pulse_counts[0] - inst->hall_pulse_counts[1];
        } else {
            diff = inst->hall_pulse_counts[1] - inst->hall_pulse_counts[0];
        }

        if ((total > 0U) && ((diff * 100U / total) > MOTOR_HALL_WARNING_THRESHOLD)) {
            inst->hall_status = MOTOR_HALL_STATUS_ERROR;
        } else {
            inst->hall_status = MOTOR_HALL_STATUS_BOTH;
            inst->active_hall_count = 2U;
        }
    }
}

/* ============ 初始化 / 周期任务 ============ */

void MotorHall_Init(void)
{
    uint8_t i;

    for (i = 0U; i < MOTOR_HALL_MAX_AXIS_NUM; i++) {
        MotorHallInst_t *inst = &s_inst[i];
        uint8_t j;

        /* IDLE 重入：仅复位业务态（EXTI 保持登录） */
        inst->valid = (i == MOTOR_HALL_ISR_AXIS) ? 1U : 0U;
        inst->id = i;

        inst->last_pulse_interval = 0U;
        inst->last_pulse_time_us = 0U;
        inst->pulse_counter = 0U;
        inst->speed_data_ready = 0U;
        inst->current_rpm = 0.0f;
        inst->filtered_rpm = 0.0f;

        inst->hall_pulse_counts[0] = 0U;
        inst->hall_pulse_counts[1] = 0U;
        inst->active_hall_count = MotorHall_CalcActiveHalls(inst);

        inst->current_direction = MOTOR_HALL_DIR_NONE;
        inst->last_valid_direction = MOTOR_HALL_DIR_NONE;
        inst->direction_confidence = 0U;
        inst->direction_confirm_count = 0U;
        inst->direction_data_ready = 0U;
        inst->direction_changed = 0U;

        inst->hall_a_last_second_count = 0U;
        inst->hall_b_last_second_count = 0U;
        inst->hall_status = MOTOR_HALL_STATUS_NONE;

        inst->stalled = 0U;
        inst->is_running = 0U;
        inst->measurement_valid = 0U;
        inst->total_measurements = 0U;
        inst->error_count = 0U;

        for (j = 0U; j < MOTOR_HALL_RPM_WINDOW_SIZE; j++) {
            inst->rpm_window[j] = 0.0f;
        }
        inst->write_index = 0U;
        inst->valid_count = 0U;

        for (j = 0U; j < 6U; j++) {
            inst->interval_history[j] = 0U;
        }
        inst->interval_idx = 0U;
        inst->interval_valid_count = 0U;

        inst->rpm_last_ms = rt_tick_get_millisecond();
        inst->check_last_ms = inst->rpm_last_ms;
        inst->last_total = 0U;
        inst->first_run = 1U;
        inst->pulse_accum = 0;
    }

    if (s_hw_ready == 0U) {
        MotorHall_RegisterIrq(1U);   /* A（引脚宏见 dev_hall_motor.h） */
        MotorHall_RegisterIrq(0U);   /* B */
        s_hw_ready = 1U;
    }

    HALL_MOTOR_PRINT("init axis=%u hall A/B exti both-edge prio=%u",
                     (unsigned)MOTOR_HALL_ISR_AXIS, (unsigned)MOTOR_HALL_IRQ_PRIORITY);
}

/* 对应参考 motor_hall_update（1:1，按实例展开） */
void MotorHall_Task(void)
{
    uint8_t i;

    for (i = 0U; i < MOTOR_HALL_MAX_AXIS_NUM; i++) {
        MotorHallInst_t *inst = &s_inst[i];
        uint32_t now;

        if (inst->valid == 0U) {
            continue;
        }
        now = rt_tick_get_millisecond();

        UsTimer_UpdateTimestamp();

        if ((now - inst->rpm_last_ms) >= MOTOR_HALL_TASK_PERIOD_MS) {
            MotorHall_CalculateRpm(inst);
            MotorHall_PerformStallDetection(inst);
            inst->rpm_last_ms = now;
        }

        if ((now - inst->check_last_ms) >= MOTOR_HALL_CHECK_INTERVAL_MS) {
            MotorHall_CheckHallStatus(inst);
            inst->check_last_ms = now;
        }

        /* ---- 脉冲增量累积（与参考完全一致：回绕 + 方向回退） ---- */
        {
            uint32_t total = inst->hall_pulse_counts[0] + inst->hall_pulse_counts[1];

            if (inst->first_run != 0U) {
                inst->last_total = total;
                inst->first_run = 0U;
            }
            if (total != inst->last_total) {
                uint32_t delta;
                MotorHallDir_t dir;

                if (total > inst->last_total) {
                    delta = total - inst->last_total;
                } else {
                    delta = inst->last_total - total;   /* 计数器回绕 */
                }

                dir = inst->current_direction;
                if ((dir == MOTOR_HALL_DIR_STOP) || (dir == MOTOR_HALL_DIR_NONE)) {
                    dir = inst->last_valid_direction;
                }
                if (dir == MOTOR_HALL_DIR_FORWARD) {
                    inst->pulse_accum += (int32_t)delta;
                } else if (dir == MOTOR_HALL_DIR_REVERSE) {
                    inst->pulse_accum -= (int32_t)delta;
                }
                inst->last_total = total;
            }
        }

        /* Watch 观测量刷新 */
        if (i == MOTOR_HALL_ISR_AXIS) {
            uint32_t rpm_x10 = (uint32_t)(inst->filtered_rpm * 10.0f);
            g_mothall_dbg_delta   = inst->pulse_accum;
            g_mothall_dbg_rpm_x10 = rpm_x10;
            g_mothall_dbg_dir     = (uint8_t)inst->current_direction;
            g_mothall_dbg_status  = (uint8_t)inst->hall_status;
        }
    }

    /* 周期观测打印（对应参考 2000ms 打印；RPM ×10 拆整型，无浮点打印） */
    {
        static uint32_t s_last_print_ms = 0U;
        uint32_t now = rt_tick_get_millisecond();
        if ((now - s_last_print_ms) >= MOTOR_HALL_PRINT_INTERVAL_MS) {
            s_last_print_ms = now;
            HALL_MOTOR_PRINT("rpm=%u.%u dir=%u interval=%u running=%u stalled=%u",
                             (unsigned)(g_mothall_dbg_rpm_x10 / 10U),
                             (unsigned)(g_mothall_dbg_rpm_x10 % 10U),
                             (unsigned)g_mothall_dbg_dir,
                             (unsigned)s_inst[MOTOR_HALL_ISR_AXIS].last_pulse_interval,
                             (unsigned)s_inst[MOTOR_HALL_ISR_AXIS].is_running,
                             (unsigned)s_inst[MOTOR_HALL_ISR_AXIS].stalled);
        }
    }
}

/* ============ 查询接口（对应参考 API 一一映射） ============ */

int32_t MotorHall_GetDeltaPulses(uint8_t axis_id)
{
    int32_t delta;

    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0;
    }
    delta = s_inst[axis_id].pulse_accum;
    s_inst[axis_id].pulse_accum = 0;
    return delta;
}

float MotorHall_GetRpm(uint8_t axis_id)
{
    if ((axis_id >= MOTOR_HALL_MAX_AXIS_NUM) || (s_inst[axis_id].is_running == 0U)) {
        return 0.0f;
    }
    return s_inst[axis_id].filtered_rpm;
}

float MotorHall_GetRpmRaw(uint8_t axis_id)
{
    if ((axis_id >= MOTOR_HALL_MAX_AXIS_NUM) || (s_inst[axis_id].is_running == 0U)) {
        return 0.0f;
    }
    return s_inst[axis_id].current_rpm;
}

uint32_t MotorHall_GetPulseIntervalUs(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].last_pulse_interval;
}

MotorHallDir_t MotorHall_GetDirection(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return MOTOR_HALL_DIR_NONE;
    }
    if (s_inst[axis_id].current_rpm == 0.0f) {
        return MOTOR_HALL_DIR_STOP;
    }
    if (s_inst[axis_id].direction_confidence > 50U) {
        return s_inst[axis_id].current_direction;
    }
    return MOTOR_HALL_DIR_NONE;
}

uint8_t MotorHall_GetDirectionConfidence(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].direction_confidence;
}

uint8_t MotorHall_IsDirectionChanged(uint8_t axis_id)
{
    uint8_t changed;

    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    changed = s_inst[axis_id].direction_changed;
    s_inst[axis_id].direction_changed = 0U;
    return changed;
}

uint32_t MotorHall_GetHallACount(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].hall_pulse_counts[0];
}

uint32_t MotorHall_GetHallBCount(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].hall_pulse_counts[1];
}

uint32_t MotorHall_GetTotalPulseCount(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].hall_pulse_counts[0] + s_inst[axis_id].hall_pulse_counts[1];
}

void MotorHall_ResetCounts(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return;
    }
    s_inst[axis_id].hall_pulse_counts[0] = 0U;
    s_inst[axis_id].hall_pulse_counts[1] = 0U;
    s_inst[axis_id].pulse_counter = 0U;
    s_inst[axis_id].hall_a_last_second_count = 0U;
    s_inst[axis_id].hall_b_last_second_count = 0U;
    s_inst[axis_id].hall_status = MOTOR_HALL_STATUS_NONE;
    s_inst[axis_id].last_total = 0U;
    s_inst[axis_id].first_run = 1U;
}

MotorHallStatus_t MotorHall_GetStatus(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return MOTOR_HALL_STATUS_NONE;
    }
    return s_inst[axis_id].hall_status;
}

uint8_t MotorHall_GetActiveHallCount(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    return s_inst[axis_id].active_hall_count;
}

uint16_t MotorHall_GetPulsesPerRev(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    if (MOTOR_HALL_CUSTOM_PULSES_PER_REV > 0U) {
        return MOTOR_HALL_CUSTOM_PULSES_PER_REV;
    }
    return (uint16_t)(MOTOR_HALL_POLE_PAIRS * 2U * s_inst[axis_id].active_hall_count);
}

uint8_t MotorHall_IsRunning(uint8_t axis_id)
{
    uint64_t time_since_last_pulse;

    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 0U;
    }
    time_since_last_pulse = UsTimer_GetTimestampUs() - s_inst[axis_id].last_pulse_time_us;
    return (time_since_last_pulse <= ((uint64_t)MOTOR_HALL_STOP_DETECTION_MS * 1000UL)) ? 1U : 0U;
}

uint8_t MotorHall_IsStalled(uint8_t axis_id)
{
    if (axis_id >= MOTOR_HALL_MAX_AXIS_NUM) {
        return 1U;      /* 同参考：无效句柄返回 1 */
    }
    return s_inst[axis_id].stalled;
}

/* EOF */
