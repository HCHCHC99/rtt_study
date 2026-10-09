# 设备接口表（ops）设计思路

> 记录日期：2026-08-25
> 结论：**硬件设备才用函数指针接口表（ops），算法设备不用**。

---

## 一、背景

ADC 设备、电流传感器设备、母线电压设备三个设备：

| 设备 | 类型 | 底层 |
|---|---|---|
| ADC（dev_adc） | 硬件设备 | HC32 ADC 驱动（Adp/hc32_drv_adc） |
| 电流传感器（dev_power/dev_cur_sensor） | 算法设备 | 无硬件，读 ADC 缓冲 + 算法 |
| 母线电压（dev_power/dev_bus_voltage） | 算法设备 | 无硬件，读 ADC 缓冲 + 算法 |

最初纠结：**电流/电压要不要也建一个类似 `dev_adc_ops` 的函数指针表？**

---

## 二、核心认知：统一注册调度本身就是函数指针表

`Dev/dev_registry.h` 里的 `SysModule_t` **已经是一个函数指针表**：

```c
typedef struct {
    const char *name;
    void (*init)(void);      /* 函数指针 */
    void (*task)(void);      /* 函数指针 */
    uint8_t     prio;
    uint16_t    period_ms;
    ...
} SysModule_t;
```

- 三个设备都通过 `SYS_MODULE_REGISTER("xxx", Init, Task, prio, period)` 注册进统一调度；
- **统一调度（注册/周期执行）已经被 `SysModule_t` 保证**，不需要第二个表。

结论：**不给电流/电压建 `cur_ops/vm_ops`，不会在统一注册调度上出任何问题。**

---

## 三、两种"接口"要分开看

| 接口 | 是什么 | 谁有 | 解决什么问题 |
|---|---|---|---|
| **管理接口** `SysModule_t` | init/task/prio/period | 所有设备 | 统一注册、周期调度 |
| **功能接口** `dev_adc_ops` | init/start/stop/get_latest/get_raw/read_ring/get_mean | 只有 ADC | 抽象"可替换的底层硬件" |

> 功能接口只在"**实现可以被替换**"时才需要。

---

## 四、决策：加 vs 不加（给电流/电压建算法接口表）

### 加的优点
- 外观统一：所有设备都有 ops 表
- 可运行时切换算法（如过流判定：均值窗口 → RMS）
- 可注入测试替身（mock）
- 算法可复用/多实例

### 加的缺点
- **空抽象 / 过度设计**：算法设备没有"硬件可换"，表里通常只有一个实现
- 代码量翻倍：每个算法设备多一个头文件 + 表 + 绑定函数
- 调用链变绕：调试要多跳一层
- 违背 YAGNI（"以后可能用" ≠ "现在需要"）

### 不加的优点
- 简单直接、好读好调试
- 调度已由 `SysModule_t` 统一，不需要第二个表
- 改动最小；算法设备本质就是算法，直接写最清晰

### 不加的缺点
- 外观不统一（若团队强制风格统一会不舒服）
- 换算法要改源码（编译期切换）
- 测试 mock 要靠宏/条件编译

---

## 五、决策规则

```
这个模块有没有"可替换的实现"？
├── 有 ≥2 种实现，且需要运行时切换/复用 → 加接口表（ADC 属于这类）
├── 只有 1 种实现，且稳定 → 不加（电流/电压属于这类）
└── 想"以防万一" → 也别加（YAGNI）
```

**最终决定：电流传感器、母线电压不加 ops 表。**

---

## 六、中间方案（以后真想换算法，比 ops 表便宜）

1. **参数配置结构体**：把阈值/窗口/滤波点数集中成 `XxxCfg_t`，`Xxx_SetConfig()` 改参数即可 —— 90% 的"换算法"其实是换参数。
2. **编译期宏选算法**：
   ```c
   #if defined(CUR_ALGO_AVG_WINDOW)
       /* 均值+窗口 */
   #elif defined(CUR_ALGO_RMS)
       /* RMS */
   #endif
   ```
   零运行时开销、零间接层。

---

## 七、当前架构落地情况（2026-08-25）

```
┌─ Adp（硬件驱动层）─────────────────────┐
│  hc32_drv_adc  (实现 dev_adc_ops)      │
│  hc32_drv_gpio                        │
└───────────────────────────────────────┘
        ▲ dev_adc_ops（接口表，仅 ADC）
┌─ Dev（设备层）─────────────────────────┐
│  dev_adc/     ADC 设备（缓冲生产者）     │
│  dev_power/dev_cur_sensor（算法设备，1ms ISR）│
│  dev_power/dev_bus_voltage（算法设备，1ms ISR）│
│  dev_registry 注册（SysModule_t，task=NULL）    │
│  dev_power/dev_power_isr（1ms 心跳分发）        │
│  dev_mgr       系统状态机（消费事件）      │
└───────────────────────────────────────┘
```

- `Adp/` 只有硬件驱动（无电流/电压代码）✅
- 三个设备统一注册（`SysModule_t`，task=NULL：**ISR 驱动**，`Dev_Start` 线程不再启动）✅；`SysModule_t` 计划升级为 **B（协作周期）+ C（独立线程）混合**（加 `thread_entry`，见 `md_record\模块运行设计.md` 第四章）✅
- ADC 走 `dev_adc_ops` 接口表，绑定：`Dev_Adc_Bind(&hc32_adc_ops)` ✅
- 电流/电压（1ms ISR，TMR0_2 心跳）调 `Dev_Adc_GetMean()`（10ms 滑动均值）做算法，无 ops 表 ✅

---

## 八、一句话总结

> **统一调度靠 `SysModule_t`（它本身就是函数指针表）；功能接口表（ops）只在"实现可替换"时有价值——ADC 换芯片需要，电流/电压算法不换实现就不需要。真想换算法，用参数结构体或编译期宏，别上函数指针表。**

---

## 九、Dev 层统一命名（2026-08-25）

Dev 层文件/文件夹统一 dev_ 前缀：

| 原 | 现 |
|---|---|
| Dev\mgr\ | Dev\dev_mgr\（dev_model / dev_state / dev_sm_thread / dev_event_def） |
| Dev\power\ | Dev\dev_power\（dev_cur_sensor / dev_bus_voltage） |
| Dev\dev_adc.c（顶层） | Dev\dev_adc\dev_adc.c |
| Adp\adc_drv | Adp\hc32_drv_adc（hc32_drv_* 标识 HC32 驱动） |

函数名（Sys_State_* / CurrentSensor_* / AdcDrv_* 等）保持不变，仅统一文件名/文件夹名。



