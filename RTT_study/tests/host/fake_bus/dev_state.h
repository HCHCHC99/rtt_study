#ifndef FAKE_BUS_DEV_STATE_H
#define FAKE_BUS_DEV_STATE_H

#include <stdint.h>

extern uint32_t fake_sys_events;
void Sys_Event_Send(uint32_t bits);

#endif
