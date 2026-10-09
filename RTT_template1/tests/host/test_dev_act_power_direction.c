#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Expose arbitration internals for a focused host test. */
#define static

#include "dev_act.c"

#undef static

static ArbDir_t test_output_dir;

rt_err_t rt_mutex_init(rt_mutex_t *mutex, const char *name, uint8_t flag)
{
    (void)mutex;
    (void)name;
    (void)flag;
    return RT_EOK;
}

rt_err_t rt_mutex_take(rt_mutex_t *mutex, int timeout)
{
    (void)mutex;
    (void)timeout;
    return RT_EOK;
}

rt_err_t rt_mutex_release(rt_mutex_t *mutex)
{
    (void)mutex;
    return RT_EOK;
}

rt_mq_t rt_mq_create(const char *name, uint32_t msg_size, uint32_t max_msgs, uint8_t flag)
{
    (void)name;
    (void)msg_size;
    (void)max_msgs;
    (void)flag;
    static struct rt_mq mq;
    return &mq;
}

rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, uint32_t size)
{
    (void)mq;
    (void)buffer;
    (void)size;
    return RT_EOK;
}

rt_err_t rt_mq_urgent(rt_mq_t mq, const void *buffer, uint32_t size)
{
    (void)mq;
    (void)buffer;
    (void)size;
    return RT_EOK;
}

rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, uint32_t size, int timeout)
{
    (void)mq;
    (void)buffer;
    (void)size;
    (void)timeout;
    return -1;
}

int rt_snprintf(char *buf, uint32_t size, const char *fmt, ...)
{
    (void)fmt;
    if ((buf != NULL) && (size != 0U)) {
        buf[0] = '\0';
    }
    return 0;
}

uint32_t rt_tick_get_millisecond(void)
{
    static uint32_t tick;
    return ++tick;
}

static void test_output_fwd(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    test_output_dir = DIR_FWD;
}

static void test_output_rev(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    test_output_dir = DIR_REV;
}

static void test_output_stop(uint8_t axis_id)
{
    (void)axis_id;
    test_output_dir = DIR_NONE;
}

static const ArbOutputOps_t test_output_ops = {
    test_output_fwd,
    test_output_rev,
    test_output_stop,
};

static void send_power_command(ArbData_t *arb, uint8_t device_id,
                               ArbCmdType_t cmd_type)
{
    ArbCommandMsg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.axis_id = 0U;
    msg.device_id = device_id;
    msg.priority = PRIO_POWER;
    msg.cmd_type = (uint8_t)cmd_type;
    msg.duty_pct = 98U;
    Arb_ProcessMessage(arb, &msg);
}

static void reset_arb(ArbData_t *arb)
{
    memset(arb, 0, sizeof(*arb));
    arb->enable = 1U;
    test_output_dir = DIR_NONE;
    s_arb_output_ops = &test_output_ops;
}

static void test_power_direction_replaces_old_power_allow(void)
{
    ArbData_t arb;

    reset_arb(&arb);
    send_power_command(&arb, DEV_ID_POWER_NEG, CMD_TYPE_RUN_REV);
    assert(arb.allow_rev.count == 1U);
    assert(arb.allow_fwd.count == 0U);

    send_power_command(&arb, DEV_ID_POWER_POS, CMD_TYPE_RUN_FWD);
    Arb_Decision(&arb);

    assert(arb.allow_fwd.count == 1U);
    assert(arb.allow_rev.count == 0U);
    assert(arb.conflict_fault == 0U);
    assert(arb.active_dir == DIR_FWD);
    assert(test_output_dir == DIR_FWD);
}

static void test_reverse_power_direction_replaces_forward(void)
{
    ArbData_t arb;

    reset_arb(&arb);
    send_power_command(&arb, DEV_ID_POWER_POS, CMD_TYPE_RUN_FWD);
    send_power_command(&arb, DEV_ID_POWER_NEG, CMD_TYPE_RUN_REV);
    Arb_Decision(&arb);

    assert(arb.allow_fwd.count == 0U);
    assert(arb.allow_rev.count == 1U);
    assert(arb.conflict_fault == 0U);
    assert(arb.active_dir == DIR_REV);
    assert(test_output_dir == DIR_REV);
}

int main(void)
{
    test_power_direction_replaces_old_power_allow();
    test_reverse_power_direction_replaces_forward();
    printf("dev_act power direction tests passed\n");
    return 0;
}
