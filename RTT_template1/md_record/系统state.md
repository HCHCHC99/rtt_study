# 系统状态机（表驱动）现状与故障联动方案

> 记录日期：2026-08-25
> 状态：**已实施（2026-08-25）**，armcc 编译通过

---

## 一、现状核对（已读代码确认）

| 项 | 状态 | 位置 |
|---|---|---|
| 表驱动（跳转表 + 函数表） | ✅ 已有 | `Dev\dev_mgr\dev_state.c`：`sys_jump[]` + `sys_func[]` |
| 状态入口函数 | ✅ 已有（6 个） | `sys_enter_init/idle/run/stop/fault/emergency` |
| 状态枚举 | ✅ 已有 | `SYS_STATE_INIT/IDLE/RUN/FAULT/EMERGENCY/STOP` |
| main.c 启用 | ✅ 已有 | `App_Model_Init(); Sys_Sm_Thread_Start();` |
| 事件驱动线程 | ✅ 已有 | `dev_sm_thread.c`：`rt_event_recv → Sys_State_Dispatch` |
| 电压过压/欠压事件位 | ✅ 已有 | `EVT_SYS_VOLT_OVER / EVT_SYS_VOLT_UNDER`，跳转表已配 → FAULT |
| **过流事件位** | ✅ 已有 | `EVT_SYS_OVER_CURRENT (1U<<8)` |
| **power 设备触发事件** | ✅ 已有 | `dev_bus_voltage.c / dev_cur_sensor.c` 的 `Isr1ms`（1ms ISR）故障跳变 → `Sys_Event_Send` |
| **FAULT 进入动作** | ✅ 已有 | `sys_enter_fault` 记 `mySystem.error_code` + 打印；EMERGENCY enter 末尾自动跳 FAULT |

上电流程（已通）：`INIT_DONE|WORK_ENABLE` → INIT→IDLE→RUN。

---

## 二、目标

1. 上电自动进入 RUN（现状已满足）；
2. **过流 / 过压 / 欠压**时，power 设备（1ms ISR）通过 **RT-Thread 事件组**（`rt_event`）通知状态机 → 进入 **EMERGENCY**（enter 末尾跳 **FAULT**）；
3. 进入 FAULT 记录故障原因（故障码），打印；
4. 恢复路径：`EVT_SYS_RECOVERY` → FAULT→IDLE（再使能回 RUN）。

---

## 三、方案设计

### 3.1 事件位（`Dev\dev_mgr\dev_event_def.h`）

新增：
```c
#define EVT_SYS_OVER_CURRENT   (1U << 8)   /* 过流 */
```
（现有：INIT_DONE(0) FAULT(1) EMERGENCY(2) RECOVERY(3) VOLT_NORMAL(4) VOLT_OVER(5) VOLT_UNDER(6) WORK_ENABLE(7) ST_WORK_ERROR(13)）

### 3.2 跳转表（`dev_state.c` `sys_jump[]`）

新增 6 行（IDLE/RUN 遇过压/欠压/过流 → **EMERGENCY**，enter_emergency 末尾跳 FAULT）：
```c
{SYS_STATE_IDLE,  EVT_SYS_OVER_CURRENT, SYS_STATE_EMERGENCY},
{SYS_STATE_RUN,   EVT_SYS_OVER_CURRENT, SYS_STATE_EMERGENCY},
```
`Sys_State_Dispatch()` 加对应投递；`dev_sm_thread.c` 的 `SYS_SM_WAIT_EVENTS` 加 `EVT_SYS_OVER_CURRENT`。

### 3.3 故障码（`Dev\dev_mgr\dev_model.h`）

`System_t` 增加字段（当前最小裁剪版没有）：
```c
uint32_t error_code;   /* 故障码：0=无 1=过流 2=过压 3=欠压 */
```
`Sys_State_Dispatch` 在投递故障事件时记录 `mySystem.error_code`；`sys_enter_fault` 打印故障码。

### 3.4 power 设备触发（`Dev\dev_power\*.c`）

在 **1ms ISR（`Isr1ms`，TMR0_2 心跳）**里**状态跳变时发一次事件**（避免每 1ms 重复发；ISR 不打印）：

- `dev_bus_voltage.c`：
  - 检测到过压跳变（`s_u8Fault: 0→2`）→ `Sys_Event_Send(EVT_SYS_VOLT_OVER)`
  - 检测到欠压跳变（`0→1`）→ `Sys_Event_Send(EVT_SYS_VOLT_UNDER)`
  - 恢复正常（`s_u8Fault: 2/1→0`）→ **不发事件**（恢复路径为手动 `Sys_State_Recover()`）
- `dev_cur_sensor.c`：
  - 过流跳变（`s_u8Status: 0→1`）→ `Sys_Event_Send(EVT_SYS_OVER_CURRENT)`
  - 恢复正常（`1→0`）→ 不发事件（过流仍手动恢复）

依赖方向：`dev_power → dev_mgr`（设备层通知管理层故障），需 include `dev_state.h` / `dev_event_def.h`。

### 3.5 FAULT 进入动作（`sys_enter_fault`）

- 打印故障码（RTT `MAIN_E`）；
- 记录 `mySystem.error_code`；
- 预留：停轴/停设备（当前无真实轴，先留注释/空动作）。

---

## 四、改动清单

| 文件 | 改动 |
|---|---|
| `Dev\dev_mgr\dev_event_def.h` | 加 `EVT_SYS_OVER_CURRENT (1U<<8)` |
| `Dev\dev_mgr\dev_model.h` | `System_t` 加 `uint32_t error_code;` |
| `Dev\dev_mgr\dev_state.c` | 跳转表 +2 行；Dispatch 加 OVER_CURRENT；enter_fault 记故障码 |
| `Dev\dev_mgr\dev_sm_thread.c` | WAIT_EVENTS 加 OVER_CURRENT |
| `Dev\dev_power\dev_bus_voltage.c` | 故障跳变 → `Sys_Event_Send(VOLT_OVER/UNDER)` |
| `Dev\dev_power\dev_cur_sensor.c` | 过流跳变 → `Sys_Event_Send(OVER_CURRENT)` |

---

## 五、已确认决策（2026-08-25）

1. **恢复方式（2026-08-26 更新）**：电压故障**自动恢复**（迟滞 1V + 延时后发 `EVT_SYS_VOLT_NORMAL`，故障位图清空 → FAULT→IDLE）；过流仍**手动** `Sys_State_Recover()`。
2. **过压 / 欠压 / 过流 → EMERGENCY**；`sys_enter_emergency` 末尾**跳入 FAULT**（急停动作后进入稳定故障态）。
3. **`System_t` 增加 `error_code` 故障码字段**：0=无 1=过流 2=过压 3=欠压。
4. **`enter_fault` 动作**：记故障码 + 打印（先不接真实设备）。
   **打印规范**：需要打印的代码可 `#include "applications/common.h"`（含 rtt_log）；**全部用 `MAIN_D`**；**不打印中文**；**不打印浮点**（浮点拆成整型分别打印小数点前后）。

---

## 六、参考：当前状态机表

```
状态: INIT IDLE RUN FAULT EMERGENCY STOP
INIT    --INIT_DONE--> IDLE
IDLE    --WORK_ENABLE--> RUN
IDLE    --FAULT--> FAULT
IDLE    --EMERGENCY--> EMERGENCY
IDLE    --ST_WORK_ERROR--> STOP
IDLE    --VOLT_OVER / VOLT_UNDER / OVER_CURRENT--> EMERGENCY
RUN     --FAULT--> FAULT
RUN     --EMERGENCY--> EMERGENCY
RUN     --ST_WORK_ERROR--> STOP
RUN     --VOLT_OVER / VOLT_UNDER / OVER_CURRENT--> EMERGENCY
STOP    --WORK_ENABLE--> RUN
STOP    --RECOVERY--> IDLE
FAULT   --RECOVERY--> IDLE
EMERGENCY --FAULT--> FAULT   (enter_emergency 末尾自动投递)
EMERGENCY --RECOVERY--> IDLE
```


---

## 七、实施完成记录（2026-08-25）

- dev_event_def.h：加 EVT_SYS_OVER_CURRENT (1U<<8)；
- dev_model.h：System_t 加 error_code；
- dev_state.h：加故障码宏 SYS_ERR_* + Sys_State_Recover()；
- dev_state.c：过压/欠压/过流 → EMERGENCY，enter_emergency 末尾跳 FAULT；Dispatch 记录/清除故障码 + POWER_PRINT（over volt / under volt / over curr）；enter_fault 打印故障码；Sys_State_Recover() 实现（手动恢复）；
- dev_sm_thread.c：WAIT_EVENTS 加 OVER_CURRENT；日志统一 MAIN_D；
- dev_power\\*.c：改为 **1ms ISR 检测**（BusVoltage_Isr1ms / CurrentSensor_Isr1ms，TMR0_2 心跳，读 10ms 滑动均值），故障跳变时 Sys_Event_Send(...)，**ISR 不打印**（打印在 dev_state.c Dispatch）；
- main.c：cmd_power 浮点拆整型打印；修 \\r\\n 转义；
- 验证：armcc 逐文件 0 error；待上板 RTT/sys_evt 验证。



- 采样/检测机制升级（2026-08-25）：ADC 改 TMR0_1 硬件触发 500us/2kHz（AOS 事件路由 + EOCA 滑动窗口），检测改 TMR0_2 1ms ISR（详见 移植电压电流.md 第 8 节）。


### 2026-08-26 追加：阈值 RAM 化 + 电压自动恢复
- 阈值从宏改为 RAM 配置变量：`g_volt_cfg`（over_th/under_th/hyst/recover_ms，迟滞默认 **1V**）、`g_cur_cfg`（over_th_ma/window_ms）；debugger 改变量实时生效，重启回默认；
- `System_t.fault_bits`：bit0 过流 bit1 过压 bit2 欠压；故障置位、电压正常清电压位、**全清才自动恢复**、手动恢复全清；
- 电压恢复正常（过迟滞+延时）→ 设备发 `EVT_SYS_VOLT_NORMAL` → 状态机自动 `FAULT→IDLE`；过流仍手动。

### 2026-08-26 追加②：初始化移入 IDLE + 恢复回故障前状态
- 设备初始化由 main 移到 `sys_enter_idle()`：`Dev_Registry_InitAll()` 每次进 IDLE 执行（上电一次初始化；故障恢复/回 IDLE 清业务状态）；main 去掉相同初始化，避免上电两次；
- 硬件触发只启动一次（首次进 IDLE：`HcDrv_Timer_Start1ms(Dev_Power_Isr1ms)` + `Dev_Adc_Start()`），后续回 IDLE 不重复启动；
- `System_t.prev_state`：首次故障时记录故障前状态；电压自动恢复/手动恢复均**回到故障前状态**（RUN->RUN，否则->IDLE）。

### 2026-08-26 追加③：恢复延时 500ms + 极性重发（接受）
- 电压恢复延时默认 **3000ms -> 500ms**（`VOL_RECOVER_DELAY_MS_DFT (500U)`，运行时 `g_volt_cfg.recover_ms`）；
- 配置默认宏统一移到 .h（bus_voltage/cur_sensor/polarity/di_task/led_task 的 .h）；
- **确认（A）**：故障恢复回到 IDLE 时，`Dev_Registry_InitAll` 会把极性复位为 UNKNOWN，di_task 约 10ms 后重新判出 FWD 并**再发一次 EVT_ACT_POLARITY_FWD**——无害（轴事件组位本来就置着，属再次确认；系统状态机不受影响），接受此行为。
