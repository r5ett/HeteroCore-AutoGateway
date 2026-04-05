#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

/* ================== 下面是全局变量和宏定义 ================== */
#define SPI_DATA_LEN 32 /* 假设我们每次从 H7 读回来的数据长度是 32 字节 */

static u8 h7_rx_buffer[SPI_DATA_LEN];  /* 用于保存从H7读回来的波形数据 */
static int h7_data_ready = 0;          /* 数据就绪标志位：0代表没数据，1代表有新数据 */

/* 定义一个等待队列头，用于阻塞应用层的 read() 函数 */
static DECLARE_WAIT_QUEUE_HEAD(h7_wait_queue);

/* 保护缓冲区的互斥锁（防止读数据和写数据发生并发冲突） */
static DEFINE_MUTEX(h7_mutex);

/* ================== 中断处理线程 (底层干活的苦力) ================== */
/* 必须写在 probe 前面，否则编译会报找不到函数的错误 */
static irqreturn_t h7_gateway_irq_thread(int irq, void *dev_id)
{
    struct spi_device *spi = dev_id;
    int ret;
    u8 *tx_cmd;
    u8 *rx_buf;
    struct spi_transfer t;
    struct spi_message m;

    /* ✅ 正确做法：使用 kzalloc 动态分配安全的 DMA 内存 (GFP_KERNEL 允许休眠等待) */
    tx_cmd = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
    rx_buf = kzalloc(SPI_DATA_LEN, GFP_KERNEL);

    if (!tx_cmd || !rx_buf) {
        dev_err(&spi->dev, "Failed to allocate DMA memory!\n");
        ret = -ENOMEM;
        goto out_free; // 分配失败直接退出
    }

    tx_cmd[0] = 0xAA; // 告诉 H7 "我来拿数据了"

    /* 清零并初始化传输结构体 */
    memset(&t, 0, sizeof(t));
    t.tx_buf = tx_cmd;
    t.rx_buf = rx_buf;
    t.len = SPI_DATA_LEN;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    /* 发起真正的底层全双工传输 */
    ret = spi_sync(spi, &m); 
    if (ret == 0) {
        // 1. 获取锁，把收到的数据拷贝到全局缓冲区
        mutex_lock(&h7_mutex);
        memcpy(h7_rx_buffer, rx_buf, SPI_DATA_LEN);
        h7_data_ready = 1; // 标记数据已经准备好
        mutex_unlock(&h7_mutex);

        // 2. 唤醒正在由于 read() 阻塞的应用程序
        wake_up_interruptible(&h7_wait_queue);
    } else {
        dev_err(&spi->dev, "SPI sync failed in IRQ: %d\n", ret);
    }

out_free:
    /* ⚠️ 千万别忘了释放内存，否则会导致内核内存泄漏！ */
    if (tx_cmd) kfree(tx_cmd);
    if (rx_buf) kfree(rx_buf);

    return IRQ_HANDLED;
}

/* ================== 下面是和字符设备对接的核心 ================== */
static int spi_drv_open(struct inode *inode, struct file *file){
    return 0;
}

static int spi_drv_close(struct inode *inode, struct file *file){
    return 0;
}

static ssize_t spi_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset){
    size_t copy_len;
    int ret;

    // 1. 核心机制：如果没有数据，且用户没有设置非阻塞模式(O_NONBLOCK)，则休眠等待
    // 当中断函数执行 wake_up_interruptible 时，这里才会被唤醒往下走
    wait_event_interruptible(h7_wait_queue, h7_data_ready == 1);

    // 2. 确定要拷贝的数据长度（防止用户态给的 buffer 太小导致越界）
    copy_len = min(size, (size_t)SPI_DATA_LEN);

    // 3. 将内核数据拷贝到用户态
    mutex_lock(&h7_mutex);
    ret = copy_to_user(buf, h7_rx_buffer, copy_len);
    h7_data_ready = 0; // 数据被读走了，清除标志位
    mutex_unlock(&h7_mutex);

    if (ret) {
        return -EFAULT; // 如果拷贝失败，返回错误码
    }

    return copy_len; // 返回实际读取到的字节数
}

static ssize_t spi_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset){
    return size;
}

static struct file_operations spi_fops = {
    .owner = THIS_MODULE,
    .open = spi_drv_open, /* 设备文件被打开时调用 */
    .release = spi_drv_close, /* 设备文件被关闭时调用 */
    .read = spi_drv_read, /* 设备文件被读取时调用 */
    .write = spi_drv_write, /* 设备文件被写入时调用 */
};

/* ================== 下面是和 SPI 总线对接的核心 ================== */
/* 定义 miscdevice 结构体，这会帮你自动创建 /dev/h7_data 节点 */
static struct miscdevice h7_misc_dev = {
    .minor = MISC_DYNAMIC_MINOR, // 让内核自动分配次设备号
    .name  = "h7_data",          // 这个就是将来出现在 /dev 下的名字：/dev/h7_data
    .fops  = &spi_fops,
};

/* SPI 设备被匹配到时调用 */
static int h7_gateway_probe(struct spi_device *spi)
{
    int ret;

    /* A. 强制配置 SPI 硬件参数 */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000; // 1MHz
    ret = spi_setup(spi);
    if (ret < 0) {
        dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
        return ret;
    }

    /* B. 注册字符设备节点 /dev/h7_data */
    ret = misc_register(&h7_misc_dev);
    if (ret < 0) {
        dev_err(&spi->dev, "misc_register failed: %d\n", ret);
        return ret;
    }

    /* C. 申请硬件中断（使用线程化中断） */
    ret = devm_request_threaded_irq(&spi->dev, spi->irq, 
                                    NULL, h7_gateway_irq_thread, 
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 
                                    "h7_ready", spi);
    if (ret < 0) {
        dev_err(&spi->dev, "request threaded irq failed: %d\n", ret);
        misc_deregister(&h7_misc_dev); // 如果中断申请失败，记得注销设备
        return ret;
    }
    
    dev_info(&spi->dev, "H7 Gateway Driver Probed Successfully!\n");
    return 0;
}

/* SPI 设备被卸载时调用 */
static int h7_gateway_remove(struct spi_device *spi)
{
    /* 注销字符设备 */
    misc_deregister(&h7_misc_dev);
    dev_info(&spi->dev, "H7 Gateway Driver Removed!\n");
    return 0;
}

/* 设备树匹配表 */
static const struct of_device_id h7_gateway_of_match[] = {
    {.compatible = "mydev,h7-gateway"}, /* 设备树中的compatible属性 */
    {},
};

MODULE_DEVICE_TABLE(of, h7_gateway_of_match);

/* 定义 SPI 驱动结构体 */
static struct spi_driver h7_gateway_driver = {
    .probe = h7_gateway_probe, /* 当driver和device匹配时自动调用 */
    .remove = h7_gateway_remove, /* 驱动卸载时调用 */
    .driver = {
        .name = "h7_gateway",
        .of_match_table = h7_gateway_of_match,
        .owner = THIS_MODULE,
    },
};

// 3. 一键注册宏 (替代手写 module_init / module_exit)
module_spi_driver(h7_gateway_driver);

// /* ================== 下面是模块的入口和出口函数 ================== */
// /* 模块入口函数 */
// static int __init spi_drv_init(void){

// }

// /* 模块出口函数 */
// static void __exit spi_drv_exit(void){

// }

// module_init(spi_drv_init);
// module_exit(spi_drv_exit);
MODULE_LICENSE("GPL");