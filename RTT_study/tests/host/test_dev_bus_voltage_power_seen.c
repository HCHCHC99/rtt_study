#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define static

#include "dev_bus_voltage.c"

#undef static

float fake_adc_mean_v;
uint32_t fake_sys_events;

int Dev_Adc_GetMean(uint8_t id, float *value)
{
    (void)id;
    *value = fake_adc_mean_v;
    return 0;
}

void Sys_Event_Send(uint32_t bits)
{
    fake_sys_events |= bits;
}

static void reset_bus(void)
{
    BusVoltage_Init();
    s_u8SeenValid = 0U;
    fake_sys_events = 0U;
}

static void test_low_bus_before_valid_power_is_not_latched_as_seen(void)
{
    reset_bus();
    fake_adc_mean_v = 1.0f;
    BusVoltage_Isr1ms();

    assert(s_u8Status == 1U);
    assert(s_u8SeenValid == 0U);
    assert((fake_sys_events & EVT_SYS_VOLT_UNDER) != 0U);
}

static void test_valid_bus_remembers_seen_state_before_next_under(void)
{
    reset_bus();
    fake_adc_mean_v = 24.0f;
    BusVoltage_Isr1ms();
    assert(s_u8SeenValid != 0U);
    assert(s_u8Status == 0U);

    fake_adc_mean_v = 1.0f;
    BusVoltage_Isr1ms();
    assert(s_u8Status == 1U);
    assert(s_u8SeenValid != 0U);
    assert((fake_sys_events & EVT_SYS_VOLT_UNDER) != 0U);
}

int main(void)
{
    test_low_bus_before_valid_power_is_not_latched_as_seen();
    test_valid_bus_remembers_seen_state_before_next_under();
    printf("dev_bus_voltage power-seen tests passed\n");
    return 0;
}
