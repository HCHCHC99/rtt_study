#ifndef FAKE_POLARITY_DEV_MODEL_H
#define FAKE_POLARITY_DEV_MODEL_H

#define MAX_AXIS_NUM (2U)

#include <stdint.h>

extern uint32_t fake_axis_events;
void Act_Event_Send(uint32_t bits);

#endif
