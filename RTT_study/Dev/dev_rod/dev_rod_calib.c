/**
 * @file    dev_rod_calib.c
 * @brief   过流软限位判定实现：判定逻辑与打印自 dev_state.c 过流分支原样搬入（行为保持）
 * @note    依赖注入：仅通过 RodPosition_t 与 RodStateCtx_t 指针访问推杆域状态，
 *          不引用 mySystem / dev_model.h；返回枚举（含故障方向）交由系统状态机分流处理。
 */
#include "dev_rod_calib.h"
#include "dev_act.h"          /* Arb_GetData / DIR_*：仲裁输出方向 */
#include "dev_cur_sensor.h" /* CurrentSensor_GetFaultMa / g_cur_cfg：打印参数 */
#include "rtt_manager.h"     /* POWER_PRINT */
#include <rtthread.h>                     /* RT_EOK */

RodCalibResult_t RodCalib_OnOverCurrent(RodPosition_t *pos, RodStateCtx_t *state)
{
    ArbData_t arb;
    uint8_t arb_dir = DIR_NONE;
    uint8_t arb_en = 0U;

    if (Arb_GetData(0U, &arb) == RT_EOK) {
        arb_dir = arb.active_dir;
        arb_en = arb.enable;
    }
    if ((arb_en == 0U) || (arb_dir != DIR_FWD && arb_dir != DIR_REV)) {
        POWER_PRINT("over curr ignored dir=%u en=%u ma=%ld th=%ld win=%ums",
                    (unsigned)arb_dir, (unsigned)arb_en,
                    (long)CurrentSensor_GetFaultMa(),
                    (long)g_cur_cfg.over_th_ma,
                    (unsigned)g_cur_cfg.window_ms);
        return ROD_CALIB_IGNORED;
    } else if (arb_dir == DIR_FWD) {
        /* 伸出中过流：上限窗口使能（或未校准首次）-> 判上限位校准；窗口外 -> 故障 */
        if (!RodPosition_IsCalibrated(pos) ||
            RodPosition_IsInCalibZoneMax(pos)) {
            state->calib_req = ROD_CALIB_REQ_MAX;
            POWER_PRINT("over curr FWD -> calib req MAX ma=%ld th=%ld win=%ums",
                        (long)CurrentSensor_GetFaultMa(),
                        (long)g_cur_cfg.over_th_ma,
                        (unsigned)g_cur_cfg.window_ms);
            return ROD_CALIB_CALIB_MAX;
        } else {
            POWER_PRINT("over curr FAULT FWD ma=%ld th=%ld win=%ums (pos out of calibWin)",
                        (long)CurrentSensor_GetFaultMa(),
                        (long)g_cur_cfg.over_th_ma,
                        (unsigned)g_cur_cfg.window_ms);
            return ROD_CALIB_FAULT_FWD;
        }
    } else {
        /* 缩回中过流：下限窗口使能（或未校准首次）-> 判下限位校准；窗口外 -> 故障 */
        if (!RodPosition_IsCalibrated(pos) ||
            RodPosition_IsInCalibZoneMin(pos)) {
            state->calib_req = ROD_CALIB_REQ_MIN;
            POWER_PRINT("over curr REV -> calib req MIN ma=%ld th=%ld win=%ums",
                        (long)CurrentSensor_GetFaultMa(),
                        (long)g_cur_cfg.over_th_ma,
                        (unsigned)g_cur_cfg.window_ms);
            return ROD_CALIB_CALIB_MIN;
        } else {
            POWER_PRINT("over curr FAULT REV ma=%ld th=%ld win=%ums (pos out of calibWin)",
                        (long)CurrentSensor_GetFaultMa(),
                        (long)g_cur_cfg.over_th_ma,
                        (unsigned)g_cur_cfg.window_ms);
            return ROD_CALIB_FAULT_REV;
        }
    }
}
