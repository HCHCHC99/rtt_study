\## RT-Thread消息队列方案完整设计



\---



\## 一、方案A（消息队列 + 互斥量方案）优缺点总结



\### 优点



\*\*1. 解耦彻底\*\*

输入源模块（电源极性、过流检测、电压检测、手动IO、CAN等）与仲裁逻辑完全分离。输入源只需构造并发送一条标准格式的消息，完全不关心仲裁器如何处理、何时处理。这使得各模块可以独立开发、测试和修改，互不影响。



\*\*2. ISR安全性高\*\*

所有硬件中断（如过流触发、限位触发）中只需调用 `rt\_mq\_send` 发送消息，这是一个轻量级操作，不会在中断上下文中执行耗时的队列操作或PWM输出。仲裁线程在普通线程上下文中处理所有重活，符合RTOS的最佳实践。



\*\*3. 天然的流量整形\*\*

消息队列的缓冲区可以平滑突发的大量输入事件。例如，电源极性跳变时可能产生多个边沿中断，消息队列可以将这些事件排队，由仲裁线程逐个处理，避免CPU瞬时过载。



\*\*4. 完整的时序可追溯\*\*

每条消息都携带时间戳和设备ID，消息队列本身保持FIFO顺序。在调试复杂故障时，可以完整回放事件发生的先后顺序，精准定位问题根因。



\*\*5. 多轴扩展自然\*\*

通过消息中的 `axis\_id` 字段区分目标轴，仲裁线程可以统一处理所有轴的消息，无需为每个轴创建独立的处理逻辑。新增轴只需增加对应的互斥量数组元素。



\*\*6. 优先级管理灵活\*\*

消息中携带的 `priority` 字段与消息队列的调度优先级无关，而是在仲裁决策时进行比较。这使得不同输入源的相对优先级可以在运行时动态调整，而无需改变线程调度策略。



\*\*7. 调试观察方便\*\*

可以通过MSH命令查看消息队列的当前状态（待处理消息数、队列使用率），也可以dump各轴的仲裁数据结构（当前block/allow队列内容），极大提升了系统的可观测性。



\### 缺点



\*\*1. 内存开销增加\*\*

消息队列需要预分配消息池内存。假设每条消息16字节、队列深度16条，则需要256字节RAM。加上仲裁线程栈1.5KB、互斥量若干，总开销约2KB。对于HC32F460的192KB RAM，可以接受。



\*\*2. 延迟有所增加\*\*

消息从发送到仲裁线程处理，存在消息排队和线程调度延迟。在RT-Thread中，如果仲裁线程优先级设置得当（略高于普通应用线程），最坏延迟通常<1ms。对于推杆控制（控制周期10ms\~50ms），完全可接受。



\*\*3. 系统复杂度提升\*\*

需要管理消息队列的创建、线程的创建和启动、互斥量的初始化和获取释放。相比裸机直接函数调用，代码量增加约20%\~30%。但结构更清晰，长期维护成本更低。



\*\*4. 引入优先级反转风险\*\*

如果使用互斥量保护共享数据，低优先级线程持有互斥量时可能阻塞高优先级线程。RT-Thread的互斥量支持优先级继承，可以规避此问题，但需要正确配置。



\*\*5. 消息队列满时的处理策略\*\*

如果输入源产生事件的速度持续超过仲裁线程的处理速度，消息队列可能溢出。需要设计合理的处理策略（阻塞发送、丢弃旧消息、丢弃新消息等）。



\---



\## 二、架构改造方案



\### 2.1 改造前后的对比



| 维度 | 裸机方式 | 消息队列方式 |

|------|---------|-------------|

| 输入源触发仲裁 | 直接调用 `Motor\_ArbitrationDecision()` | 发送消息到队列 |

| 队列操作执行者 | 输入源自身 | 仲裁线程 |

| 共享数据保护 | 无（依赖单线程） | 互斥量保护 |

| ISR处理 | 在ISR中操作队列并执行仲裁 | ISR只发消息 |

| 多轴支持 | 需复制仲裁逻辑 | 统一消息，循环处理 |

| 事件追踪 | 需要手动加日志 | 消息队列自带FIFO记录 |



\### 2.2 改造后的系统架构图



```

┌─────────────────────────────────────────────────────────────────────────────┐

│                             硬件中断层                                     │

│   霍尔中断  │  限位中断  │  过流中断  │  GPIO中断（极性变化）              │

└──────┬──────┴──────┬──────┴──────┬──────┴───────────────┬─────────────────┘

&#x20;      │             │             │                      │

&#x20;      ▼             ▼             ▼                      ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                           设备模块层                                       │

│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │

│  │ 电源极性模块 │  │ 电压检测模块 │  │ 电流检测模块 │  │ 手动IO模块  │       │

│  │ (极性变化)  │  │ (过压/欠压) │  │  (过流)    │  │ (按键指令)  │       │

│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘       │

│         │                │                │                │               │

│         └────────────────┼────────────────┼────────────────┘               │

│                          │                │                                 │

│                          ▼                ▼                                 │

│              ┌──────────────────────────────────────────────────────────┐  │

│              │           rt\_mq\_send() / rt\_mq\_urgent()                  │  │

│              │    (普通消息 / 紧急消息，后者插入队首)                    │  │

│              └──────────────────────────────────────────────────────────┘  │

└─────────────────────────────────────────────────────────────────────────────┘

&#x20;                                      │

&#x20;                                      ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                         消息队列 (rt\_mq\_t)                                  │

│         ┌──────────────────────────────────────────────────────────┐       │

│         │  消息池：16条消息 × 24字节 = 384字节                     │       │

│         │  消息格式：ArbCommandMsg\_t                               │       │

│         │  包含：axis\_id / device\_id / priority / cmd\_type / param │       │

│         └──────────────────────────────────────────────────────────┘       │

└─────────────────────────────────────────────────────────────────────────────┘

&#x20;                                      │

&#x20;                                      ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                          仲裁线程 (优先级: 15)                              │

│  while(1) {                                                                │

│      rt\_mq\_recv(\&mq, \&msg, sizeof(msg), RT\_WAITING\_FOREVER);              │

│      rt\_mutex\_take(\&g\_arb\_mutex\[msg.axis\_id], RT\_WAITING\_FOREVER);       │

│      Arb\_ProcessMessage(axis, \&msg);    // 操作 block/allow 队列          │

│      Motor\_ArbitrationDecision(axis);   // 执行仲裁决策                   │

│      rt\_mutex\_release(\&g\_arb\_mutex\[msg.axis\_id]);                        │

│  }                                                                        │

└─────────────────────────────────────────────────────────────────────────────┘

&#x20;                                      │

&#x20;                                      ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                       轴仲裁数据结构 (每轴独立)                             │

│  ┌──────────────────────────────────────────────────────────────┐         │

│  │  block\_fwd 队列  │  block\_rev 队列  │  allow\_fwd 队列  │ allow\_rev │ │

│  │  (阻塞正转)      │  (阻塞反转)      │  (允许正转)      │ (允许反转) │ │

│  └──────────────────────────────────────────────────────────────┘         │

└─────────────────────────────────────────────────────────────────────────────┘

&#x20;                                      │

&#x20;                                      ▼

┌─────────────────────────────────────────────────────────────────────────────┐

│                           硬件输出层                                       │

│                    PWM占空比  │  方向GPIO  │  刹车/使能                    │

└─────────────────────────────────────────────────────────────────────────────┘

```



\---



\## 三、关键结构体与枚举定义



\### 3.1 命令类型枚举



```c

/\*\*

&#x20;\* @brief 仲裁命令类型

&#x20;\* @note  与裸机代码保持一致，用于消息传递

&#x20;\*/

typedef enum {

&#x20;   CMD\_TYPE\_NONE = 0,          /\* 无命令（保留） \*/

&#x20;   CMD\_TYPE\_RUN\_FWD,           /\* 正转运行（添加allow\_fwd） \*/

&#x20;   CMD\_TYPE\_RUN\_REV,           /\* 反转运行（添加allow\_rev） \*/

&#x20;   CMD\_TYPE\_STOP,              /\* 停止（移除自己的allow，添加自己的block） \*/

&#x20;   CMD\_TYPE\_BLOCK\_FWD,         /\* 阻塞正转（添加block\_fwd） \*/

&#x20;   CMD\_TYPE\_BLOCK\_REV,         /\* 阻塞反转（添加block\_rev） \*/

&#x20;   CMD\_TYPE\_UNBLOCK\_FWD,       /\* 解除阻塞正转（移除block\_fwd） \*/

&#x20;   CMD\_TYPE\_UNBLOCK\_REV,       /\* 解除阻塞反转（移除block\_rev） \*/

&#x20;   CMD\_TYPE\_CLEAR\_ALLOW\_FWD,   /\* 清空所有正转允许 \*/

&#x20;   CMD\_TYPE\_CLEAR\_ALLOW\_REV,   /\* 清空所有反转允许 \*/

&#x20;   CMD\_TYPE\_EMERGENCY\_STOP,    /\* 紧急停止（清空所有allow） \*/

} ArbCmdType\_t;

```



\### 3.2 设备ID枚举（与裸机保持一致，扩展新增）



```c

/\*\*

&#x20;\* @brief 仲裁设备ID枚举

&#x20;\* @note  每个输入源有唯一的设备ID，用于追踪命令来源

&#x20;\*/

typedef enum {

&#x20;   DEV\_ID\_NONE             = 0,

&#x20;   DEV\_ID\_POWER\_POS        = 1,    /\* 电源极性：正极 \*/

&#x20;   DEV\_ID\_POWER\_NEG        = 2,    /\* 电源极性：负极 \*/

&#x20;   DEV\_ID\_LIMIT\_FWD        = 3,    /\* 正向限位（硬件限位开关） \*/

&#x20;   DEV\_ID\_LIMIT\_REV        = 4,    /\* 反向限位（硬件限位开关） \*/

&#x20;   DEV\_ID\_CAN              = 5,    /\* CAN总线命令 \*/

&#x20;   DEV\_ID\_IO\_FWD           = 6,    /\* 手动IO：正转按键 \*/

&#x20;   DEV\_ID\_IO\_REV           = 7,    /\* 手动IO：反转按键 \*/

&#x20;   DEV\_ID\_EMERGENCY        = 8,    /\* 急停按钮 \*/

&#x20;   DEV\_ID\_RTURN\_FWD        = 9,    /\* 旋转限位：正向 \*/

&#x20;   DEV\_ID\_RTURN\_REV        = 10,   /\* 旋转限位：反向 \*/

&#x20;   DEV\_ID\_OVERVOLTAGE\_FWD  = 11,   /\* 过压：阻塞正转 \*/

&#x20;   DEV\_ID\_OVERVOLTAGE\_REV  = 12,   /\* 过压：阻塞反转 \*/

&#x20;   DEV\_ID\_UNDERVOLTAGE\_FWD = 13,   /\* 欠压：阻塞正转 \*/

&#x20;   DEV\_ID\_UNDERVOLTAGE\_REV = 14,   /\* 欠压：阻塞反转 \*/

&#x20;   DEV\_ID\_OVERCUR\_FWD      = 15,   /\* 过流：阻塞正转 \*/

&#x20;   DEV\_ID\_OVERCUR\_REV      = 16,   /\* 过流：阻塞反转 \*/

&#x20;   DEV\_ID\_ROD\_LIMIT\_FWD    = 17,   /\* 推杆限位：正向（来自推杆状态模块） \*/

&#x20;   DEV\_ID\_ROD\_LIMIT\_REV    = 18,   /\* 推杆限位：反向（来自推杆状态模块） \*/

&#x20;   DEV\_ID\_MAX

} ArbDeviceId\_t;

```



\### 3.3 优先级枚举



```c

/\*\*

&#x20;\* @brief 仲裁优先级枚举

&#x20;\* @note  数值越小优先级越高，冲突时高优先级胜出

&#x20;\*/

typedef enum {

&#x20;   PRIO\_EMERGENCY = 0,     /\* 紧急（最高优先级：急停） \*/

&#x20;   PRIO\_LIMIT = 1,         /\* 限位（硬件/软件限位） \*/

&#x20;   PRIO\_FAULT = 2,         /\* 故障（过压/欠压/过流） \*/

&#x20;   PRIO\_MANUAL = 3,        /\* 手动IO（按键控制） \*/

&#x20;   PRIO\_CAN = 4,           /\* CAN总线命令 \*/

&#x20;   PRIO\_POWER = 5,         /\* 电源极性（最低优先级） \*/

&#x20;   PRIO\_NONE = 255,        /\* 无优先级（无效值） \*/

} ArbPriority\_t;

```



\### 3.4 命令消息结构体



```c

/\*\*

&#x20;\* @brief 仲裁命令消息结构体

&#x20;\* @note  通过消息队列在输入源与仲裁线程之间传递

&#x20;\*/

typedef struct {

&#x20;   uint8\_t axis\_id;            /\* 目标轴ID（0 \~ MAX\_AXIS\_NUM-1） \*/

&#x20;   uint8\_t device\_id;          /\* 来源设备ID（DEV\_ID\_\*） \*/

&#x20;   uint8\_t priority;           /\* 优先级（PRIO\_\*） \*/

&#x20;   uint8\_t cmd\_type;           /\* 命令类型（CMD\_TYPE\_\*） \*/

&#x20;   float param;                /\* 参数（占空比，单位：百分比%） \*/

&#x20;   uint32\_t timestamp;         /\* 时间戳（ms，用于调试） \*/

} ArbCommandMsg\_t;

```



\### 3.5 单条命令记录结构体



```c

/\*\*

&#x20;\* @brief 命令队列中的单条命令记录

&#x20;\* @note  用于 block\_fwd/block\_rev/allow\_fwd/allow\_rev 队列

&#x20;\*/

typedef struct {

&#x20;   uint8\_t device\_id;          /\* 发出命令的设备ID \*/

&#x20;   uint8\_t priority;           /\* 命令优先级 \*/

&#x20;   uint8\_t cmd\_type;           /\* 命令类型（用于调试） \*/

&#x20;   float param;                /\* 参数（占空比） \*/

&#x20;   uint32\_t timestamp;         /\* 时间戳 \*/

} ArbCommandRecord\_t;

```



\### 3.6 命令队列结构体



```c

/\*\*

&#x20;\* @brief 命令队列结构体

&#x20;\* @note  每个方向（正转/反转）的允许和阻塞各有一个队列

&#x20;\*/

\#define MAX\_CMD\_QUEUE\_SIZE  10   /\* 每个队列最大容量 \*/



typedef struct {

&#x20;   ArbCommandRecord\_t records\[MAX\_CMD\_QUEUE\_SIZE];  /\* 命令记录数组 \*/

&#x20;   uint8\_t count;              /\* 当前队列中的命令数量 \*/

} ArbCommandQueue\_t;

```



\### 3.7 轴仲裁数据结构体



```c

/\*\*

&#x20;\* @brief 轴仲裁数据结构体

&#x20;\* @note  每个轴独立拥有，与 Axis\_t 关联

&#x20;\*/

typedef struct {

&#x20;   /\* ===== 四个命令队列 ===== \*/

&#x20;   ArbCommandQueue\_t block\_fwd;    /\* 阻塞正转队列 \*/

&#x20;   ArbCommandQueue\_t block\_rev;    /\* 阻塞反转队列 \*/

&#x20;   ArbCommandQueue\_t allow\_fwd;    /\* 允许正转队列 \*/

&#x20;   ArbCommandQueue\_t allow\_rev;    /\* 允许反转队列 \*/



&#x20;   /\* ===== 当前仲裁结果 ===== \*/

&#x20;   MotorDir\_t active\_dir;          /\* 当前活动方向（FWD/REV/STOP） \*/

&#x20;   float current\_duty;             /\* 当前输出占空比（%） \*/

&#x20;   uint8\_t active\_device\_id;       /\* 当前活跃的设备ID（谁在控制） \*/



&#x20;   /\* ===== 状态信息 ===== \*/

&#x20;   MotorState\_t state;             /\* 电机状态（IDLE/RAMPING/RUNNING） \*/

&#x20;   uint8\_t enable;                 /\* 是否使能 \*/



&#x20;   /\* ===== 调试信息 ===== \*/

&#x20;   uint32\_t last\_arbitration\_time; /\* 上次仲裁时间戳 \*/

&#x20;   uint32\_t arbitration\_count;     /\* 仲裁次数累计 \*/

&#x20;   uint8\_t conflict\_fault;         /\* 冲突故障标志 \*/

} ArbData\_t;

```



\### 3.8 扩展后的轴对象结构体



```c

/\*\*

&#x20;\* @brief 扩展后的轴对象结构体

&#x20;\* @note  整合推杆位置、推杆状态和电机仲裁数据

&#x20;\*/

typedef struct {

&#x20;   uint8\_t id;                     /\* 轴序号 \*/

&#x20;   AxisDir\_t dir;                  /\* 轴方向配置 \*/



&#x20;   /\* ===== 事件组 ===== \*/

&#x20;   rt\_event\_t evt\_act;             /\* 轴事件组 \*/



&#x20;   /\* ===== 推杆位置模块 ===== \*/

&#x20;   RodPosition\_t position;



&#x20;   /\* ===== 推杆状态模块 ===== \*/

&#x20;   RodStateModule\_t state;



&#x20;   /\* ===== 电机仲裁模块 ===== \*/

&#x20;   ArbData\_t arb;



&#x20;   /\* ===== 互斥量（保护本轴的仲裁数据） ===== \*/

&#x20;   rt\_mutex\_t arb\_mutex;



} Axis\_t;

```



\---



\## 四、关键代码实现



\### 4.1 消息队列和仲裁线程初始化



```c

/\*\*

&#x20;\* @brief 仲裁模块初始化

&#x20;\* @note  在系统初始化时调用，创建消息队列和仲裁线程

&#x20;\*/

static rt\_mq\_t g\_arb\_mq = RT\_NULL;

static rt\_thread\_t g\_arb\_thread = RT\_NULL;



/\* 消息池大小：16条消息 × 消息体大小 \*/

\#define ARB\_MQ\_POOL\_SIZE    (16 \* sizeof(ArbCommandMsg\_t))

static uint8\_t g\_arb\_mq\_pool\[ARB\_MQ\_POOL\_SIZE];



int Arb\_Module\_Init(void)

{

&#x20;   int i;



&#x20;   /\* 1. 创建消息队列 \*/

&#x20;   g\_arb\_mq = rt\_mq\_create("arb\_mq",

&#x20;                           sizeof(ArbCommandMsg\_t),

&#x20;                           16,                    /\* 最大16条消息 \*/

&#x20;                           RT\_IPC\_FLAG\_FIFO);     /\* FIFO顺序 \*/

&#x20;   if (g\_arb\_mq == RT\_NULL) {

&#x20;       return -1;

&#x20;   }



&#x20;   /\* 2. 初始化各轴的互斥量 \*/

&#x20;   for (i = 0; i < MAX\_AXIS\_NUM; i++) {

&#x20;       char name\[16];

&#x20;       rt\_snprintf(name, sizeof(name), "arb\_mtx%d", i);

&#x20;       rt\_mutex\_init(\&mySystem.axis\[i].arb\_mutex, name, RT\_IPC\_FLAG\_PRIO);

&#x20;   }



&#x20;   /\* 3. 创建仲裁线程 \*/

&#x20;   g\_arb\_thread = rt\_thread\_create("arb\_thread",

&#x20;                                   Arb\_Thread\_Entry,

&#x20;                                   RT\_NULL,

&#x20;                                   2048,          /\* 栈大小 \*/

&#x20;                                   15,            /\* 优先级（中等偏高） \*/

&#x20;                                   10);           /\* 时间片 \*/

&#x20;   if (g\_arb\_thread == RT\_NULL) {

&#x20;       return -2;

&#x20;   }

&#x20;   rt\_thread\_startup(g\_arb\_thread);



&#x20;   return 0;

}

```



\### 4.2 仲裁线程主循环



```c

/\*\*

&#x20;\* @brief 仲裁线程入口函数

&#x20;\* @note  阻塞等待消息队列中的命令，处理完后执行仲裁

&#x20;\*/

static void Arb\_Thread\_Entry(void \*param)

{

&#x20;   ArbCommandMsg\_t msg;

&#x20;   rt\_err\_t ret;



&#x20;   (void)param;



&#x20;   while (1) {

&#x20;       /\* 阻塞等待消息（永久等待） \*/

&#x20;       ret = rt\_mq\_recv(g\_arb\_mq,

&#x20;                        \&msg,

&#x20;                        sizeof(ArbCommandMsg\_t),

&#x20;                        RT\_WAITING\_FOREVER);



&#x20;       if (ret == RT\_EOK) {

&#x20;           /\* 检查轴ID是否有效 \*/

&#x20;           if (msg.axis\_id >= MAX\_AXIS\_NUM) {

&#x20;               continue;

&#x20;           }



&#x20;           Axis\_t \*axis = \&mySystem.axis\[msg.axis\_id];



&#x20;           /\* 加锁保护本轴的仲裁数据 \*/

&#x20;           rt\_mutex\_take(\&axis->arb\_mutex, RT\_WAITING\_FOREVER);



&#x20;           /\* 处理消息：操作block/allow队列 \*/

&#x20;           Arb\_ProcessMessage(axis, \&msg);



&#x20;           /\* 执行仲裁决策 \*/

&#x20;           Arb\_Decision(axis);



&#x20;           /\* 释放互斥量 \*/

&#x20;           rt\_mutex\_release(\&axis->arb\_mutex);

&#x20;       }

&#x20;   }

}

```



\### 4.3 消息处理函数（核心逻辑）



```c

/\*\*

&#x20;\* @brief 处理单条仲裁命令消息

&#x20;\* @param axis  目标轴对象

&#x20;\* @param msg   命令消息

&#x20;\* @note  根据cmd\_type操作对应的block/allow队列

&#x20;\*/

static void Arb\_ProcessMessage(Axis\_t \*axis, const ArbCommandMsg\_t \*msg)

{

&#x20;   ArbData\_t \*arb = \&axis->arb;



&#x20;   switch (msg->cmd\_type) {

&#x20;       case CMD\_TYPE\_RUN\_FWD:

&#x20;           /\* 正转运行：移除自己的BLOCK，添加自己的ALLOW \*/

&#x20;           Arb\_CmdList\_Remove(\&arb->block\_fwd, msg->device\_id);

&#x20;           Arb\_CmdList\_SetAllow(\&arb->allow\_fwd, \&arb->block\_fwd,

&#x20;                                msg->device\_id, msg->priority,

&#x20;                                msg->cmd\_type, msg->param, msg->timestamp);

&#x20;           break;



&#x20;       case CMD\_TYPE\_RUN\_REV:

&#x20;           /\* 反转运行：移除自己的BLOCK，添加自己的ALLOW \*/

&#x20;           Arb\_CmdList\_Remove(\&arb->block\_rev, msg->device\_id);

&#x20;           Arb\_CmdList\_SetAllow(\&arb->allow\_rev, \&arb->block\_rev,

&#x20;                                msg->device\_id, msg->priority,

&#x20;                                msg->cmd\_type, msg->param, msg->timestamp);

&#x20;           break;



&#x20;       case CMD\_TYPE\_STOP:

&#x20;           /\* 停止：移除自己的ALLOW，添加自己的BLOCK \*/

&#x20;           Arb\_CmdList\_Remove(\&arb->allow\_fwd, msg->device\_id);

&#x20;           Arb\_CmdList\_Remove(\&arb->allow\_rev, msg->device\_id);

&#x20;           /\* 注：是否添加BLOCK取决于具体设备语义，有些设备停止时不应阻塞其他设备 \*/

&#x20;           break;



&#x20;       case CMD\_TYPE\_BLOCK\_FWD:

&#x20;           /\* 阻塞正转：添加block\_fwd \*/

&#x20;           Arb\_CmdList\_SetBlock(\&arb->block\_fwd,

&#x20;                                msg->device\_id, msg->priority,

&#x20;                                msg->cmd\_type, msg->timestamp);

&#x20;           break;



&#x20;       case CMD\_TYPE\_BLOCK\_REV:

&#x20;           /\* 阻塞反转：添加block\_rev \*/

&#x20;           Arb\_CmdList\_SetBlock(\&arb->block\_rev,

&#x20;                                msg->device\_id, msg->priority,

&#x20;                                msg->cmd\_type, msg->timestamp);

&#x20;           break;



&#x20;       case CMD\_TYPE\_UNBLOCK\_FWD:

&#x20;           /\* 解除阻塞正转：移除block\_fwd \*/

&#x20;           Arb\_CmdList\_Remove(\&arb->block\_fwd, msg->device\_id);

&#x20;           break;



&#x20;       case CMD\_TYPE\_UNBLOCK\_REV:

&#x20;           /\* 解除阻塞反转：移除block\_rev \*/

&#x20;           Arb\_CmdList\_Remove(\&arb->block\_rev, msg->device\_id);

&#x20;           break;



&#x20;       case CMD\_TYPE\_CLEAR\_ALLOW\_FWD:

&#x20;           /\* 清空所有正转允许 \*/

&#x20;           Arb\_CmdList\_ClearAll(\&arb->allow\_fwd);

&#x20;           break;



&#x20;       case CMD\_TYPE\_CLEAR\_ALLOW\_REV:

&#x20;           /\* 清空所有反转允许 \*/

&#x20;           Arb\_CmdList\_ClearAll(\&arb->allow\_rev);

&#x20;           break;



&#x20;       case CMD\_TYPE\_EMERGENCY\_STOP:

&#x20;           /\* 紧急停止：清空所有allow，保留block \*/

&#x20;           Arb\_CmdList\_ClearAll(\&arb->allow\_fwd);

&#x20;           Arb\_CmdList\_ClearAll(\&arb->allow\_rev);

&#x20;           break;



&#x20;       default:

&#x20;           break;

&#x20;   }

}

```



\### 4.4 队列操作函数（移植自裸机）



```c

/\*\*

&#x20;\* @brief 从队列中移除指定设备ID的命令

&#x20;\* @param q         目标队列

&#x20;\* @param device\_id 要移除的设备ID

&#x20;\*/

static void Arb\_CmdList\_Remove(ArbCommandQueue\_t \*q, uint8\_t device\_id)

{

&#x20;   for (int i = 0; i < q->count; i++) {

&#x20;       if (q->records\[i].device\_id == device\_id) {

&#x20;           /\* 找到并移除：后面的元素前移 \*/

&#x20;           for (int j = i; j < q->count - 1; j++) {

&#x20;               q->records\[j] = q->records\[j + 1];

&#x20;           }

&#x20;           q->count--;

&#x20;           /\* 清空最后一个位置 \*/

&#x20;           if (q->count < MAX\_CMD\_QUEUE\_SIZE) {

&#x20;               memset(\&q->records\[q->count], 0, sizeof(ArbCommandRecord\_t));

&#x20;           }

&#x20;           return;

&#x20;       }

&#x20;   }

}



/\*\*

&#x20;\* @brief 向队列中添加ALLOW命令（按优先级排序）

&#x20;\* @param q          目标队列（allow\_fwd 或 allow\_rev）

&#x20;\* @param block\_q    对应的阻塞队列（如 allow\_fwd 对应 block\_fwd）

&#x20;\* @param device\_id  设备ID

&#x20;\* @param priority   优先级（数值越小越高）

&#x20;\* @param cmd\_type   命令类型

&#x20;\* @param param      参数（占空比）

&#x20;\* @param timestamp  时间戳

&#x20;\* @note  如果block\_q非空，则拒绝添加ALLOW

&#x20;\*/

static void Arb\_CmdList\_SetAllow(ArbCommandQueue\_t \*q,

&#x20;                                 ArbCommandQueue\_t \*block\_q,

&#x20;                                 uint8\_t device\_id,

&#x20;                                 uint8\_t priority,

&#x20;                                 uint8\_t cmd\_type,

&#x20;                                 float param,

&#x20;                                 uint32\_t timestamp)

{

&#x20;   /\* 安全检查：如果对应阻塞队列非空，拒绝添加ALLOW \*/

&#x20;   if (block\_q != NULL \&\& block\_q->count > 0) {

&#x20;       return;

&#x20;   }



&#x20;   /\* 先移除已存在的同设备命令（更新） \*/

&#x20;   Arb\_CmdList\_Remove(q, device\_id);



&#x20;   /\* 队列已满，丢弃 \*/

&#x20;   if (q->count >= MAX\_CMD\_QUEUE\_SIZE) {

&#x20;       return;

&#x20;   }



&#x20;   /\* 按优先级插入（数值越小优先级越高） \*/

&#x20;   int insert\_pos = q->count;

&#x20;   for (int i = 0; i < q->count; i++) {

&#x20;       if (priority < q->records\[i].priority) {

&#x20;           insert\_pos = i;

&#x20;           break;

&#x20;       }

&#x20;   }



&#x20;   /\* 后移元素 \*/

&#x20;   for (int i = q->count; i > insert\_pos; i--) {

&#x20;       q->records\[i] = q->records\[i - 1];

&#x20;   }



&#x20;   /\* 插入新命令 \*/

&#x20;   q->records\[insert\_pos].device\_id = device\_id;

&#x20;   q->records\[insert\_pos].priority = priority;

&#x20;   q->records\[insert\_pos].cmd\_type = cmd\_type;

&#x20;   q->records\[insert\_pos].param = param;

&#x20;   q->records\[insert\_pos].timestamp = timestamp;

&#x20;   q->count++;

}



/\*\*

&#x20;\* @brief 向队列中添加BLOCK命令

&#x20;\* @param q          目标队列（block\_fwd 或 block\_rev）

&#x20;\* @param device\_id  设备ID

&#x20;\* @param priority   优先级

&#x20;\* @param cmd\_type   命令类型

&#x20;\* @param timestamp  时间戳

&#x20;\* @note  BLOCK命令不按优先级排序，按FIFO顺序

&#x20;\*/

static void Arb\_CmdList\_SetBlock(ArbCommandQueue\_t \*q,

&#x20;                                 uint8\_t device\_id,

&#x20;                                 uint8\_t priority,

&#x20;                                 uint8\_t cmd\_type,

&#x20;                                 uint32\_t timestamp)

{

&#x20;   /\* 检查是否已存在同设备命令 \*/

&#x20;   for (int i = 0; i < q->count; i++) {

&#x20;       if (q->records\[i].device\_id == device\_id) {

&#x20;           return;  /\* 已存在，不重复添加 \*/

&#x20;       }

&#x20;   }



&#x20;   /\* 队列已满，丢弃 \*/

&#x20;   if (q->count >= MAX\_CMD\_QUEUE\_SIZE) {

&#x20;       return;

&#x20;   }



&#x20;   /\* 追加到队尾 \*/

&#x20;   q->records\[q->count].device\_id = device\_id;

&#x20;   q->records\[q->count].priority = priority;

&#x20;   q->records\[q->count].cmd\_type = cmd\_type;

&#x20;   q->records\[q->count].timestamp = timestamp;

&#x20;   q->count++;

}



/\*\*

&#x20;\* @brief 清空队列所有命令

&#x20;\* @param q 目标队列

&#x20;\*/

static void Arb\_CmdList\_ClearAll(ArbCommandQueue\_t \*q)

{

&#x20;   memset(q->records, 0, sizeof(q->records));

&#x20;   q->count = 0;

}

```



\### 4.5 仲裁决策函数（核心逻辑，移植自裸机）



```c

/\*\*

&#x20;\* @brief 执行仲裁决策

&#x20;\* @param axis 目标轴对象

&#x20;\* @note  比较block/allow队列，决定最终输出方向

&#x20;\*/

static void Arb\_Decision(Axis\_t \*axis)

{

&#x20;   ArbData\_t \*arb = \&axis->arb;

&#x20;   ArbCommandRecord\_t \*fwd\_cmd = NULL;

&#x20;   ArbCommandRecord\_t \*rev\_cmd = NULL;



&#x20;   /\* 1. 检查使能状态 \*/

&#x20;   if (!arb->enable) {

&#x20;       Arb\_ApplyStop(axis);

&#x20;       return;

&#x20;   }



&#x20;   /\* 2. 选出有效的正向命令 \*/

&#x20;   if (arb->block\_fwd.count == 0 \&\& arb->allow\_fwd.count > 0) {

&#x20;       fwd\_cmd = \&arb->allow\_fwd.records\[0];  /\* 已按优先级排序，第一个最高 \*/

&#x20;   }



&#x20;   /\* 3. 选出有效的反向命令 \*/

&#x20;   if (arb->block\_rev.count == 0 \&\& arb->allow\_rev.count > 0) {

&#x20;       rev\_cmd = \&arb->allow\_rev.records\[0];

&#x20;   }



&#x20;   /\* 4. 冲突判断 \*/

&#x20;   ArbCommandRecord\_t \*final = NULL;

&#x20;   arb->conflict\_fault = 0;



&#x20;   if (fwd\_cmd \&\& rev\_cmd) {

&#x20;       /\* 正转和反转都有命令 \*/

&#x20;       if (fwd\_cmd->device\_id == rev\_cmd->device\_id) {

&#x20;           /\* 同一设备同时请求正转和反转 → 冲突 \*/

&#x20;           arb->conflict\_fault = 1;

&#x20;           final = NULL;

&#x20;       } else if (fwd\_cmd->priority < rev\_cmd->priority) {

&#x20;           /\* 正转优先级更高 \*/

&#x20;           final = fwd\_cmd;

&#x20;       } else if (rev\_cmd->priority < fwd\_cmd->priority) {

&#x20;           /\* 反转优先级更高 \*/

&#x20;           final = rev\_cmd;

&#x20;       } else {

&#x20;           /\* 优先级相同 → 冲突 \*/

&#x20;           arb->conflict\_fault = 1;

&#x20;           final = NULL;

&#x20;       }

&#x20;   } else {

&#x20;       /\* 只有一个方向有效 \*/

&#x20;       final = fwd\_cmd ? fwd\_cmd : rev\_cmd;

&#x20;   }



&#x20;   /\* 5. 执行决策 \*/

&#x20;   if (final) {

&#x20;       /\* 有有效命令 \*/

&#x20;       arb->active\_device\_id = final->device\_id;

&#x20;       arb->active\_dir = (final->cmd\_type == CMD\_TYPE\_RUN\_FWD) ? DIR\_FWD : DIR\_REV;

&#x20;       arb->current\_duty = final->param;

&#x20;       arb->state = MS\_RUNNING;



&#x20;       /\* 调用硬件输出（根据方向） \*/

&#x20;       if (arb->active\_dir == DIR\_FWD) {

&#x20;           Motor\_OnArbitrationFwd(axis, arb->current\_duty);

&#x20;       } else {

&#x20;           Motor\_OnArbitrationRev(axis, arb->current\_duty);

&#x20;       }

&#x20;   } else {

&#x20;       /\* 无有效命令 → 停止 \*/

&#x20;       Arb\_ApplyStop(axis);

&#x20;   }



&#x20;   arb->last\_arbitration\_time = rt\_tick\_get\_millisecond();

&#x20;   arb->arbitration\_count++;

}



/\*\*

&#x20;\* @brief 应用停止状态

&#x20;\* @param axis 目标轴对象

&#x20;\*/

static void Arb\_ApplyStop(Axis\_t \*axis)

{

&#x20;   ArbData\_t \*arb = \&axis->arb;



&#x20;   arb->active\_device\_id = DEV\_ID\_NONE;

&#x20;   arb->active\_dir = DIR\_NONE;

&#x20;   arb->current\_duty = 0.0f;

&#x20;   arb->state = MS\_IDLE;



&#x20;   Motor\_OnArbitrationStop(axis);

}

```



\### 4.6 输入源发送消息的封装函数



```c

/\*\*

&#x20;\* @brief 发送仲裁命令消息

&#x20;\* @param axis\_id    目标轴ID

&#x20;\* @param device\_id  设备ID（来源）

&#x20;\* @param priority   优先级

&#x20;\* @param cmd\_type   命令类型

&#x20;\* @param param      参数（占空比）

&#x20;\* @param urgent     是否紧急消息（true=插入队首）

&#x20;\* @return 0=成功，-1=失败

&#x20;\* @note  供所有输入源模块调用，ISR安全

&#x20;\*/

int Arb\_SendCommand(uint8\_t axis\_id,

&#x20;                    uint8\_t device\_id,

&#x20;                    uint8\_t priority,

&#x20;                    uint8\_t cmd\_type,

&#x20;                    float param,

&#x20;                    bool urgent)

{

&#x20;   ArbCommandMsg\_t msg;



&#x20;   if (axis\_id >= MAX\_AXIS\_NUM) {

&#x20;       return -1;

&#x20;   }



&#x20;   msg.axis\_id = axis\_id;

&#x20;   msg.device\_id = device\_id;

&#x20;   msg.priority = priority;

&#x20;   msg.cmd\_type = cmd\_type;

&#x20;   msg.param = param;

&#x20;   msg.timestamp = rt\_tick\_get\_millisecond();



&#x20;   if (urgent) {

&#x20;       /\* 紧急消息插入队首 \*/

&#x20;       return rt\_mq\_urgent(g\_arb\_mq, \&msg, sizeof(ArbCommandMsg\_t));

&#x20;   } else {

&#x20;       return rt\_mq\_send(g\_arb\_mq, \&msg, sizeof(ArbCommandMsg\_t));

&#x20;   }

}

```



\### 4.7 各输入模块调用示例



```c

/\* 示例1：电源极性模块 - 正向 \*/

void Polarity\_OnFwd(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_POWER\_POS,

&#x20;                   PRIO\_POWER,

&#x20;                   CMD\_TYPE\_RUN\_FWD,

&#x20;                   85.0f,   /\* 占空比85% \*/

&#x20;                   false);  /\* 普通消息 \*/

}



/\* 示例2：电源极性模块 - 反向 \*/

void Polarity\_OnRev(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_POWER\_NEG,

&#x20;                   PRIO\_POWER,

&#x20;                   CMD\_TYPE\_RUN\_REV,

&#x20;                   85.0f,

&#x20;                   false);

}



/\* 示例3：电压检测模块 - 过压（阻塞两个方向） \*/

void Voltage\_OnOverVoltage(uint8\_t axis\_id)

{

&#x20;   /\* 阻塞正转 \*/

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_OVERVOLTAGE\_FWD,

&#x20;                   PRIO\_FAULT,

&#x20;                   CMD\_TYPE\_BLOCK\_FWD,

&#x20;                   0.0f,

&#x20;                   true);   /\* 紧急消息 \*/



&#x20;   /\* 阻塞反转 \*/

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_OVERVOLTAGE\_REV,

&#x20;                   PRIO\_FAULT,

&#x20;                   CMD\_TYPE\_BLOCK\_REV,

&#x20;                   0.0f,

&#x20;                   true);

}



/\* 示例4：电压检测模块 - 电压恢复正常（解除阻塞） \*/

void Voltage\_OnNormal(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_OVERVOLTAGE\_FWD,

&#x20;                   PRIO\_FAULT,

&#x20;                   CMD\_TYPE\_UNBLOCK\_FWD,

&#x20;                   0.0f,

&#x20;                   false);



&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_OVERVOLTAGE\_REV,

&#x20;                   PRIO\_FAULT,

&#x20;                   CMD\_TYPE\_UNBLOCK\_REV,

&#x20;                   0.0f,

&#x20;                   false);

}



/\* 示例5：手动IO模块 - 正转按键按下 \*/

void ManualIO\_OnFwdPress(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_IO\_FWD,

&#x20;                   PRIO\_MANUAL,

&#x20;                   CMD\_TYPE\_RUN\_FWD,

&#x20;                   70.0f,

&#x20;                   false);

}



/\* 示例6：手动IO模块 - 正转按键释放 \*/

void ManualIO\_OnFwdRelease(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_IO\_FWD,

&#x20;                   PRIO\_MANUAL,

&#x20;                   CMD\_TYPE\_STOP,

&#x20;                   0.0f,

&#x20;                   false);

}



/\* 示例7：推杆状态模块 - 到达上限位（阻塞正转） \*/

void RodState\_OnExtendLimit(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_ROD\_LIMIT\_FWD,

&#x20;                   PRIO\_LIMIT,

&#x20;                   CMD\_TYPE\_BLOCK\_FWD,

&#x20;                   0.0f,

&#x20;                   true);

}



/\* 示例8：推杆状态模块 - 离开上限位（解除阻塞正转） \*/

void RodState\_OnExtendLimitRelease(uint8\_t axis\_id)

{

&#x20;   Arb\_SendCommand(axis\_id,

&#x20;                   DEV\_ID\_ROD\_LIMIT\_FWD,

&#x20;                   PRIO\_LIMIT,

&#x20;                   CMD\_TYPE\_UNBLOCK\_FWD,

&#x20;                   0.0f,

&#x20;                   false);

}

```



\### 4.8 调试接口（MSH命令）



```c

/\*\*

&#x20;\* @brief 打印轴仲裁状态（MSH命令）

&#x20;\*/

static void cmd\_dump\_arb(int argc, char \*\*argv)

{

&#x20;   uint8\_t axis\_id = 0;



&#x20;   if (argc >= 2) {

&#x20;       axis\_id = atoi(argv\[1]);

&#x20;   }

&#x20;   if (axis\_id >= MAX\_AXIS\_NUM) {

&#x20;       return;

&#x20;   }



&#x20;   Axis\_t \*axis = \&mySystem.axis\[axis\_id];

&#x20;   ArbData\_t \*arb = \&axis->arb;



&#x20;   /\* 加锁读取 \*/

&#x20;   rt\_mutex\_take(\&axis->arb\_mutex, RT\_WAITING\_FOREVER);



&#x20;   MAIN\_D("=== Axis %d Arbitration Status ===", axis\_id);

&#x20;   MAIN\_D("State: %s", (arb->state == MS\_IDLE) ? "IDLE" :

&#x20;                       (arb->state == MS\_RUNNING) ? "RUNNING" : "RAMPING");

&#x20;   MAIN\_D("Active Dir: %s, Duty: %.1f%%",

&#x20;          (arb->active\_dir == DIR\_FWD) ? "FWD" :

&#x20;          (arb->active\_dir == DIR\_REV) ? "REV" : "STOP",

&#x20;          arb->current\_duty);

&#x20;   MAIN\_D("Active Device: %d", arb->active\_device\_id);



&#x20;   MAIN\_D("Block FWD (%d):", arb->block\_fwd.count);

&#x20;   for (int i = 0; i < arb->block\_fwd.count; i++) {

&#x20;       MAIN\_D("  \[%d] dev=%d prio=%d",

&#x20;              i, arb->block\_fwd.records\[i].device\_id,

&#x20;              arb->block\_fwd.records\[i].priority);

&#x20;   }



&#x20;   MAIN\_D("Block REV (%d):", arb->block\_rev.count);

&#x20;   for (int i = 0; i < arb->block\_rev.count; i++) {

&#x20;       MAIN\_D("  \[%d] dev=%d prio=%d",

&#x20;              i, arb->block\_rev.records\[i].device\_id,

&#x20;              arb->block\_rev.records\[i].priority);

&#x20;   }



&#x20;   MAIN\_D("Allow FWD (%d):", arb->allow\_fwd.count);

&#x20;   for (int i = 0; i < arb->allow\_fwd.count; i++) {

&#x20;       MAIN\_D("  \[%d] dev=%d prio=%d duty=%.1f%%",

&#x20;              i, arb->allow\_fwd.records\[i].device\_id,

&#x20;              arb->allow\_fwd.records\[i].priority,

&#x20;              arb->allow\_fwd.records\[i].param);

&#x20;   }



&#x20;   MAIN\_D("Allow REV (%d):", arb->allow\_rev.count);

&#x20;   for (int i = 0; i < arb->allow\_rev.count; i++) {

&#x20;       MAIN\_D("  \[%d] dev=%d prio=%d duty=%.1f%%",

&#x20;              i, arb->allow\_rev.records\[i].device\_id,

&#x20;              arb->allow\_rev.records\[i].priority,

&#x20;              arb->allow\_rev.records\[i].param);

&#x20;   }



&#x20;   MAIN\_D("Conflict Fault: %d", arb->conflict\_fault);

&#x20;   MAIN\_D("Arbitration Count: %u", arb->arbitration\_count);



&#x20;   rt\_mutex\_release(\&axis->arb\_mutex);

}

MSH\_CMD\_EXPORT\_ALIAS(cmd\_dump\_arb, dump\_arb, "dump arb status: dump\_arb \[axis\_id]");



/\*\*

&#x20;\* @brief 查看消息队列状态

&#x20;\*/

static void cmd\_dump\_mq(void)

{

&#x20;   MAIN\_D("=== Message Queue Status ===");

&#x20;   /\* 注：RT-Thread没有直接查询队列深度的API，可记录全局变量 \*/

}

MSH\_CMD\_EXPORT(cmd\_dump\_mq, "dump message queue status");

```



\---



\## 五、消息队列配置参数建议



| 参数 | 推荐值 | 说明 |

|------|--------|------|

| 消息大小 | `sizeof(ArbCommandMsg\_t)` ≈ 16\~20字节 | 根据实际结构体大小 |

| 队列深度 | 16条 | 可缓存约16个事件 |

| 内存池 | 16 × 24 = 384字节 | 适当保留对齐余量 |

| 仲裁线程栈 | 2048字节 | 含递归调用余量 |

| 仲裁线程优先级 | 15 | 高于普通任务，低于中断处理 |

| 互斥量 | 每轴一个 | 支持多轴并发访问 |



\---



\## 六、改造步骤建议



1\. \*\*第一步\*\*：在 `dev\_model.h` 中扩展 `Axis\_t` 结构体，添加 `ArbData\_t` 和 `rt\_mutex\_t`

2\. \*\*第二步\*\*：移植裸机代码中的队列操作函数（`Arb\_CmdList\_\*`）

3\. \*\*第三步\*\*：移植仲裁决策函数（`Arb\_Decision`）

4\. \*\*第四步\*\*：创建消息队列和仲裁线程

5\. \*\*第五步\*\*：修改各输入源模块，将直接调用改为发送消息

6\. \*\*第六步\*\*：实现调试MSH命令

7\. \*\*第七步\*\*：联调测试

