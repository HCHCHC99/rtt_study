#ifndef FAKE_HC32_LL_H
#define FAKE_HC32_LL_H

#include <stdint.h>

typedef enum { PIN_RESET = 0, PIN_SET = 1 } fake_pin_status_t;
typedef enum { FAKE_IRQN_0 = 0 } IRQn_Type;
typedef uint32_t en_int_src_t;

typedef struct {
    uint32_t u32Filter;
    uint32_t u32FilterClock;
    uint32_t u32Edge;
} stc_extint_init_t;

typedef struct {
    en_int_src_t enIntSrc;
    IRQn_Type enIRQn;
    void (*pfnCallback)(void);
} stc_irq_signin_config_t;

typedef struct {
    uint16_t u16PinDir;
    uint16_t u16PinAttr;
    uint16_t u16PullUp;
    uint16_t u16ExtInt;
} stc_gpio_init_t;

#define GPIO_PORT_C                       0U
#define GPIO_PIN_13                       13U
#define GPIO_PIN_14                       14U
#define EXTINT_CH13                       13U
#define EXTINT_CH14                       14U
#define INT013_IRQn                       FAKE_IRQN_0
#define INT014_IRQn                       FAKE_IRQN_0
#define INT_SRC_PORT_EIRQ13               13U
#define INT_SRC_PORT_EIRQ14               14U
#define DDL_IRQ_PRIO_02                   2U

#define PIN_DIR_IN                        0U
#define PIN_ATTR_DIGITAL                  0U
#define PIN_PU_ON                         1U
#define PIN_EXTINT_ON                     1U
#define EXTINT_FILTER_OFF                 0U
#define EXTINT_FCLK_DIV1                  0U
#define EXTINT_TRIG_BOTH                  0U

#define EXTINT_Init(channel, config)      ((void)(channel), (void)(config), 0)
#define EXTINT_ClearExtIntStatus(channel) ((void)(channel))
#define GPIO_StructInit(config)           ((void)(config))
#define GPIO_Init(port, pin, config)      ((void)(port), (void)(pin), (void)(config), 0)
#define GPIO_ReadInputPins(port, pin)     PIN_RESET
#define INTC_IrqSignIn(config)            ((void)(config), 0)
#define NVIC_ClearPendingIRQ(irqn)        ((void)(irqn))
#define NVIC_SetPriority(irqn, prio)      ((void)(irqn), (void)(prio))
#define NVIC_EnableIRQ(irqn)              ((void)(irqn))
#define LL_PERIPH_WE(domain)              ((void)(domain))
#define LL_PERIPH_WP(domain)              ((void)(domain))
#define LL_PERIPH_GPIO                    0U

#endif
