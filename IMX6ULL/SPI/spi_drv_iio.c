#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/slab.h>

#define SPI_DATA_LEN 32

struct h7_gateway_state {
    struct spi_device *spi;
    struct mutex lock;
    u8 rx_buf[SPI_DATA_LEN] ____cacheline_aligned;
    struct {
        s16 channels[3]; // X, Y, Z
        s64 ts __aligned(8);
    } scan;
};

/* 1. 补全通道定义 */
static const struct iio_chan_spec h7_channels[] = {
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .scan_index = 0,
        .scan_type = { .sign = 's', .realbits = 16, .storagebits = 16, .endianness = IIO_BE },
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .scan_index = 1,
        .scan_type = { .sign = 's', .realbits = 16, .storagebits = 16, .endianness = IIO_BE },
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .scan_index = 2,
        .scan_type = { .sign = 's', .realbits = 16, .storagebits = 16, .endianness = IIO_BE },
    },
    IIO_CHAN_SOFT_TIMESTAMP(3),
};

/* 2. 补全 read_raw 接口：防止内核注册时因找不到指针而崩溃 */
static int h7_read_raw(struct iio_dev *indio_dev,
                       struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask)
{
    return -EINVAL; // 虽然我们用 Buffer，但这个函数必须存在
}

static const struct iio_info h7_iio_info = {
    .driver_module = THIS_MODULE,
    .read_raw = h7_read_raw, // 关键补全
};

static irqreturn_t h7_gateway_irq_thread(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct h7_gateway_state *st = iio_priv(indio_dev);
    struct spi_transfer t = { .rx_buf = st->rx_buf, .len = SPI_DATA_LEN };
    struct spi_message m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    if (spi_sync(st->spi, &m) == 0) {
        memcpy(st->scan.channels, st->rx_buf, 6);
        iio_push_to_buffers_with_timestamp(indio_dev, &st->scan, iio_get_time_ns(indio_dev));
    }
    return IRQ_HANDLED;
}

static int h7_gateway_probe(struct spi_device *spi)
{
    struct iio_dev *indio_dev;
    struct h7_gateway_state *st;
    struct iio_buffer *buffer;
    int ret;

    /* 初始化设备 */
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
    if (!indio_dev) return -ENOMEM;

    st = iio_priv(indio_dev);
    st->spi = spi;
    
    indio_dev->name = "h7_gateway";
    indio_dev->info = &h7_iio_info;
    indio_dev->channels = h7_channels;
    indio_dev->num_channels = ARRAY_SIZE(h7_channels);
    indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

    /* 4.9 内核特有 Buffer 配置 */
    buffer = iio_kfifo_allocate();
    if (!buffer) return -ENOMEM;
    iio_device_attach_buffer(indio_dev, buffer);

    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 20000000;
    spi_setup(spi);

    ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, h7_gateway_irq_thread,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "h7_ready", indio_dev);
    if (ret < 0) {
        iio_kfifo_free(buffer);
        return ret;
    }

    return devm_iio_device_register(&spi->dev, indio_dev);
}

static int h7_gateway_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    if (indio_dev->buffer) iio_kfifo_free(indio_dev->buffer);
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
    .driver = { .name = "h7_gateway", .of_match_table = h7_gateway_of_match },
};
module_spi_driver(h7_gateway_driver);
MODULE_LICENSE("GPL");