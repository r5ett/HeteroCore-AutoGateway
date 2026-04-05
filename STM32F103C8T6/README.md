# STM32F103 车身控制节点

基于 FreeRTOS 实时操作系统，负责模拟车身低速传感器的状态上报。

## 📅 任务调度逻辑
1.  **DoorTask (Priority: Normal)**: 每 2000ms 翻转一次车门状态并上报 (CAN ID: 0x101)。
2.  **TPMSTask (Priority: BelowNormal)**: 每 500ms 递增模拟胎压数据并上报 (CAN ID: 0x201)。
3.  **FaultTask (Priority: High)**: 最高优先级任务。由按键中断释放信号量唤醒，发送严重故障报文 (CAN ID: 0x050)，2 秒后自动发送清零帧实现故障自愈。

## 硬件连接
* CAN_TX: PA12
* CAN_RX: PA11
* Fault_Key: PB12