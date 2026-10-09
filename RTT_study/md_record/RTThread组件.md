# RT-Thread 组件

基于 RT-Thread 4.1.1，组件位于 `rt-thread/components/` 目录。

---

## 核心组件

| 组件 | 说明 | Kconfig 位置 |
|------|------|---------------|
| **DFS** | 设备虚拟文件系统，支持 FatFS | `components/dfs/Kconfig:1` |
| **FinSH/MSH** | Shell 命令行接口 | `components/finsh/Kconfig:1` |
| **Drivers** | 设备驱动框架 (串口/I2C/SPI/USB等) | `components/drivers/Kconfig:1` |

---

## 网络组件

| 组件 | 说明 | Kconfig 位置 |
|------|------|---------------|
| **LwIP** | 轻量级 TCP/IP 协议栈 | `components/net/lwip/Kconfig:1` |
| **SAL** | Socket 抽象层 | `components/net/sal/Kconfig:1` |
| **Netdev** | 网络接口设备管理 | `components/net/netdev/Kconfig:1` |
| **AT** | AT 命令支持 (ESP8266/4G模块) | `components/net/at/Kconfig:1` |

---

## 基础库与 POSIX 兼容

| 组件 | 说明 | Kconfig 位置 |
|------|------|---------------|
| **libc/posix** | POSIX 层 (文件系统/线程/时钟) | `components/libc/posix/Kconfig:1` |
| **libc/cplusplus** | C++ 支持 | `components/libc/cplusplus/Kconfig:1` |

---

## 高级组件

| 组件 | 说明 | Kconfig 位置 |
|------|------|---------------|
| **LWP** | 轻量级进程 | `components/lwp/Kconfig:1` |
| **FAL** | Flash 抽象层 | `components/fal/Kconfig:3` |
| **VBus** | 虚拟软件总线 | `components/vbus/Kconfig:1` |

---

## 工具类

| 组件 | 说明 | Kconfig 位置 |
|------|------|---------------|
| **ulog** | 日志框架 | `components/utilities/Kconfig:18` |
| **utest** | 单元测试框架 | `components/utilities/Kconfig:202` |
| **rym** | Ymodem 协议 | `components/utilities/Kconfig:3` |

---

## 搜索关键字速查

| 组件 | 关键字 |
|------|--------|
| 文件系统 | `RT_USING_DFS`, `DFS_USING_POSIX`, `RT_USING_DFS_ELMFAT` |
| Shell | `RT_USING_MSH`, `RT_USING_FINSH`, `FINSH_USING_MSH` |
| 串口 | `RT_USING_SERIAL` |
| I2C | `RT_USING_I2C` |
| SPI | `RT_USING_SPI`, `RT_USING_SFUD` |
| CAN | `RT_USING_CAN` |
| USB | `RT_USING_USB` |
| 网络 | `RT_USING_LWIP`, `RT_USING_NETDEV`, `RT_USING_SAL` |
| AT指令 | `RT_USING_AT`, `AT_USING_CLIENT` |
| POSIX | `RT_USING_POSIX_FS`, `RT_USING_PTHREADS`, `RT_USING_POSIX_CLOCK` |
| C++ | `RT_USING_CPLUSPLUS` |
| 轻量级进程 | `RT_USING_LWP` |
| Flash抽象层 | `RT_USING_FAL` |
| 日志 | `RT_USING_ULOG` |
| 测试 | `RT_USING_UTEST` |
