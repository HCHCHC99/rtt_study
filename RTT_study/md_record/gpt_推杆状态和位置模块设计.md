\## 推杆位置模块与推杆状态模块 —— 最终设计方案



\---



\## 一、系统上下文



本模块位于一个多轴推杆控制系统之中，该系统基于RT-Thread实时操作系统，采用事件驱动架构。整体系统由以下层级构成：



\*\*第一层为物理层\*\*，包括电机霍尔编码器用于采集电机转动步数、上下限位霍尔开关用于检测推杆是否到达机械端点、以及电机驱动电路用于控制电机正反转和PWM占空比。



\*\*第二层为设备抽象层\*\*，包括之前已实现的ADC设备用于采集电压电流、电源极性检测设备用于判断正负极接线状态、以及后续需要实现的电机PWM驱动设备。



\*\*第三层为数据处理与仲裁层\*\*，这是我们此次设计的核心所在。该层包含三个并行的功能模块：推杆位置模块负责将霍尔脉冲累积量换算为毫米单位的机械行程并管理位置校准；推杆状态模块负责根据当前位置和运动方向判定推杆处于停止、伸出中、缩回中、到位或故障等行为状态；电机仲裁系统模块负责综合各路输入源（电源极性、过压欠压、过流保护、限位信号）决策电机最终允许的运动方向。三者之间的关系是位置模块为状态模块提供位置数据，状态模块为仲裁系统提供限位事件，仲裁系统为状态模块提供方向指令，形成完整的闭环。



\*\*第四层为应用层\*\*，包括系统主状态机（管理系统上电、运行、故障、急停等宏观状态）和用户命令接口（用于手动控制或远程控制推杆伸缩）。



本模块在整个系统中的定位是：\*\*介于硬件抽象层与电机仲裁层之间的中间件\*\*，负责将原始的霍尔脉冲和限位开关信号，加工成语义明确的推杆位置和推杆状态信息，供上层决策使用。



\---



\## 二、整体设计思路



本设计遵循\*\*单一职责原则\*\*和\*\*接口隔离原则\*\*，将推杆的位置感知与行为状态判定解耦为两个独立的模块。



\*\*位置模块（Rod Position Module）\*\* 的职责是回答"推杆在哪里"这个问题。它接收电机霍尔编码器的脉冲增量作为输入，结合减速比和丝杆导程等物理参数将脉冲数换算为毫米单位的机械行程。它维护当前位置值、校准状态（是否已建立绝对位置基准）、以及一个动态计算的校准允许标志（指示当前位置是否处于限位附近的校准容差区域内，允许利用限位开关重新修正位置）。该模块不关心推杆是在伸出还是缩回，也不关心电机当前是否在运行，它只专注于位置的计算和校准管理。



\*\*状态模块（Rod State Module）\*\* 的职责是回答"推杆当前处于什么运行状态"这个问题。它接收来自仲裁系统的方向指令（正向伸出、反向缩回或停止）作为运动意图输入，从位置模块读取当前位置和校准区标志作为位置感知输入，从GPIO读取上下限位霍尔的实际电平作为物理限位输入。综合这三路信息，状态模块通过一个有限状态机判定当前状态属于停止、伸出中、伸出到限位、伸出故障、缩回中、缩回到限位、缩回故障等枚举值之一。同时，状态模块负责在检测到推杆到达限位时，通过事件组向仲裁系统发送限位事件，仲裁系统据此在对应方向的阻塞队列中添加限位设备ID，从而阻止电机继续向限位方向运动。



\*\*两个模块的关系是组合而非继承\*\*。轴对象（Axis\_t）同时包含一个位置模块实例和一个状态模块实例，状态模块持有位置模块的指针以读取位置数据，但位置模块不依赖状态模块。数据流向为单向：霍尔脉冲输入→位置模块→位置数据→状态模块→限位事件→仲裁系统。



\*\*与电机仲裁系统的关系\*\*是松耦合的事件驱动。状态模块不直接调用任何电机控制接口，也不关心仲裁系统是否真的执行了停止动作。它只负责在限位触发时发送事件，仲裁系统根据自身规则决定如何处理该事件。这种设计使得状态模块可以独立测试和替换，也使得仲裁规则可以灵活调整而不影响状态感知逻辑。



\*\*与系统状态机的关系\*\*是间接联动。系统状态机控制设备的宏观使能状态（如通过EVT\_ACT\_WORK\_ENABLE使能所有轴的工作），推杆状态模块在此基础上独立运行。如果系统处于急停或故障状态，系统状态机通过EVT\_ACT\_HOLD事件通知仲裁系统停止所有轴，推杆状态模块会感知到方向指令变为停止并相应更新状态，但模块本身不处理系统级的安全逻辑。



\*\*位置校准的核心逻辑\*\*分为两个阶段。上电初始阶段，推杆尚未建立绝对位置基准，此时位置模块的已校准标志为假，状态模块处于未知状态。系统在此阶段会给推杆一个缩回指令，推杆向缩回方向运动直至触发下限位霍尔，位置模块检测到限位触发且允许校准标志为真（首次校准的允许标志独立于动态校准区判断），于是将当前位置重置为0毫米并将已校准标志设为真，状态模块随之退出未知状态进入正常状态机运转。正常运行阶段，推杆在校准区内运动时，当前位置可能在限位理论位置附近有累计误差，位置模块通过实时比较当前位置与限位理论值是否在容差范围内来动态更新校准允许标志，当推杆到达限位且校准允许标志为真时，位置模块将当前位置重置为限位理论值（下限为0毫米，上限为总行程毫米），消除累计误差，实现软件层面的闭环修正。



\---



\## 三、模块在整个系统中的位置示意图



```

┌─────────────────────────────────────────────────────────────────────────────┐

│                          系统主状态机 (sys\_sm)                             │

│     RUN → EVT\_ACT\_WORK\_ENABLE     FAULT/EMERGENCY → EVT\_ACT\_HOLD          │

└──────────────────────────────┬──────────────────────────────────────────────┘

&#x20;                              │

&#x20;                              ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                        轴事件组 (axis.evt\_act)                             │

│            接收：WORK\_ENABLE / WORK\_DISABLE / HOLD / POLARITY\_\*            │

└──────────────────────────────┬──────────────────────────────────────────────┘

&#x20;                              │

&#x20;                              ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                       电机仲裁系统 (Actuator Arb)                          │

│    输入：电源极性(allow\_fwd/allow\_rev) 过压过流(block\_fwd/block\_rev)      │

│         限位事件(block\_fwd/block\_rev)                                     │

│    输出：方向指令 (FWD / REV / STOP) + 占空比                             │

└──────────────────────────────┬──────────────────────────────────────────────┘

&#x20;                              │ 方向指令 (dir\_from\_arbit)

&#x20;                              ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                      推杆状态模块 (Rod State)                              │

│  ┌─────────────────────────────────────────────────────────────────────┐   │

│  │  状态机：STOP / EXTENDING / EXTEND\_LIMIT / EXTEND\_FAULT            │   │

│  │          RETRACTING / RETRACT\_LIMIT / RETRACT\_FAULT               │   │

│  │  输入：方向指令 + 位置数据 + 限位霍尔电平                          │   │

│  │  输出：状态枚举 + 限位事件 (EVT\_ROD\_LIMIT\_EXTEND/RETRACT)         │   │

│  └───────────────────────────────────────┬─────────────────────────────┘   │

│                                          │ 读取位置数据                    │

└──────────────────────────────────────────┼──────────────────────────────────┘

&#x20;                                          │

&#x20;                                          ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                      推杆位置模块 (Rod Position)                           │

│  ┌─────────────────────────────────────────────────────────────────────┐   │

│  │  位置计算：霍尔脉冲→减速比→丝杆位移→当前位置(mm)                    │   │

│  │  校准管理：已校准标志 / 校准允许标志 / 校准区判断                   │   │

│  │  输入：霍尔脉冲增量 + 限位触发事件 + 校准参数                      │   │

│  │  输出：当前位置 / 校准状态 / 校准区标志                            │   │

│  └─────────────────────────────────────────────────────────────────────┘   │

│                          ▲                          ▲                       │

└──────────────────────────┼──────────────────────────┼───────────────────────┘

&#x20;                          │                          │

&#x20;                   霍尔脉冲增量                  限位霍尔触发

&#x20;                   (编码器中断)                  (GPIO中断/轮询)

&#x20;                          │                          │

&#x20;                          ▼                          ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                          硬件层 (Hardware)                                 │

│              电机霍尔编码器    下限位霍尔开关    上限位霍尔开关             │

└─────────────────────────────────────────────────────────────────────────────┘

```



\---



\## 四、推杆位置模块关键结构体与枚举



\### 4.1 位置校准状态枚举



```c

/\*\*

&#x20;\* @brief 位置校准状态

&#x20;\*/

typedef enum

{

&#x20;   POSITION\_NOT\_CALIBRATED = 0,    /\* 未建立绝对位置基准（上电初始态） \*/

&#x20;   POSITION\_CALIBRATED             /\* 已建立绝对位置基准 \*/

} PositionCalibState\_t;

```



该枚举表示推杆是否已完成首次校准，建立了可信的绝对位置。上电时初始为未校准，第一次到达限位并成功重置位置后切换为已校准。



\### 4.2 推杆位置模块结构体



```c

/\*\*

&#x20;\* @brief 推杆位置模块结构体

&#x20;\* @note  每个推杆轴独立拥有一个实例

&#x20;\*/

typedef struct

{

&#x20;   /\* ===== 当前位置 ===== \*/

&#x20;   float position\_mm;              /\* 当前位置（毫米） \*/



&#x20;   /\* ===== 校准状态 ===== \*/

&#x20;   PositionCalibState\_t calib\_state;   /\* 是否已建立绝对位置基准 \*/

&#x20;   bool calib\_pending;                 /\* 等待首次校准标志（上电时置位，首次到位清除） \*/

&#x20;   bool calib\_allowed;                 /\* 当前是否允许重新校准（动态判断） \*/



&#x20;   /\* ===== 校准区标志（供状态模块读取） ===== \*/

&#x20;   bool in\_calib\_zone\_min;             /\* 当前位置在下限校准区内 \*/

&#x20;   bool in\_calib\_zone\_max;             /\* 当前位置在上限校准区内 \*/



&#x20;   /\* ===== 物理参数（配置项） ===== \*/

&#x20;   float stroke\_mm;                    /\* 推杆总行程（毫米），例如100.0f \*/

&#x20;   float reduction\_ratio;              /\* 减速比（电机转数 : 丝杆转数） \*/

&#x20;   float pulse\_to\_mm;                  /\* 单位霍尔脉冲对应的位移（毫米/脉冲） \*/

&#x20;   float calib\_tolerance\_mm;           /\* 校准容差（毫米），例如3.0f \*/



&#x20;   /\* ===== 限位霍尔状态 ===== \*/

&#x20;   bool min\_limit\_triggered;           /\* 下限位霍尔当前触发状态 \*/

&#x20;   bool max\_limit\_triggered;           /\* 上限位霍尔当前触发状态 \*/



&#x20;   /\* ===== 调试信息 ===== \*/

&#x20;   int32\_t total\_pulses;               /\* 累计霍尔脉冲数（从上次校准起算） \*/

} RodPosition\_t;

```



该结构体包含了位置模块所有的运行状态和配置参数。其中 `position\_mm` 是核心输出数据，`calib\_state` 和 `calib\_allowed` 共同决定位置的可信度，`in\_calib\_zone\_min` 和 `in\_calib\_zone\_max` 是状态模块判定限位到达的关键依据。所有物理参数应在系统初始化时通过配置接口写入。



\---



\## 五、推杆状态模块关键结构体与枚举



\### 5.1 推杆状态枚举



```c

/\*\*

&#x20;\* @brief 推杆运行状态枚举

&#x20;\*/

typedef enum

{

&#x20;   ROD\_STATE\_STOP = 0,         /\* 停止（电机无方向指令，或已停止） \*/

&#x20;   ROD\_STATE\_EXTENDING,        /\* 伸出中（正在向正方向运动） \*/

&#x20;   ROD\_STATE\_EXTEND\_LIMIT,     /\* 伸出到限位（到达上限位，停止运动） \*/

&#x20;   ROD\_STATE\_EXTEND\_FAULT,     /\* 伸出故障（伸出过程中发生异常） \*/

&#x20;   ROD\_STATE\_RETRACTING,       /\* 缩回中（正在向负方向运动） \*/

&#x20;   ROD\_STATE\_RETRACT\_LIMIT,    /\* 缩回到限位（到达下限位，停止运动） \*/

&#x20;   ROD\_STATE\_RETRACT\_FAULT,    /\* 缩回故障（缩回过程中发生异常） \*/

&#x20;   ROD\_STATE\_UNKNOWN           /\* 未知状态（未校准，或状态机尚未初始化） \*/

} RodState\_t;

```



该枚举覆盖了推杆所有正常和异常的工作状态。正常状态包括停止、伸出中、缩回中、伸出到限位、缩回到限位，异常状态包括伸出故障和缩回故障，初始上电且未校准时为未知状态。



\### 5.2 推杆方向枚举



```c

/\*\*

&#x20;\* @brief 推杆运动方向

&#x20;\*/

typedef enum

{

&#x20;   ROD\_DIR\_STOP = 0,   /\* 停止 \*/

&#x20;   ROD\_DIR\_FWD = 1,    /\* 正向（伸出） \*/

&#x20;   ROD\_DIR\_REV = -1    /\* 反向（缩回） \*/

} RodDirection\_t;

```



该枚举用于表示运动意图和实际运动方向，与电机仲裁系统的输出方向保持语义一致。



\### 5.3 推杆状态模块结构体



```c

/\*\*

&#x20;\* @brief 推杆状态模块结构体

&#x20;\* @note  每个推杆轴独立拥有一个实例，持有位置模块的引用

&#x20;\*/

typedef struct

{

&#x20;   /\* ===== 当前状态 ===== \*/

&#x20;   RodState\_t state;               /\* 当前状态 \*/

&#x20;   RodState\_t prev\_state;          /\* 上一状态（用于检测跳变） \*/



&#x20;   /\* ===== 当前方向 ===== \*/

&#x20;   RodDirection\_t direction;       /\* 当前运动方向（来自仲裁系统） \*/



&#x20;   /\* ===== 位置模块引用（只读访问） ===== \*/

&#x20;   const RodPosition\_t \*position;  /\* 指向位置模块的只读指针 \*/



&#x20;   /\* ===== 限位霍尔状态（直接来自GPIO） ===== \*/

&#x20;   bool min\_limit\_switch;          /\* 下限位霍尔电平（true=触发） \*/

&#x20;   bool max\_limit\_switch;          /\* 上限位霍尔电平（true=触发） \*/



&#x20;   /\* ===== 故障管理 ===== \*/

&#x20;   uint32\_t fault\_code;            /\* 当前故障码（0=无故障） \*/

&#x20;   bool external\_fault;            /\* 外部注入故障标志 \*/



&#x20;   /\* ===== 运动监控（超时与堵转检测） ===== \*/

&#x20;   uint32\_t move\_start\_tick;       /\* 本次运动开始时间戳（ms） \*/

&#x20;   uint32\_t move\_timeout\_ms;       /\* 运动超时阈值（ms），0表示禁用 \*/

&#x20;   float min\_velocity\_thresh;      /\* 最小速度阈值（mm/s），低于此值判定为堵转 \*/

&#x20;   uint32\_t stall\_start\_tick;      /\* 堵转开始时间戳（ms） \*/



&#x20;   /\* ===== 事件发送状态（防重复发送） ===== \*/

&#x20;   bool limit\_fwd\_sent;            /\* 是否已发送正向限位事件 \*/

&#x20;   bool limit\_rev\_sent;            /\* 是否已发送反向限位事件 \*/



&#x20;   /\* ===== 统计信息 ===== \*/

&#x20;   uint32\_t extend\_count;          /\* 伸出动作次数累计 \*/

&#x20;   uint32\_t retract\_count;         /\* 缩回动作次数累计 \*/

&#x20;   uint32\_t fault\_count;           /\* 故障次数累计 \*/

&#x20;   uint32\_t limit\_reach\_count;     /\* 限位到达次数累计 \*/

} RodStateModule\_t;

```



该结构体通过 `position` 指针持有位置模块的只读引用，实现了两个模块间的松耦合。状态模块同时维护限位霍尔电平（来自GPIO直接读取）和从位置模块读取的校准区标志，两者共同参与状态判定。故障管理包含故障码和外部故障注入两种机制，便于调试和系统集成。运动监控参数用于检测运动超时和堵转，增强了系统的安全性和可靠性。



\---



\## 六、关键代码设计思路



\### 6.1 位置模块核心更新函数



位置模块的更新函数由周期性任务调用（建议周期为10ms），每次调用传入霍尔脉冲的增量值（可正可负，正表示正向转动，负表示反向转动）：



```c

void RodPosition\_Update(RodPosition\_t \*pos, int32\_t delta\_pulses)

{

&#x20;   /\* 1. 脉冲增量 → 机械位移增量（毫米） \*/

&#x20;   float delta\_mm = (float)delta\_pulses \* pos->pulse\_to\_mm;

&#x20;   pos->position\_mm += delta\_mm;



&#x20;   /\* 2. 限位保护：防止累计误差导致位置溢出校准区判断范围 \*/

&#x20;   if (pos->position\_mm > pos->stroke\_mm + pos->calib\_tolerance\_mm \* 2) {

&#x20;       pos->position\_mm = pos->stroke\_mm + pos->calib\_tolerance\_mm \* 2;

&#x20;   }

&#x20;   if (pos->position\_mm < -pos->calib\_tolerance\_mm \* 2) {

&#x20;       pos->position\_mm = -pos->calib\_tolerance\_mm \* 2;

&#x20;   }



&#x20;   /\* 3. 更新校准区标志 \*/

&#x20;   pos->in\_calib\_zone\_min = (pos->position\_mm >= -pos->calib\_tolerance\_mm) \&\&

&#x20;                            (pos->position\_mm <= pos->calib\_tolerance\_mm);

&#x20;   pos->in\_calib\_zone\_max = (pos->position\_mm >= pos->stroke\_mm - pos->calib\_tolerance\_mm) \&\&

&#x20;                            (pos->position\_mm <= pos->stroke\_mm + pos->calib\_tolerance\_mm);



&#x20;   /\* 4. 动态更新校准允许标志 \*/

&#x20;   if (pos->calib\_state == POSITION\_NOT\_CALIBRATED) {

&#x20;       /\* 首次校准：只要限位触发即可校准，不受动态规则限制 \*/

&#x20;       pos->calib\_allowed = true;

&#x20;   } else {

&#x20;       /\* 已校准后的正常运行：仅在校准区内允许重新校准 \*/

&#x20;       pos->calib\_allowed = pos->in\_calib\_zone\_min || pos->in\_calib\_zone\_max;

&#x20;   }

}

```



该函数的逻辑要点是：位置累加后立即进行限位保护防止数值溢出，然后同步更新两个校准区标志，最后根据校准状态决定校准允许标志的值。首次校准时始终允许，已校准后仅在校准区内允许。



\### 6.2 位置模块限位处理函数



当限位霍尔触发时，由GPIO中断或轮询任务调用以下函数：



```c

void RodPosition\_OnMinLimit(RodPosition\_t \*pos, bool triggered)

{

&#x20;   pos->min\_limit\_triggered = triggered;



&#x20;   /\* 下限位触发且允许校准 → 重置位置为0 \*/

&#x20;   if (triggered \&\& pos->calib\_allowed) {

&#x20;       pos->position\_mm = 0.0f;

&#x20;       pos->calib\_state = POSITION\_CALIBRATED;

&#x20;       pos->calib\_pending = false;      /\* 清除首次校准等待标志 \*/

&#x20;   }

}



void RodPosition\_OnMaxLimit(RodPosition\_t \*pos, bool triggered)

{

&#x20;   pos->max\_limit\_triggered = triggered;



&#x20;   /\* 上限位触发且允许校准 → 重置位置为总行程 \*/

&#x20;   if (triggered \&\& pos->calib\_allowed) {

&#x20;       pos->position\_mm = pos->stroke\_mm;

&#x20;       pos->calib\_state = POSITION\_CALIBRATED;

&#x20;       pos->calib\_pending = false;

&#x20;   }

}

```



限位处理函数的核心逻辑是：只有在校准允许标志为真时才执行位置重置，避免因霍尔信号抖动或误触发导致位置错乱。重置成功后同时更新校准状态并清除等待标志。



\### 6.3 状态模块核心更新函数



状态模块的更新函数与位置模块在同一周期任务中调用（建议10ms），每次传入仲裁系统的方向指令：



```c

void RodState\_Update(RodStateModule\_t \*sm, RodDirection\_t dir, uint32\_t current\_tick)

{

&#x20;   /\* 1. 保存上一状态 \*/

&#x20;   sm->prev\_state = sm->state;



&#x20;   /\* 2. 读取位置模块信息 \*/

&#x20;   const RodPosition\_t \*pos = sm->position;



&#x20;   /\* 3. 检测外部故障 \*/

&#x20;   if (sm->external\_fault) {

&#x20;       sm->state = (sm->direction == ROD\_DIR\_FWD) ? 

&#x20;                   ROD\_STATE\_EXTEND\_FAULT : ROD\_STATE\_RETRACT\_FAULT;

&#x20;       sm->fault\_code |= (1U << 0);

&#x20;       goto update\_done;

&#x20;   }



&#x20;   /\* 4. 未校准 → UNKNOWN \*/

&#x20;   if (pos->calib\_state == POSITION\_NOT\_CALIBRATED) {

&#x20;       sm->state = ROD\_STATE\_UNKNOWN;

&#x20;       sm->direction = ROD\_DIR\_STOP;

&#x20;       goto update\_done;

&#x20;   }



&#x20;   /\* 5. 保存当前方向（来自仲裁系统） \*/

&#x20;   sm->direction = dir;



&#x20;   /\* 6. 状态机主逻辑 \*/

&#x20;   switch (sm->state) {

&#x20;       case ROD\_STATE\_STOP:

&#x20;           if (dir == ROD\_DIR\_FWD) {

&#x20;               sm->state = ROD\_STATE\_EXTENDING;

&#x20;               sm->move\_start\_tick = current\_tick;

&#x20;           } else if (dir == ROD\_DIR\_REV) {

&#x20;               sm->state = ROD\_STATE\_RETRACTING;

&#x20;               sm->move\_start\_tick = current\_tick;

&#x20;           }

&#x20;           break;



&#x20;       case ROD\_STATE\_EXTENDING:

&#x20;           if (dir != ROD\_DIR\_FWD) {

&#x20;               /\* 方向改变 → 停止或反向 \*/

&#x20;               sm->state = (dir == ROD\_DIR\_STOP) ? ROD\_STATE\_STOP : ROD\_STATE\_RETRACTING;

&#x20;           } else if (pos->in\_calib\_zone\_max || sm->max\_limit\_switch) {

&#x20;               /\* 到达上限位 \*/

&#x20;               sm->state = ROD\_STATE\_EXTEND\_LIMIT;

&#x20;               sm->limit\_reach\_count++;

&#x20;               if (!sm->limit\_fwd\_sent) {

&#x20;                   RodState\_SendLimitEvent(ROD\_DIR\_FWD, true);

&#x20;                   sm->limit\_fwd\_sent = true;

&#x20;               }

&#x20;           } else if (RodState\_CheckTimeout(sm, current\_tick) ||

&#x20;                      RodState\_CheckStall(sm, current\_tick)) {

&#x20;               sm->state = ROD\_STATE\_EXTEND\_FAULT;

&#x20;               sm->fault\_count++;

&#x20;           }

&#x20;           break;



&#x20;       case ROD\_STATE\_RETRACTING:

&#x20;           if (dir != ROD\_DIR\_REV) {

&#x20;               sm->state = (dir == ROD\_DIR\_STOP) ? ROD\_STATE\_STOP : ROD\_STATE\_EXTENDING;

&#x20;           } else if (pos->in\_calib\_zone\_min || sm->min\_limit\_switch) {

&#x20;               sm->state = ROD\_STATE\_RETRACT\_LIMIT;

&#x20;               sm->limit\_reach\_count++;

&#x20;               if (!sm->limit\_rev\_sent) {

&#x20;                   RodState\_SendLimitEvent(ROD\_DIR\_REV, true);

&#x20;                   sm->limit\_rev\_sent = true;

&#x20;               }

&#x20;           } else if (RodState\_CheckTimeout(sm, current\_tick) ||

&#x20;                      RodState\_CheckStall(sm, current\_tick)) {

&#x20;               sm->state = ROD\_STATE\_RETRACT\_FAULT;

&#x20;               sm->fault\_count++;

&#x20;           }

&#x20;           break;



&#x20;       /\* 限位状态的退出条件 \*/

&#x20;       case ROD\_STATE\_EXTEND\_LIMIT:

&#x20;           if (dir == ROD\_DIR\_REV) {

&#x20;               sm->state = ROD\_STATE\_RETRACTING;

&#x20;               sm->limit\_fwd\_sent = false;

&#x20;           } else if (dir == ROD\_DIR\_STOP) {

&#x20;               sm->state = ROD\_STATE\_STOP;

&#x20;           }

&#x20;           break;



&#x20;       case ROD\_STATE\_RETRACT\_LIMIT:

&#x20;           if (dir == ROD\_DIR\_FWD) {

&#x20;               sm->state = ROD\_STATE\_EXTENDING;

&#x20;               sm->limit\_rev\_sent = false;

&#x20;           } else if (dir == ROD\_DIR\_STOP) {

&#x20;               sm->state = ROD\_STATE\_STOP;

&#x20;           }

&#x20;           break;



&#x20;       /\* 故障态的清除 \*/

&#x20;       case ROD\_STATE\_EXTEND\_FAULT:

&#x20;       case ROD\_STATE\_RETRACT\_FAULT:

&#x20;           if (!sm->external\_fault \&\& dir == ROD\_DIR\_STOP) {

&#x20;               sm->state = ROD\_STATE\_STOP;

&#x20;               sm->fault\_code = 0;

&#x20;           } else if (!sm->external\_fault \&\& dir == ROD\_DIR\_FWD) {

&#x20;               sm->state = ROD\_STATE\_EXTENDING;

&#x20;               sm->fault\_code = 0;

&#x20;           } else if (!sm->external\_fault \&\& dir == ROD\_DIR\_REV) {

&#x20;               sm->state = ROD\_STATE\_RETRACTING;

&#x20;               sm->fault\_code = 0;

&#x20;           }

&#x20;           break;



&#x20;       default:

&#x20;           break;

&#x20;   }



update\_done:

&#x20;   /\* 状态变化时触发日志回调（可选） \*/

&#x20;   if (sm->state != sm->prev\_state) {

&#x20;       RodState\_LogTransition(sm);

&#x20;   }

}

```



该函数的核心设计思路是：状态机以当前状态为基准，结合方向指令的变化、位置模块提供的校准区标志、以及限位霍尔的实际电平，决定下一个状态。每个状态的转换条件都明确定义，包括正常运动、到位停止、方向切换、超时和堵转故障等场景。限位事件的发送带有防重复机制，防止在限位区间内反复发送事件。



\### 6.4 限位事件发送函数



限位事件通过轴事件组发送给仲裁系统：



```c

void RodState\_SendLimitEvent(RodDirection\_t dir, bool active)

{

&#x20;   uint32\_t event\_bit = 0;



&#x20;   if (dir == ROD\_DIR\_FWD) {

&#x20;       event\_bit = active ? EVT\_ROD\_LIMIT\_EXTEND : EVT\_ROD\_LIMIT\_RELEASED;

&#x20;   } else if (dir == ROD\_DIR\_REV) {

&#x20;       event\_bit = active ? EVT\_ROD\_LIMIT\_RETRACT : EVT\_ROD\_LIMIT\_RELEASED;

&#x20;   } else {

&#x20;       return;

&#x20;   }



&#x20;   /\* 通过已有的 Act\_Event\_Send 函数广播到所有轴的事件组 \*/

&#x20;   Act\_Event\_Send(event\_bit);

}

```



该函数是状态模块与仲裁系统的唯一接口。状态模块只负责发送限位事件，不关心事件被如何处理，仲裁系统通过订阅这些事件来更新其阻塞队列。



\### 6.5 轴对象集成



在轴对象结构体中，位置模块和状态模块作为两个独立成员存在：



```c

/\* 在 Axis\_t 结构体中的集成方式 \*/

typedef struct {

&#x20;   uint8\_t id;

&#x20;   AxisDir\_t dir;



&#x20;   /\* 事件组（用于接收系统事件和发送限位事件） \*/

&#x20;   rt\_event\_t evt\_act;



&#x20;   /\* 推杆位置模块实例 \*/

&#x20;   RodPosition\_t position;



&#x20;   /\* 推杆状态模块实例（持有位置模块的指针） \*/

&#x20;   RodStateModule\_t state;



&#x20;   /\* ... 后续扩展的仲裁相关字段 \*/

} Axis\_t;

```



初始化时，需要将位置模块的地址赋值给状态模块的 `position` 指针：



```c

void Axis\_Init(Axis\_t \*axis, uint8\_t id)

{

&#x20;   /\* 初始化位置模块 \*/

&#x20;   RodPosition\_Init(\&axis->position);

&#x20;   RodPosition\_SetParams(\&axis->position, 100.0f, 1.0f, 0.005f, 3.0f);



&#x20;   /\* 初始化状态模块，将位置模块的指针传入 \*/

&#x20;   RodState\_Init(\&axis->state);

&#x20;   axis->state.position = \&axis->position;

&#x20;   axis->state.move\_timeout\_ms = 5000;   /\* 5秒超时 \*/

&#x20;   axis->state.min\_velocity\_thresh = 0.5f; /\* 0.5mm/s \*/

}

```



\### 6.6 周期性更新任务



在系统的周期性任务（建议10ms）中，依次更新所有轴的位置和状态：



```c

void Actuator\_Tick(uint32\_t current\_tick)

{

&#x20;   for (int i = 0; i < MAX\_AXIS\_NUM; i++) {

&#x20;       Axis\_t \*axis = \&mySystem.axis\[i];



&#x20;       /\* 1. 从霍尔编码器读取脉冲增量（由中断累积） \*/

&#x20;       int32\_t delta\_pulses = Hall\_GetDeltaPulses(i);

&#x20;       Hall\_ClearDeltaPulses(i);



&#x20;       /\* 2. 更新位置模块 \*/

&#x20;       RodPosition\_Update(\&axis->position, delta\_pulses);



&#x20;       /\* 3. 读取限位霍尔电平（直接GPIO读取） \*/

&#x20;       axis->state.min\_limit\_switch = GPIO\_Read(MIN\_LIMIT\_PORT, MIN\_LIMIT\_PIN);

&#x20;       axis->state.max\_limit\_switch = GPIO\_Read(MAX\_LIMIT\_PORT, MAX\_LIMIT\_PIN);



&#x20;       /\* 4. 如果限位霍尔变化，通知位置模块（用于校准重置） \*/

&#x20;       /\*   注：也可以通过中断直接调用 RodPosition\_OnMinLimit/MaxLimit \*/



&#x20;       /\* 5. 获取仲裁系统的方向指令 \*/

&#x20;       RodDirection\_t dir = Arbitrator\_GetDirection(i);



&#x20;       /\* 6. 更新状态模块 \*/

&#x20;       RodState\_Update(\&axis->state, dir, current\_tick);

&#x20;   }

}

```



该周期任务完成了从硬件输入到软件状态的全链路更新，确保每个控制周期（10ms）内所有轴的位置和状态都得到刷新。



\---



\## 七、模块接口总览



\### 7.1 位置模块对外接口



| 函数名 | 方向 | 说明 |

|--------|------|------|

| `RodPosition\_Init` | 输入 | 初始化位置模块 |

| `RodPosition\_SetParams` | 输入 | 配置物理参数（行程、减速比、脉冲当量、容差） |

| `RodPosition\_Update` | 输入 | 周期性更新，传入脉冲增量 |

| `RodPosition\_OnMinLimit` | 输入 | 下限位霍尔触发事件注入 |

| `RodPosition\_OnMaxLimit` | 输入 | 上限位霍尔触发事件注入 |

| `RodPosition\_GetCurrent` | 输出 | 获取当前位置（毫米） |

| `RodPosition\_IsCalibrated` | 输出 | 获取校准状态 |

| `RodPosition\_IsCalibAllowed` | 输出 | 获取校准允许标志 |

| `RodPosition\_IsInCalibZoneMin` | 输出 | 是否在下限校准区内 |

| `RodPosition\_IsInCalibZoneMax` | 输出 | 是否在上限校准区内 |



\### 7.2 状态模块对外接口



| 函数名 | 方向 | 说明 |

|--------|------|------|

| `RodState\_Init` | 输入 | 初始化状态模块 |

| `RodState\_SetTimeout` | 输入 | 设置运动超时阈值 |

| `RodState\_SetStallParam` | 输入 | 设置堵转检测参数 |

| `RodState\_Update` | 输入 | 周期性更新，传入方向指令和时间戳 |

| `RodState\_SetFault` | 输入 | 外部故障注入 |

| `RodState\_ClearFault` | 输入 | 手动清除故障 |

| `RodState\_Get` | 输出 | 获取当前状态枚举 |

| `RodState\_IsMoving` | 输出 | 是否在运动中 |

| `RodState\_IsAtLimit` | 输出 | 是否在限位位置 |

| `RodState\_IsFault` | 输出 | 是否处于故障态 |

| `RodState\_SendLimitEvent` | 输出 | 向仲裁系统发送限位事件 |



\### 7.3 与外部模块的接口依赖



| 依赖项 | 提供方 | 说明 |

|--------|--------|------|

| 霍尔脉冲增量 | 霍尔编码器中断 | 每10ms累计增量 |

| 限位霍尔电平 | GPIO读取 | 直接读取电平值 |

| 方向指令 | 电机仲裁系统 | 仲裁后的最终方向 |

| 限位事件发送 | `Act\_Event\_Send` | 已有的事件广播函数 |



\---



\## 八、设计原则总结



本设计方案遵循以下核心原则：



\*\*单一职责原则\*\*：位置模块只负责位置计算和校准管理，状态模块只负责状态判定和限位事件发送，两者互不重叠。



\*\*接口隔离原则\*\*：两个模块之间的交互通过清晰的数据接口（位置模块提供只读查询接口，状态模块通过指针读取）实现，没有多余的依赖。



\*\*松耦合原则\*\*：状态模块不内嵌位置模块，而是通过指针引用，使得两个模块可以独立编译、测试和替换。



\*\*事件驱动原则\*\*：状态模块与仲裁系统之间通过事件通信，状态模块不直接调用仲裁系统的任何控制接口，仲裁系统也不主动查询状态模块，完全通过事件驱动。



\*\*单向依赖原则\*\*：数据流向为硬件→位置模块→状态模块→仲裁系统，上层依赖下层，下层不依赖上层，形成了清晰的分层结构。

