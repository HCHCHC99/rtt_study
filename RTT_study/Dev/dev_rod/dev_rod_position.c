/**
 * @file    dev_rod_position.c
 * @brief   推杆位置模块实现：脉冲累加、限位校准、校准区/允许标志管理
 */
#include "dev_rod_position.h"
#include "dev_param.h"      /* ROD_CALIB_WIN_*_DFT 存储默认值（单点） */

void RodPosition_Init(RodPosition_t *pos)
{
    pos->position_mm       = 0.0f;
    pos->calib_state       = POSITION_NOT_CALIBRATED;
    pos->calib_pending     = true;
    pos->calib_allowed     = true;   /* 未校准：允许首次校准 */
    pos->in_calib_zone_min = false;
    pos->in_calib_zone_max = false;
    pos->min_limit_triggered = false;
    pos->max_limit_triggered = false;
    pos->stroke_mm         = 0.0f;
    pos->reduction_ratio   = 1.0f;
    pos->pulse_to_mm       = 0.0f;
    pos->calib_win_a       = ROD_CALIB_WIN_A_DFT;
    pos->calib_win_b       = ROD_CALIB_WIN_B_DFT;
    pos->calib_win_c       = ROD_CALIB_WIN_C_DFT;
    pos->calib_win_d       = ROD_CALIB_WIN_D_DFT;
}

void RodPosition_SetParams(RodPosition_t *pos, float stroke_mm, float reduction_ratio,
                           float hall_pulses_per_rev, float screw_lead_mm)
{
    pos->stroke_mm          = stroke_mm;
    pos->reduction_ratio    = reduction_ratio;
    pos->pulse_to_mm        = screw_lead_mm / (reduction_ratio * hall_pulses_per_rev);
}

void RodPosition_Update(RodPosition_t *pos, int32_t delta_pulses)
{
    /* 1. 脉冲增量 -> 机械位移增量（mm）；位置无界累加，允许越过 [0, stroke]
          （用户定：窗口 a~d 只作校准使能判定，不钳位实际行程计算） */
    float delta_mm = (float)delta_pulses * pos->pulse_to_mm;
    pos->position_mm += delta_mm;

    /* 2. 更新校准使能窗口标志（窗口参数 calib_win_a/b/c/d，flash A 块加载；
          a>b 视同 a==b、c<d 视同 c==d，宽容处理不报错） */
    if (pos->calib_win_a < pos->calib_win_b) {
        pos->in_calib_zone_min = (pos->position_mm >= pos->calib_win_a) &&
                                 (pos->position_mm <= pos->calib_win_b);
    } else {
        pos->in_calib_zone_min = (pos->position_mm <= pos->calib_win_a);
    }
    if (pos->calib_win_c > pos->calib_win_d) {
        pos->in_calib_zone_max = (pos->position_mm >= pos->calib_win_d) &&
                                 (pos->position_mm <= pos->calib_win_c);
    } else {
        pos->in_calib_zone_max = (pos->position_mm >= pos->calib_win_c);
    }

    /* 3. 动态更新校准允许标志 */
    if (pos->calib_state == POSITION_NOT_CALIBRATED) {
        pos->calib_allowed = true;   /* 首次校准：始终允许 */
    } else {
        pos->calib_allowed = pos->in_calib_zone_min || pos->in_calib_zone_max;
    }
}

bool RodPosition_OnMinLimit(RodPosition_t *pos, bool triggered)
{
    pos->min_limit_triggered = triggered;
    /* 下限位触发且允许校准 -> 重置位置为 0 */
    if (triggered && pos->calib_allowed) {
        pos->position_mm = 0.0f;
        pos->calib_state = POSITION_CALIBRATED;
        pos->calib_pending = false;
        return true;
    }
    return false;
}

bool RodPosition_OnMaxLimit(RodPosition_t *pos, bool triggered)
{
    pos->max_limit_triggered = triggered;
    /* 上限位触发且允许校准 -> 重置位置为总行程 */
    if (triggered && pos->calib_allowed) {
        pos->position_mm = pos->stroke_mm;
        pos->calib_state = POSITION_CALIBRATED;
        pos->calib_pending = false;
        return true;
    }
    return false;
}

float RodPosition_GetCurrent(const RodPosition_t *pos)      { return pos->position_mm; }
bool  RodPosition_IsCalibrated(const RodPosition_t *pos)    { return (pos->calib_state == POSITION_CALIBRATED); }
bool  RodPosition_IsCalibAllowed(const RodPosition_t *pos)  { return pos->calib_allowed; }
bool  RodPosition_IsInCalibZoneMin(const RodPosition_t *pos){ return pos->in_calib_zone_min; }
bool  RodPosition_IsInCalibZoneMax(const RodPosition_t *pos){ return pos->in_calib_zone_max; }

/* EOF */
