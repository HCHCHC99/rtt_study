# Flash 存储模块设计（移植版·双实例）

> 记录日期：2026-09-08（同日升级双实例并重构保存链路：Rod_Task 驱动 + 依赖注入）
> 更新：2026-09-20 A 块参数改**表驱动存储**（新增 §3.4）：记录 44B→84B，新增 param_list/param_set 命令与越界校验；同日新增过/欠压确认窗参数 volt_over_ms/volt_under_ms（记录 84B→92B，追加 97→89）；**布局变更，首次烧录需 param_erase**
> 状态：✅ 已实施；A 块扇区内循环 + 磨损换扇区已实测通过；B 块待上板验证
> 来源：裸机工程 `D:\HB_chuchai_v.6.0.4`（hc32f46x_flash / param_manager / App_Params），芯片同为 HC32F460
> 流程图：`ob/Flash慢块A流程图.canvas`、`ob/Flash快块B流程图.canvas`（Obsidian）

---

## 一、需求背景

为 RT-Thread 工程增加**参数掉电保存**能力，两类数据两种策略：

- **慢块 A（配置参数）**：母线电压阈值、电流传感器阈值等配置，改动极少 → 2 扇区
- **快块 B（推杆行程）**：欠压急停时保存停车位置，供上电恢复位置基准；欠压边沿仅存一次 → 10 扇区

**移植范围裁剪**（原 App_Params 为 Modbus 寄存器映射 + 实时数据 + 模拟，大部分不适用本项目，只移植存储内核）：

| 原文件 | 移植结果 | 位置 |
|---|---|---|
| hc32f46x_flash.c/.h | EFM 薄封装（重命名） | `Adp/hc32_drv_flash.c/.h` |
| param_manager.c/.h | 存储引擎（逻辑不动，适配 RTOS） | `Utils/param_manager.c/.h` |
| App_Params.c/.h | 记录定义 + 默认值 + 应用 | `Dev/dev_param/dev_param.c`（A）+ `dev_param_rod.c`（B，新建） |

---

## 二、三层架构与双实例

```
applications/main.c        上电调 Dev_Param_Init()（一次性，A/B 扫描加载的编排入口）
        │
Dev/dev_param/             应用层：双实例
  ├─ dev_param.c           慢块 A：ParamRecord_t(92B) 表驱动（s_param_table 单点描述，
  │                        defaults/apply/save/show 统一遍历，见 §3.4）
  │                        （兼编排角色：Init/EraseAll/param_show 统一调度 A+B）
  └─ dev_param_rod.c       快块 B：ParamStrokeRecord_t(24B) + 保存链路自包含
                           （Request/Poll 状态机）+ 恢复应用；不依赖 mySystem
        │
Utils/param_manager/       引擎层：顺序追加 + seq + 头尾魔数 + CRC32 + 回滚扫描 + 磨损均衡
        │                   多实例设计（Config + Runtime 由调用者持有），第二实例零修改接入
Adp/hc32_drv_flash         适配层：EFM 解锁/擦除/写字/读字，调用方无需关心解锁
        │
hc32_ll_efm.c（DDL）        已在构建中（无需改 .cproject）
```

---

## 三、Flash 布局与记录结构

### 3.1 存储区划分

| 实例 | 扇区 | 地址范围 | 记录大小 | 追加次数/擦 | 擦寿命/扇区 | 保存总寿命 |
|---|---|---|---|---|---|---|
| **A 慢块**（配置） | 62→61（2 个） | 0x7A000~0x7FFFF | 92B | 89 | 1 万擦 | 89 万/扇区 × 2 |
| **B 快块**（行程） | 60→51（10 个） | 0x66000~0x79FFF | 24B | **341** | 1 万擦 | 341 万/扇区 × 10 |

- 扇区号 = 绝对地址 / 0x2000（`PARAM_SECTOR_SIZE`）；两块无重叠；app 区可用至 0x65FFF（416KB），当前固件 ~82KB
- 换扇区方向均为倒序（secStart → secEnd 回卷）
- A/B 魔数不同（A: 0x55AA55AA/0xAA44AA44；B: 0x66AA66AA/0xAA66AA66）——扇区已隔离，区分魔数属零成本防御

### 3.2 B 块记录 ParamStrokeRecord_t（24 字节，pack(4)）

| 偏移 | 字段 | 说明 |
|---|---|---|
| 0 | head_magic | 0x66AA66AA |
| 4 | sequence_id | 每次保存 +1，回滚扫描选最大 |
| 8 | erase_count | 当前扇区擦写计数 |
| 12 | position_mm | 推杆当前行程 mm（float，可含容差负值） |
| 16 | checksum | CRC32 |
| 20 | tail_magic | 0xAA66AA66 |

- 头尾 20B 为引擎必需（魔数×2 + seq + erase_count + CRC），精简会丧失掉电写一半的检测能力，B 恰是掉电场景，不做精简
- **寿命换算：每 341 次保存 = 1 次扇区擦除**；每天 100 次欠压 ≈ 每年 107 次擦，单扇区可扛 ~93 年，寿命远非瓶颈（瓶颈是硬件擦写耐久）

### 3.3 引擎行为（param_manager，两实例共用）

- **追加写**：每次保存顺序写到 `curr_addr`，写完前移；seq 在写入前 +1，CRC 重算
- **扇区溢出**：`curr_addr + 记录大小 > 扇区尾` 时：`erase_count < 10000`（MAX_ERASE_LIFE）→ 原地擦除重写、erase_count+1；否则换下一扇区（倒序回卷）、擦新扇区、erase_count 重置 1
- **上电扫描**（Param_Init）：从 secStart 向 secEnd 逐扇区扫描，魔数 + CRC 全通过视为有效块，**seq 最大者加载**；全无效则写默认值到 secStart。加载后若剩余空间不足以再存一次 → **上电预擦除**（2026-09-23 新增）：就地擦除并把当前记录回写扇区头（erase_count+1 重算 CRC）——把边界擦除从掉电保存途中挪到满电上电期；该条件与 Save 溢出条件互补，正常"一次掉电一存"工况下掉电保存永远装得下，8ms 擦除不再发生在放电途中。加载指针指向下一空位（原指向记录本身，首次保存会先覆写失败再重试一次，已一并修正）
- **写失败重试**：最多 7 次，失败后写指针前移跳过坏点

### 3.4 A 块参数表驱动存储（2026-09-20）

**思路**：重构前 A 块加一个参数要同步改 5 处（ParamRecord_t 字段 / 默认值宏 / defaults 赋值 / apply 镜像 / save 回写 / show 打印），全是重复模式，漏改无任何告警。改为 `s_param_table[]` 单点描述：每参数一行 `ParamDesc_t`，defaults/apply/save/show 四处遍历统一走表。

**加参数 = 5 处机械动作**：
1. dev_param.h 默认值宏 `XXX_DFT` + 限值宏 `XXX_MIN/MAX`
2. `ParamRecord_t` 加字段
3. 消费结构加字段（或走钩子）
4. 表加一行
5. 表后编译期断言加一行

**ParamDesc_t 字段语义**：

| 字段 | 说明 |
|---|---|
| name | msh 参数名（param_set/param_list 用；拼错只能运行时发现，param_list 可查） |
| rec_off | 记录内偏移，一律 `offsetof(ParamRecord_t, 字段)`，禁手写数字 |
| ram_base / ram_off | 消费 RAM 镜像目标；`RT_NULL` 或 `&g_param_record` = **自镜像**（无独立消费 RAM，当前值就存在记录里，生效走钩子） |
| type | `PARAM_T_F32/U32/U16/U8`（dev_param.h），宽度由 `PARAM_TYPE_SIZE` 宏唯一决定 |
| def_f / def_u | 默认值（SetDefaults 回调经表填入） |
| min / max | param_set 允许范围（含边界），0/0=不限；**只做单参数上下限，跨字段约束（欠压<过压-迟滞、校准窗≤stroke）不校验** |
| apply_hook | 可选应用钩子，见下 |

**apply_hook（应用钩子）**：镜像搞不定的两类参数用：
- 消费变量是其他模块 static，拿不到地址 → 经公开 setter 应用（`motor_seq` → `Dev_MotorGpio_SetDirInvert()`）
- 自镜像组改值后要重算派生量 → 统一挂 `Param_HookRodApply`（9 个 rod 参数 → `Dev_Param_RodApply()` 写 RodPosition_t 并重算 pulse_to_mm）
- **必须幂等**：多行挂同一钩子会被重复调用（rod 组 9 行一次 Apply 连跑 9 次），钩子只允许"按当前值设状态"，禁止累加

**应用链（两阶段，`Param_TableApply`）**：阶段 1 record→消费 RAM 字段级镜像（size≤4 单指令原子，线程单写者）；阶段 2 跑钩子。触发点三个：上电 `Dev_Param_Init` 加载后 / `param_erase` 后 / `param_set` 热改即时。
时序契约：上电首次 Apply 时 rod 模块未 init，`Dev_Param_RodApply` 内 `s_rod_inited` 守卫直接跳过，实际应用由 `App_Model_Init` 末尾补做（与表驱动前时序一致）。

**三道防线**：
1. **编译期**：30 条 `PARAM_STATIC_ASSERT`（20 记录字段 + 9 消费字段 + 1 变量）逐行核对"表行 type 声明 ↔ 字段实际宽度"——type 标错、字段改名/改宽直接编译失败；另有记录 ≤256B 引擎上限 + 4 字节对齐两条老断言
2. **param_set 运行时**：越界**拒收**并打印 `[PARAM_WARNNING]`（自带 `====` 分隔线，仅出错时输出，开关 `PARAM_WARNNING_PRINT_EN`）
3. **上电加载**：Flash 旧值越界**仅告警不裁剪**（提示 param_erase），不替用户做主

**MSH 命令**：`param_list`（当前值/默认值/限值一行一参）、`param_set <name> <value>`（按名热改 → 钩子即时生效，`param_save` 持久化）。

**注意点 / 坑**：
- **改 ParamRecord_t 布局 = 换存储格式**：旧记录 CRC 不过 → 上电自动走默认值路径，不会加载错值；但布局变更（44B→84B→92B）后首次烧录必须 `param_erase` 清掉旧扇区数据
- 表驱动后断点调试不如直写直观：追 apply/save 行为要进表遍历跳一层间接
- `PARAM_TABLE_NUM` 是唯一留在 .c 的宏（依赖 static 表定义，无法上移 .h——开发规范 §11 特例）
- **编译顺序**：`Param_SetDefaults` 引用 `PARAM_TABLE_NUM`（宏依赖 `s_param_table`），函数定义必须放在表之后，否则 implicit declaration / undeclared（已踩，见 §八 第 8 条）
- volt/cur 组仍走"独立 RAM 镜像"路线：Save 时表反向回写 record（`Param_TableSave`），自镜像组天然跳过

---

## 四、RTOS / 构建适配决策

| # | 原实现 | 移植后 | 理由 |
|---|---|---|---|
| 1 | DDL 硬件 CRC | **软件 CRC32**（CRC-32/ISO-HDLC，poly 0xEDB88320） | hc32_ll_crc.c 被构建排除；软件实现零构建改动，结果与原硬件配置等价 |
| 2 | 裸机 `__disable_irq()/__enable_irq()` | `rt_hw_interrupt_disable()/enable()` | RTOS 下无条件开中断破坏嵌套状态 |
| 3 | 栈上 `tempBuf[256]` 硬编码 | 静态缓冲 + paramSize 上限 + 编译期断言 | 结构体超 256B 爆栈，编译期拦截 |
| 4 | `.ramfunc` 段 | **去除** | BUS_HOLD 下擦/写期间 CPU 取指自动停等，无需 ramfunc |
| 5 | 裸机打印宏 | `rtt_manager.h` 的 `FLASH_PRINT`（关）/ `PARAM_PRINT`+`RODP_PRINT`（开）/ `PARAM_WARNNING`（开，仅出错时） | A 块 `[PARAM]`、B 块 `[RODP]` 前缀；告警 `[PARAM_WARNNING]` 自带分隔线 |

**解锁流程**（hc32_drv_flash.c 内部完成）：擦/写前 `EFM_REG_Unlock → EFM_FWMC_Cmd(ENABLE) → EFM_SetBusStatus(EFM_BUS_HOLD) → EFM_ClearStatus`；操作后 `EFM_ClearStatus → EFM_FWMC_Cmd(DISABLE) → EFM_REG_Lock`。

**关键约束**：单 bank Flash，擦/写期间（BUS_HOLD）全系统停顿（纯写 ~0.3ms、擦 ~8ms）——A 块保存要求电机停止（`Dev_Param_Save` 检查任一仲裁轴 `MS_RUNNING` 即拒绝）；B 块欠压保存时机已保证电机刹车后（见第六节）。

---

## 五、对外接口

| 接口 | 块 | 说明 |
|---|---|---|
| `Dev_Param_Init()` | A+B | 上电扫 Flash 加载并应用（main 调一次，内部有一次性保护；内部调 `Dev_Param_RodInit`） |
| `Dev_Param_Save()` | A | 表驱动反向镜像（消费 RAM → 记录，`Param_TableSave`）并写入（电机运行中拒绝） |
| `Dev_Param_EraseAll()` | A+B | 擦除全部参数扇区并重写默认值 |
| `Dev_Param_RodSave(mm)` | B | 保存推杆行程（PollSave 延时到期后取值调用；也可直接调用） |
| `Dev_Param_RodGet(&mm)` | B | 读取 B 块行程值 |
| `Dev_Param_RodApply(pos)` | B | 恢复位置基准：`position_mm` + `CALIBRATED`；**并注入位置实例指针**（欠压保存的取值来源）；`App_Model_Init` 末尾调用；无有效块时跳过恢复 |
| `Dev_Param_RodSaveRequest()` | B | 欠压边沿保存请求（非阻塞、幂等；欠压边沿仅存一次） |
| `Dev_Param_RodPollSave()` | B | 保存状态机驱动（Rod_Task 10ms 调用）：请求后计 2 tick（≈20ms）取当前位置写入 |
| `Dev_Param_RodEraseAll()` | B | 擦 B 全部扇区重写默认值 |
| `Dev_Param_FillTest()` / `Dev_Param_RodFillTest(mm)` | A/B | 测试：连续保存填满当前扇区（B 需传行程值） |

**MSH 命令**：`param_show`（A+B 两块状态）/ `param_list`（A 块参数：当前值/默认值/限值）/ `param_set <name> <value>`（按名热改，越界拒收）/ `param_save`（A）/ `param_erase`（A+B）/ `dir_set <hall 0/1> <motor 0/1>`（相序设置并保存）

---

## 六、快块 B 的欠压保存链路（核心时序）

**依据**：实测欠压掉电到 MCU 不能工作 **270ms**，覆盖全链路 + 最坏擦除。

```text
1ms ISR 欠压窗口确认 → 置事件位
   ↓ 状态机线程（µs 级调度延迟）
Sys_State_Dispatch 欠压分支: Dev_Param_RodSaveRequest()（幂等置请求，边沿一次）
   ↓
sys_enter_emergency: EVT_ACT_HOLD + EMERGENCY_STOP（50/50 刹车，寄存器级）
                     [急停入口保持纯净，不掺 flash 代码]
   ↓（并行：Rod_Task 10ms 周期）
Dev_Param_RodPollSave(): 计 2 个 tick ≈ 20ms（等刹车滑行稳定，零线程阻塞）
   → 取当前位置（Apply 注入的实例指针）→ Dev_Param_RodSave()
   → 24B 写入（纯写 ~0.3ms；扇区满含擦 ~8ms）
─────────────────────────────────────────────
从欠压确认到写完 ≈ 20~30ms，余量 270ms
```

**设计决策记录**：
- **保存链路自包含 + 依赖注入**：请求（Request）、延时（Poll 计数）、执行（Save）全在 dev_param_rod.c 内；位置实例由 `RodApply` 注入，模块不摸 `mySystem`
- **Rod_Task 驱动**：行程数据所有者保存行程；tick 计数延时零阻塞（对比 `rt_thread_mdelay(20)` 占状态机线程 20ms、对比 rt_timer 回调阻塞软定时器链并引入异步竞态）
- **不用 ISR 直写**：擦 8ms 杀死 1ms 心跳链，收益仅 µs 级
- **欠压边沿仅存一次**（用户定：不做周期补存，磨损寿命优先；Request 幂等保证）
- **挂点用 Request 接口**而非 dev_state 静态标志/`error_code`：语义明确、不受其他故障覆盖影响；过流/推杆/手动急停不触发保存
- 兜底：写一半掉电 → CRC 失败 → 回滚旧块，最坏丢"最后一次"

**上电恢复两步**（时序关键：`Dev_Param_Init` 在 `App_Model_Init` 之前跑）：
1. `Dev_Param_RodInit`（main 内）：扫 Flash，值进 `s_rod_record`
2. `Dev_Param_RodApply(&mySystem.axis[0].position)`（`App_Model_Init` 末尾，`RodPosition_Init/SetParams` 之后）：写 `position_mm` + 置 `CALIBRATED`，霍尔增量在此基准上继续累加，**同时注入实例指针**；首次上电写默认值路径跳过恢复（`s_rod_defaults_written`），保持 NOT_CALIBRATED 走原校准流程

---

## 七、测试指南

**主循环测试开关**（main.c，volatile 全局，debug Expressions 直接改值）：

```text
慢块 A: savelabel = 1   修改 over_th(+0.5V 回绕 [23,27)) 并保存一组
        savelabel = 2   连续保存填满 A 当前扇区（~89 组）
快块 B: savelabel = 3   保存当前推杆行程一组（main 内组合 RodSave+取值）
        savelabel = 4   连续保存填满 B 当前扇区（~341 组）
```

**B 块验证流程**：
1. 上电看 `[RODP] defaults set`（首次）→ 推/转推杆
2. `savelabel=3` → `[RODP] saved: stroke=xxx.xmm seq=N addr=0x000780xx`（B 区首块 0x78000）
3. 复位 → `[RODP] loaded: stroke=xxx.xmm` + `[RODP] restored stroke=xxx.xmm (CALIBRATED)` → `param_show` 两块状态
4. 磨损换扇区模拟：`savelabel=4` 填满 → debug 改 `s_rod_record.erase_count=10000` → `savelabel=3` → 地址跳 0x00076000（sec 59）、erase 重置 1

### 已完成测试（2026-09-08）

| 测试项 | 方法 | 结果 |
|---|---|---|
| A 首次上电写默认值 | 上电看 `[PARAM] defaults set` | ✅ |
| A 扇区内循环追加 | savelabel=1 连续，seq 递增、地址 +44 | ✅ |
| A 扇区填充 | savelabel=2 | ✅ |
| A 磨损换扇区 | 溢出边缘 + erase_count=10000，地址跳 0x7A000（sec 61） | ✅ |
| A 复位回滚加载 | 重启后 `[PARAM] loaded` 最大 seq | ✅ |
| 编译 | Studio GCC 6 文件 0E/0W | ✅ |
| B 全流程（保存/恢复/欠压链路/磨损换扇区） | 待上板 | ⬜ |

---

## 八、注意事项 / 踩坑记录

1. **include 路径**：Studio GCC 的 `-I` 列表只有工程根与 applications 等，**没有 Utils/Dev**。跨目录 include 一律用目录前缀（`"applications/rtt_manager.h"`、`"Utils/param_manager.h"`、`"Dev/dev_mgr/dev_model.h"`——dev_model.h 在 dev_mgr/ 下），同目录文件才允许裸文件名。
2. **打印浮点**：阈值/行程是 float，打印按规范拆整型（`Param_VoltFmt`/`Rod_MmFmt` 拆为 `符号整数.1位小数`），不使用 `%f`。
3. **测试副作用**：A 块 `SaveTest` 改动 over_th 并持久化，测完 `param_erase` 恢复默认；B 块 erase 后行程归零且不接管校准（RAM 现值不动）。
4. **erase_count 在 RAM**：磨损判断读记录结构体字段（非 flash 回读），断电丢失最近一次扇区切换前的累计值，可接受。
5. **构建清单**：实际构建以 RT-Thread Studio 为准（`Dev/dev_param/SConscript` 为 `Glob('*.c')`，新 .c 自动发现）；Keil 副本工程用户不维护（打不开）。
6. **位置恢复的固有误差**：保存的位置略早于最终停住点（刹车滑行段未记账）；20ms tick 延时已缩小该误差，增量记账模式下偏差仅影响初始基准。
7. **依赖注入模式**：dev_param_rod.c 对位置模块只有"一个实例指针"依赖（`RodApply` 注入），对 `mySystem` 零依赖；测试钩子一律传参。
8. **表驱动编译顺序**（2026-09-20 踩）：`Param_SetDefaults` 引用 `PARAM_TABLE_NUM`（宏依赖 `s_param_table` 定义），函数必须放在表之后，否则 implicit declaration + conflicting types。当时因 include 路径报错中止编译，该问题**延迟两天才暴露**——教训：重构后必须完成一次全量编译并确认 0E 再收工，"改完了"≠"编译过了"。
