#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

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

/* ================== 中断处理线程 (DMA 加强版) ================== */
static irqreturn_t h7_gateway_irq_thread(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct h7_gateway_state *st = iio_priv(indio_dev);
    int ret;
    u8 *tx_cmd, *rx_buf;
    struct spi_transfer t;
    struct spi_message m;
    
    // [新增] 用于保存 DMA 物理地址
    dma_addr_t tx_dma, rx_dma; 

    /* 1. 申请内存 (增加 GFP_DMA 标志，确保内存在 DMA 可访问的区域) */
    tx_cmd = kzalloc(SPI_DATA_LEN, GFP_KERNEL | GFP_DMA);
    rx_buf = kzalloc(SPI_DATA_LEN, GFP_KERNEL | GFP_DMA);
    if (!tx_cmd || !rx_buf) goto out_free;

    tx_cmd[0] = 0xAA;

    /* 2. 建立 DMA 映射：将虚拟地址转换为物理地址，并自动处理 Cache 同步！ */
    // DMA_TO_DEVICE: 数据从内存到设备 (发送)
    tx_dma = dma_map_single(&st->spi->dev, tx_cmd, SPI_DATA_LEN, DMA_TO_DEVICE);
    if (dma_mapping_error(&st->spi->dev, tx_dma)) goto out_free;

    // DMA_FROM_DEVICE: 数据从设备到内存 (接收)
    rx_dma = dma_map_single(&st->spi->dev, rx_buf, SPI_DATA_LEN, DMA_FROM_DEVICE);
    if (dma_mapping_error(&st->spi->dev, rx_dma)) {
        dma_unmap_single(&st->spi->dev, tx_dma, SPI_DATA_LEN, DMA_TO_DEVICE);
        goto out_free;
    }

    /* 3. 配置传输结构体 */
    memset(&t, 0, sizeof(t));
    t.tx_buf = tx_cmd;
    t.rx_buf = rx_buf;
    t.tx_dma = tx_dma; // [核心] 告诉底层 SPI 控制器：发这个物理地址
    t.rx_dma = rx_dma; // [核心] 告诉底层 SPI 控制器：收到这里来
    t.len = SPI_DATA_LEN;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    
    /* [终极奥义] 告诉 SPI 核心层："我已经做好了 DMA 映射，你直接用 DMA 硬件搬运！" */
    m.is_dma_mapped = 1; 

    /* 4. 发起传输 (此时 CPU 不会傻傻等，而是硬件自动搬运) */
    ret = spi_sync(st->spi, &m);
    
    /* 5. 传输完成！立刻解除 DMA 映射 (非常重要！否则会导致内存泄漏和 Cache 错乱) */
    dma_unmap_single(&st->spi->dev, rx_dma, SPI_DATA_LEN, DMA_FROM_DEVICE);
    dma_unmap_single(&st->spi->dev, tx_dma, SPI_DATA_LEN, DMA_TO_DEVICE);

    /* 6. 数据处理 */
    if (ret == 0) {
        mutex_lock(&st->lock);
        memcpy(st->rx_buf, rx_buf, SPI_DATA_LEN);
        mutex_unlock(&st->lock);
    }

out_free:
    if (tx_cmd) kfree(tx_cmd);
    if (rx_buf) kfree(rx_buf);
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