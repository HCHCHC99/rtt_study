#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Expose ISR callbacks and the instance array so this host test can inject
   hall edges without bringing up the HC32 EXTI hardware. */
#define static

#include "dev_hall_motor.c"

#undef static

static uint64_t fake_time_us;
static uint32_t fake_pulse_interval_us;
static uint32_t fake_tick_ms;

void UsTimer_UpdateTimestamp(void)
{
}

uint64_t UsTimer_GetTimestampUs(void)
{
    return fake_time_us;
}

uint32_t UsTimer_GetDelta(void)
{
    return 1U;
}

uint32_t UsTimer_DeltaToUs(uint32_t cnt)
{
    return (cnt == 1U) ? fake_pulse_interval_us : 0U;
}

uint32_t rt_tick_get_millisecond(void)
{
    return fake_tick_ms;
}

static void reset_test_state(void)
{
    memset(s_inst, 0, sizeof(s_inst));
    s_inst[0].valid = 1U;
    s_inst[0].id = 0U;
    s_isr_a = &s_inst[0];
    fake_time_us = 1000000ULL;
    fake_pulse_interval_us = 50000U;
    fake_tick_ms = 1000U;
}

static void advance_and_task(uint32_t elapsed_ms)
{
    fake_time_us += (uint64_t)elapsed_ms * 1000ULL;
    fake_tick_ms += elapsed_ms;
    MotorHall_Task();
}

static void run_one_measured_pulse(void)
{
    Hall_A_IrqCallback();
    advance_and_task(MOTOR_HALL_TASK_PERIOD_MS);
}

static void run_two_measured_pulses(void)
{
    run_one_measured_pulse();
    run_one_measured_pulse();
}

static void test_running_rpm_is_updated_on_new_pulse(void)
{
    reset_test_state();
    run_two_measured_pulses();
    advance_and_task(MOTOR_HALL_TASK_PERIOD_MS);

    assert(s_inst[0].pulse_counter == 2U);
    assert(s_inst[0].current_rpm > 0.0f);
    assert(g_mothall_dbg_rpm_x10 == (uint32_t)(s_inst[0].filtered_rpm * 10.0f));
}

static void test_stopped_motor_zeroes_rpm_after_timeout(void)
{
    reset_test_state();
    run_two_measured_pulses();
    assert(g_mothall_dbg_rpm_x10 > 0U);

    advance_and_task(MOTOR_HALL_STOP_DETECTION_MS);

    assert(s_inst[0].is_running == 0U);
    assert(s_inst[0].current_rpm == 0.0f);
    assert(s_inst[0].filtered_rpm == 0.0f);
    assert(s_inst[0].last_pulse_interval == 0U);
    assert(g_mothall_dbg_rpm_x10 == 0U);
}

int main(void)
{
    test_running_rpm_is_updated_on_new_pulse();
    test_stopped_motor_zeroes_rpm_after_timeout();
    printf("dev_hall_motor stop-rpm tests passed\n");
    return 0;
}
