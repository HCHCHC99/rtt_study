#ifndef FAKE_POLARITY_BUS_VOLTAGE_H
#define FAKE_POLARITY_BUS_VOLTAGE_H

#include <stdint.h>

extern uint8_t fake_bus_seen_valid;
extern uint8_t fake_bus_under_voltage;
uint8_t BusVoltage_HasSeenValid(void);
uint8_t BusVoltage_IsUnderVoltage(void);

#endif
