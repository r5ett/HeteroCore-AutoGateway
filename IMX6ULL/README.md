# IMX6ULL 中央网关大脑

本目录包含运行在 i.MX6ULL Linux 系统上的核心网关驱动与应用。

## 🛠️ 技术实现
###驱动程序说明 (Drivers)

spi_drv_iio.ko 
- 核心特性：整合了 Linux IIO 子系统、DMA 传输、mmap 零拷贝及 FASYNC 异步通知技术。
- 性能优化：通过 dma_alloc_coherent 申请一页 (4KB) 一致性 DMA 共享内存，应用层可直接映射访问。
- 事件驱动：利用硬件 DMA 将 SPI 数据直接搬运至缓冲区，并在数据就绪时通过 kill_fasync 向应用层发送 SIGIO 信号。
- 标准接口：同时注册为 IIO 设备，支持通过标准 IIO 缓冲区读取数据。

spi_drv_dma.ko 
- 实现方式：基于 IIO 子系统框架，采用 threaded_irq 实现中断线程化处理。
- 数据传输：在中断线程中使用 dma_map_single 动态映射 DMA 缓冲区，利用硬件循环搬运 SPI 原始数据。

spi_drv.ko (
- 实现方式：基于 miscdevice 杂项设备框架编写。
- 缓存机制：内核层使用 kfifo 环形队列缓存 SPI 接收到的数据，应用层通过标准 read 系统调用获取。

###应用程序说明 (Applications)
mqtt (云端网关程序)
- 主要功能：全量传感器数据聚合与云端同步。
- 技术细节：通过 mmap 指针直接读取 H7 的高频姿态数据，并使用 select 多路复用监听 CAN 总线上的 F103 数据。
- 数据上报：将采集到的六轴加速度、胎压、车门状态等信息封装为 JSON 格式，通过 MQTT 协议推送至巴法云平台。

mmap (高性能本地监控)
- 核心技术：演示“零拷贝”数据传输，通过 signal(SIGIO, ...) 捕获驱动层的异步通知。
- 渲染逻辑：一旦收到信号，直接从映射的共享内存读取数据并刷新终端仪表盘，最大程度降低 CPU 占用。

iio (标准接口测试)
- 功能描述：演示如何使用 Linux 标准工业 I/O 接口获取数据。
- 操作流程：通过 sysfs 接口配置 IIO 缓冲区使能状态，随后从 /dev/iio:deviceX 节点读取结构化数据包。

central_gateway (聚合网关基础版)
- 功能描述：使用传统 read 阻塞模式实现的 SPI + CAN 多源数据聚合程序。

## 硬件连接
拓展板
J1_4<--->SPI_INT
J2_5<--->SPI1_MOSI
J2_6<--->SPI1_MISO
J2_7<--->SPI1_SCLK
J2_8<--->SPI1_CS0




