#ifndef FAKE_DRV_GPIO_H
#define FAKE_DRV_GPIO_H

#define B ('B')

#define GET_PIN(port, pin) (((port) - 'A') * 16U + (pin))

#endif
