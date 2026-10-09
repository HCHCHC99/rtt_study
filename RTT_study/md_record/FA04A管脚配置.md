# FA04A 管脚配置

> 基准日期：2026-09-08（行号以当日代码为准，后续改动会漂移，以宏名搜索为准）
> 引脚绑定一律以宏形式放各模块 .h（开发规范 §11），本文记录"宏名 → 引脚 → 代码位置"。
> 启用开关集中在 Dev/dev_config.h。

## 一、当前启用管脚（FA04A 新板）

| 引脚 | 功能 | 电气语义 | 宏定义位置（文件:行） |
|---|---|---|---|
| PA4 | 电压检测 ADC1_CH4 | 150k:10k 分压，gain=16，满量程 52.8V | Adp/hc32_drv_adc.h:107-110（`ADC_DRV_VOLT_*`） |
| PA6 | 电流检测 ADC1_CH6 | 差分放大 20 倍 × 10mΩ = 200mV/A（→5000 mA/V，dev_cur_sensor.h:11-15） | Adp/hc32_drv_adc.h:112-115（`ADC_DRV_CUR_*`） |
| PC13 | 电机霍尔 A | EXTI CH13 双边沿，INT013_IRQn，EIRQ13 | Dev/dev_hall_motor/dev_hall_motor.h:18-19、24、26、28（`MOTOR_HALL_A_*`） |
| PC14 | 电机霍尔 B | EXTI CH14 双边沿，INT014_IRQn，EIRQ14 | Dev/dev_hall_motor/dev_hall_motor.h:20-21、25、27、29（`MOTOR_HALL_B_*`） |
| PB8 | 电机控制 PL1 | 高有效：正转（伸出）；与 PB9 成对写、永不双高 | Dev/dev_gpio_motor/dev_gpio_motor.h:20-21（`MOTOR_GPIO_FWD_*`） |
| PB9 | 电机控制 PH1 | 高有效：反转（缩回） | Dev/dev_gpio_motor/dev_gpio_motor.h:22-23（`MOTOR_GPIO_REV_*`） |
| PH2 | LED 心跳 | 输出，1000ms 翻转 | Task/led_task.h:12-13（`LED_PORT/LED_PIN` ← Adp/hc32_drv_gpio.h:13-14 `PH2_*`） |

### 电机 GPIO 真值表（dev_gpio_motor.h 文件头）

| PB8(PL1) | PB9(PH1) | 动作 |
|---|---|---|
| 0 | 0 | 停转/急停（双低 = 刹车态，待上板实测确认） |
| 1 | 0 | 正转（伸出） |
| 0 | 1 | 反转（缩回） |
| 1 | 1 | 非法 → 按停止处理 |

## 二、保留但未启用（模拟/停用/编译排除）

| 引脚 | 功能 | 状态 | 宏定义位置（文件:行） |
|---|---|---|---|
| PC14 | 极性检测 P | `POLARITY_SIM_MODE_EN=1` 模拟模式：Init/Scan 不碰 GPIO，与霍尔 B 同脚无运行时冲突 | Dev/dev_power/dev_polarity.h:13（`POWER_DIR_P_PIN`）、35（模拟开关） |
| PC15 | 极性检测 N | 同上（新板真实极性脚为 PB15：正接低/反接高，暂不启用） | Dev/dev_power/dev_polarity.h:14（`POWER_DIR_N_PIN`） |
| PB2 | 推杆霍尔上限位（旧板） | 代码保留未启用（2026-09-09 从 v1.0.1 移植回）：dev_hall_rod 已编译但未注册；新板软限位走过流校准链 | Dev/dev_hall_rod/dev_hall_rod.h:14（`ROD_HALL_MAX_PIN`） |
| PB10 | 推杆霍尔下限位（旧板） | 同上 | Dev/dev_hall_rod/dev_hall_rod.h:15（`ROD_HALL_MIN_PIN`） |
| PB6 | 旧板 PWM TIM4_3_OVL | 代码保留未启用（2026-09-09 从 v1.0.1 移植回）：dev_pwm / hc32_drv_pwm 已编译但未注册（TMR4 不初始化，无引脚配置） | Adp/hc32_drv_pwm.h:46-47（`PWM_OVL_*`） |
| PB7 | 旧板 PWM TIM4_3_OVH | 同上 | Adp/hc32_drv_pwm.h:44-45（`PWM_OVH_*`） |
| PB8 | 旧板 PWM TIM4_3_OUL | 同上（FA04A 该脚现为电机控制 PL1，见第一部分；启用 dev_pwm 前先解决引脚冲突） | Adp/hc32_drv_pwm.h:42-43（`PWM_OUL_*`） |
| PB9 | 旧板 PWM TIM4_3_OUH | 同上（FA04A 该脚现为电机控制 PH1，见第一部分） | Adp/hc32_drv_pwm.h:40-41（`PWM_OUH_*`） |

## 三、代码中定义但未使用的宏（注意勿误启用）

| 引脚 | 宏 | 位置 | 说明 |
|---|---|---|---|
| PA3 | `PA3_PORT/PA3_PIN` | Adp/hc32_drv_gpio.h:16-17 | 无任何使用点 |
| PA7 | `ADC_DRV_TOGGLE_PIN` | Adp/hc32_drv_adc.h:33 | ADC ISR 调试翻转宏，未接线 |
| PA9/PA10 | `USART1_TX/RX`（board 版） | board/board_config.h:22-27 | `BSP_USING_UART1` 未定义；且 PA9/PA10 为旧板电机霍尔脚，启用会冲突 |
| PA2/PA3 | `USART2_TX/RX` | board/board_config.h:30-35 | `BSP_USING_UART2` 未定义 |
| PC13/PH2 | `USART3_RX/TX` | board/board_config.h:38-43 | `BSP_USING_UART3` 未定义；PC13 现为电机霍尔 A |
| PB9/PE6 | `USART4_TX/RX` | board/board_config.h:46-51 | `BSP_USING_UART4` 未定义；**PB9 现为电机控制 PH1，启用 UART4 会冲突**（msh 控制台名 "uart4" 见 rtconfig.h，实际输出走 RTT） |
| PB6/PB7 | `CAN1_TX/RX` 等 | board/board_config.h:56-66 | CAN 段未启用 |

## 四、板级基础（无代码绑定）

- SWD 调试口：PA13(SWDIO) / PA14(SWCLK)（标准占用，代码中无宏）
- 电机霍尔 EXTI：PC13/PC14 双边沿计数，纯观测（脉冲增量），不做堵转判定（过流唯一判定堵转/软限位，见 dev_state.c 过流分支）
- ADC 同一 SEQ_A 对齐采样：电压/电流时间对齐（Adp/hc32_drv_adc.c 通道映射表引用 hc32_drv_adc.h 绑定宏）

## 五、换板改管脚操作索引

1. ADC（电压/电流）：只改 Adp/hc32_drv_adc.h:107-115 四宏一组（通道号+DDL 枚举+端口+引脚），dev_adc.c 已引用宏
2. 电机霍尔：只改 Dev/dev_hall_motor/dev_hall_motor.h:18-29（端口/引脚/EXTI/IRQ/EIRQ 成组）
3. 电机输出（GPIO）：只改 Dev/dev_gpio_motor/dev_gpio_motor.h:20-23；旧板 PWM/推杆霍尔回退路径的代码已于 2026-09-08 删除（dev_pwm、dev_hall_rod、hc32_drv_pwm 模块及 DEV_ENABLE_PWM/DEV_ENABLE_HALL_ROD 开关均已移除），如需旧板方案请从版本历史恢复
4. 极性：启用真实检测需改 dev_polarity.h:35 `POLARITY_SIM_MODE_EN=0`（注意 PC14 与霍尔 B 冲突，FA04A 真实脚在 PB15，需同步改 13-14 行宏）
