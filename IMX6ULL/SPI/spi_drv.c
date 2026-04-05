#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/dma-mapping.h> // 引入 DMA 映射头文件
#include <linux/wait.h>
#include <linux/mutex.h>

#define SPI_DATA_LEN 32
#define FIFO_SIZE 1024

static DEFINE_KFIFO(h7_fifo, u8, FIFO_SIZE);
static DECLARE_WAIT_QUEUE_HEAD(h7_wait_queue);
static DEFINE_MUTEX(h7_mutex);

/* 中断处理：完全使用硬件 DMA 搬运数据，解放 CPU */
static irqreturn_t h7_gateway_irq_thread(int irq, void *dev_id)
{
    struct spi_device *spi = dev_id;
    int ret;
    u8 *tx_cmd, *rx_buf;
    struct spi_transfer t;
    struct spi_message m;
    dma_addr_t tx_dma, rx_dma; // 定义物理 DMA 地址变量

    // 1. 分配支持 DMA 访问的内存 (GFP_DMA)
    tx_cmd = kzalloc(SPI_DATA_LEN, GFP_KERNEL | GFP_DMA);
    rx_buf = kzalloc(SPI_DATA_LEN, GFP_KERNEL | GFP_DMA);
    if (!tx_cmd || !rx_buf) goto out_free;

    tx_cmd[0] = 0xAA; 

    // 2. 将虚拟地址映射为物理 DMA 地址，并处理 Cache 一致性
    tx_dma = dma_map_single(&spi->dev, tx_cmd, SPI_DATA_LEN, DMA_TO_DEVICE);
    rx_dma = dma_map_single(&spi->dev, rx_buf, SPI_DATA_LEN, DMA_FROM_DEVICE);

    memset(&t, 0, sizeof(t));
    t.tx_buf = tx_cmd;
    t.rx_buf = rx_buf;
    t.tx_dma = tx_dma; // 填入发送 DMA 物理地址
    t.rx_dma = rx_dma; // 填入接收 DMA 物理地址
    t.len = SPI_DATA_LEN;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    
    // 3. 核心：强制告诉 Linux 内核，这帧数据请使用 DMA 硬件传输！
    m.is_dma_mapped = 1; 

    // 4. 触发传输 (此时 CPU 会立刻休眠，纯靠 SPI DMA 硬件搬砖)
    ret = spi_sync(spi, &m);

    // 5. 传输完成，解除 DMA 映射
    dma_unmap_single(&spi->dev, rx_dma, SPI_DATA_LEN, DMA_FROM_DEVICE);
    dma_unmap_single(&spi->dev, tx_dma, SPI_DATA_LEN, DMA_TO_DEVICE);

    if (ret == 0) {
        mutex_lock(&h7_mutex);
        kfifo_in(&h7_fifo, rx_buf, SPI_DATA_LEN); // 将收到的 DMA 数据压入队列
        mutex_unlock(&h7_mutex);
        wake_up_interruptible(&h7_wait_queue);
    }

out_free:
    if (tx_cmd) kfree(tx_cmd);
    if (rx_buf) kfree(rx_buf);
    return IRQ_HANDLED;
}

static ssize_t spi_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset){
    int ret;
    unsigned int copied;

    wait_event_interruptible(h7_wait_queue, !kfifo_is_empty(&h7_fifo));

    mutex_lock(&h7_mutex);
    ret = kfifo_to_user(&h7_fifo, buf, size, &copied);
    mutex_unlock(&h7_mutex);

    return ret ? -EFAULT : copied;
}

static struct file_operations spi_fops = {
    .owner = THIS_MODULE,
    .read  = spi_drv_read,
};

static struct miscdevice h7_misc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "h7_data",
    .fops  = &spi_fops,
};

static int h7_gateway_probe(struct spi_device *spi)
{
    int ret;
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 20000000;
    ret = spi_setup(spi);
    if (ret < 0) return ret;

    INIT_KFIFO(h7_fifo);
    ret = misc_register(&h7_misc_dev);
    if (ret < 0) return ret;

    ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, h7_gateway_irq_thread,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "h7_ready", spi);
    if (ret < 0) {
        misc_deregister(&h7_misc_dev);
        return ret;
    }

    // 注意：这里我们彻底删除了之前导致死锁的 PIO 唤醒代码！
    // 一切都保持最纯净的 DMA 模式。
    
    dev_info(&spi->dev, "H7 DMA-Accelerated Gateway Probed!\n");
    return 0;
}

static int h7_gateway_remove(struct spi_device *spi)
{
    misc_deregister(&h7_misc_dev);
    return 0;
}

static const struct of_device_id h7_gateway_of_match[] = {
    {.compatible = "mydev,h7-gateway"},
    {},
};
MODULE_DEVICE_TABLE(of, h7_gateway_of_match);

static struct spi_driver h7_gateway_driver = {
    .probe = h7_gateway_probe,
    .remove = h7_gateway_remove,
    .driver = {
        .name = "h7_gateway",
        .of_match_table = h7_gateway_of_match,
    },
};
module_spi_driver(h7_gateway_driver);
MODULE_LICENSE("GPL");