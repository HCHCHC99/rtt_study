# RT-Thread 组件使用情况

> 日期：2026-09-15　配置来源：`rtconfig.h`；使用情况按代码实际调用核对（`Dev/Task/applications` 全量 grep）
> 结论一句话：核心用到 **线程 + 事件集 + 消息队列 + 互斥量 + FinSH + 串口控制台**，mailbox/内存池/PIN/SPI/workqueue 为模板遗留未用。

## 1. 内核对象（实际在用）

| 组件 | rtconfig.h 开关 | API / 实例 | 使用位置 |
|---|---|---|---|
| 线程 | `RT_USING_USER_MAIN` | `rt_thread_create` | main 线程（栈 `TASK_STACK_MAIN`，优先级 10）+ Task_Set 统一创建：tshell / dev(monitor) / act(仲裁) / led / di / rod；sys_sm 线程独立建（`dev_sm_thread.c`） |
| 事件集 | `RT_USING_EVENT` | `rt_event_create/send/recv` | `sys_evt`（系统事件：过压/欠压/过流/极性换向）+ 每轴 `evt_act`（极性沿/RUN/限位通知）；`Sys_Event_Send / Act_Event_Send` ISR 安全（仅置位唤醒） |
| 消息队列 | `RT_USING_MESSAGEQUEUE` | `rt_mq_create/send/urgent/recv` | 仲裁命令队列 `s_arb_mq`（`dev_act.c`）：任意线程/ISR 经 `Arb_SendCommand` 投递，`Arb_ThreadEntry` 阻塞消费 → ProcessMessage → Decision → ops 写 GPIO |
| 互斥量 | `RT_USING_MUTEX` | `rt_mutex_init`（静态，每轴一把，`RT_IPC_FLAG_PRIO` 优先级继承） | 保护 `ArbData_t`（`Arb_GetData/ProcessMessage/Decision` 全部持锁访问） |
| 信号量 | `RT_USING_SEMAPHORE` | `rt_sem_init` | 仅 `dev_pwm.c`（该模块**未注册未启用**，实际死代码） |
| 小内存堆 | `RT_USING_SMALL_MEM` / `_AS_HEAP` | create 类动态分配底层 | rt_event_create / rt_mq_create / rt_thread_create 等 |
| 内存池 | `RT_USING_MEMPOOL` | — | **未使用** |

## 2. 组件框架（实际在用）

| 组件 | 开关 | 说明 |
|---|---|---|
| FinSH / msh | `RT_USING_FINSH` / `RT_USING_MSH`（tshell 线程，优先级 20） | `MSH_CMD_EXPORT_ALIAS`：param_show / param_save / param_erase / dir_set 等调试命令 + 内置命令；历史 5 条 |
| 设备框架 + 控制台 | `RT_USING_DEVICE` / `RT_USING_CONSOLE` / `RT_USING_SERIAL(V1)` | 控制台 `uart4`（`RT_CONSOLE_DEVICE_NAME`）；`rt_kprintf` 可经 `RTT_PRINTF_EN` 重定向到 SEGGER RTT（`rtt_manager.h`） |
| 组件自动初始化 | `RT_USING_COMPONENTS_INIT` | 板级/组件 init 流程 |

## 3. 调度与调试设施（实际在用）

| 项 | 配置 | 说明 |
|---|---|---|
| tick 基准 | `RT_TICK_PER_SECOND=1000` | 1ms 系统节拍（时间基准；µs 级另有 TMR6 忙等 UsTimer，非 RT-Thread） |
| 空闲钩子 | `RT_USING_IDLE_HOOK` | 饥饿检测 canary（`Task_Set_Beat`：最低优先级线程喂狗，全线程饿死报警） |
| 栈溢出检查 | `RT_USING_OVERFLOW_CHECK` / `RT_USING_STACK_CHECK` | + `Task_Stack_Dump` 统一打印各线程栈/总栈/堆余量 |
| 调度器位图 | `RT_USING_CPU_FFS` | 硬件前导零指令加速就绪表查找 |
| 线程名长度 | `RT_NAME_MAX 8` | 命名注意 ≤8 字符 |
| 主线程 | `RT_MAIN_THREAD_PRIORITY 10` | main 跑 1s 打印循环 + savelabel 测试触发 |

## 4. 配置了但未使用（裁剪候选，收益小可不动）

| 组件 | 开关 | 现状 |
|---|---|---|
| mailbox | `RT_USING_MAILBOX` | 无 `rt_mb_` 调用（队列需求由 rt_mq 覆盖） |
| 内存池 | `RT_USING_MEMPOOL` | 无使用 |
| PIN 设备框架 | `RT_USING_PIN` | 代码直接走 `Hc32_Gpio` DDL（Adp 层），不经 rt_pin |
| SPI | `RT_USING_SPI` | 未使用 |
| 系统工作队列 | `RT_USING_SYSTEM_WORKQUEUE` | 无 `rt_work` 调用 |
| 信号量 | `RT_USING_SEMAPHORE` | 仅未启用的 dev_pwm 引用；裁 dev_pwm 可一并去 |

## 5. 线程清单（现役）

| 线程名 | 创建方式 | 上下文职责 |
|---|---|---|
| main | USER_MAIN | 1s 打印循环（SYS_MON/ROD_POS/SM_DIAG）+ savelabel 测试触发 |
| tshell | FinSH | msh 命令 |
| sys_sm | `rt_thread_create`（dev_sm_thread） | 事件驱动状态机：Dispatch 过流/欠压分流、EMERGENCY/FAULT/RECOVERY |
| act | registry（C 模式） | 仲裁：mq 收命令 → Decision → ops 写 GPIO |
| dev | registry（B 模式） | 100ms Monitor_Task 刷 g_monitor |
| rod | Task_Set | 10ms：霍尔积分/位置/校准执行/PollSave |
| di | Task_Set | 10ms：极性扫描（窗口消抖在设备内） |
| led | Task_Set | 1s 翻转 |
| idle | 内核 | 饥饿 canary 钩子 |

## 6. 执行模型映射（对应开发规范 §9）

- **A ISR 驱动**：电压/电流检测（TMR0_2 1ms 心跳）+ ADC EOCA 滑窗——不经 RT-Thread IPC，事件仅 rt_event_send 置位
- **B 协作周期**：monitor（registry dev 线程 100ms 刷 g_monitor）
- **C 独立线程阻塞**：act 仲裁（rt_mq_recv 阻塞 + 互斥量）、sys_sm（rt_event_recv 阻塞）
