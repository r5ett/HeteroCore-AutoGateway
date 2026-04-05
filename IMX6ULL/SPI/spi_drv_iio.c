#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/slab.h>

#define SPI_DATA_LEN 32

/* 1. 定义私有数据结构，替代散装的全局变量（高级驱动的标志） */
struct h7_gateway_state {
    struct spi_device *spi;
    struct mutex lock;
    u8 rx_buf[SPI_DATA_LEN]; // 保存最新的中断数据
};

/* 2. 定义 IIO 通道：告诉内核我们提供什么数据 */
// 假设：前 6 个字节是 MPU6050 的 Accel X/Y/Z (每轴 16bit)
#define H7_ACCEL_CHAN(_axis, _index) { \
    .type = IIO_ACCEL, \
    .modified = 1, \
    .channel2 = IIO_MOD_##_axis, \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
    .scan_index = _index, \
}

static const struct iio_chan_spec h7_channels[] = {
    H7_ACCEL_CHAN(X, 0),
    H7_ACCEL_CHAN(Y, 1),
    H7_ACCEL_CHAN(Z, 2),
};

/* ================== 中断处理线程 ================== */
static irqreturn_t h7_gateway_irq_thread(int irq, void *private)
{
    // 注意：这里传进来的不再是 spi，而是 indio_dev
    struct iio_dev *indio_dev = private;
    struct h7_gateway_state *st = iio_priv(indio_dev);
    int ret;
    u8 *tx_cmd, *rx_buf;
    struct spi_transfer t;
    struct spi_message m;

    tx_cmd = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
    rx_buf = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
    if (!tx_cmd || !rx_buf) goto out;

    tx_cmd[0] = 0xAA;
    memset(&t, 0, sizeof(t));
    t.tx_buf = tx_cmd;
    t.rx_buf = rx_buf;
    t.len = SPI_DATA_LEN;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    ret = spi_sync(st->spi, &m);
    if (ret == 0) {
        // 将数据保存到我们专属的 state 结构体中
        mutex_lock(&st->lock);
        memcpy(st->rx_buf, rx_buf, SPI_DATA_LEN);
        mutex_unlock(&st->lock);
    }

out:
    kfree(tx_cmd);
    kfree(rx_buf);
    return IRQ_HANDLED;
}

/* ================== IIO 核心：读函数 ================== */
/* 当应用层读取对应 sysfs 节点时，触发此函数 */
static int h7_read_raw(struct iio_dev *indio_dev,
                       struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask)
{
    struct h7_gateway_state *st = iio_priv(indio_dev);
    s16 raw_data;

    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    mutex_lock(&st->lock);
    // 假设数据格式：buf[0-1]是X轴, buf[2-3]是Y轴, buf[4-5]是Z轴 (大端模式)
    switch (chan->channel2) {
        case IIO_MOD_X:
            raw_data = (st->rx_buf[0] << 8) | st->rx_buf[1];
            break;
        case IIO_MOD_Y:
            raw_data = (st->rx_buf[2] << 8) | st->rx_buf[3];
            break;
        case IIO_MOD_Z:
            raw_data = (st->rx_buf[4] << 8) | st->rx_buf[5];
            break;
        default:
            mutex_unlock(&st->lock);
            return -EINVAL;
    }
    mutex_unlock(&st->lock);

    *val = raw_data;
    return IIO_VAL_INT; // 告诉内核，返回的是一个普通整数
}

static const struct iio_info h7_iio_info = {
    .read_raw = h7_read_raw,
};

/* ================== Probe / Remove ================== */
static int h7_gateway_probe(struct spi_device *spi)
{
    struct iio_dev *indio_dev;
    struct h7_gateway_state *st;
    int ret;

    /* 1. 配置 SPI */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;
    ret = spi_setup(spi);
    if (ret < 0) return ret;

    /* 2. 分配 IIO 设备空间 (连带私有结构体一起分配) */
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
    if (!indio_dev) return -ENOMEM;

    st = iio_priv(indio_dev);
    st->spi = spi;
    mutex_init(&st->lock);

    /* 3. 配置 IIO 属性 */
    indio_dev->name = "h7_mpu6050";
    indio_dev->info = &h7_iio_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = h7_channels;
    indio_dev->num_channels = ARRAY_SIZE(h7_channels);

    /* 4. 申请中断，注意最后参数传的是 indio_dev */
    ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, h7_gateway_irq_thread,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "h7_ready", indio_dev);
    if (ret < 0) return ret;

    /* 5. 破除死锁的强制时钟唤醒 (保留上阶段的精髓) */
    {
        u8 *dummy_tx = kzalloc(SPI_DATA_LEN, GFP_KERNEL);
        if (dummy_tx) {
            struct spi_transfer t = {0};
            struct spi_message m;
            t.tx_buf = dummy_tx;
            t.len = SPI_DATA_LEN;
            spi_message_init(&m);
            spi_message_add_tail(&t, &m);
            spi_sync(spi, &m);
            kfree(dummy_tx);
        }
    }

    /* 6. 注册 IIO 设备到内核 */
    ret = devm_iio_device_register(&spi->dev, indio_dev);
    if (ret < 0) return ret;

    dev_info(&spi->dev, "H7 Gateway IIO Driver Probed!\n");
    return 0;
}

static int h7_gateway_remove(struct spi_device *spi)
{
    // devm_ 函数会自动处理注销，所以这里非常干净
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