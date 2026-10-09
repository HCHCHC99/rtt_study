/**
 * @file    dev_act.h
 * @brief   Motor arbitration kernel
 * @note    Four command queues select one motor direction. The arbitration
 *          thread is a permitted C-mode module and is created in module init.
 */
#ifndef __DEV_ACT_H__
#define __DEV_ACT_H__

#include <stdint.h>
#include <rtthread.h>
#include "task_set.h"     /* TASK_STACK_ARB / TASK_PRIO_ACT: single source */

/* Module configuration */
#define ARB_MAX_AXIS_NUM            2U     /* keep in sync with MAX_AXIS_NUM (dev_model.h) */
#define MAX_CMD_QUEUE_SIZE          10U
#define ARB_MQ_NAME                 "arb_mq"
#define ARB_MQ_DEPTH                16U
#define ARB_THREAD_STACK_SIZE       TASK_STACK_ARB
#define ARB_THREAD_PRIORITY         TASK_PRIO_ACT    /* registry (C mode) creates and names the thread "act" */

/* Debug print switch and wrapper are added by rtt_manager.h during integration */

/**
 * @brief Arbitration command type
 */
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

/**
 * @brief Source device ID
 */
typedef enum {
    DEV_ID_NONE = 0,
    DEV_ID_POWER_POS = 1,
    DEV_ID_POWER_NEG = 2,
    DEV_ID_LIMIT_FWD = 3,
    DEV_ID_LIMIT_REV = 4,
    DEV_ID_CAN = 5,
    DEV_ID_IO_FWD = 6,
    DEV_ID_IO_REV = 7,
    DEV_ID_EMERGENCY = 8,
    DEV_ID_RTURN_FWD = 9,
    DEV_ID_RTURN_REV = 10,
    DEV_ID_OVERVOLTAGE_FWD = 11,
    DEV_ID_OVERVOLTAGE_REV = 12,
    DEV_ID_UNDERVOLTAGE_FWD = 13,
    DEV_ID_UNDERVOLTAGE_REV = 14,
    DEV_ID_OVERCUR_FWD = 15,
    DEV_ID_OVERCUR_REV = 16,
    DEV_ID_ROD_LIMIT_FWD = 17,
    DEV_ID_ROD_LIMIT_REV = 18,
    DEV_ID_MAX
} ArbDeviceId_t;

/**
 * @brief Command priority; a lower number has higher priority
 */
typedef enum {
    PRIO_EMERGENCY = 0,
    PRIO_LIMIT = 1,
    PRIO_FAULT = 2,
    PRIO_MANUAL = 3,
    PRIO_CAN = 4,
    PRIO_POWER = 5,
    PRIO_NONE = 255
} ArbPriority_t;

/**
 * @brief Arbitrated motor direction
 */
typedef enum {
    DIR_NONE = 0,
    DIR_FWD = 1,
    DIR_REV = 2
} ArbDir_t;

/**
 * @brief Arbitration output state
 */
typedef enum {
    MS_IDLE = 0,
    MS_RUNNING = 1
} ArbState_t;

/**
 * @brief Message sent from input sources to the arbitration thread
 */
typedef struct {
    uint8_t axis_id;
    uint8_t device_id;
    uint8_t priority;
    uint8_t cmd_type;
    uint8_t duty_pct;
    uint32_t timestamp;
} ArbCommandMsg_t;

/**
 * @brief One command record stored in a direction queue
 */
typedef struct {
    uint8_t device_id;
    uint8_t priority;
    uint8_t cmd_type;
    uint8_t duty_pct;
    uint32_t timestamp;
} ArbCommandRecord_t;

/**
 * @brief Priority-ordered command queue for one direction
 */
typedef struct {
    ArbCommandRecord_t records[MAX_CMD_QUEUE_SIZE];
    uint8_t count;
} ArbCommandQueue_t;

/**
 * @brief Hardware output operations bound by integration code
 */
typedef struct {
    void (*fwd)(uint8_t axis_id, uint8_t duty_pct);
    void (*rev)(uint8_t axis_id, uint8_t duty_pct);
    void (*stop)(uint8_t axis_id);
} ArbOutputOps_t;

/**
 * @brief Per-axis arbitration data
 */
typedef struct {
    ArbCommandQueue_t block_fwd;
    ArbCommandQueue_t block_rev;
    ArbCommandQueue_t allow_fwd;
    ArbCommandQueue_t allow_rev;

    ArbDir_t active_dir;
    uint8_t active_device_id;
    uint8_t duty_pct;
    ArbState_t state;
    uint8_t enable;

    uint32_t last_arbitration_time;
    uint32_t arbitration_count;
    uint8_t conflict_fault;
} ArbData_t;

/* Debugger watch variables; refreshed after every arbitration decision */
extern volatile uint8_t g_arb_dbg_block_fwd_count;
extern volatile uint8_t g_arb_dbg_block_rev_count;
extern volatile uint8_t g_arb_dbg_allow_fwd_count;
extern volatile uint8_t g_arb_dbg_allow_rev_count;
extern volatile uint8_t g_arb_dbg_active_device_id;
extern volatile uint8_t g_arb_dbg_active_dir;
extern volatile uint8_t g_arb_dbg_state;
extern volatile uint8_t g_arb_dbg_conflict_fault;
extern volatile uint32_t g_arb_dbg_arbitration_count;
extern ArbData_t * volatile g_arb_dbg_watch;

void Arb_Module_Init(void);
void Arb_ThreadEntry(void *parameter);
rt_err_t Arb_SendCommand(uint8_t axis_id,
                         uint8_t device_id,
                         uint8_t priority,
                         uint8_t cmd_type,
                         uint8_t duty_pct,
                         rt_bool_t urgent);
rt_err_t Arb_SetEnable(uint8_t axis_id, rt_bool_t enable);
rt_err_t Arb_GetData(uint8_t axis_id, ArbData_t *data);
rt_err_t Arb_BindOutputOps(const ArbOutputOps_t *ops);

#endif /* __DEV_ACT_H__ */
