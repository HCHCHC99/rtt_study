# registry 升级实施记录

> 实施日期：2026-08-28  
> 设计依据：`md_record/模块运行设计.md` 第四节「registry 升级设计（B + C 混合）」  
> 本次只修改 `Dev/dev_registry.h`、`Dev/dev_registry.c`，并新增本记录文件。

## 改动内容

1. `SysModule_t` 按设计重排并扩展：
   - 新增 `void (*thread_entry)(void *param)`，作为 C 模式线程入口。
   - 新增 `uint16_t thread_stack`，保存独立线程栈大小，单位为字节。
   - `task` 与 `period_ms` 继续保留给 B 模式；`prio` 继续同时表示 B 模块排序优先级和 C 线程优先级。
   - 结构体字段顺序改为设计文档中的顺序：`name/init`、B 模式字段、C 模式字段、`prio/last_tick/enabled`。

2. 注册宏继续放在 `Dev/dev_registry.h`：
   - `SYS_MODULE_REGISTER(_name, _init, _task, _prio, _period)` 保持 B 模式参数顺序，初始化器同步到新的结构体布局。
   - 新增 `SYS_MODULE_REGISTER_THREAD(_name, _init, _thread, _prio, _stack)`，B 模式字段固定为 `RT_NULL/0`，C 模式字段写入线程入口和栈大小。
   - 两个宏只写 `.h`，未在 `.c` 增加新的宏定义。

3. `Dev_Registry_Add` 增加执行模型分发：
   - 保留原有的空指针/容量检查、重名检查、复制入表和注册打印。
   - 复制成功后按设计执行 `module->init()`；A、B、C 模式都走同一初始化入口。
   - 当 `module->thread_entry != RT_NULL` 时进入 C 模式，调用  
     `rt_thread_create(module->name, module->thread_entry, RT_NULL, module->thread_stack, module->prio, 10)`。
   - 线程创建成功后立即 `rt_thread_startup()`。
   - 创建失败时通过 `MAIN_D` 打印英文错误：`[DEV_REG] thread create failed: %s`，不打印中文。
   - `thread_entry == RT_NULL` 时保持原 B/A 模式路径，不创建线程。

4. `Dev_Registry_UpdateAll` 未改逻辑：
   - 继续先检查 `enabled` 和 `m->task == RT_NULL`。
   - C 模式模块 `task` 为空，会自然被跳过。
   - B 模式仍按 `period_ms` 与 `last_tick` 调用任务。

5. 未实际注册任何新模块：
   - `Dev_RegisterAll()` 中原有 ADC、电流、电压、极性、monitor 注册块保持不变。
   - C 模式注册块留给后续集成阶段统一添加。

## 验证结果

1. 新内容 grep 命中：
   - `Dev/dev_registry.h` 命中 `thread_entry`、`thread_stack`、`SYS_MODULE_REGISTER_THREAD`。
   - `Dev/dev_registry.h` 命中更新后的 `SYS_MODULE_REGISTER`。
   - `Dev/dev_registry.c` 命中 `if (module->thread_entry != RT_NULL)`、`rt_thread_create(module->name, ...)`、`rt_thread_startup` 和 `MAIN_D` 失败打印。
   - `Dev_Registry_UpdateAll` 中继续命中 `if (m->task == RT_NULL)` 跳过逻辑。

2. 旧内容残留 grep 无命中：
   - 旧初始化器 `{ #_name, _init, _task, _prio, _period, 0, 1 }` 无命中。
   - 旧结构体顺序特征 `task` 后紧跟 `prio`、`_task, _prio, _period, 0, 1` 均无命中。

3. armcc 编译验证：
   - 工具：`F:\Keil5\ARM\ARMCC\bin\armcc.exe`。
   - 版本：MDK Professional 5.33，ARM Compiler 5.06 update 7 (build 960)。
   - 已使用工程对应的 `--cpu Cortex-M4.fp --apcs=interwork --c99` 参数编译 `Dev/dev_registry.c`，其展开包含 `Dev/dev_registry.h`。
   - 结果：armcc 退出码 `0`，无错误输出。
   - 另外单独预处理 `Dev/dev_registry.h`，退出码 `0`，确认头文件与宏定义可正常展开。

## 当前工作区说明

- git 状态中还存在 `Dev/dev_act/dev_act.c` 删除与 `Dev/dev_act/dev_act.h` 修改；这是本次实施前已有工作区状态，未检查、未修改、未恢复。
- 本次实施写入的工程文件只有：
  - `Dev/dev_registry.h`
  - `Dev/dev_registry.c`
  - `md_record/registry升级-实施记录.md`
