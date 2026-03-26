# HeteroCore-AutoGateway 异构多核自动化网关 
## 📡 H7_HighFreq_Sensing_Node (高频姿态感知节点)

本项目是“异构多核自动化网关”系统的核心感知单元。基于 STM32H743IITx 强大的 400MHz 算力，本节点专职负责以 50Hz (20ms) 的高频率读取 MPU6050 传感器的 6 轴姿态数据，并以 500kbps 的波特率通过 FDCAN 总线向全车网络进行广播，供 i.MX6ULL 中央网关及 F103 执行节点调用。

### 🛠️ 硬件拓扑与接线指南

**核心主控：** 硬石 YS-H7Multi 工业控制底板 (搭载 STM32H743IITx)  
**传感器：** MPU6050 六轴加速度计/陀螺仪  
**调试器：** CMSIS-DAP 仿真器  

#### 引脚映射表 (Pinout)

| 模块名称 | STM32H7 引脚 | 接口类型 | 备注说明 |
| :--- | :--- | :--- | :--- |
| **MPU6050** | `PF0` | I2C2_SDA | 硬件 I2C，挂载地址 `0x68` (左移后 `0xD0`) |
| **MPU6050** | `PF1` | I2C2_SCL | 需要外接 3.3V 与 GND |
| **CAN分析仪/总线**| 底板绿色端子 `1H`/`1L` | FDCAN1 | 内部路由至 `PB9`(TX) / `PI9`(RX)，由底板 VP230 芯片转为差分信号 |
| **USB转TTL** | `PD5` | USART2_TX | 波特率 `115200`，用于 PC 端串口日志监控 |
| **USB转TTL** | `PD6` | USART2_RX | |

> **⚠️ 硬件避坑提示：** CAN 总线两端必须开启 120Ω 终端电阻。请确保底板 CAN 接口旁边的拨码开关已打到 `ON`。

---

### ⚙️ 系统时钟与总线配置

* **SYSCLK (CPU 主频):** 400 MHz
* **APB1/APB2/APB3/APB4:** 100 MHz
* **FDCAN1 引擎配置:**
  * 模式：经典 CAN 2.0B 模式 (Classic Mode)
  * 波特率：**500 kbps** (Nominal Prescaler=10, TimeSeg1=15, TimeSeg2=4, SyncJumpWidth=1)
  * 内存分配：Tx FIFO Queue = 32 容量，Rx FIFO 0 = 32 容量

---

### 📦 CAN 通信矩阵 (矩阵 ID: `0x301`)

本节点以固定频率（50Hz）向总线广播数据帧。
* **帧类型:** 标准数据帧 (Standard Data Frame)
* **数据长度 (DLC):** 8 Bytes

| 字节偏移 | 信号名称 | 数据类型 | 描述 |
| :--- | :--- | :--- | :--- |
| Byte 0 | `Accel_X_High` | `uint8_t` | X轴加速度高 8 位 |
| Byte 1 | `Accel_X_Low`  | `uint8_t` | X轴加速度低 8 位 |
| Byte 2 | `Accel_Y_High` | `uint8_t` | Y轴加速度高 8 位 |
| Byte 3 | `Accel_Y_Low`  | `uint8_t` | Y轴加速度低 8 位 |
| Byte 4 | `Accel_Z_High` | `uint8_t` | Z轴加速度高 8 位 |
| Byte 5 | `Accel_Z_Low`  | `uint8_t` | Z轴加速度低 8 位 |
| Byte 6 | `Reserved`     | `uint8_t` | 预留 (0x00) |
| Byte 7 | `Reserved`     | `uint8_t` | 预留 (0x00) |

*(解析方式：`int16_t ax = (Byte0 << 8) | Byte1;`)*

---

### 🚀 编译与烧录指南 (Keil MDK)

1. 使用 Keil MDK v5 打开 `MDK-ARM/` 目录下的工程文件。
2. 点击魔术棒 ⚙️ -> `Debug` 选项卡，选择 **`CMSIS-DAP Debugger`**。
3. **🚨 极度重要 (解决 Cortex-M7 下载失败报错):**
   * 进入 `Settings` -> `Flash Download` 选项卡。
   * 确保 `Programming Algorithm` 中已添加 `STM32H7x_2048`。
   * 将最上方的 **`RAM for Algorithm`** 强制修改为：Start: **`0x20000000`**, Size: **`0x00020000`**。
   * 勾选 `Reset and Run`。
4. 切换到 `Debug` -> `Settings` -> 左侧 `Connect & Reset Options`，将 Connect 改为 **`under Reset`**，Reset 改为 **`SYSRESETREQ`**。
5. 编译 (F7) 并下载 (F8)。

---

### 💻 预期运行状态

系统复位后，连接 PD5/PD6 的串口助手应输出如下日志：
` ` `text
H7 Node Ready! Starting FDCAN...
Send CAN 0x301 - AX:1024 AY:-512 AZ:16384
Send CAN 0x301 - AX:1012 AY:-510 AZ:16388
...
` ` `
此时，开启 USB-CAN 分析仪上位机，并挂载至 `1H/1L` 总线上，应能看到每隔 20ms 涌入一条 ID 为 `00000301` 的高频报文，晃动开发板时，Data 区前 6 个字节将发生剧烈变化。