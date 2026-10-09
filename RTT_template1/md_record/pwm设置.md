# PWM 设置与架构

> 记录日期：2026-08-30
> 原则：本文只记录**架构、接口、变量名、设计决策**；具体数值（频率、占空比、周期等）
> 一律以代码为准，不在此记录——改值不需要同步本文。
> 模拟注入的用法见 `模拟输入.md`。

---

## 一、文件与职责

| 文件 | 层 | 职责 |
|---|---|---|
| `Adp\hc32_drv_pwm.h/.c` | Adp（硬件适配） | TMRA 寄存器操作：通道初始化、比较值、强制极性、比较输出使能；引脚/通道/时钟等硬件绑定宏 |
| `Dev\dev_pwm.h/.c` | Dev（设备层） | 电机语义（正转/反转/停止）、严格互补驱动、仲裁输出绑定；硬件无关 |
| `applications\main.c` | 应用 | 自测线程（`DEV_ENABLE_ARB_SELFTEST` 控制，量产置 0） |

硬件绑定宏（引脚、TMRA 单元、通道号、复用功能码、计数时钟）全部在 `hc32_drv_pwm.h`；
设备级可调项（运行占空比等）在 `dev_pwm.h`；换芯片只改 Adp 层。

## 二、信号链路

```
输入源（极性/状态机/自测…）
  |  Arb_SendCommand(axis, dev_id, prio, cmd, duty, urgent)
  v
rt_mq --> act 仲裁线程
  |  四队列（block/allow x fwd/rev）--> Arb_Decision
  v
s_arb_output_ops.fwd / rev / stop     <-- 函数指针（Arb_BindOutputOps 绑定）
  v
Dev_PwmMotor_RunFwd / RunRev / Stop   <-- Dev 层电机语义
  |  严格互补：PHU/PHV 同一比较值 + 反相极性
  v
PwmHw_SetCompareValue / SetForcePolarity <-- Adp 层
  v
TMRA4 硬件寄存器（PERAR 周期 / CMPAR 比较 / PCONR 极性）
```

## 三、严格互补原理

两通道共用同一比较值 CMP，但极性相反：

- PHU：CompareMatch=LOW，PeriodMatch=HIGH → 高电平区间 [0, CMP)
- PHV：CompareMatch=HIGH，PeriodMatch=LOW → 高电平区间 [CMP, Period]

任意时刻 PHU 高则 PHV 低，零同电平窗口（寄存器级同时翻转）。

| 模式 | PHU | PHV | 电机 |
|---|---|---|---|
| 正转（伸出） | 高 = duty% | 高 = 100−duty% | 一个方向 |
| 反转（缩回） | 高 = 100−duty% | 高 = duty% | 另一方向 |
| 停止 | FORCE_LOW 强制双低 | 同左 | 静止（无毛刺） |

停止不用比较值清零（那会产生毛刺），用硬件 FORCE 极性直接钳低。

## 四、注册与执行

注册块在 `Dev_RegisterAll` 内 `DEV_ENABLE_PWM` 开关。
模块名 `pwm`，init = `Dev_Pwm_Init`（建通道 + 硬件初始化 + 绑定仲裁 ops），无 task（正反转为瞬切，无 ramp）。
IDLE 重入（InitAll）：`s_inited` 守卫跳过，硬件配置不变。
FCG 时钟门控 / GPIO 复用：写保护域，WE/WP 解锁上锁（见 开发规范.md §12）。

## 五、调试观测点

| 变量 / 打印 | 用途 |
|---|---|
| `[ARB] tmra probe: cnt_clk=... Hz` | 启动时实测 TMRA 计数时钟（校准 PWM_TMRA_CLK_HZ） |
| `[ARB] motor fwd/rev/stop` | 仲裁 ops 每次调用的标记 |
| `[TASK_SET] STARVED ...` | 饿死检测（canary prio + 任务名，见 task_set） |
| `g_arb_dbg_*` | Watch：四队列 count / active_dir / device / state / conflict / 仲裁计数 |
| `g_pol_sim_state` | 模拟极性注入（0~4，见 模拟输入.md） |
| `g_volt_sim_mv` / `g_cur_sim_ma` | 模拟电压/电流注入（见 模拟输入.md） |

## 六、已知设计决策

1. 停止用 FORCE_LOW 而非 CMP=0：比较值清零会产生毛刺；FORCE 极性无毛刺且解除即时。
2. 正反转为瞬切（无 ramp）：hkb_1 同为瞬切；若将来需要软启，可启用 Dev_Pwm_Task（预留接口）。
3. 严格互补零同电平：两通道同一比较事件同时翻转；H 桥驱动芯片的传播延迟差异（ns 级）由驱动芯片自身死区处理。
4. duty_pct 未映射速度：仲裁命令携带的占空比当前忽略，运行侧固定占空比（见 dev_pwm.h）；如需调速，在 PwmMotor_ApplyDuty 中映射即可。
