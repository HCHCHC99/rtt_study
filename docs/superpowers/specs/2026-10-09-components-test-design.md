# RT-Thread 组件功能验证工程（第一批：内核对象）设计文档

- 日期：2026-10-09
- 工程路径：`D:\RT_HC32F460_v.1.0.1\RTT_template1`
- 状态：设计已获用户口头确认（对话内），本文档为书面版本

## 1. 背景与目标

本工程原为一套"推杆/霍尔电机"产品固件（小华 EV_F460_LQ100_V2 开发板，HC32F460PETB，Cortex-M4 200MHz，RT-Thread 4.1.1）。
用户要求：**业务（电机/推杆）代码不再需要，但保留在工程里编译；本工程今后仅用于验证 RT-Thread 组件功能**。

目标：新建一个测试文件夹，每个 RT-Thread 组件对应一个自包含测试文件，通过 msh 命令逐个触发验证，打印 PASS/FAIL。

## 2. 用户已确认的决定

1. **业务代码处理**：不删除、保留编译，但 `main.c` 不再调用任何业务代码，程序不执行到即可（用户原话："可以不删除，并且保留编译，但main.c不用到那些代码，程序不执行到即可"）。
2. **第一批范围**：仅内核对象（线程/信号量/互斥量/事件/邮箱/消息队列/内存池/堆/定时器/空闲钩子）。设备框架、外设驱动留待后续批次。
3. **实现方案**：方案 A —— 自建轻量校验宏 + 一组件一文件 + msh 命令触发（否决了 utest 组件方案与单大文件方案）。

## 3. 工程现状关键事实（设计依据）

- 控制台：`uart4`（`RT_CONSOLE_DEVICE_NAME "uart4"`，USB 虚拟串口，finsh/msh 在此交互）。
  `applications/rtt_console.c` 强覆盖 `rt_kprintf`（RT_WEAK）重定向到 SEGGER RTT → **所有测试打印出现在 J-Link RTT Viewer**。
- 已开启内核能力（`rtconfig.h`）：信号量/互斥量/事件/邮箱/消息队列/内存池/堆（small mem as heap）、overflow check、栈检查、idle hook（4 个槽位）、RT_TICK_PER_SECOND=1000、软定时器线程**未**开启（`RT_USING_TIMER_SOFT` 无）。
- 已开启组件：finsh/msh（tshell 优先级 20）、设备框架、serial V1+DMA、pin、SPI、system workqueue（优先级 23）。main 线程优先级 10。
- `rtconfig.h:7` `#include "Task/task_set.h"`：内核各线程栈大小宏来自业务代码头文件。因业务代码保留编译，此耦合**不需要处理**。
- `Dev/`、`Task/`、`Utils/` 内**无任何 `INIT_APP_EXPORT` 等自动初始化**，全部业务线程仅在旧 `main()` 里显式启动 → 换掉 main.c 后业务代码天然"永不执行"。
- `tests/` 目录已被占用（PC 端宿主单元测试 + fake 头），且在 `.cproject` 排除表中 → 新目录命名 `components_test/` 避开。
- `.cproject` sourceEntries 为全工程收录（entry 无 name 限制 + 一串 `excluding=`），`components_test/` 不在排除表 → **Studio 构建自动纳入新目录**。
- 顶层 `SConscript` 自动扫描所有带 `SConscript` 的子目录 → SCons 构建自动纳入。
- 本目录**不是 git 仓库**；pwsh 沙箱在工作区初始化失败（SetNamedSecurityInfoW 错误），文件操作全部改用读写工具。

## 4. 方案 A 架构

### 4.1 目录结构

```
RTT_template1/
├── applications/main.c          ← 重写为最小骨架（原内容备份 main.c.bak_orig）
└── components_test/             ← 新增
    ├── SConscript
    ├── kt_common.h              ← 公共宏：CHECK/日志/计数/汇总
    ├── kt_thread.c  kt_sem.c  kt_mutex.c  kt_event.c
    ├── kt_mail.c    kt_mq.c   kt_mp.c     kt_heap.c
    ├── kt_timer.c   kt_idle.c kt_menu.c
```

### 4.2 文件与命令清单

| 文件 | msh 命令 | 验证点 |
|---|---|---|
| `kt_common.h` | — | `KT_CHECK(cond,...)` 断言宏（失败打印 文件:行+表达式，失败计数+1，不中断）；`KT_LOG`（`[KT][模块]` 前缀）；模块结果结构体；`kt_module_report()` 汇总打印 |
| `kt_thread.c` | `kt_thread` | 动态创建（rt_thread_create）+静态初始化（rt_thread_init）；启动/退出同步（线程末尾放信号量）；`rt_thread_mdelay` 计时精度（对比 rt_tick_get，1s 允差 ±2 tick）；同优先级时间片轮转（双线程计数对比）；运行时优先级修改（以 `rt-thread/src/thread.c` 实际 API 为准） |
| `kt_sem.c` | `kt_sem` | 计数语义（release 2 次 → take 2 次成功）；take 超时返回 `-RT_ETIMEOUT` 且实测耗时吻合；生产者/消费者两线程同步；静态 rt_sem_init/detach |
| `kt_mutex.c` | `kt_mutex` | 同线程重复持有（hold 计数，释放等次数）；非持有者 trytake → `-RT_EBUSY`；**优先级继承**：低优先级(28)持有 → 高优先级(24)等待 → 持有者 current_priority 被抬到 24 → 释放后恢复 28 |
| `kt_event.c` | `kt_event` | OR / AND 组合接收；RT_EVENT_FLAG_CLEAR；收先于发 → 超时 -RT_ETIMEOUT；静态初始化 |
| `kt_mail.c` | `kt_mail` | 定长邮箱收发；满时 send → `-RT_EFULL`；`rt_mb_send_wait` 超时；`rt_mb_urgent` 插队顺序；静态初始化 |
| `kt_mq.c` | `kt_mq` | 变长消息收发（字节数核对）；满 → `-RT_EFULL`；空 get 超时；urgent；静态初始化 |
| `kt_mp.c` | `kt_mp` | 块分配/释放；耗尽后阻塞分配被另线程 free 唤醒（带 2s 超时保护）；静态 rt_mp_init |
| `kt_heap.c` | `kt_heap` | malloc/free、realloc 扩容内容保留、calloc 清零；`rt_memory_info` used 增减；碎片场景（A,B 相邻释放后大块可分配） |
| `kt_timer.c` | `kt_timer` | 硬定时器 oneshot 仅触发 1 次；周期定时器 N 次触发计时吻合（±1 tick/次）；stop 后不再触发；重新 start；静态 rt_timer_init/detach。**回调运行于 tick 上下文：只置 volatile 标志 / rt_sem_release，禁止打印** |
| `kt_idle.c` | `kt_idle` | `rt_thread_idle_sethook` 注册后 hook 被周期调用（hook 置 volatile 标志，主测线程 mdelay 后检查）；验证完 `rt_thread_idle_delhook` 摘除 |
| `kt_menu.c` | `kt_list` / `kt_all` | `kt_list` 列出全部测试及一句话说明；`kt_all` 顺序执行全部 10 项，打印每模块 `RESULT` 与总表、总耗时 |
| `SConscript` | — | 与 `applications/SConscript` 同款：`Glob('*.c')` + `DefineGroup`，CPPPATH 指向自身 |

### 4.3 并发与安全设计

- 测试工作线程优先级 **24~30**（低于 tshell=20 / workq=23，不干扰命令行）；每测同时 ≤3 个临时线程；栈 1024~2048B。
- **所有阻塞调用带 ≤2s 超时**：任何调度异常都不会挂死测试（挂死即 FAIL 并返回 shell）。
- 每个测试结束自我清理（删线程/删对象/摘 hook），命令可反复执行。
- API 行为一律以本工程内置源码 `rt-thread/src/`（4.1.1）为准，不凭外部记忆。

### 4.4 输出协议

```
[KT][sem] #3 take timeout=100ms PASS
[KT][sem] ---------- RESULT: PASS (8/8) ----------      ← 每模块
[KT][all] sem     PASS 8/8                               ← kt_all 总表
[KT][all] ======== TOTAL: PASS (10/10 modules) ========
```
打印统一 `rt_kprintf` → RTT Viewer；命令在 uart4 串口终端输入。

### 4.5 构建接线（本批零配置改动）

- RT-Thread Studio：新目录自动纳入编译（见 §3）。
- SCons：自动纳入。
- Keil uvprojx：本批**不动**；需要时 `scons --target=mdk5` 重生成。
- `rtconfig.h` 本批**零改动**。`kt_timer` 仅测硬定时器；软定时器线程测试留批次③。

### 4.6 验证流程

Studio 编译 → J-Link 下载 → 开 RTT Viewer + uart4 终端 → `kt_list` → 逐条执行，一条通过再下一条；全部通过后跑 `kt_all` 收口。

## 5. 范围外（后续批次，本文档不展开）

- 批次②：pin / serial 收发 / finsh 命令机制 / 组件自动初始化（INIT_EXPORT 导出验证）。
- 批次③：软定时器线程、SPI 回环、I2C/ADC/RTC/看门狗（每项需改 rtconfig + 解除 `.cproject` 对应 `drv_*.c` 排除）。
- 业务代码的永久清理/删除（用户明确保留）。

## 6. 风险与备注

- `main.c.bak_orig` 仅为保险备份，构建器不识别其后缀，不会编译。
- 本环境无 git，文档只落盘不提交。
- pwsh 沙箱不可用不影响本任务（纯文件读写 + 用户侧编译验证）。
