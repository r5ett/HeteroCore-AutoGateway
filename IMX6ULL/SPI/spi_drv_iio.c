#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>           /* 设备树解析 */
#include <linux/miscdevice.h>   /* 零拷贝节点注册 */
#include <linux/mm.h>           /* mmap 内存映射 */

#define SPI_DATA_LEN 32
#define SHARED_MEM_SIZE PAGE_SIZE // 申请一页内存 (4KB)
#define MAX_FRAMES 100            // 共享环形缓冲帧数

/* =========================================================================
 * 1. 数据结构定义
 * ========================================================================= */

/* 内核与用户态共享的 DMA 内存结构 (mmap 零拷贝核心) */
struct h7_mmap_ctrl {
    volatile u32 head_idx;                 // 当前 DMA 写到了哪一帧
    volatile u32 update_count;             // 总共更新了多少次
    u8 tx_dummy[SPI_DATA_LEN];             // 供 SPI DMA 用的安全发送缓冲区 (全0)
    u8 buffer[MAX_FRAMES][SPI_DATA_LEN];   // 真正的数据存储区
};

/* 驱动私有状态结构体 */
struct h7_gateway_state {
    struct spi_device *spi;
    struct mutex lock;
    
    /* mmap 零拷贝核心变量 */
    struct h7_mmap_ctrl *mmap_vaddr; // 内核虚拟地址 (CPU 看的)
    dma_addr_t mmap_paddr;           // 物理总线地址 (DMA 看的)
    struct fasync_struct *async_queue;
    struct miscdevice mmap_dev;      // 专门用于 mmap 的字符设备节点
    
    /* IIO 缓冲区用的临时扫描结构体 */
    struct {
        s16 channels[3];
        s64 ts __aligned(8);
    } scan;
};

/* =========================================================================
 * 2. IIO 子系统配置 (通道声明与操作函数)
 * ========================================================== */

/* 通道定义：针对 4.9 内核补全 scan_type，防止注册时崩溃 */
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

/* 必须存在的 read_raw 回调，防止内核因指针为空报错 */
static int h7_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask)
{
    return -EINVAL; // 业务数据都通过 Buffer 或 mmap 读取
}

static const struct iio_info h7_iio_info = {
    .driver_module = THIS_MODULE,
    .read_raw = h7_read_raw,
};

/* =========================================================================
 * 3. Mmap 零拷贝操作函数
 * ========================================================================= */

static int h7_mmap_op(struct file *file, struct vm_area_struct *vma)
{
    struct h7_gateway_state *st = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > SHARED_MEM_SIZE) return -EINVAL;

    /* 关闭 Cache：必须让应用层读到 DMA 搬运后的实时真实物理内存 */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    /* 将 DMA 物理地址映射到用户态的 vma */
    if (remap_pfn_range(vma, vma->vm_start, st->mmap_paddr >> PAGE_SHIFT, 
                        size, vma->vm_page_prot)) {
        return -EAGAIN;
    }
    return 0;
}

/* 当应用层调用 fcntl 开启 FASYNC 时，会触发此函数 */
static int h7_mmap_fasync(int fd, struct file *filp, int mode)
{
    struct h7_gateway_state *st = filp->private_data;
    // fasync_helper 是内核提供的标准帮手，帮我们把应用层的进程挂到队列里
    return fasync_helper(fd, filp, mode, &st->async_queue);
}

/* 当应用层意外崩溃或关闭文件时，必须把它从队列里踢出去，防止内核向死进程发信号 */
static int h7_mmap_release(struct inode *inode, struct file *file)
{
    h7_mmap_fasync(-1, file, 0);
    return 0;
}

static int h7_mmap_open(struct inode *inode, struct file *file)
{
    struct miscdevice *cdev = file->private_data;
    struct h7_gateway_state *st = container_of(cdev, struct h7_gateway_state, mmap_dev);
    file->private_data = st; 
    return 0;
}

static const struct file_operations h7_mmap_fops = {
    .owner   = THIS_MODULE,
    .open    = h7_mmap_open,
    .mmap    = h7_mmap_op, 
    .fasync  = h7_mmap_fasync,  
    .release = h7_mmap_release, 
};

/* =========================================================================
 * 4. 中断处理函数 (核心底层引擎)
 * ========================================================================= */

static irqreturn_t h7_gateway_irq_thread(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct h7_gateway_state *st = iio_priv(indio_dev);
    u32 idx = st->mmap_vaddr->head_idx;
    
    struct spi_transfer t = {0};
    struct spi_message m;

    /* 配置 DMA 直接写入一致性物理内存，0次CPU拷贝！ */
    t.tx_buf = st->mmap_vaddr->tx_dummy;
    t.tx_dma = st->mmap_paddr + offsetof(struct h7_mmap_ctrl, tx_dummy);
    t.rx_buf = st->mmap_vaddr->buffer[idx];
    t.rx_dma = st->mmap_paddr + offsetof(struct h7_mmap_ctrl, buffer) + (idx * SPI_DATA_LEN);
    t.len = SPI_DATA_LEN;
    
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    m.is_dma_mapped = 1; /* 激活纯硬件 DMA 模式 */

    if (spi_sync(st->spi, &m) == 0) {
        /* 推入 IIO Buffer 供标准 Linux 工具测试使用 */
        memcpy(st->scan.channels, st->mmap_vaddr->buffer[idx], 6);
        iio_push_to_buffers_with_timestamp(indio_dev, &st->scan, iio_get_time_ns(indio_dev));

        /* 更新 mmap 环形指针，唤醒应用层 */
        st->mmap_vaddr->head_idx = (idx + 1) % MAX_FRAMES;
        st->mmap_vaddr->update_count++;

        if (st->async_queue && (st->mmap_vaddr->update_count % 100 == 0)) {
            // 向应用层发射 SIGIO 信号！
            kill_fasync(&st->async_queue, SIGIO, POLL_IN);
        }
    }
    return IRQ_HANDLED;
}

/* =========================================================================
 * 5. 驱动加载与卸载 (Probe / Remove)
 * ========================================================================= */

static int h7_gateway_probe(struct spi_device *spi)
{
    struct iio_dev *indio_dev;
    struct h7_gateway_state *st;
    struct iio_buffer *buffer;
    struct device_node *np = spi->dev.of_node;
    u32 custom_speed = 1000000; /* 保底 1MHz */
    int ret;

    /* --- 1. 设备树(DTS)动态解析 --- */
    if (np) {
        if (of_property_read_u32(np, "gateway,custom-speed-hz", &custom_speed) == 0) {
            dev_info(&spi->dev, "DTS Parsed: custom-speed-hz = %u\n", custom_speed);
        } else {
            dev_warn(&spi->dev, "DTS Missing custom speed, using default 1MHz!\n");
        }
    }

    /* --- 2. IIO 设备分配与初始化 --- */
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
    if (!indio_dev) return -ENOMEM;

    st = iio_priv(indio_dev);
    st->spi = spi;
    mutex_init(&st->lock);

    indio_dev->name = "h7_gateway";
    indio_dev->info = &h7_iio_info;
    indio_dev->channels = h7_channels;
    indio_dev->num_channels = ARRAY_SIZE(h7_channels);
    indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

    /* 配置 IIO Buffer (4.9 标准 API) */
    buffer = iio_kfifo_allocate();
    if (!buffer) return -ENOMEM;
    iio_device_attach_buffer(indio_dev, buffer);

    /* =======================================================
     * 核心修复：强制赋予 SPI 设备 32 位 DMA 寻址掩码
     * 解决 coherent DMA mask is unset 报错
     * ======================================================= */
    if (!spi->dev.dma_mask) {
        spi->dev.dma_mask = &spi->dev.coherent_dma_mask;
    }
    spi->dev.coherent_dma_mask = DMA_BIT_MASK(32);
    /* ======================================================= */

    /* --- 3. 开辟 Mmap 一致性 DMA 内存 --- */
    st->mmap_vaddr = dma_alloc_coherent(&spi->dev, SHARED_MEM_SIZE, &st->mmap_paddr, GFP_KERNEL);
    if (!st->mmap_vaddr) {
        dev_err(&spi->dev, "Failed to allocate DMA memory!\n");
        ret = -ENOMEM;
        goto err_free_iio;
    }
    memset(st->mmap_vaddr, 0, SHARED_MEM_SIZE); /* 刷零 */

    /* 注册零拷贝专用字符设备节点 */
    st->mmap_dev.minor = MISC_DYNAMIC_MINOR;
    st->mmap_dev.name  = "h7_mmap";
    st->mmap_dev.fops  = &h7_mmap_fops;
    ret = misc_register(&st->mmap_dev);
    if (ret) goto err_free_dma;

    /* --- 4. 配置并启动 SPI 硬件 (应用 DTS 解析出的速率) --- */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = custom_speed; /* 动态速率 */
    ret = spi_setup(spi);
    if (ret) goto err_deregister_misc;

    /* --- 5. 申请中断 --- */
    ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, h7_gateway_irq_thread,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "h7_ready", indio_dev);
    if (ret < 0) goto err_deregister_misc;

    /* --- 6. 注册 IIO 设备到内核 --- */
    ret = devm_iio_device_register(&spi->dev, indio_dev);
    if (ret < 0) goto err_deregister_misc;

    dev_info(&spi->dev, "HeteroCore Gateway IIO+Mmap Driver Probed Successfully!\n");
    return 0;

/* --- 异常回滚处理机制 (防止资源泄漏) --- */
err_deregister_misc:
    misc_deregister(&st->mmap_dev);
err_free_dma:
    dma_free_coherent(&spi->dev, SHARED_MEM_SIZE, st->mmap_vaddr, st->mmap_paddr);
err_free_iio:
    iio_kfifo_free(buffer);
    return ret;
}

static int h7_gateway_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct h7_gateway_state *st = iio_priv(indio_dev);

    /* 清理我们申请的所有系统资源 */
    misc_deregister(&st->mmap_dev);
    dma_free_coherent(&spi->dev, SHARED_MEM_SIZE, st->mmap_vaddr, st->mmap_paddr);
    if (indio_dev->buffer) {
        iio_kfifo_free(indio_dev->buffer);
    }
    
    dev_info(&spi->dev, "HeteroCore Gateway Driver Removed.\n");
    return 0;
}

/* =========================================================================
 * 6. 平台设备树匹配与模块注册
 * ========================================================================= */

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
MODULE_DESCRIPTION("Heterogeneous Gateway IIO & Mmap Zero-Copy Driver");