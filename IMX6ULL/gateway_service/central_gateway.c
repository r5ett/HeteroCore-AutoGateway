#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// ==========================================
// 全局状态缓存 (用于 Dashboard 渲染)
// ==========================================
float latest_ax = 0.0, latest_ay = 0.0, latest_az = 0.0;
int latest_tpms = 0;
int latest_door = -1; // -1: 等待数据, 0: 关闭, 1: 打开
int alarm_active = 0; // 0: 正常, 1: 报警

// 渲染仪表盘的函数
void render_dashboard() {
    // \033[H 将光标移动到屏幕左上角 (不闪烁)
    printf("\033[H"); 
    
    printf("======================================================\n");
    printf("[Central Gateway] Real-Time Dashboard Active          \n");
    printf("======================================================\n");
    
    // 1. 打印 SPI 高频数据 (尾部多加几个空格，防止残影)
    printf("[SPI-H7]   AX: %6.3fg | AY: %6.3fg | AZ: %6.3fg       \n", 
           latest_ax, latest_ay, latest_az);
    
    // 2. 打印 CAN 胎压数据
    if (latest_tpms > 0) {
        printf("[CAN-F103] Tire Pressure: %3d kPa                   \n", latest_tpms);
    } else {
        printf("[CAN-F103] Tire Pressure: Waiting...              \n");
    }

    // 3. 打印 CAN 车门数据
    if (latest_door == 1) {
        printf("[CAN-F103] Door Status  : \033[33mOPEN \033[0m                   \n");
    } else if (latest_door == 0) {
        printf("[CAN-F103] Door Status  : CLOSED                  \n");
    } else {
        printf("[CAN-F103] Door Status  : Waiting...              \n");
    }

    // 4. 报警系统
    printf("------------------------------------------------------\n");
    if (alarm_active) {
        printf("\033[31m[CRITICAL ALARM] Hardware Fault Detected!     \033[0m\n");
    } else {
        printf("\033[32m[SYSTEM STATUS ] All Nodes Normal             \033[0m\n");
    }
    
    fflush(stdout); // 强制立刻输出到终端
}

int main() {
    int spi_fd, can_fd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    
    // 清屏 (\033[2J) 并将光标移到左上角 (\033[H)
    printf("\033[2J\033[H");
    
    printf("Initializing interfaces...\n");

    // 1. 初始化 SPI
    spi_fd = open("/dev/h7_data", O_RDONLY);
    if (spi_fd < 0) {
        perror("Failed to open /dev/h7_data");
        return -1;
    }

    // 2. 初始化 CAN
    system("ip link set can0 down 2>/dev/null");
    system("ip link set can0 type can bitrate 500000 2>/dev/null");
    system("ip link set can0 up 2>/dev/null");

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    strcpy(ifr.ifr_name, "can0");
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    int max_fd = (spi_fd > can_fd ? spi_fd : can_fd) + 1;

    // 清屏准备进入仪表盘模式
    printf("\033[2J");

    // 3. 多路复用循环
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(spi_fd, &read_fds);
        FD_SET(can_fd, &read_fds);

        int ret = select(max_fd, &read_fds, NULL, NULL, NULL);
        if (ret < 0) break;

        // 如果 SPI 有数据
        if (FD_ISSET(spi_fd, &read_fds)) {
            uint8_t rx_buffer[32] = {0};
            int bytes = read(spi_fd, rx_buffer, 32);
            if (bytes == 32) {
                uint8_t checksum = 0;
                for(int i = 0; i < 31; i++) checksum += rx_buffer[i];
                
                if (checksum == rx_buffer[31]) {
                    int16_t raw_x = (int16_t)((rx_buffer[0] << 8) | rx_buffer[1]);
                    int16_t raw_y = (int16_t)((rx_buffer[2] << 8) | rx_buffer[3]);
                    int16_t raw_z = (int16_t)((rx_buffer[4] << 8) | rx_buffer[5]);
                    
                    latest_ax = raw_x / 16384.0f;
                    latest_ay = raw_y / 16384.0f;
                    latest_az = raw_z / 16384.0f;
                    
                    render_dashboard(); // 更新界面
                }
            }
        }

        // 如果 CAN 有数据
        if (FD_ISSET(can_fd, &read_fds)) {
            struct can_frame frame;
            int bytes = read(can_fd, &frame, sizeof(frame));
            if (bytes > 0) {
                switch (frame.can_id) {
                    case 0x101:
                        latest_door = frame.data[0];
                        break;
                    case 0x201: 
                        latest_tpms = (frame.data[0] << 8) | frame.data[1];
                        break;
                    case 0x050:
                        // 如果发来的第一个字节不是 0，说明有故障
                        if (frame.data[0] != 0x00) {
                            alarm_active = 1; // 触发报警
                        } else {
                            // 如果发来的字节是 0x00，说明故障排除了！
                            alarm_active = 0; // 解除报警
                        }
                        break;
                }
                render_dashboard(); // 更新界面
            }
        }
    }

    close(spi_fd);
    close(can_fd);
    return 0;
}