/**
 * @file    dev_act.c
 * @brief   Motor arbitration kernel implementation
 */
#include "dev_act.h"
#include "dev_model.h"
#include "rtt_manager.h"
#include <rtthread.h>
#include <string.h>

typedef struct {
    uint8_t id;
    ArbData_t arb;
    struct rt_mutex arb_mutex;
} ArbAxis_t;

static ArbAxis_t s_arb_axis[ARB_MAX_AXIS_NUM];
static rt_mq_t s_arb_mq = RT_NULL;
static const ArbOutputOps_t *s_arb_output_ops = RT_NULL;

static void Arb_DefaultOutputFwd(uint8_t axis_id, uint8_t duty_pct);
static void Arb_DefaultOutputRev(uint8_t axis_id, uint8_t duty_pct);
static void Arb_DefaultOutputStop(uint8_t axis_id);
static uint8_t Arb_GetAxisId(const ArbData_t *arb);
static void Arb_ProcessMessage(ArbData_t *arb, const ArbCommandMsg_t *msg);
static void Arb_RemoveOppositePowerAllow(ArbData_t *arb,
                                         const ArbCommandMsg_t *msg);
static void Arb_CmdListRemove(ArbCommandQueue_t *queue, uint8_t device_id);
static void Arb_CmdListSetAllow(ArbCommandQueue_t *queue,
                                ArbCommandQueue_t *block_queue,
                                uint8_t device_id,
                                uint8_t priority,
                                uint8_t cmd_type,
                                uint8_t duty_pct,
                                uint32_t timestamp);
static void Arb_CmdListSetBlock(ArbCommandQueue_t *queue,
                                uint8_t device_id,
                                uint8_t priority,
                                uint8_t cmd_type,
                                uint32_t timestamp);
static void Arb_CmdListClearAll(ArbCommandQueue_t *queue);
static void Arb_Decision(ArbData_t *arb);
static void Arb_ApplyStop(ArbData_t *arb);
static void Arb_RefreshDebug(ArbData_t *arb);

static const ArbOutputOps_t s_arb_default_output_ops = {
    Arb_DefaultOutputFwd,
    Arb_DefaultOutputRev,
    Arb_DefaultOutputStop
};

volatile uint8_t g_arb_dbg_block_fwd_count = 0U;
volatile uint8_t g_arb_dbg_block_rev_count = 0U;
volatile uint8_t g_arb_dbg_allow_fwd_count = 0U;
volatile uint8_t g_arb_dbg_allow_rev_count = 0U;
volatile uint8_t g_arb_dbg_active_device_id = (uint8_t)DEV_ID_NONE;
volatile uint8_t g_arb_dbg_active_dir = (uint8_t)DIR_NONE;
volatile uint8_t g_arb_dbg_state = (uint8_t)MS_IDLE;
volatile uint8_t g_arb_dbg_conflict_fault = 0U;
volatile uint32_t g_arb_dbg_arbitration_count = 0U;
ArbData_t * volatile g_arb_dbg_watch = RT_NULL;

void Arb_Module_Init(void)
{
    uint8_t i;
    rt_err_t ret;

    if (ARB_MAX_AXIS_NUM != (uint8_t)MAX_AXIS_NUM) {
        ARB_PRINT("[ARB] axis num mismatch arb=%u model=%u",
                  (unsigned)ARB_MAX_AXIS_NUM, (unsigned)MAX_AXIS_NUM);
        return;
    }

    if (s_arb_mq != RT_NULL) {
        /* IDLE re-entry: reset business state, keep mq/mutex/thread alive */
        for (i = 0U; i < ARB_MAX_AXIS_NUM; i++) {
            ret = rt_mutex_take(&s_arb_axis[i].arb_mutex, RT_WAITING_FOREVER);
            if (ret == RT_EOK) {
                memset(&s_arb_axis[i].arb, 0, sizeof(s_arb_axis[i].arb));
                s_arb_axis[i].arb.enable = 1U;
                (void)rt_mutex_release(&s_arb_axis[i].arb_mutex);
            }
        }
        Arb_RefreshDebug(&s_arb_axis[0U].arb);
        ARB_PRINT("[ARB] reset axis=%u", (unsigned)ARB_MAX_AXIS_NUM);
        return;
    }

    s_arb_output_ops = &s_arb_default_output_ops;

    for (i = 0U; i < ARB_MAX_AXIS_NUM; i++) {
        char name[12];

        memset(&s_arb_axis[i].arb, 0, sizeof(s_arb_axis[i].arb));
        s_arb_axis[i].id = i;
        s_arb_axis[i].arb.enable = 1U;

        (void)rt_snprintf(name, sizeof(name), "arb_mtx%u", (unsigned)i);
        ret = rt_mutex_init(&s_arb_axis[i].arb_mutex, name, RT_IPC_FLAG_PRIO);
        if (ret != RT_EOK) {
            ARB_PRINT("[ARB] mutex%u init failed ret=%d", (unsigned)i, (int)ret);
            return;
        }
    }

    s_arb_mq = rt_mq_create(ARB_MQ_NAME,
                            sizeof(ArbCommandMsg_t),
                            ARB_MQ_DEPTH,
                            RT_IPC_FLAG_FIFO);
    if (s_arb_mq == RT_NULL) {
        ARB_PRINT("[ARB] mq create failed");
        return;
    }

    ARB_PRINT("[ARB] init axis=%u mq=%u prio=%u",
              (unsigned)ARB_MAX_AXIS_NUM,
              (unsigned)ARB_MQ_DEPTH,
              (unsigned)ARB_THREAD_PRIORITY);
}

rt_err_t Arb_SendCommand(uint8_t axis_id,
                         uint8_t device_id,
                         uint8_t priority,
                         uint8_t cmd_type,
                         uint8_t duty_pct,
                         rt_bool_t urgent)
{
    ArbCommandMsg_t msg;

    if ((s_arb_mq == RT_NULL) ||
        (axis_id >= ARB_MAX_AXIS_NUM) ||
        (device_id <= (uint8_t)DEV_ID_NONE) ||
        (device_id >= (uint8_t)DEV_ID_MAX) ||
        (priority > (uint8_t)PRIO_POWER) ||
        (duty_pct > 100U)) {
        return RT_EINVAL;
    }

    switch ((ArbCmdType_t)cmd_type) {
        case CMD_TYPE_RUN_FWD:
        case CMD_TYPE_RUN_REV:
        case CMD_TYPE_STOP:
        case CMD_TYPE_BLOCK_FWD:
        case CMD_TYPE_BLOCK_REV:
        case CMD_TYPE_UNBLOCK_FWD:
        case CMD_TYPE_UNBLOCK_REV:
        case CMD_TYPE_CLEAR_ALLOW_FWD:
        case CMD_TYPE_CLEAR_ALLOW_REV:
        case CMD_TYPE_EMERGENCY_STOP:
            break;

        default:
            return RT_EINVAL;
    }

    msg.axis_id = axis_id;
    msg.device_id = device_id;
    msg.priority = priority;
    msg.cmd_type = cmd_type;
    msg.duty_pct = duty_pct;
    msg.timestamp = rt_tick_get_millisecond();

    if (urgent != RT_FALSE) {
        return rt_mq_urgent(s_arb_mq, &msg, sizeof(msg));
    }

    return rt_mq_send(s_arb_mq, &msg, sizeof(msg));
}

rt_err_t Arb_SetEnable(uint8_t axis_id, rt_bool_t enable)
{
    ArbAxis_t *axis;
    rt_err_t ret;

    if (axis_id >= ARB_MAX_AXIS_NUM) {
        return RT_EINVAL;
    }

    axis = &s_arb_axis[axis_id];
    ret = rt_mutex_take(&axis->arb_mutex, RT_WAITING_FOREVER);
    if (ret != RT_EOK) {
        return ret;
    }

    axis->arb.enable = (enable != RT_FALSE) ? 1U : 0U;
    (void)rt_mutex_release(&axis->arb_mutex);
    return RT_EOK;
}

rt_err_t Arb_GetData(uint8_t axis_id, ArbData_t *data)
{
    ArbAxis_t *axis;
    rt_err_t ret;

    if ((axis_id >= ARB_MAX_AXIS_NUM) || (data == RT_NULL)) {
        return RT_EINVAL;
    }

    axis = &s_arb_axis[axis_id];
    ret = rt_mutex_take(&axis->arb_mutex, RT_WAITING_FOREVER);
    if (ret != RT_EOK) {
        return ret;
    }

    *data = axis->arb;
    (void)rt_mutex_release(&axis->arb_mutex);
    return RT_EOK;
}

rt_err_t Arb_BindOutputOps(const ArbOutputOps_t *ops)
{
    if ((ops == RT_NULL) ||
        (ops->fwd == RT_NULL) ||
        (ops->rev == RT_NULL) ||
        (ops->stop == RT_NULL)) {
        return RT_EINVAL;
    }

    s_arb_output_ops = ops;
    return RT_EOK;
}

void Arb_ThreadEntry(void *parameter)
{
    ArbCommandMsg_t msg;
    ArbAxis_t *axis;
    rt_err_t ret;

    (void)parameter;

    while (1) {
        ret = rt_mq_recv(s_arb_mq,
                         &msg,
                         sizeof(msg),
                         RT_WAITING_FOREVER);
        if (ret != RT_EOK) {
            continue;
        }

        if (msg.axis_id >= ARB_MAX_AXIS_NUM) {
            continue;
        }

        axis = &s_arb_axis[msg.axis_id];
        ret = rt_mutex_take(&axis->arb_mutex, RT_WAITING_FOREVER);
        if (ret != RT_EOK) {
            continue;
        }

        Arb_ProcessMessage(&axis->arb, &msg);
        Arb_Decision(&axis->arb);
        Arb_RefreshDebug(&axis->arb);

        (void)rt_mutex_release(&axis->arb_mutex);
    }
}

static void Arb_ProcessMessage(ArbData_t *arb, const ArbCommandMsg_t *msg)
{
    /* 正/负电源检测是同一个物理方向源：换向时替换旧方向 allow，
       避免 3.3V 预供电期间残留 REV 后，24V 正向沿触发同优先级冲突停机。 */
    Arb_RemoveOppositePowerAllow(arb, msg);

    switch ((ArbCmdType_t)msg->cmd_type) {
        case CMD_TYPE_RUN_FWD:
            Arb_CmdListRemove(&arb->block_fwd, msg->device_id);
            Arb_CmdListSetAllow(&arb->allow_fwd,
                                &arb->block_fwd,
                                msg->device_id,
                                msg->priority,
                                msg->cmd_type,
                                msg->duty_pct,
                                msg->timestamp);
            break;

        case CMD_TYPE_RUN_REV:
            Arb_CmdListRemove(&arb->block_rev, msg->device_id);
            Arb_CmdListSetAllow(&arb->allow_rev,
                                &arb->block_rev,
                                msg->device_id,
                                msg->priority,
                                msg->cmd_type,
                                msg->duty_pct,
                                msg->timestamp);
            break;

        case CMD_TYPE_STOP:
            Arb_CmdListRemove(&arb->allow_fwd, msg->device_id);
            Arb_CmdListRemove(&arb->allow_rev, msg->device_id);
            break;

        case CMD_TYPE_BLOCK_FWD:
            Arb_CmdListSetBlock(&arb->block_fwd,
                                msg->device_id,
                                msg->priority,
                                msg->cmd_type,
                                msg->timestamp);
            break;

        case CMD_TYPE_BLOCK_REV:
            Arb_CmdListSetBlock(&arb->block_rev,
                                msg->device_id,
                                msg->priority,
                                msg->cmd_type,
                                msg->timestamp);
            break;

        case CMD_TYPE_UNBLOCK_FWD:
            Arb_CmdListRemove(&arb->block_fwd, msg->device_id);
            break;

        case CMD_TYPE_UNBLOCK_REV:
            Arb_CmdListRemove(&arb->block_rev, msg->device_id);
            break;

        case CMD_TYPE_CLEAR_ALLOW_FWD:
            Arb_CmdListClearAll(&arb->allow_fwd);
            break;

        case CMD_TYPE_CLEAR_ALLOW_REV:
            Arb_CmdListClearAll(&arb->allow_rev);
            break;

        case CMD_TYPE_EMERGENCY_STOP:
            Arb_CmdListClearAll(&arb->allow_fwd);
            Arb_CmdListClearAll(&arb->allow_rev);
            break;

        default:
            break;
    }
}

static void Arb_RemoveOppositePowerAllow(ArbData_t *arb,
                                         const ArbCommandMsg_t *msg)
{
    if ((msg->cmd_type != (uint8_t)CMD_TYPE_RUN_FWD) &&
        (msg->cmd_type != (uint8_t)CMD_TYPE_RUN_REV)) {
        return;
    }

    if (msg->device_id == (uint8_t)DEV_ID_POWER_POS) {
        Arb_CmdListRemove(&arb->allow_rev, (uint8_t)DEV_ID_POWER_NEG);
    } else if (msg->device_id == (uint8_t)DEV_ID_POWER_NEG) {
        Arb_CmdListRemove(&arb->allow_fwd, (uint8_t)DEV_ID_POWER_POS);
    }
}

static void Arb_CmdListRemove(ArbCommandQueue_t *queue, uint8_t device_id)
{
    uint8_t i;
    uint8_t j;

    for (i = 0U; i < queue->count; i++) {
        if (queue->records[i].device_id == device_id) {
            for (j = i; j < (uint8_t)(queue->count - 1U); j++) {
                queue->records[j] = queue->records[j + 1U];
            }

            queue->count--;
            if (queue->count < MAX_CMD_QUEUE_SIZE) {
                memset(&queue->records[queue->count],
                       0,
                       sizeof(queue->records[queue->count]));
            }
            return;
        }
    }
}

static void Arb_CmdListSetAllow(ArbCommandQueue_t *queue,
                                ArbCommandQueue_t *block_queue,
                                uint8_t device_id,
                                uint8_t priority,
                                uint8_t cmd_type,
                                uint8_t duty_pct,
                                uint32_t timestamp)
{
    uint8_t index;
    uint8_t i;

    if ((block_queue != RT_NULL) && (block_queue->count > 0U)) {
        return;
    }

    Arb_CmdListRemove(queue, device_id);
    if (queue->count >= MAX_CMD_QUEUE_SIZE) {
        return;
    }

    index = 0U;
    while (index < queue->count) {
        if (priority < queue->records[index].priority) {
            break;
        }
        index++;
    }

    for (i = queue->count; i > index; i--) {
        queue->records[i] = queue->records[i - 1U];
    }

    queue->records[index].device_id = device_id;
    queue->records[index].priority = priority;
    queue->records[index].cmd_type = cmd_type;
    queue->records[index].duty_pct = duty_pct;
    queue->records[index].timestamp = timestamp;
    queue->count++;
}

static void Arb_CmdListSetBlock(ArbCommandQueue_t *queue,
                                uint8_t device_id,
                                uint8_t priority,
                                uint8_t cmd_type,
                                uint32_t timestamp)
{
    uint8_t i;

    for (i = 0U; i < queue->count; i++) {
        if (queue->records[i].device_id == device_id) {
            return;
        }
    }

    if (queue->count >= MAX_CMD_QUEUE_SIZE) {
        return;
    }

    queue->records[queue->count].device_id = device_id;
    queue->records[queue->count].priority = priority;
    queue->records[queue->count].cmd_type = cmd_type;
    queue->records[queue->count].duty_pct = 0U;
    queue->records[queue->count].timestamp = timestamp;
    queue->count++;
}

static void Arb_CmdListClearAll(ArbCommandQueue_t *queue)
{
    memset(queue->records, 0, sizeof(queue->records));
    queue->count = 0U;
}

static void Arb_Decision(ArbData_t *arb)
{
    ArbCommandRecord_t *fwd_cmd = RT_NULL;
    ArbCommandRecord_t *rev_cmd = RT_NULL;
    ArbCommandRecord_t *final_cmd = RT_NULL;
    uint8_t axis_id;

    arb->conflict_fault = 0U;

    if (arb->enable == 0U) {
        Arb_ApplyStop(arb);
        return;
    }

    if ((arb->block_fwd.count == 0U) && (arb->allow_fwd.count > 0U)) {
        fwd_cmd = &arb->allow_fwd.records[0U];
    }

    if ((arb->block_rev.count == 0U) && (arb->allow_rev.count > 0U)) {
        rev_cmd = &arb->allow_rev.records[0U];
    }

    if ((fwd_cmd != RT_NULL) && (rev_cmd != RT_NULL)) {
        if (fwd_cmd->device_id == rev_cmd->device_id) {
            arb->conflict_fault = 1U;
        } else if (fwd_cmd->priority < rev_cmd->priority) {
            final_cmd = fwd_cmd;
        } else if (rev_cmd->priority < fwd_cmd->priority) {
            final_cmd = rev_cmd;
        } else {
            arb->conflict_fault = 1U;
        }
    } else if (fwd_cmd != RT_NULL) {
        final_cmd = fwd_cmd;
    } else {
        final_cmd = rev_cmd;
    }

    if (final_cmd != RT_NULL) {
        arb->active_device_id = final_cmd->device_id;
        arb->duty_pct = final_cmd->duty_pct;
        arb->state = MS_RUNNING;
        axis_id = Arb_GetAxisId(arb);

        if ((ArbCmdType_t)final_cmd->cmd_type == CMD_TYPE_RUN_FWD) {
            arb->active_dir = DIR_FWD;
            s_arb_output_ops->fwd(axis_id, arb->duty_pct);
        } else {
            arb->active_dir = DIR_REV;
            s_arb_output_ops->rev(axis_id, arb->duty_pct);
        }
    } else {
        Arb_ApplyStop(arb);
    }

    arb->last_arbitration_time = rt_tick_get_millisecond();
    arb->arbitration_count++;
}

static void Arb_ApplyStop(ArbData_t *arb)
{
    arb->active_device_id = (uint8_t)DEV_ID_NONE;
    arb->active_dir = DIR_NONE;
    arb->duty_pct = 0U;
    arb->state = MS_IDLE;
    s_arb_output_ops->stop(Arb_GetAxisId(arb));
}

static uint8_t Arb_GetAxisId(const ArbData_t *arb)
{
    uint8_t i;

    for (i = 0U; i < ARB_MAX_AXIS_NUM; i++) {
        if (arb == &s_arb_axis[i].arb) {
            return i;
        }
    }

    return ARB_MAX_AXIS_NUM;
}

static void Arb_RefreshDebug(ArbData_t *arb)
{
    g_arb_dbg_watch = arb;
    g_arb_dbg_block_fwd_count = arb->block_fwd.count;
    g_arb_dbg_block_rev_count = arb->block_rev.count;
    g_arb_dbg_allow_fwd_count = arb->allow_fwd.count;
    g_arb_dbg_allow_rev_count = arb->allow_rev.count;
    g_arb_dbg_active_device_id = arb->active_device_id;
    g_arb_dbg_active_dir = (uint8_t)arb->active_dir;
    g_arb_dbg_state = (uint8_t)arb->state;
    g_arb_dbg_conflict_fault = arb->conflict_fault;
    g_arb_dbg_arbitration_count = arb->arbitration_count;
}

static void Arb_DefaultOutputFwd(uint8_t axis_id, uint8_t duty_pct)
{
    ARB_PRINT("[ARB] axis%u fwd duty=%u", (unsigned)axis_id, (unsigned)duty_pct);
}

static void Arb_DefaultOutputRev(uint8_t axis_id, uint8_t duty_pct)
{
    ARB_PRINT("[ARB] axis%u rev duty=%u", (unsigned)axis_id, (unsigned)duty_pct);
}

static void Arb_DefaultOutputStop(uint8_t axis_id)
{
    ARB_PRINT("[ARB] axis%u stop duty=0", (unsigned)axis_id);
}

/* EOF */
