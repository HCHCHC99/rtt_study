/*
 * msh_rtt.c — 把 finsh/msh 的输入源从 uart4 换到 SEGGER RTT（一根 SWD/J-Link 线完成打印+命令）
 *
 * 原理（RT-Thread 4.1.1，均已在源码核实）：
 *  - tshell 从"输入设备"逐字节读入，读空则睡在 rx_sem 上，由设备 rx_indicate 回调唤醒
 *    （rt-thread/components/finsh/shell.c:172-174）；
 *  - 回显/提示符/命令输出本就经 rt_kprintf -> RTT（applications/rtt_console.c 强覆盖），
 *    因此只需提供"RTT 输入设备"并 finsh_set_device("rtt") 绑定，输出零改动；
 *  - 本设备 read = SEGGER_RTT_Read 下行通道 0 = J-Link RTT Viewer Terminal 的键入；
 *  - RTT 下行无中断：20ms 轮询线程查 SEGGER_RTT_HasData()，有数据就调 rx_indicate 回调；
 *  - 绑定时机：finsh 的 shell 在 INIT_APP_EXPORT(finsh_system_init) 才创建（shell.c:778/804），
 *    晚于本文件的 INIT_ENV_EXPORT(5)，故 finsh_set_device 由轮询线程首拍执行（那时初始化必已结束）。
 *
 * 开关：rtt_manager.h 的 MSH_RTT_EN（1=上电绑 RTT；0=维持 uart4 现状）。改后需重编译。
 * 注意：tshell 只有一个输入源，绑 RTT 后 uart4 不再有 msh；要还原改 MSH_RTT_EN=0 重编译。
 */
#include <rtthread.h>
#include "rtt_manager.h"
#include "RTT/SEGGER_RTT.h"

/* 原型在 components/finsh/finsh.h:172；applications 编现行无该目录 -I，此处自声明（与原型一致） */
extern void finsh_set_device(const char *device_name);

#define RTT_DOWN_CH     0           /* RTT Viewer Terminal 键入所在下行通道 */
#define RTT_POLL_MS     20          /* 下行轮询周期 */
#define RTT_POLL_PRIO   19          /* ≤19：比 tshell(20) 更紧急，首拍换绑 RTT 不被饿死 */
#define RTT_POLL_STACK  512

static struct rt_device g_rtt_dev;

static rt_err_t rtt_dev_init(rt_device_t dev)
{
    return RT_EOK;
}

static rt_err_t rtt_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    return RT_EOK;                  /* finsh 以 RDWR|INT_RX|STREAM 打开，无需处理 */
}

static rt_err_t rtt_dev_close(rt_device_t dev)
{
    return RT_EOK;
}

/* tshell 逐字节读（shell.c: rt_device_read(dev, -1, &ch, 1)）；空时返回 0，靠 rx_sem 唤醒重试 */
static rt_size_t rtt_dev_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    return SEGGER_RTT_Read(RTT_DOWN_CH, buf, size);
}

/* 兜底输出（回显主路径是 rt_kprintf -> RTT，一般不走这里） */
static rt_size_t rtt_dev_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    return SEGGER_RTT_Write(0, buf, size);
}

/* RTT 下行没有中断：轮询线程有数据就调 rx_indicate 回调（finsh_rx_ind 里释放 rx_sem）。
   device.c 只有 setter 没有 public 触发函数，故直呼回调成员（rtdef.h:1082 无条件存在） */
static void rtt_rx_poll_entry(void *parameter)
{
    /* shell 在 INIT_APP_EXPORT(finsh_system_init, shell.c:804) 里才创建（:778 rt_calloc），
       本线程创建于 INIT_ENV_EXPORT(5)。线程优先级 19（< tshell 20，防互相饿死），
       因此首拍必然晚于全部初始化，此时 shell 已就绪，可安全绑定 */
    finsh_set_device("rtt");
    /* ref_count==1 证明 rt_device_open 成功（device.c：open 返回 EOK 才自增）。
       finsh_set_device 的失败路径是静默的（shell.c:225-241 open 非 EOK 无 else 无报错），
       用 ref_count 把"绑没绑上"暴露到日志 */
    MAIN_D_SYNC("msh input -> RTT ch0 (RTT Viewer Terminal). uart4 detached (bind ref=%d)",
                (int)g_rtt_dev.ref_count);

    while (1)
    {
        rt_size_t n = SEGGER_RTT_HasData(RTT_DOWN_CH);

        if ((n != 0U) && (g_rtt_dev.rx_indicate != RT_NULL))
        {
            g_rtt_dev.rx_indicate(&g_rtt_dev, n);
        }
        rt_thread_mdelay(RTT_POLL_MS);
    }
}

static int msh_rtt_init(void)
{
#if MSH_RTT_EN
    rt_thread_t tid;

    /* 本工程未开 RT_USING_DEVICE_OPS，用直成员回调（rtdef.h:1089-1094） */
    g_rtt_dev.type    = RT_Device_Class_Char;
    g_rtt_dev.init    = rtt_dev_init;
    g_rtt_dev.open    = rtt_dev_open;
    g_rtt_dev.close   = rtt_dev_close;
    g_rtt_dev.read    = rtt_dev_read;
    g_rtt_dev.write   = rtt_dev_write;
    g_rtt_dev.control = RT_NULL;

    if (rt_device_register(&g_rtt_dev, "rtt", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        MAIN_D_SYNC("msh_rtt: register 'rtt' failed, msh stays on uart4");
        return 0;
    }

    tid = rt_thread_create("rtt_rx", rtt_rx_poll_entry, RT_NULL,
                           RTT_POLL_STACK, RTT_POLL_PRIO, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    /* 注意：此处不能直接 finsh_set_device("rtt")——finsh 的 shell 尚未创建
       （finsh_system_init 是 INIT_APP_EXPORT，晚于本 INIT_ENV_EXPORT），
       提前调用会命中 shell.c:214 RT_ASSERT(shell != RT_NULL)（已实测）。
       绑定由 rtt_rx 线程首拍完成。 */
#endif /* MSH_RTT_EN */
    return 0;
}
INIT_ENV_EXPORT(msh_rtt_init);      /* 仅注册设备+起轮询线程；绑定在线程首拍（见上注释） */

/* EOF */
