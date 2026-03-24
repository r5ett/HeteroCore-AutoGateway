# HeteroCore-AutoGateway (异构多核车载网关系统)

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Status](https://img.shields.io/badge/Status-In%20Development-orange.svg)
![CAN Bus](https://img.shields.io/badge/Protocol-CAN%202.0B-green.svg)

## 📖 项目简介
本项目是一个面向汽车电子架构的**异构多核车载网关通信演示系统**。系统模拟了真实车辆中的网络拓扑，由运行 Linux 的高性能中央网关主机（i.MX6ULL），以及运行 FreeRTOS 的高/低频边缘采集节点（STM32H7 / STM32F103）共同组成。各节点之间通过车规级 CAN 2.0B 总线进行高可靠性实时通信。

## 🏗️ 系统架构拓扑

本系统包含三个核心异构节点：

1. **中央网关大脑 (i.MX6ULL - Linux)** - 角色：全车数据路由、指令下发与多模态网关。
   - 状态：⏳ 待开发
2. **高频姿态感知节点 (STM32H7 - FreeRTOS/BareMetal)** - 角色：负责 I2C 高频读取 MPU6050 姿态传感器数据，进行数据融合并打包上报。
   - 状态：⏳ 待开发
3. **低速车身控制节点 (STM32F103 - FreeRTOS)** - 角色：负责车门状态监控、胎压周期上报，以及突发最高优先级硬件故障报警。
   - 状态：✅ 已完成基础架构

## 🛠️ 技术栈与核心亮点

* **操作系统层：** 嵌入式 Linux (主控) + FreeRTOS (边缘节点)
* **通信总线层：** CAN 2.0B 协议（500kbps）、回环测试、总线仲裁与脱离恢复机制
* **RTOS 调度逻辑：** * 基于 `Mutex` (互斥锁) 的多任务底层 CAN 发送资源保护与防死锁设计。
    * 基于 `Binary Semaphore` (二值信号量) 的外部硬件中断 (EXTI) 极速响应与最高优先级任务抢占。
* **人机交互：** I2C OLED 实时状态监控终端。

## 📂 目录结构

```text
├── IMX6ULL_Linux_Gateway/      # 中央网关工程目录 (待更新)
├── H7_HighSpeed_Node/          # H7 高频姿态感知节点目录 (待更新)
└── F103_FreeRTOS_CAN_Node/     # F103 低频车身控制节点目录 (已上传)
    ├── Core/                   # CubeMX 生成的核心代码
    └── README.md               # 该节点的详细配置与任务调度说明