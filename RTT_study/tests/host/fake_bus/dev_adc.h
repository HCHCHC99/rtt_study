#ifndef FAKE_BUS_DEV_ADC_H
#define FAKE_BUS_DEV_ADC_H

#include <stdint.h>

extern float fake_adc_mean_v;
int Dev_Adc_GetMean(uint8_t id, float *value);

#endif
