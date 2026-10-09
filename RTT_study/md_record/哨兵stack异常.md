# 任务栈哨兵异常与 RT-Thread heap/MSP 栈重叠

> 记录日期：2026-08-28  
> 状态：heap/MSP 重叠已定位、已修复、已实测验证；后续任务栈压缩配置需长跑复测  
> 关联：本文件整合并替代 `task剩75bug.md`

## 1. 结论

这个问题的根因不是 LED/DI 等任务业务栈真的溢出，而是 GCC 版 BSP 初始化 RT-Thread heap 时，把链接脚本保留的 startup `.heap` 和 MSP `.stack` 区域也包含了进去。

RT-Thread 的动态线程栈来自 `rt_malloc()`。当某个线程栈被分配到 MSP 栈区域时，中断/MSP 栈数据会覆盖线程栈里的 `'#'` 哨兵。`Task_Stack_Monitor()` 从栈低地址向上扫描第一个非 `'#'` 字节，因此会把 MSP 栈残留误判成该线程的高水位，报出类似“只剩 75 字节”的假告警。

修复方向是让 RT-Thread heap 起点避开链接脚本保留的 `.heap` 和 `.stack` 区域。

## 2. 原始现象

最早 RTT 输出：

```text
[MAIN] [TASK_STACK] WARN led sp=0x1FFFF09C base=0x1FFFE954 size=2048 used=1973(96%)
```

按该输出计算：

```text
2048 - 1973 = 75 字节
```

当时还观察到几个现象：

1. 修改 `TASK_STACK_LED` 为不同值时，经常表现为“剩余量固定是 75 字节”。
2. 调换 `Di_Task_Start()` 和 `Led_Task_Start()` 启动顺序后，假高水位会从 LED 转移到先创建的 DI。
3. 调试器 Memory 窗口显示栈基址附近仍有 `'#'` 哨兵，但某些低地址出现孤立非哨兵数据。

这些现象说明它不像单纯的业务栈溢出：普通 LED/DI 业务栈需求不应该随线程创建顺序和总大小变化得如此有规律。

## 3. 关键定位过程

### 3.1 哨兵监控语义

`Task/task_stack.c` 中当前实现是：

```c
p = (rt_uint8_t *)t->stack_addr;
while (*p == '#')
{
    p++;
}
free_bytes = p - t->stack_addr;
```

Cortex-M 栈向下增长，RT-Thread 创建线程时会把整块栈填成 `'#'`。理论上低地址连续 `'#'` 数量表示历史最低剩余量。

但如果不是线程自己，而是 MSP、DMA、野指针或其他模块写入栈区域，监控无法区分写入者。只要低地址出现一个非 `'#'` 字节，后续大量区域即使仍是 `'#'`，也会被算成“已用”。

### 3.2 关闭 UART4 后现象变化

临时关闭 `BSP_USING_UART4` 后，假剩余量从 `75B` 变成 `156B`。

这说明关闭 UART4 本身不是直接修复，而是改变了 RT-Thread heap 的分配布局。`uart4` 设备相关分配减少后，后续线程栈地址整体移动，落到 MSP 栈区域中的位置也变化，因此哨兵污染点变化。

这个现象把怀疑方向从“某个 task 业务栈不够”转向“公共内存布局/heap 边界错误”。

### 3.3 map 对照定位

修改 `board.h` 前，`Debug/rtthread.map` 中：

```text
RT heap 起始 / __bss_end__ = 0x1FFFA9D0
.heap        = 0x1FFFA9D0 ~ 0x1FFFC9D0
.stack       = 0x1FFFC9D0 ~ 0x1FFFE9D0
__StackTop   = 0x1FFFE9D0
```

而 GCC 下原 `board.h` 定义：

```c
extern int __bss_end;
#define HEAP_BEGIN (&__bss_end)
#define HEAP_END   HC32_SRAM_END
```

这导致 `rt_system_heap_init()` 的范围从 `0x1FFFA9D0` 开始，实际包含了链接脚本保留的：

```text
newlib/startup heap：0x1FFFA9D0 ~ 0x1FFFC9D0
MSP stack       ：0x1FFFC9D0 ~ 0x1FFFE9D0
```

以某一版现象为例：

```text
led base      = 0x1FFFE87C
__StackTop    = 0x1FFFE9D0
重叠低地址区  = 0x1FFFE87C ~ 0x1FFFE9D0，共 0x154 = 340 字节
```

MSP 栈从 `__StackTop` 向下使用后，数据落在 LED 栈块低地址内。哨兵扫描从 `led base` 向上找到第一个非 `'#'`，于是得到错误的 `used=1892(92%)`。

当时 LED 的真实瞬时栈用量应按 `top - sp` 计算：

```text
led top = base + size = 0x1FFFF07C
led sp  = 0x1FFFF034
真实瞬时用量 = 0x48 = 72 字节
```

所以 `used=1892` 不是 LED 业务真实栈高水位。

## 4. 根因

问题的本质是 BSP 提供给 RT-Thread 的 heap 边界和 GCC linker 的内存保留区不一致。

在 Cortex-M 上：

```text
MSP：reset/startup、interrupt/exception 使用
PSP：RT-Thread 线程运行时使用
```

RT-Thread 线程栈来自 heap，但 MSP 栈是启动/中断栈，不属于 `rt_malloc()` 可分配区域。

原 `board.h` 的 GCC 分支使用 `__bss_end` 作为 RT-Thread heap 起点，而当前 `link.ld` 在 `.bss` 后又放了 `.heap` 和 `.stack`，因此 heap 覆盖了 MSP 栈。`rt_thread_create()` 本身没有错，heap 分配器也不知道这个范围内还有 linker 保留区。

### 4.1 修改前内存布局

本节地址来自修改 `board.h` 前的 `Debug/rtthread.map` 和实测日志。区间使用左闭右开表示。

```text
0x1FFF8000  RAM start
0x1FFF8928  .data end / .bss start
0x1FFFA9D0  .bss end
            startup/linker .heap start
            旧 RT-Thread HEAP_BEGIN
0x1FFFC9D0  startup/linker .heap end
            startup/linker MSP .stack start
0x1FFFE9D0  MSP .stack end / __StackTop
            旧 RT-Thread heap 内部地址，仍然是可分配区
0x20027000  RAM end / HC32_SRAM_END / 旧 RT-Thread HEAP_END
```

修改前逻辑范围：

```text
linker .heap : [0x1FFFA9D0, 0x1FFFC9D0)  8KB
linker .stack: [0x1FFFC9D0, 0x1FFFE9D0)  8KB
旧 RT heap   : [0x1FFFA9D0, 0x20027000)  0x2C630 = 181808B
```

因此旧 RT heap 包含整个 `.heap_stack` 区域，大小 16KB。运行时可见这些动态栈已经进入 MSP 栈区域：

```text
sys workq = 0x1FFFBB7C
tshell    = 0x1FFFC630
dev       = 0x1FFF D74C
sys_sm    = 0x1FFFDFE4
led       = 0x1FFFE87C
di        = 0x1FFFF114
rod       = 0x1FFFF5AC
```

其中 `led` 的栈块和 MSP 栈顶部重叠：

```text
led overlap = [0x1FFFE87C, 0x1FFFE9D0)，0x154 = 340B
```

### 4.2 修改后内存布局

`board.h` 修改后，linker 的 `.heap_stack` 物理位置不变，但 RT-Thread heap 的逻辑起点改变。

```text
0x1FFF8000  RAM start
0x1FFF8928  .data end / .bss start
0x1FFFA9D0  .bss end
            startup/linker .heap start
0x1FFFC9D0  startup/linker .heap end
            startup/linker MSP .stack start
0x1FFFE9D0  MSP .stack end / __StackTop
            新 RT-Thread HEAP_BEGIN
0x1FFFEAA0  修复后实测 main 线程栈 base
0x20027000  RAM end / HC32_SRAM_END / 新 RT-Thread HEAP_END
```

修改后逻辑范围：

```text
linker .heap : [0x1FFFA9D0, 0x1FFFC9D0)   8KB，不进入 RT heap
linker .stack: [0x1FFFC9D0, 0x1FFFE9D0)   8KB，不进入 RT heap
新 RT heap   : [0x1FFFE9D0, 0x20027000)   0x28630 = 165424B
```

修复后实测动态线程栈都在 `__StackTop` 之后：

```text
main      = 0x1FFFEAA0
sys workq = 0x1FFFFB7C
tshell    = 0x20000630
dev       = 0x2000174C
sys_sm    = 0x20001FE4
led       = 0x2000287C
di        = 0x20003114
rod       = 0x200035AC
```

`tidle0` 例外，它是静态 BSS 栈：

```text
tidle0 base = 0x1FFF89CC
```

简单对比：

```text
旧 RT heap = [0x1FFFA9D0, 0x20027000)，0x2C630 = 181808B，包含 MSP stack
新 RT heap = [0x1FFFE9D0, 0x20027000)，0x28630 = 165424B，避开 MSP stack
差值       = 0x4000 = 16384B
```

## 5. 最终修复

### 5.1 `board/board.h`

GCC 分支的 RT-Thread heap 起点改为 `__StackTop`：

```c
#else
/* Skip the startup-owned newlib heap and MSP stack reserved by link.ld. */
extern int __StackTop;
#define HEAP_BEGIN (&__StackTop)
#endif
```

修改后：

```text
RT heap 起点 = 0x1FFFE9D0 = __StackTop
MSP stack    = 0x1FFFC9D0 ~ 0x1FFFE9D0，不再进入 RT heap
```

`.config` 中也同步关闭了 `BSP_USING_UART4`：

```text
# CONFIG_BSP_USING_UART4 is not set
```

注意：关闭 UART4 只是定位过程中的隔离实验，不是本问题的根本修复。根本修复是 heap 起点修正。

### 5.2 修复后实测

说明：下面这份日志是 heap/MSP 重叠修复后的第一次验证数据。当时 `task_stack.h` 仍使用旧栈配置，主要用于确认哨兵假高水位已经消失。

修复后任务栈监控如下：

```text
rod       base=0x200035AC size=2048 used=168(8%)
di        base=0x20003114 size=1024 used=72(7%)
led       base=0x2000287C size=2048 used=72(3%)
sys_sm    base=0x20001FE4 size=2048 used=72(3%)
dev       base=0x2000174C size=2048 used=304(14%)
tshell    base=0x20000630 size=4096 used=500(12%)
sys workq base=0x1FFFFB7C size=2048 used=72(3%)
tidle0    base=0x1FFF89CC size=256  used=72(28%)
main      base=0x1FFFEAA0 size=4096 used=532(12%)
```

动态线程栈均分配到 `__StackTop = 0x1FFFE9D0` 之后。LED 恢复为合理的：

```text
used=72(3%)
```

不再出现 75B/156B 假高水位。

### 5.3 后续任务栈配置调整

heap 边界确认正确后，`Task/task_stack.h` 又做了后续调整，把非关键任务栈从临时验证值压缩回来，并把告警阈值提高到 90%。

当前 `task_stack.h` 配置：

| 线程/用途 | 宏 | 当前值 |
|---|---|---:|
| main | `TASK_STACK_MAIN` | 4096B |
| sys_sm | `TASK_STACK_SYS_SM` | 256B |
| dev | `TASK_STACK_DEV` | 512B |
| rod | `TASK_STACK_ROD` | 256B |
| di | `TASK_STACK_DI` | 256B |
| led | `TASK_STACK_LED` | 256B |
| tshell/finsh | `TASK_STACK_FINSH` | 1024B |
| tidle0 | `TASK_STACK_IDLE` | 256B |
| sys workq | `TASK_STACK_WORKQ` | 256B |
| timer | `TASK_STACK_TIMER` | 512B |

告警阈值：

```c
#define TASK_STACK_WARN_PCT   (90U)
```

登记表配置总和从旧配置的 `20224B` 降到 `7680B`。其中 idle 栈是静态 BSS，timer 软定时器线程当前未启用；动态创建的 main/sys_sm/dev/rod/di/led/tshell/workq 栈总配置从约 `19456B` 降到 `6912B`，粗略释放约 `12544B` heap。实际 heap 释放量还会受线程控制块、timer、allocator 元数据影响。

`main.c` 当前创建顺序调整为：

```text
Dev_Start()
Sys_Sm_Thread_Start()
Led_Task_Start()
Di_Task_Start()
Act_Arbitrator_Init()
Rod_Task_Start()
Task_Stack_Dump()
```

`Task_Stack_Monitor()` 现在每秒打印所有已登记线程的 `///////` 行；只有已用比例达到或超过 90% 时才额外输出 `WARN`。因此日志中没有 `WARN` 不代表没有打印监控数据。

注意：压缩后的任务栈配置还需要长跑复测。短时快照没有超过 90% 不能证明长期高水位安全；尤其是 `rod` 曾在旧记录中标注过 1024B 溢出，当前改为 256B 后必须重点观察 `WARN`、HardFault 和哨兵剩余量。

## 6. 效果与影响

修复效果：

1. RT-Thread heap 不再覆盖 MSP reserved stack。
2. 动态线程栈不会被 MSP 栈数据污染。
3. 哨兵栈监控恢复可信。
4. LED/DI 假“栈满”告警消失。
5. 线程创建顺序不再决定哪个任务出现假 75B 剩余。

影响：

1. RT-Thread heap 起点后移 16KB，可分配 heap 比原实现少 16KB。
2. 保留的 16KB 包括 startup `.heap` 8KB 和 MSP `.stack` 8KB。
3. 当前工程 heap 余量仍然充足，实测动态任务栈分配正常。
4. 后续压缩 `task_stack.h` 后，heap 压力进一步降低；但任务栈安全水位必须重新长跑确认。

## 7. 其他可行方案对比

### 方案 A：采用当前修复，`HEAP_BEGIN = &__StackTop`

做法：

```c
extern int __StackTop;
#define HEAP_BEGIN (&__StackTop)
```

优点：

1. 改动最小，只影响 GCC 分支。
2. 不修改 RT-Thread 内核和线程创建逻辑。
3. 不修改 linker 脚本，风险低。
4. 立即避开 `.heap` 和 `.stack`。
5. 便于回退和 review。

缺点：

1. 保留的 16KB 暂时不参与 RT-Thread heap。
2. `__StackTop` 作为 heap 起点可读性一般，必须依赖注释。
3. 如果以后 linker 布局变化，例如 `.stack` 移到 RAM 顶部，这里也要同步调整。

### 方案 B：调整 linker 脚本，把 MSP 栈固定到 RAM 顶部

思路：

```text
RAM 顶部预留 MSP stack
RT heap 从 .bss/newlib heap 结束后开始
RT heap 结束 = __StackLimit
```

并增加 linker assert，保证：

```text
heap end == stack limit
stack top <= RAM end
```

优点：

1. 这是常见嵌入式内存布局：普通 heap/data 在低地址，MSP 栈在高地址。
2. MSP 栈位置固定，不受 RT-Thread heap 分配影响。
3. 内存职责最清晰。
4. 可以通过 linker assert 在链接期发现重叠。
5. 长期扩展性更好。

缺点：

1. 需要修改 linker 脚本，改动面比 `board.h` 大。
2. 所有线程、对象、堆块地址都会变化，历史调试地址失效。
3. 需要重新验证 flash/RAM、启动文件、调试脚本、RAM region。
4. HC32 的主 SRAM/retention RAM 区域划分需要额外确认。

### 方案 C：只改 linker 符号，不改 `board.h`

思路是保留当前 linker 布局，但让 BSP 使用的符号指向 `__StackTop`：

```ld
__bss_end__ = _ebss;      /* startup 清 BSS 仍使用这个符号 */
__bss_end   = __StackTop; /* 仅供 board.h/RT-Thread heap 使用 */
```

优点：

1. 不修改 `board.h`。
2. `board.c` 现有代码不用变。
3. 改动行数少。

缺点：

1. `__bss_end` 的语义变成谎言，实际不是 BSS 末尾。
2. 后续维护者容易误解，甚至把其他分配器又接回错误边界。
3. 隐藏了真实设计意图，不如直接改 `HEAP_BEGIN` 清晰。
4. 依赖 `__bss_end__` 和 `__bss_end` 这两个相似符号不被误改。

因此只把它作为“不能修改 board.h 时的过渡方案”，不推荐作为长期方案。

### 方案 D：所有任务栈改为静态分配

思路是使用 `rt_thread_init()` 加静态数组，避免线程栈来自 `rt_malloc()`。

优点：

1. 任务栈地址确定。
2. 不依赖动态 heap。
3. 适合安全关键或长期运行系统。

缺点：

1. 需要修改所有任务创建代码。
2. RT-Thread 组件、finsh、IPC、device 等仍可能使用 heap，无法消除所有分配。
3. 增加静态 RAM 规划成本。
4. 不能替代正确的 heap 边界。

## 8. 后续规则

1. 新增 linker 保留区时，必须同步检查 `HEAP_BEGIN` / `HEAP_END`。
2. 动态线程栈地址应与 `__StackTop`、`__StackLimit` 对照，不能只看业务代码栈大小。
3. `Task_Stack_Monitor()` 的哨兵结果适合监控任务栈，但不能单独作为越界写入者的定位证据。
4. 若以后启用 MSP/heap 相关 linker 优化，应重新做一次 map 对照和长跑验证。
5. 任务栈大小只允许在 `Task/task_stack.h` 统一调整；当前告警阈值为 90%。
6. 调整任务栈后必须 clean/rebuild、查看 `Task_Stack_Dump()`，并长跑确认没有 `WARN`、HardFault、业务异常。
