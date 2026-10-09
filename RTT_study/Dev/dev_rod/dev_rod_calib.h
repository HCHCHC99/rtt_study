/**
 * @file    dev_rod_calib.h
 * @brief   过流软限位判定模块：系统状态机过流事件中的推杆域决策（校准请求 / 忽略 / 真故障）
 * @note    判定规则（自 dev_state.c 过流分支抽出，行为保持）：
 *          1. 仲裁未使能或方向无效（stop/无效）：忽略（输出已停电流必回落），不进故障；
 *          2. 伸出（FWD）+ 未校准或上限校准窗口命中：写 state->calib_req = ROD_CALIB_REQ_MAX
 *             （rod_task 消费执行校准），调用方应即时清 FWD 允许（软限位不算故障）；
 *          3. 缩回（REV）+ 未校准或下限校准窗口命中：写 state->calib_req = ROD_CALIB_REQ_MIN，
 *             调用方应即时清 REV 允许；
 *          4. 窗口外（位置记忆在中间）：真故障（异物卡住），调用方走系统故障机制
 *             （error_code / fault_bits / EMERGENCY→FAULT），并记录故障方向供换向清除。
 */
#ifndef __DEV_ROD_CALIB_H__
#define __DEV_ROD_CALIB_H__

#include <stdint.h>
#include "dev_rod_position.h"
#include "dev_rod_state.h"

/**
 * @brief  过流判定结果：调用方（dev_state 过流分支）据此分流
 */
typedef enum {
    ROD_CALIB_IGNORED = 0,  /* 仲裁未使能/方向无效：忽略（输出已停电流必回落），不进故障 */
    ROD_CALIB_CALIB_MAX,    /* 伸出中 + 上限窗口命中/未校准：calib_req=MAX 已写（rod_task 执行校准） */
    ROD_CALIB_CALIB_MIN,    /* 缩回中 + 下限窗口命中/未校准：calib_req=MIN 已写（rod_task 执行校准） */
    ROD_CALIB_FAULT_FWD,    /* 真故障（伸出方向窗口外=异物卡住）：调用方置故障位，方向供方向相关清除 */
    ROD_CALIB_FAULT_REV,    /* 真故障（缩回方向窗口外） */
} RodCalibResult_t;

/**
 * @brief  过流事件的软限位判定（含全部决策打印）
 * @param  pos    推杆位置（校准状态/窗口判定）
 * @param  state  推杆状态上下文（写入 calib_req）
 * @return 判定结果枚举（校准类调用方应即时清对应方向允许；FAULT_* 走系统故障机制）
 */
RodCalibResult_t RodCalib_OnOverCurrent(RodPosition_t *pos, RodStateCtx_t *state);

#endif /* __DEV_ROD_CALIB_H__ */
