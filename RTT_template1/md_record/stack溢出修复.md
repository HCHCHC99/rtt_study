# 栈溢出修复（rod 线程 HardFault）

> 记录日期：2026-08-26
> 状态：**根因已定位（rod 线程栈溢出）；rod 1024→2048 本次已改；待上板验证**

---

## 一、现象（2026-08-26 实测）

- rod 线程运行后卡死在 `rt_schedule()` → HardFault → `while(1)`；
- RTT 打印（经 RTT_PRINTF 重定向）：

```
[RT_PRINTF] psr: 0x00000014
[RT_PRINTF] r04: 0xdeadbeef
[RT_PRINTF] r05: 0xdeadbeef
[RT_PRINTF] r06: 0xdeadbeef
[RT_PRINTF] r10: 0xdeadbeef
[RT_PRINTF] hard fault on thread: rod
```

- 特征：**不是第一次进 `rt_schedule()` 就崩，而是某次进 `rt_schedule()` 卡死**；`r04/r05/r06/r10 = 0xdeadbeef` 是栈毒化值（RT-Thread 初始化栈时填充 `0xdeadbeef`，说明寄存器现场已被未初始化/被踩的栈内存污染）。

---

## 二、根因分析

| 证据 | 指向 |
|---|---|
| `hard fault on thread: rod` | 崩在 rod 线程上下文 |
| `r04~r06/r10 = 0xdeadbeef` | 栈内存被踩穿，寄存器现场来自被破坏的栈区 |
| 不是首次 `rt_schedule()` 崩 | 栈在运行中逐渐写穿（rod 每 10ms 执行霍尔→位置→限位→状态全链路） |
| 进入 `rt_thread_sleep` 后 `rt_schedule()` 卡死 | 栈溢出破坏当前线程 TCB/链表，调度器访问非法内存 |

**结论：rod 线程栈 1024 字节不够用。** 其调用链含 `float` 位置计算（`RodPosition_Update`）、GPIO 读取、限位注入、状态机跳转、`ROD_PRINT`（SEGGER_RTT_printf 有较大的内部缓冲需求），且运行期有中断嵌套，1024 紧张。

---

## 三、原始栈 vs 修复后栈（对照表）

> "原始"= HardFault 时的 git HEAD（`702f0bf 新增 电源极性模块 led灯`）实测值；
> "修复后"= 当前工作区（未提交）实测值。

| 线程 | 原始栈（HEAD） | 修复后 | 定义位置 | 说明 |
|---|---|---|---|---|
| main | **2048** | **4096** | `rtconfig.h` `RT_MAIN_THREAD_STACK_SIZE` | 主线程：初始化+1s 采样打印+MSH |
| sys_sm | **1024** | **2048** | 原始在 `dev_sm_thread.c`，修复后移到 `Dev/dev_mgr/dev_model.h` `SYS_SM_THREAD_STACK` | 系统状态机事件线程 |
| rod | **1024** | **2048** | `Task/rod_task.h` `ROD_THREAD_STACK` | **本次 HardFault 元凶，1024→2048** |
| di | 1024 | 1024 | `Task/di_task.h`（宏从 .c 移到 .h） | DI 采集（2ms 极性扫描） |
| led | 1024 | 1024 | `Task/led_task.h`（宏从 .c 移到 .h） | LED 1s 翻转 |
| dev | 2048 | 2048 | `Dev/dev_registry.h` `DEV_THREAD_STACK_SIZE` | 设备管理线程 |
| finsh | 4096 | 4096 | `rtconfig.h` | MSH shell |
| idle | 256 | 256 | `rtconfig.h` | 空闲线程 |

---

## 四、修复内容（2026-08-26）

1. **`Task/rod_task.h`**：`ROD_THREAD_STACK` 1024 → **2048**（本次落实；注释记录原因）。
2. **`Dev/dev_mgr/dev_model.h`**：新增 `SYS_SM_THREAD_STACK 2048 / PRIO 22 / TICK 10`（原 1024 定义在 `dev_sm_thread.c`，移入 .h 并加大）。
3. **`rtconfig.h`**：`RT_MAIN_THREAD_STACK_SIZE` 2048 → **4096**。
4. **`rtconfig.h`**：开启 `RT_USING_STACK_CHECK 1`（配合已有的 `RT_USING_OVERFLOW_CHECK`），栈溢出时会打印 `thread:xxx stack overflow`，便于下次直接定位。
5. **`Task/led_task.h`**：修正 include guard 位置（`#define __LED_TASK_H__` 原本写在宏定义之后，移到 `#ifndef` 之后）。di_task.h 已确认正确。

---

## 五、验证方法

1. RT-Thread Studio 右键工程 **Refresh** → 重新编译 → 烧录；
2. 观察 RTT Viewer：
   - 若不再 HardFault，且 rod 任务正常跑（推杆状态打印/限位事件），修复成功；
   - 若仍有 `hard fault on thread: rod` 或新增 `rod stack overflow` 提示 → 继续加大 `ROD_THREAD_STACK`（如 3072/4096）。
3. 用 `list_thread`（MSH）看各线程 `max used`，可精确评估余量。

---

## 六、经验沉淀

- **RT-Thread 线程栈不是"够用就行"**：含 RTT 打印（SEGGER_RTT_printf 缓冲大）、float 计算、中断嵌套的线程，1024 极易爆；保守起步 2048。
- `0xdeadbeef` 是 RT-Thread 栈填充毒化值：寄存器/内存出现它 = 访问了未初始化栈区（栈溢出典型特征）。
- 栈检查宏：`RT_USING_STACK_CHECK` + `RT_USING_OVERFLOW_CHECK` 都开，才能在崩之前打出提示。
- 线程配置宏统一放 .h（用户规范），避免散落 .c。

---

## 七、待办

- [ ] 上板验证 rod 2048 是否足够；
- [ ] 若足够，把 `di/led` 是否也要保守加大评估一次（当前 1024 未见异常，暂不动）；
- [ ] 提交本次修复（git）。
