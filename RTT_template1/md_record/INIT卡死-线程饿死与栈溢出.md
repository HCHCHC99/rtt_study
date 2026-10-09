# INIT 卡死：线程饿死与 rod 栈溢出写坏 TCB

> 记录日期：2026-08-30
> 现象级别：上电卡 INIT；极性/ADC/状态机全部无响应

---

## 一、现象

1. 上电后 `[SYS_MON]` 永远显示 `sm: sys=INIT`；ADC 全零（`Dev_Adc_Start`/1ms 心跳都在 IDLE 入口才启动，属下游症状）。
2. `[SM_DIAG]`：`cur=0 evt_flag=0x81 sys_sm=yes entered=0 recv=0` —— INIT_DONE 事件已发出、从未被消费；sys_sm 线程已创建，但 `entered=0` 证明**从未执行过第一条指令**（计数器判据，不受丢打印干扰）。
3. 实测优先级敏感：`SYS_SM_THREAD_PRIO` 改 **19 → 系统正常跑通**；21/22 → 卡死（sys_sm 饿死）。

## 二、诊断路径

1. 静态排除：跳转表 `{INIT, INIT_DONE}→IDLE` 正确；事件已置位（`sys_evt->set=0x81`）；`rt_event_recv` 全仓唯一消费者是 sys_sm；`rt_thread_find("sys_sm")` 找得到线程。
2. 计数器二分（打印不可信，RTT 会丢行）：`entered=0 recv=0` 锁定"线程从未被调度"。
3. `[TH_DIAG]` 全线程转储抓到真凶：**名为 `!` 的线程（prio 20，stat=2），而 rod 线程从列表消失** —— rod 的 TCB 名字/优先级字段被写坏。

## 三、根因

rod_task 为接入仲裁新增 `Arb_GetData(i, &arb)`：栈上局部 `ArbData_t` 约 **360B**（4×(10 条记录×8B+count)+方向/状态字段），而 `TASK_STACK_ROD` 只有 **256B** → **必然栈溢出**。溢出向下写穿堆上相邻的 rod TCB：名字变 `!`、优先级字段变垃圾值。

坏掉的 rod 线程带着垃圾优先级（≤21）且循环异常，把 prio 21/22 的 sys_sm、di_task 全部饿死；sys_sm 改到 19/5（高于坏值）即可抢占，"看起来好了"——优先级数字只是掩护，真实根因是栈溢出。

> 修正：此前怀疑 tshell(finsh) 空转饿死——prio5 构建的 TH_DIAG 显示 tshell=SUSPEND（正常阻塞），该理论不成立。shell.c 的 EOF 分支让出补丁（mdelay 10ms）无害，保留。

## 四、修复

1. `TASK_STACK_ROD` 256 → **2048**（float 状态机 + Arb_GetData 约360B 拷贝 + ROD_PRINT 余量）。
2. sys_sm 优先级：验证期用 5/19 均可；栈修复后 22 已无饿死风险，建议回归 22 后复测。
3. `[SM_DIAG]`/`[TH_DIAG]` 诊断打印保留（每秒一次，成本低，上线前可关）。

## 五、预期健康状态

`[SM_DIAG] cur=2(RUN) evt_flag=0x0 entered=1 recv>=1`；`[TH_DIAG]` 中 rod 名字正常、无 `!` 线程。

## 六、教训

1. **栈溢出在 RTOS 上的表现是"玄学"**：TCB 名字/优先级被写坏 → 表面像优先级配置问题。名字异常的线程 = 堆被踩的铁证。
2. 大结构体按值拷贝（`Arb_GetData` 的 340B 拷贝）进小栈线程前必查栈水位；`Task_Stack_Monitor` 哨兵本可报警，但饿死发生在哨兵读到之前。
3. 判据要用变量计数器（entered/recv），打印会丢；`[SYS_MON]` 读的是状态变量，不受丢打印影响，"卡 INIT"是真实逻辑故障而非显示故障。

### 追加（2026-08-30）：di@256B 栈溢出实锤
1. `TASK_STACK_DI` 256 → **1024**。di 实际执行链 Polarity_Scan（GPIO 驱动读+窗口判定）+ 跳变时 rt_event_send/rt_mq_send + POLARITY_PRINT（SEGGER_RTT_printf 单次 200~400B 栈）+ Task_Set_Beat，峰值远超 256B。
2. 实测特征：di prio=22 时被饿死从未运行（溢出潜伏）；改 prio=18 真正运行立刻被内核栈检查抓获（`thread:di stack overflow` 打印后进入保护停机）。
3. 追溯：更早 rod TCB 名字变 `!` 的堆踩踏，疑似同源（di/rod 栈与对方 TCB 在堆上相邻）。
4. "应用线程优先级 >20 会饿死"不是 RT-Thread 限制（RT_THREAD_PRIORITY_MAX=32）：是栈溢出破坏期的症状，栈修复后需复测 21/22 是否仍饿死。

### 追加（2026-08-30）：电源三模块模拟模式
1. `VOLT_SIM_MODE_EN` / `CUR_SIM_MODE_EN` / `POLARITY_SIM_MODE_EN`（各自头文件，默认 0 真实模式，独立开关）。
2. 电压/电流：1ms ISR 的取数点替换为 `g_volt_sim_mv`(mV) / `g_cur_sim_ma`(mA) 直接赋值——ADC 读取与 +1.2V 偏置一并旁路，阈值/迟滞/故障/恢复/事件链全部照常；表达式窗口改值即时生效。
3. 极性：`Polarity_Scan` 改用 `g_pol_sim_state`（0=UNKNOWN 保持 1=掉电 2=正接 3=反接 4=异常），跳过 GPIO 与消抖窗口；跳变沿→轴事件+仲裁命令的下游链路照常。UNKNOWN/非法值=保持上次状态。

## 七、后续加固（2026-08-30）：task_set + 饿死防护

1. **task_stack.c/.h → task_set.c/.h 重命名**：定位从"只管栈"扩展为"栈 + 优先级"双权威来源。rtconfig.h、main.c、dev_model.h、dev_registry.h、di/led/rod_task.h、dev_act.h 全部改引 task_set.h；优先级宏 `TASK_PRIO_*` 收编（sys_sm=19、act=15、led=18、dev/rod=20、di=22、arbtst=24），原各头文件宏改为别名引用，调用点零改动。
2. **统一创建入口 `Task_Set_Create()`**：栈下限（256B）/优先级合法性检查 + 失败打印，di/rod/led/dev/registry(C 模式)/arbtst 全部改走该入口。
3. **饿死金丝雀**：`TASK_PRIO_CANARY=28`（仅高于 idle）500ms 心跳，主循环每秒查新鲜度，超 3s 打印 `STARVATION` 告警——能发现"高优先级线程空转饿死低优先级"这类此前无告警的故障（开关 `TASK_SET_STARVATION_GUARD_EN`）。
4. **TH_DIAG 乱码修复**：线程名按 `RT_NAME_MAX=8` 定长存储无 NUL（如 `sys workq` 截断），`%s` 直接打会越界读到乱码；改为拷贝到 `RT_NAME_MAX+1` 缓冲补 NUL 后打印。
5. **金丝雀升级为每任务心跳点名**（实测 LED 饿死时金丝雀只能报"自己也断了"，报不出名单）：`Task_Set_Create` 增加 `beat_ms` 参数（周期任务填周期，事件驱动填 0 跳过检测），任务循环内调 `Task_Set_Beat()` 刷新心跳；`Task_Set_StarvationCheck` 每秒扫描，超 5s 无心跳即逐个点名 `[TASK_SET] STARVED led prio=21 stale=...`。金丝雀本身也纳入心跳监测（beat=500ms）。
