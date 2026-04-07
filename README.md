# HeteroCore-AutoGateway (异构多核多协议车载网关系统)

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Status](https://img.shields.io/badge/Status-Completed-brightgreen.svg)
![Protocol](https://img.shields.io/badge/Protocol-SPI%20DMA%20|%20CAN%202.0B-orange.svg)

---

## 📖 项目简介
本项目是一个全栈式的**异构多核车载网关演示系统**。系统模拟了现代汽车电子电气架构（EEA），由运行 Linux 的高性能中央网关（i.MX6ULL）与两个功能不同的边缘 MCU 节点（STM32H7 / STM32F103）组成。

项目核心展示了如何在不同算力、不同实时性要求的硬件之间，通过高速 **SPI DMA** 和工业级 **CAN 总线** 实现安全、可靠的数据路由与状态监控。

---

## 🏗️ 系统架构拓扑

```text
       [ 高频姿态感知节点 ]             [ 中央网关大脑 ]              [ 车身控制节点 ]
       STM32H7 (裸机)                 i.MX6ULL (Linux)            STM32F103 (FreeRTOS)
      +----------------+             +----------------+            +------------------+
      | MPU6050 (I2C)  |             |  Select-Based  |            |  Tire Pressure   |
      | Checksum Calc  |--- SPI ---->|  Multiplexing  |<--- CAN ---|  Door Status     |
      | DMA + D-Cache  |  (20MHz)    |  Kernel kfifo  |   (500k)   |  Priority Alarm  |
      +----------------+             +----------------+            +------------------+
🚀 核心技术亮点
1. 极致性能的 SPI 通信链路
高速 DMA 传输：实现在 20MHz 速率下的 SPI DMA 稳定通信，大幅降低大数据量下的 CPU 占用率。

硬核 Cache 管理：针对 STM32H7 的 Cortex-M7 架构，解决了 32 字节对齐的 D-Cache 刷新陷阱，确保了 DMA 传输与内存数据的一致性。

端到端校验 (E2E Checksum)：自定义 32 字节通信协议帧，包含单字节累加和校验，确保数据在高速传输中的绝对完整。

2. Linux 多路复用网关中心
异步事件处理：应用层采用 select() 机制同时监听 SPI 字符设备与 SocketCAN 接口，实现高效的非阻塞报文路由。

内核级无锁队列：驱动层引入 kfifo 环形缓冲区，有效平滑了高速硬件中断与用户态读取之间的速度波动。

实时监控 Dashboard：基于终端 ANSI 控制码实现固定行刷新的仪表盘，直观展示加速度姿态、胎压及故障状态。

3. 车规级实时控制逻辑
任务优先级抢占：F103 侧基于 FreeRTOS 任务优先级机制，确保 0x050 紧急故障帧 在按键触发后秒级响应并抢占总线。

故障自愈机制：实现了报警触发、锁存以及 2 秒后自动发送清零帧的逻辑闭环。

📂 目录结构说明
IMX6ULL_Linux_Gateway/ : 包含 Linux 字符设备驱动及 select 多路复用网关程序。

H7_HighSpeed_Node/ : STM32H7 姿态感知节点工程，包含 I2C 驱动、D-Cache 同步逻辑及 SPI 从机通信。

F103_FreeRTOS_CAN_Node/ : STM32F103 车身控制节点工程，基于 FreeRTOS 实现多路 CAN 报文发送。

🛠️ 如何快速开始
驱动加载：在 i.MX6ULL 终端执行 insmod spi_drv.ko 以创建 /dev/h7_data 节点。

启动网关：运行 ./central_gateway 进入实时监控模式。

设备启动：先运行 Linux 端程序，然后依次复位 STM32H7 和 STM32F103 节点以建立同步连接。

功能验证：

摇晃 H7 观察加速度变化。

按下 F103 故障键观察 Dashboard 红色报警提醒。