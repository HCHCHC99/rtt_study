/**
 * @file    dev_param.c
 * @brief   慢块 A：应用配置参数（表驱动）掉电保存（param_manager 实例）
 * @note    上电 main 调 Dev_Param_Init() 一次（内部统一初始化慢块 A + 快块 B，见
 *          dev_param_rod.c）；保存走 Dev_Param_Save()（电机运行中拒绝）。
 *          A 块参数采用表驱动（s_param_table）：defaults/apply/save/show 全部由表
 *          统一遍历，加参数只需 dev_param.h 宏 + ParamRecord_t 字段 + 表一行（+
 *          消费结构字段/钩子）+ 表后编译期宽度断言一行；表行带 min/max 限值：
 *          param_set 越界拒收、上电加载越界打 [PARAM_WARNNING] 告警。运行时 msh
 *          param_set <name> <value> 按名热改，param_save 持久化，param_list 列出
 *          全部参数当前值/默认值/限值。
 */
#include "dev_param.h"
#include "Utils/param_manager.h"
#include "applications/rtt_manager.h"
#include "rtthread.h"
#include "Dev/dev_config.h"
#include "Dev/dev_mgr/dev_model.h"               /* mySystem：rod 组钩子应用目标 */
#include "Dev/dev_power/dev_bus_voltage.h"
#include "Dev/dev_power/dev_cur_sensor.h"
#include "Dev/dev_hall_motor/dev_hall_motor.h"   /* g_mothall_invert_dir（hall_dir_seq 应用目标） */
#include "Dev/dev_gpio_motor/dev_gpio_motor.h"   /* Dev_MotorGpio_SetDirInvert（motor_dir_seq 应用目标） */
#include "Dev/dev_act/dev_act.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if DEV_ENABLE_PARAM

/* 编译期断言：记录尺寸不超引擎缓冲上限、4 字节对齐 */
typedef char param_size_check[(sizeof(ParamRecord_t) <= PARAM_MAX_RECORD_SIZE) ? 1 : -1];
typedef char param_align_check[((sizeof(ParamRecord_t) % 4U) == 0U) ? 1 : -1];

/* Flash 存储记录 + 引擎实例 */
ParamRecord_t g_param_record;
static Param_Runtime_t s_param_runtime;

static const Param_Config_t s_param_config = {
    .pParamBuf      = &g_param_record,
    .paramSize      = sizeof(ParamRecord_t),
    .magicHead      = PARAM_MAGIC_HEAD,
    .magicTail      = PARAM_MAGIC_TAIL,
    .checksumOffset = offsetof(ParamRecord_t, checksum),
    .seqOffset      = offsetof(ParamRecord_t, sequence_id),
    .eraseCntOffset = offsetof(ParamRecord_t, erase_count),
    .secStart       = PARAM_SEC_START,
    .secEnd         = PARAM_SEC_END,
};

/* 上电一次性保护：IDLE 复位误调时不再扫/写 Flash */
static uint8_t s_param_inited = 0U;

/*=============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  打印规范：不打印浮点——float 电压值拆为 [符号]整数.1位小数（0.1V 精度）
 */
static char s_volt_a[12];   /* Param_VoltFmt 输出缓冲（SaveTest 用） */

static void Param_VoltFmt(float v, char *pBuf, uint32_t bufSize)
{
    uint8_t neg = (v < 0.0f) ? 1U : 0U;
    uint32_t x10;

    if (neg) {
        v = -v;
    }
    x10 = (uint32_t)(v * 10.0f + 0.5f);
    rt_snprintf(pBuf, bufSize, "%s%lu.%01lu", (neg ? "-" : ""),
                (unsigned long)(x10 / 10U), (unsigned long)(x10 % 10U));
}

/**
 * @brief  float mm -> "[符号]整数.1位" 字符串（4 缓冲轮转，同条打印多值用）
 */
const char *Dev_Param_MmFmtA(float v)
{
    static char s_mm_buf[4][12];
    static uint8_t s_mm_idx = 0U;
    char *pBuf = s_mm_buf[s_mm_idx];

    s_mm_idx = (uint8_t)((s_mm_idx + 1U) % 4U);
    Param_VoltFmt(v, pBuf, sizeof(s_mm_buf[0]));
    return pBuf;
}

/*=============================================================================
 * 参数表驱动（A 块全部参数的单点描述）
 *  - 加参数：dev_param.h 默认宏 + ParamRecord_t 字段 + 消费结构字段（或钩子）+ 表一行
 *  - ram_base 为 RT_NULL 或 &g_param_record（自镜像）= 无独立消费 RAM：defaults/show
 *    生效于记录本身，应用走 apply_hook（如 rod 组经 Dev_Param_RodApply 写
 *    RodPosition_t 并重算 pulse_to_mm 派生量）
 *  - 同一钩子可被多项挂载 → Apply 阶段会重复调用，钩子实现必须幂等
 *  - 消费 RAM 与记录间为 size≤4 的字段级镜像（单指令原子），线程单写者
 *============================================================================*/
typedef struct {
    const char *name;      /* msh 参数名（param_set/param_list 用） */
    uint16_t    rec_off;   /* A 块记录内偏移 */
    void       *ram_base;  /* 消费 RAM 基址（RT_NULL/&g_param_record=自镜像，见上） */
    uint16_t    ram_off;   /* 消费 RAM 内偏移 */
    ParamType_t type;
    float       def_f;     /* 默认值（F32） */
    uint32_t    def_u;     /* 默认值（整型） */
    float       min;       /* 允许下限（含，dev_param.h *_MIN；0/0=不限） */
    float       max;       /* 允许上限（含，dev_param.h *_MAX；0/0=不限） */
    void      (*apply_hook)(void);   /* record→RAM 镜像后的可选钩子（须幂等） */
} ParamDesc_t;

/* 钩子：电机输出相序（消费变量 s_dir_invert 为 dev_gpio_motor static，经 setter 应用） */
static void Param_HookMotorSeq(void)
{
    Dev_MotorGpio_SetDirInvert((uint8_t)g_param_record.motor_dir_seq);
}

/* 钩子：rod 组（窗口/margin/机械参数）——统一经 Dev_Param_RodApply 写 RodPosition_t
   （内部重算 pulse_to_mm 派生量）。守卫：上电 A 块首次 Apply 时 rod 尚未 init，
   RodApply 内部 s_rod_inited 检查直接返回；实际应用由 App_Model_Init 末尾的
   Dev_Param_RodApply 补做 */
static void Param_HookRodApply(void)
{
    Dev_Param_RodApply(&mySystem.axis[0].position);
}

#define PARAM_REC_OFF(field)  ((uint16_t)offsetof(ParamRecord_t, field))

static const ParamDesc_t s_param_table[] = {
    /* name         rec_off                            ram_base                        ram_off                        type         默认值                       限值                          应用钩子 */
    { "volt_over",  PARAM_REC_OFF(volt_over_th),       (void *)&g_volt_cfg,            offsetof(VoltCfg_t, over_th),    PARAM_T_F32, .def_f = VOL_OVER_TH_DFT,   .min = VOL_OVER_TH_MIN, .max = VOL_OVER_TH_MAX },
    { "volt_under", PARAM_REC_OFF(volt_under_th),      (void *)&g_volt_cfg,            offsetof(VoltCfg_t, under_th),   PARAM_T_F32, .def_f = VOL_UNDER_TH_DFT,  .min = VOL_UNDER_TH_MIN, .max = VOL_UNDER_TH_MAX },
    { "volt_hyst",  PARAM_REC_OFF(volt_hyst),          (void *)&g_volt_cfg,            offsetof(VoltCfg_t, hyst),       PARAM_T_F32, .def_f = VOL_HYST_DFT,      .min = VOL_HYST_MIN, .max = VOL_HYST_MAX },
    { "volt_rec",   PARAM_REC_OFF(volt_recover_ms),    (void *)&g_volt_cfg,            offsetof(VoltCfg_t, recover_ms), PARAM_T_U32, .def_u = VOL_RECOVER_DELAY_MS_DFT, .min = VOL_RECOVER_DELAY_MS_MIN, .max = VOL_RECOVER_DELAY_MS_MAX },
    { "volt_over_ms", PARAM_REC_OFF(volt_over_ms),     (void *)&g_volt_cfg,            offsetof(VoltCfg_t, over_ms),    PARAM_T_U32, .def_u = VOL_OVER_MS_DFT,   .min = VOL_OVER_MS_MIN, .max = VOL_OVER_MS_MAX },
    { "volt_under_ms",PARAM_REC_OFF(volt_under_ms),    (void *)&g_volt_cfg,            offsetof(VoltCfg_t, under_ms),   PARAM_T_U32, .def_u = VOL_UNDER_MS_DFT,  .min = VOL_UNDER_MS_MIN, .max = VOL_UNDER_MS_MAX },
    { "cur_th",     PARAM_REC_OFF(cur_over_th_ma),     (void *)&g_cur_cfg,             offsetof(CurCfg_t, over_th_ma),  PARAM_T_F32, .def_f = CUR_OVER_CUR_TH_MA_DFT, .min = CUR_OVER_CUR_TH_MA_MIN, .max = CUR_OVER_CUR_TH_MA_MAX },
    { "cur_win",    PARAM_REC_OFF(cur_window_ms),      (void *)&g_cur_cfg,             offsetof(CurCfg_t, window_ms),   PARAM_T_U16, .def_u = CUR_OVER_WINDOW_MS_DFT, .min = CUR_OVER_WINDOW_MS_MIN, .max = CUR_OVER_WINDOW_MS_MAX },
    { "cur_block",  PARAM_REC_OFF(cur_block_ms),       (void *)&g_cur_cfg,             offsetof(CurCfg_t, block_ms),    PARAM_T_U16, .def_u = CUR_BLOCK_MS_DFT,  .min = CUR_BLOCK_MS_MIN, .max = CUR_BLOCK_MS_MAX },
    { "hall_seq",   PARAM_REC_OFF(hall_dir_seq),       (void *)&g_mothall_invert_dir,  0U,                              PARAM_T_U8,  .def_u = HALL_DIR_SEQ_DFT,  .min = HALL_DIR_SEQ_MIN, .max = HALL_DIR_SEQ_MAX },
    { "motor_seq",  PARAM_REC_OFF(motor_dir_seq),      RT_NULL,                        0U,                              PARAM_T_U8,  .def_u = MOTOR_DIR_SEQ_DFT, .min = MOTOR_DIR_SEQ_MIN, .max = MOTOR_DIR_SEQ_MAX, .apply_hook = Param_HookMotorSeq },
    { "win_a",      PARAM_REC_OFF(rod_calib_win_a),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_CALIB_WIN_A_DFT, .min = ROD_CALIB_WIN_A_MIN, .max = ROD_CALIB_WIN_A_MAX, .apply_hook = Param_HookRodApply },
    { "win_b",      PARAM_REC_OFF(rod_calib_win_b),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_CALIB_WIN_B_DFT, .min = ROD_CALIB_WIN_B_MIN, .max = ROD_CALIB_WIN_B_MAX, .apply_hook = Param_HookRodApply },
    { "win_c",      PARAM_REC_OFF(rod_calib_win_c),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_CALIB_WIN_C_DFT, .min = ROD_CALIB_WIN_C_MIN, .max = ROD_CALIB_WIN_C_MAX, .apply_hook = Param_HookRodApply },
    { "win_d",      PARAM_REC_OFF(rod_calib_win_d),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_CALIB_WIN_D_DFT, .min = ROD_CALIB_WIN_D_MIN, .max = ROD_CALIB_WIN_D_MAX, .apply_hook = Param_HookRodApply },
    { "stop_margin",PARAM_REC_OFF(rod_stop_margin),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_STOP_MARGIN_DFT, .min = ROD_STOP_MARGIN_MIN, .max = ROD_STOP_MARGIN_MAX, .apply_hook = Param_HookRodApply },
    { "stroke",     PARAM_REC_OFF(rod_stroke_mm),      RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_STROKE_DFT,    .min = ROD_STROKE_MIN, .max = ROD_STROKE_MAX, .apply_hook = Param_HookRodApply },
    { "ratio",      PARAM_REC_OFF(rod_reduction_ratio),RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_REDUCTION_RATIO_DFT, .min = ROD_REDUCTION_RATIO_MIN, .max = ROD_REDUCTION_RATIO_MAX, .apply_hook = Param_HookRodApply },
    { "pulses",     PARAM_REC_OFF(rod_hall_pulses),    RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_HALL_PULSES_DFT, .min = ROD_HALL_PULSES_MIN, .max = ROD_HALL_PULSES_MAX, .apply_hook = Param_HookRodApply },
    { "lead",       PARAM_REC_OFF(rod_screw_lead),     RT_NULL,                        0U,                              PARAM_T_F32, .def_f = ROD_SCREW_LEAD_DFT, .min = ROD_SCREW_LEAD_MIN, .max = ROD_SCREW_LEAD_MAX, .apply_hook = Param_HookRodApply },
};

#define PARAM_TABLE_NUM   (sizeof(s_param_table) / sizeof(s_param_table[0]))

/*=============================================================================
 * 编译期核对（断言）：表行 type 声明 ↔ 记录字段/消费字段实际宽度逐一对应
 * 规则：表加/改一行，此处同步加/改一行；字段改名或宽度变化会直接编译失败
 * （断言宏 PARAM_STATIC_ASSERT / PARAM_TYPE_SIZE 定义在 dev_param.h）
 *============================================================================*/
/* A 块记录字段宽度（与表 20 行一一对应） */
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_over_th)        == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_under_th)       == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_hyst)           == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_recover_ms)     == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_over_ms)        == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->volt_under_ms)       == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->cur_over_th_ma)      == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->cur_window_ms)       == PARAM_TYPE_SIZE(PARAM_T_U16));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->cur_block_ms)        == PARAM_TYPE_SIZE(PARAM_T_U16));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->hall_dir_seq)        == PARAM_TYPE_SIZE(PARAM_T_U8));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->motor_dir_seq)       == PARAM_TYPE_SIZE(PARAM_T_U8));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_calib_win_a)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_calib_win_b)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_calib_win_c)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_calib_win_d)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_stop_margin)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_stroke_mm)       == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_reduction_ratio) == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_hall_pulses)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((ParamRecord_t *)0)->rod_screw_lead)      == PARAM_TYPE_SIZE(PARAM_T_F32));
/* 消费 RAM 字段宽度（独立消费结构的行） */
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->over_th)      == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->under_th)     == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->hyst)         == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->recover_ms)   == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->over_ms)      == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((VoltCfg_t *)0)->under_ms)     == PARAM_TYPE_SIZE(PARAM_T_U32));
PARAM_STATIC_ASSERT(sizeof(((CurCfg_t *)0)->over_th_ma)    == PARAM_TYPE_SIZE(PARAM_T_F32));
PARAM_STATIC_ASSERT(sizeof(((CurCfg_t *)0)->window_ms)     == PARAM_TYPE_SIZE(PARAM_T_U16));
PARAM_STATIC_ASSERT(sizeof(((CurCfg_t *)0)->block_ms)      == PARAM_TYPE_SIZE(PARAM_T_U16));
/* 变量行（volatile uint8_t；sizeof 不受 volatile 修饰影响） */
PARAM_STATIC_ASSERT(sizeof(g_mothall_invert_dir) == PARAM_TYPE_SIZE(PARAM_T_U8));

static uint8_t Param_TypeSize(ParamType_t type)
{
    return (uint8_t)PARAM_TYPE_SIZE(type);
}

/* 当前值所在地址：有独立消费 RAM 用 RAM，否则 record 自镜像 */
static const void *Param_DescCurPtr(const ParamDesc_t *d)
{
    if ((d->ram_base != RT_NULL) && (d->ram_base != (const void *)&g_param_record)) {
        return (const uint8_t *)d->ram_base + d->ram_off;
    }
    return (const uint8_t *)&g_param_record + d->rec_off;
}

/* 按类型从地址读出为 float（范围校验用；整型参数值域远小于 2^24，float 转换无损） */
static float Param_DescRead(const void *src, ParamType_t type)
{
    switch (type) {
    case PARAM_T_F32: return *(const float *)(const void *)src;
    case PARAM_T_U32: return (float)(*(const uint32_t *)(const void *)src);
    case PARAM_T_U16: return (float)(*(const uint16_t *)(const void *)src);
    default:          return (float)(*(const uint8_t *)(const void *)src);
    }
}

/* 范围校验：min==0 且 max==0 视为不限 */
static bool Param_DescInRange(const ParamDesc_t *d, float v)
{
    if ((d->min == 0.0f) && (d->max == 0.0f)) {
        return true;
    }
    return (v >= d->min) && (v <= d->max);
}

/* 按参数类型格式化数值（告警/param_list 限值显示用；独立 4 缓冲轮转，同条打印最多 4 值） */
static const char *Param_ValFmt(const ParamDesc_t *d, float v)
{
    static char s_val_buf[4][16];
    static uint8_t s_val_idx = 0U;
    char *pBuf = s_val_buf[s_val_idx];

    s_val_idx = (uint8_t)((s_val_idx + 1U) % 4U);
    switch (d->type) {
    case PARAM_T_F32:
        Param_VoltFmt(v, pBuf, sizeof(s_val_buf[0]));
        break;
    case PARAM_T_U32:
        rt_snprintf(pBuf, sizeof(s_val_buf[0]), "%lu", (unsigned long)v);
        break;
    default:
        rt_snprintf(pBuf, sizeof(s_val_buf[0]), "%u", (unsigned)v);
        break;
    }
    return pBuf;
}

/* 按名查表（param_set 用） */
static const ParamDesc_t *Param_TableFind(const char *name)
{
    uint8_t i;
    for (i = 0; i < PARAM_TABLE_NUM; i++) {
        if (rt_strcmp(s_param_table[i].name, name) == 0) {
            return &s_param_table[i];
        }
    }
    return RT_NULL;
}

/* 写单个参数（param_set 用）：范围校验（越界 PARAM_WARNNING 拒收）→
   有独立 RAM 写 RAM，自镜像写 record；调用方负责钩子 */
static int32_t Param_DescWrite(const ParamDesc_t *d, float v)
{
    void *dst;

    if (!Param_DescInRange(d, v)) {
        PARAM_WARNNING("param_set '%s'=%s out of range [%s ~ %s], rejected",
                       d->name, Param_ValFmt(d, v),
                       Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
        return PARAM_ERR_INVD_PARAM;
    }

    dst = (void *)Param_DescCurPtr(d);
    switch (d->type) {
    case PARAM_T_F32: *(float *)(void *)dst = v; break;
    case PARAM_T_U32: *(uint32_t *)(void *)dst = (uint32_t)v; break;
    case PARAM_T_U16: *(uint16_t *)(void *)dst = (uint16_t)v; break;
    default:          *(uint8_t *)(void *)dst = (uint8_t)v; break;
    }
    return PARAM_OK;
}

/* 默认值 → A 块记录（SetDefaults 回调内调） */
static void Param_TableDefaults(void)
{
    uint8_t i;
    for (i = 0; i < PARAM_TABLE_NUM; i++) {
        const ParamDesc_t *d = &s_param_table[i];
        void *rec = (uint8_t *)&g_param_record + d->rec_off;
        switch (d->type) {
        case PARAM_T_F32: *(float *)(void *)rec = d->def_f; break;
        case PARAM_T_U32: *(uint32_t *)(void *)rec = d->def_u; break;
        case PARAM_T_U16: *(uint16_t *)(void *)rec = (uint16_t)d->def_u; break;
        default:          *(uint8_t *)(void *)rec = (uint8_t)d->def_u; break;
        }
    }
}

/* 设置默认值（Param_Init 未找到有效块时回调）：magic + 表驱动填默认值。
   置于表定义之后：函数体引用 PARAM_TABLE_NUM（宏依赖 s_param_table 定义） */
static void Param_SetDefaults(void)
{
    (void)memset(&g_param_record, 0, sizeof(ParamRecord_t));
    g_param_record.head_magic = PARAM_MAGIC_HEAD;
    g_param_record.tail_magic = PARAM_MAGIC_TAIL;

    Param_TableDefaults();
    PARAM_PRINT("[PARAM] defaults set (%u items) — use param_list to dump",
                (unsigned)PARAM_TABLE_NUM);
}

/* A 块记录 → 消费 RAM（镜像）+ 钩子（派生量/散变量）。上电加载与 EraseAll 后调用 */
static void Param_TableApply(void)
{
    uint8_t i;
    const ParamDesc_t *d;
    float v;

    for (i = 0; i < PARAM_TABLE_NUM; i++) {   /* 阶段1：record → 消费 RAM 镜像（+加载越界告警，仅告警不裁剪） */
        d = &s_param_table[i];
        v = Param_DescRead((const uint8_t *)&g_param_record + d->rec_off, d->type);
        if (!Param_DescInRange(d, v)) {
            PARAM_WARNNING("loaded '%s'=%s out of range [%s ~ %s] -- param_erase to reset",
                           d->name, Param_ValFmt(d, v),
                           Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
        }
        if ((d->ram_base != RT_NULL) && (d->ram_base != (void *)&g_param_record)) {
            (void)memcpy((uint8_t *)d->ram_base + d->ram_off,
                         (uint8_t *)&g_param_record + d->rec_off,
                         Param_TypeSize(d->type));
        }
    }
    for (i = 0; i < PARAM_TABLE_NUM; i++) {   /* 阶段2：钩子（幂等） */
        d = &s_param_table[i];
        if (d->apply_hook != RT_NULL) {
            d->apply_hook();
        }
    }
}

/* 消费 RAM → A 块记录（Dev_Param_Save 写 Flash 前调；无独立 RAM 的项天然跳过） */
static void Param_TableSave(void)
{
    uint8_t i;
    for (i = 0; i < PARAM_TABLE_NUM; i++) {
        const ParamDesc_t *d = &s_param_table[i];
        if ((d->ram_base != RT_NULL) && (d->ram_base != (void *)&g_param_record)) {
            (void)memcpy((uint8_t *)&g_param_record + d->rec_off,
                         (uint8_t *)d->ram_base + d->ram_off,
                         Param_TypeSize(d->type));
        }
    }
}

/* 表全量打印（param_list/param_show 用；每参数一行：当前值 + 默认值 + 限值范围） */
static void Param_TableShow(void)
{
    uint8_t i;
    for (i = 0; i < PARAM_TABLE_NUM; i++) {
        const ParamDesc_t *d = &s_param_table[i];
        const void *cur = Param_DescCurPtr(d);
        switch (d->type) {
        case PARAM_T_F32:
            PARAM_PRINT("[PARAM] %-10s= %s (def %s, rng %s~%s)", d->name,
                   Dev_Param_MmFmtA(*(const float *)(const void *)cur),
                   Dev_Param_MmFmtA(d->def_f),
                   Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
            break;
        case PARAM_T_U32:
            PARAM_PRINT("[PARAM] %-10s= %lu (def %lu, rng %s~%s)", d->name,
                   (unsigned long)*(const uint32_t *)(const void *)cur, (unsigned long)d->def_u,
                   Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
            break;
        case PARAM_T_U16:
            PARAM_PRINT("[PARAM] %-10s= %u (def %u, rng %s~%s)", d->name,
                   (unsigned)*(const uint16_t *)(const void *)cur, (unsigned)d->def_u,
                   Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
            break;
        default:
            PARAM_PRINT("[PARAM] %-10s= %u (def %u, rng %s~%s)", d->name,
                   (unsigned)*(const uint8_t *)(const void *)cur, (unsigned)d->def_u,
                   Param_ValFmt(d, d->min), Param_ValFmt(d, d->max));
            break;
        }
    }
}

/**
 * @brief  检查电机是否运行中（任一轴 MS_RUNNING 视为运行）
 */
static bool Param_IsMotorRunning(void)
{
    ArbData_t arb;
    for (uint8_t axis = 0U; axis < ARB_MAX_AXIS_NUM; axis++) {
        if (Arb_GetData(axis, &arb) == RT_EOK) {
            if (arb.state == MS_RUNNING) {
                return true;
            }
        }
    }
    return false;
}

/*=============================================================================
 * 外部接口
 *============================================================================*/

int32_t Dev_Param_Init(void)
{
    int32_t res;

    if (s_param_inited != 0U) {
        return PARAM_OK;
    }

    res = Param_Init(&s_param_config, &s_param_runtime, Param_SetDefaults);
    if (res == PARAM_OK) {
        Param_TableApply();   /* record → 消费 RAM + 钩子（rod 组此时未 init 由其内部守卫跳过） */
        PARAM_PRINT("[PARAM] loaded (seq=%lu) — use param_list to dump all",
                    (unsigned long)g_param_record.sequence_id);
    } else {
        PARAM_PRINT("[PARAM] init FAILED res=%d (keep RAM defaults)", (int)res);
    }

    /* 快块 B（推杆行程）统一在此初始化：扫描加载，待 App_Model_Init 后 RodApply 恢复 */
    (void)Dev_Param_RodInit();

    s_param_inited = 1U;
    return res;
}

int32_t Dev_Param_Save(void)
{
    int32_t res;

    if (s_param_inited == 0U) {
        PARAM_PRINT("[PARAM] save refused: not inited");
        return PARAM_ERR_NOT_RDY;
    }

    /* 安全检查：擦/写期间 BUS_HOLD 全系统停顿，电机运行时禁止 */
    if (Param_IsMotorRunning()) {
        PARAM_PRINT("[PARAM] save refused: motor RUNNING (stop motor first)");
        return PARAM_ERR;
    }

    /* 消费 RAM → 记录（表驱动镜像；volt/cur 组回写，record 自镜像组天然跳过） */
    Param_TableSave();

    res = Param_Save(&s_param_config, &s_param_runtime);
    if (res != PARAM_OK) {
        PARAM_PRINT("[PARAM] save FAILED res=%d", (int)res);
    }
    return res;
}

void Dev_Param_EraseAll(void)
{
    if (Param_IsMotorRunning()) {
        PARAM_PRINT("[PARAM] erase refused: motor RUNNING");
        return;
    }
    Param_Debug_EraseAll(&s_param_config, &s_param_runtime, Param_SetDefaults);
    Param_TableApply();
    Dev_Param_RodEraseAll();
}

/*=============================================================================
 * Flash 存储测试（主循环 savelabel 触发，rtt debug 里改变量即可）
 *============================================================================*/

void Dev_Param_SaveTest(void)
{
    /* 修改其中一个值：过压阈值 +0.5V，[23.0, 27.0) 回绕，保证每组内容可预测且不同 */
    g_volt_cfg.over_th += 0.5f;
    if (g_volt_cfg.over_th >= 27.0f) {
        g_volt_cfg.over_th = 23.0f;
    }

    if (Dev_Param_Save() == PARAM_OK) {
        Param_VoltFmt(g_volt_cfg.over_th, s_volt_a, sizeof(s_volt_a));
        PARAM_PRINT("[PARAM] TEST saved: over_th=%sV seq=%lu addr=0x%08lX",
                    s_volt_a, (unsigned long)g_param_record.sequence_id,
                    (unsigned long)(s_param_runtime.curr_addr - sizeof(ParamRecord_t)));
    }
}

void Dev_Param_FillTest(void)
{
    uint32_t count = 0U;
    uint32_t remain;

    /* 连续保存，直到当前扇区剩余空间只够再存两组（之后再存将走擦除/换扇区路径）。
       500 组为保险上限，正常单扇区最多 8192/92 ≈ 89 组，条件本身不会跨扇区。 */
    while (((s_param_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_param_runtime.curr_addr)
           > (2U * sizeof(ParamRecord_t))) {
        if (Dev_Param_Save() != PARAM_OK) {
            break;
        }
        count++;
        if (count >= 500U) {
            break;
        }
    }

    remain = (s_param_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_param_runtime.curr_addr;
    PARAM_PRINT("[PARAM] FILL done: +%lu blocks, remain=%luB, next=0x%08lX (seq=%lu)",
                (unsigned long)count, (unsigned long)remain,
                (unsigned long)s_param_runtime.curr_addr,
                (unsigned long)g_param_record.sequence_id);
}

/*=============================================================================
 * MSH 调试命令
 *============================================================================*/
static void cmd_param_show(void)
{
    PARAM_PRINT("[PARAM] rec: magic=0x%08lX seq=%lu erase=%lu",
           (unsigned long)g_param_record.head_magic,
           (unsigned long)g_param_record.sequence_id,
           (unsigned long)g_param_record.erase_count);
    Param_TableShow();   /* A 块全部参数（当前值 + 默认值） */
    PARAM_PRINT("[PARAM] runtime: sec=%u addr=0x%08lX saves=%lu last_res=%d",
           (unsigned)s_param_runtime.curr_sec, (unsigned long)s_param_runtime.curr_addr,
           (unsigned long)s_param_runtime.save_count, (int)s_param_runtime.last_res);
    Dev_Param_RodShow();
}
MSH_CMD_EXPORT_ALIAS(cmd_param_show, param_show, show stored param & runtime state);

static void cmd_param_save(void)
{
    int32_t res = Dev_Param_Save();
    if (res == PARAM_OK) {
        PARAM_PRINT("[PARAM] save OK");
    }
    /* 失败原因已由 Dev_Param_Save 内打印 */
}
MSH_CMD_EXPORT_ALIAS(cmd_param_save, param_save, save g_volt_cfg/g_cur_cfg to flash);

static void cmd_param_erase(void)
{
    Dev_Param_EraseAll();
    PARAM_PRINT("[PARAM] erased & re-inited with defaults");
}
MSH_CMD_EXPORT_ALIAS(cmd_param_erase, param_erase, erase all param sectors & reset defaults);

/* 方向相序设置：dir_set <hall 0/1> <motor 0/1>——写记录 + 即时应用 + 保存 Flash（掉电保持） */
static void cmd_dir_set(int argc, char **argv)
{
    unsigned long hall, mot;

    if (argc != 3) {
        PARAM_PRINT("usage: dir_set <hall 0/1> <motor 0/1>");
        return;
    }
    hall = strtoul(argv[1], RT_NULL, 0);
    mot  = strtoul(argv[2], RT_NULL, 0);
    if ((hall > 1UL) || (mot > 1UL)) {
        PARAM_PRINT("[PARAM] dir_set arg must be 0 or 1");
        return;
    }

    g_param_record.hall_dir_seq  = (uint8_t)hall;
    g_param_record.motor_dir_seq = (uint8_t)mot;
    g_mothall_invert_dir = (uint8_t)hall;             /* 应用 hall（与表镜像同款） */
    Dev_MotorGpio_SetDirInvert((uint8_t)mot);         /* 应用 motor */

    if (Dev_Param_Save() == PARAM_OK) {
        PARAM_PRINT("[PARAM] dir set: hall=%lu motor=%lu saved (seq=%lu)",
               hall, mot, (unsigned long)g_param_record.sequence_id);
    }
    /* 保存失败原因（如电机运行中）已由 Dev_Param_Save 内打印 */
}
MSH_CMD_EXPORT_ALIAS(cmd_dir_set, dir_set, set hall/motor dir seq: dir_set <0/1> <0/1>);

/* 参数表全量打印（当前值 + 默认值；param_show 的精简版） */
static void cmd_param_list(void)
{
    Param_TableShow();
}
MSH_CMD_EXPORT_ALIAS(cmd_param_list, param_list, list all flash-A params & defaults);

/* 按名设置参数：写消费 RAM（自镜像组写记录）→ 立即应用钩子；param_save 持久化 */
static void cmd_param_set(int argc, char **argv)
{
    const ParamDesc_t *d;

    if (argc != 3) {
        PARAM_PRINT("usage: param_set <name> <value>");
        return;
    }
    d = Param_TableFind(argv[1]);
    if (d == RT_NULL) {
        PARAM_PRINT("[PARAM] set: unknown '%s' (see param_list)", argv[1]);
        return;
    }
    if (Param_DescWrite(d, (float)atof(argv[2])) != PARAM_OK) {
        return;   /* 越界拒收：PARAM_WARNNING 已打印限值范围 */
    }
    if (d->apply_hook != RT_NULL) {
        d->apply_hook();
    }
    PARAM_PRINT("[PARAM] set %s ok (use param_save to store)", d->name);
}
MSH_CMD_EXPORT_ALIAS(cmd_param_set, param_set, set param by name: param_set <name> <value>);

#endif /* DEV_ENABLE_PARAM */
