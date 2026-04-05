---

### 2. 子目录：`IMX6ULL_Linux_Gateway`
**路径：** `/IMX6ULL_Linux_Gateway/README.md`

```markdown
# IMX6ULL 中央网关大脑

本目录包含运行在 i.MX6ULL Linux 系统上的核心网关驱动与应用。

## 🛠️ 技术实现
1.  **内核驱动 (`spi_drv.c`)**：
    * **架构**：基于 `miscdevice` 的字符设备驱动。
    * **缓冲区**：使用 `kfifo` 环形队列处理高频中断数据流。
    * **加速**：(可选) 支持硬件 DMA 映射，降低极高速率下的 CPU 负载。
2.  **应用网关 (`central_gateway.c`)**：
    * **多路复用**：使用 `select()` 同时监控 SPI 与 SocketCAN。
    * **协议校验**：在应用层实施 32 字节完整性校验，自动丢弃脏数据。
    * **UI 渲染**：基于 ANSI 控制码实现的实时 DashBoard。

## 编译运行
```bash
make
insmod spi_drv.ko
./central_gateway