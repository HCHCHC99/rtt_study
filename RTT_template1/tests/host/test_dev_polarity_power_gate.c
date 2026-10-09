#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define static

#include "dev_polarity.c"

#undef static

uint8_t fake_bus_seen_valid;
uint8_t fake_bus_under_voltage;
int fake_power_dir_pin;
uint32_t fake_axis_events;
uint32_t fake_sys_events;
uint32_t fake_arb_command_count;
uint8_t fake_arb_last_device;
uint8_t fake_arb_last_cmd;

uint8_t BusVoltage_HasSeenValid(void)
{
    return fake_bus_seen_valid;
}

uint8_t BusVoltage_IsUnderVoltage(void)
{
    return fake_bus_under_voltage;
}

int rt_pin_read(int pin)
{
    (void)pin;
    return fake_power_dir_pin;
}

void Act_Event_Send(uint32_t bits)
{
    fake_axis_events |= bits;
}

void Sys_Event_Send(uint32_t bits)
{
    fake_sys_events |= bits;
}

rt_err_t Arb_SendCommand(uint8_t axis_id, uint8_t device_id, uint8_t priority,
                         uint8_t cmd_type, uint8_t duty_pct, rt_bool_t urgent)
{
    (void)axis_id;
    (void)priority;
    (void)duty_pct;
    (void)urgent;
    fake_arb_command_count++;
    fake_arb_last_device = device_id;
    fake_arb_last_cmd = cmd_type;
    return RT_EOK;
}

rt_base_t rt_hw_interrupt_disable(void)
{
    return 0U;
}

void rt_hw_interrupt_enable(rt_base_t level)
{
    (void)level;
}

void UsTimer_UpdateTimestamp(void)
{
}

uint64_t UsTimer_GetTimestampUs(void)
{
    return 0U;
}

static void reset_polarity(void)
{
    Polarity_Init();
    fake_bus_seen_valid = 0U;
    fake_bus_under_voltage = 0U;
    fake_power_dir_pin = 1U;
    fake_axis_events = 0U;
    fake_sys_events = 0U;
    fake_arb_command_count = 0U;
}

static void test_no_valid_bus_keeps_polarity_unpowered(void)
{
    reset_polarity();
    fake_power_dir_pin = 1U;
    for (uint8_t i = 0U; i < (POLARITY_WIN_SIZE + 1U); i++) {
        Polarity_Scan();
    }

    assert(s_state == POLARITY_UNPOWERED);
    assert(s_dirWin.cnt == 0U);
    assert(fake_arb_command_count == 0U);
    assert((fake_axis_events & EVT_ACT_POLARITY_REV) == 0U);
}

static void test_under_fault_keeps_polarity_unpowered(void)
{
    reset_polarity();
    fake_bus_seen_valid = 1U;
    fake_bus_under_voltage = 1U;
    fake_power_dir_pin = 1U;
    for (uint8_t i = 0U; i < (POLARITY_WIN_SIZE + 1U); i++) {
        Polarity_Scan();
    }

    assert(s_state == POLARITY_UNPOWERED);
    assert(fake_arb_command_count == 0U);
    assert((fake_axis_events & EVT_ACT_POLARITY_REV) == 0U);
}

static void test_recovery_rebuilds_window_before_direction(void)
{
    reset_polarity();
    s_state = POLARITY_UNPOWERED;
    fake_bus_seen_valid = 1U;
    fake_bus_under_voltage = 0U;
    fake_power_dir_pin = 0U;

    for (uint8_t i = 0U; i < (POLARITY_WIN_SIZE - 1U); i++) {
        Polarity_Scan();
    }
    assert(s_state == POLARITY_UNPOWERED);
    assert(fake_arb_command_count == 0U);

    Polarity_Scan();
    assert(s_state == POLARITY_FWD);
    assert(fake_arb_command_count == 1U);
    assert(fake_arb_last_device == DEV_ID_POWER_POS);
    assert(fake_arb_last_cmd == CMD_TYPE_RUN_FWD);
}

int main(void)
{
    test_no_valid_bus_keeps_polarity_unpowered();
    test_under_fault_keeps_polarity_unpowered();
    test_recovery_rebuilds_window_before_direction();
    printf("dev_polarity power gate tests passed\n");
    return 0;
}
