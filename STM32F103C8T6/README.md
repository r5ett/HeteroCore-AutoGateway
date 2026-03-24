# 边缘节点：STM32F103 低速车身控制中心

本项目是 HeteroCore-AutoGateway 异构网关系统中的低频数据采集节点，基于 **STM32F103** 与 **FreeRTOS** 构建，主要负责车身常规状态的监测与极高优先级突发故障的上报。

## ⚙️ 硬件接口分配
* **CAN 总线:** PA11 (CAN_RX), PA12 (CAN_TX) - 外接 CAN 收发器
* **OLED 屏幕 (I2C):** PB6 (SCL), PB7 (SDA) - 实时状态显示终端
* **故障模拟按键:** PB12 (EXTI 触发) - 模拟外部灾难性硬件故障

## 🧠 FreeRTOS 任务调度架构 (核心亮点)

本节点包含 4 个核心任务，通过操作系统级的同步机制，完美解决了“周期发送”与“突发抢占”的资源冲突问题：

| 任务名称 | 优先级 | 功能描述 |
| :--- | :--- | :--- |
| `Task_Fault` | **High** | 灾难报警任务。平时**阻塞死等二值信号量**，一旦按键触发 EXTI 中断释放信号量，瞬间抢占 CPU，发送 `0x050` 最高级 CAN 报文。 |
| `Task_Door` | Normal | 车身心跳任务。周期性每隔 2000ms 模拟一次车门状态翻转，发送 `0x101` 数据。 |
| `Task_TPMS` | BelowNormal | 胎压监测任务。周期性采集模拟胎压数据，拆分字节后发送 `0x201` 数据。 |
| `defaultTask` | Normal | UI 渲染任务。负责读取各任务更新的全局变量，驱动 I2C OLED 屏幕进行 10FPS 的动态刷新。 |

## 🛡️ 关键底层防死锁设计 (Interview Focus)

**1. CAN 发送邮箱保护 (Mutex)**
由于 STM32 硬件仅有 3 个 CAN 发送邮箱，多任务并发极易导致寄存器覆写或死锁。本项目封装了统一的 `Send_CAN_Msg()` 接口：
* 使用 **互斥锁 (Mutex)** `osMutexAcquire` 确保同一时刻只有一个任务能操作 CAN 外设。
* 配合 `HAL_CAN_GetTxMailboxesFreeLevel` 检查邮箱余量，若邮箱满载则主动 `osDelay(1)` 交出 CPU，避免死循环阻塞导致的高优先级反转饿死问题。

**2. 中断快进快出 (Binary Semaphore)**
遵循 RTOS 最佳实践，**绝不在中断服务函数 (ISR) 中调用耗时的 CAN 发送 API**。外部按键触发后，仅在 ISR 中释放 `Sem_Fault` 二值信号量，随后立刻退出中断，由调度器唤醒处于休眠态的高优先级任务接管硬件总线。

## 📡 CAN 波特率精细计算 (500 kbps)
* **APB1 Clock:** 36 MHz
* **Prescaler:** 4  (基础 Tq = 9MHz)
* **Time Quanta:** Sync = 1, BS1 = 15, BS2 = 2 (总 Tq 数 = 18)
* **采样点 (Sample Point):** (1 + 15) / 18 = 88.8% (提供极佳的抗干扰与总线延迟容忍度)