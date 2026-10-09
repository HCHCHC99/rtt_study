#ifndef FAKE_POLARITY_DEV_ACT_H
#define FAKE_POLARITY_DEV_ACT_H

#include <stdint.h>
#include "rtthread.h"

typedef enum {
    CMD_TYPE_NONE = 0,
    CMD_TYPE_RUN_FWD,
    CMD_TYPE_RUN_REV,
    CMD_TYPE_STOP,
    CMD_TYPE_BLOCK_FWD,
    CMD_TYPE_BLOCK_REV,
    CMD_TYPE_UNBLOCK_FWD,
    CMD_TYPE_UNBLOCK_REV,
    CMD_TYPE_CLEAR_ALLOW_FWD,
    CMD_TYPE_CLEAR_ALLOW_REV,
    CMD_TYPE_EMERGENCY_STOP
} ArbCmdType_t;

typedef enum {
    DEV_ID_NONE = 0,
    DEV_ID_POWER_POS = 1,
    DEV_ID_POWER_NEG = 2,
    DEV_ID_MAX
} ArbDeviceId_t;

typedef enum {
    PRIO_EMERGENCY = 0,
    PRIO_POWER = 5,
} ArbPriority_t;

rt_err_t Arb_SendCommand(uint8_t axis_id, uint8_t device_id, uint8_t priority,
                         uint8_t cmd_type, uint8_t duty_pct, rt_bool_t urgent);

#endif
