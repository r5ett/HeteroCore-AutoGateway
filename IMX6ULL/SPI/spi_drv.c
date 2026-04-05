#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#define SPI_DATA_LEN 32
#define FIFO_SIZE 1024

static DEFINE_KFIFO(h7_fifo, u8, FIFO_SIZE);
static DECLARE_WAIT_QUEUE_HEAD(h7_wait_queue);
static DEFINE_MUTEX(h7_mutex);

/* 纯粹的 PIO 模式中断传输，最稳定，不挑硬件 */
static irqreturn_t h7_gateway_irq_thread(int irq, void *dev_id)
{
    struct spi_device *spi = dev_id;
    int ret;
    u8 *tx_cmd, *rx_buf;
    struct spi_transfer t;
    struct spi_message m;

    tx_cmd = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
    rx_buf = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
    if (!tx_cmd || !rx_buf) goto out_free;

    tx_cmd[0] = 0xAA; // 随便发点什么

    memset(&t, 0, sizeof(t));
    t.tx_buf = tx_cmd;
    t.rx_buf = rx_buf;
    t.len = SPI_DATA_LEN;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    // 阻塞式纯软件搬运，绝对不会破坏状态机
    ret = spi_sync(spi, &m);

    if (ret == 0) {
        mutex_lock(&h7_mutex);
        kfifo_in(&h7_fifo, rx_buf, SPI_DATA_LEN); // 压入环形缓冲区
        mutex_unlock(&h7_mutex);
        wake_up_interruptible(&h7_wait_queue);    // 唤醒应用层
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
    spi->max_speed_hz = 1000000;
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

    /* 🔥 唤醒代码：和中断里的 spi_sync 保持一致，不再导致死锁！ */
    {
        u8 *dummy_tx = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
        if (dummy_tx) {
            struct spi_transfer t = {0};
            struct spi_message m;
            t.tx_buf = dummy_tx;
            t.len = SPI_DATA_LEN;
            spi_message_init(&m);
            spi_message_add_tail(&t, &m);
            spi_sync(spi, &m); // 发送空时钟，骗 H7 开始工作
            kfree(dummy_tx);
        }
    }

    dev_info(&spi->dev, "H7 Stable Gateway Probed!\n");
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