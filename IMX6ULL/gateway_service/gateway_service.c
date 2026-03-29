#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main() {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;

    // 1. 创建并绑定 SocketCAN 套接字
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket Error");
        return -1;
    }
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind Error");
        return -1;
    }

    printf("======================================================\n");
    printf("[Linux Gateway] System Ready. Listening on can0 at 500kbps...\n");
    printf("======================================================\n");

    // 2. 核心路由与解析大循环
    while (1) {
        int nbytes = read(s, &frame, sizeof(struct can_frame));

        if (nbytes > 0) {
            switch (frame.can_id) {
                
                // --------- [解析节点 1: H7 MPU6050 高频姿态数据] ---------
                case 0x301: {
                    // 将收到的两个 8 字节拼装回 16 位有符号整数
                    int16_t raw_ax = (int16_t)((frame.data[0] << 8) | frame.data[1]);
                    int16_t raw_ay = (int16_t)((frame.data[2] << 8) | frame.data[3]);
                    int16_t raw_az = (int16_t)((frame.data[4] << 8) | frame.data[5]);
                    
                    // 除以灵敏度 (16384 LSB/g) 得到真实的物理量 (g)
                    float ax_g = raw_ax / 16384.0f;
                    float ay_g = raw_ay / 16384.0f;
                    float az_g = raw_az / 16384.0f;
                    
                    // 打印解析后的物理量
                    printf("[Node: H7] MPU6050 -> AX: %6.4fg | AY: %6.4fg | AZ: %6.4fg\n", ax_g, ay_g, az_g);
                    break;
                }

                // --------- [解析节点 2: F103 车门状态] ---------
                case 0x101:
                    if (frame.data[0] == 0x01) {
                        printf("[Node: F103] \033[33mDoor Status: OPEN\033[0m\n");
                    } else {
                        printf("[Node: F103] Door Status: CLOSED\n");
                    }
                    break;

                // --------- [解析节点 3: F103 胎压数据] ---------
                case 0x201: {
                    uint16_t tpms_val = (frame.data[0] << 8) | frame.data[1];
                    printf("[Node: F103] Tire Pressure: %d kPa\n", tpms_val);
                    break;
                }

                // --------- [解析节点 4: F103 紧急故障报警] ---------
                case 0x050:
                    printf("\033[31m[CRITICAL ALARM] F103 Hardware Fault Detected!\033[0m\n");
                    break;

                default:
                    // 忽略其他无关 ID
                    break;
            }
        }
    }
    close(s);
    return 0;
}