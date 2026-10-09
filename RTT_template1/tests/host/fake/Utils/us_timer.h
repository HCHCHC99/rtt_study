#ifndef FAKE_US_TIMER_H
#define FAKE_US_TIMER_H

#include <stdint.h>

void UsTimer_UpdateTimestamp(void);
uint64_t UsTimer_GetTimestampUs(void);
uint32_t UsTimer_GetDelta(void);
uint32_t UsTimer_DeltaToUs(uint32_t cnt);

#endif
