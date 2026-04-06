#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

/* IIO 设备的标准路径 (假设为 device0) */
#define IIO_DEV_PATH     "/dev/iio:device2"
#define IIO_SYSFS_BASE   "/sys/bus/iio/devices/iio:device2/"

/* 数据缓存 */
float latest_ax = 0.0, latest_ay = 0.0, latest_az = 0.0;
int latest_tpms = 0, latest_door = -1, alarm_active = 0;

/* ---------------------------------------------------------
 * 辅助函数：通过 sysfs 配置 IIO 缓冲区
 * --------------------------------------------------------- */
int setup_iio_buffer(void) {
    int fd;
    // 1. 禁用缓冲区以便配置
    fd = open(IIO_SYSFS_BASE "buffer/enable", O_WRONLY);
    write(fd, "0", 1);
    close(fd);

    // 2. 使能加速度通道 (X, Y, Z)
    fd = open(IIO_SYSFS_BASE "scan_elements/in_accel_x_en", O_WRONLY);
    write(fd, "1", 1); close(fd);
    fd = open(IIO_SYSFS_BASE "scan_elements/in_accel_y_en", O_WRONLY);
    write(fd, "1", 1); close(fd);
    fd = open(IIO_SYSFS_BASE "scan_elements/in_accel_z_en", O_WRONLY);
    write(fd, "1", 1); close(fd);
    
    // 3. 使能时间戳通道 (驱动中定义的)
    fd = open(IIO_SYSFS_BASE "scan_elements/in_timestamp_en", O_WRONLY);
    write(fd, "1", 1); close(fd);

    // 4. 设置缓冲区长度
    fd = open(IIO_SYSFS_BASE "buffer/length", O_WRONLY);
    write(fd, "100", 3); close(fd);

    // 5. 开启缓冲区
    fd = open(IIO_SYSFS_BASE "buffer/enable", O_WRONLY);
    if (write(fd, "1", 1) < 0) {
        perror("Failed to enable IIO buffer");
        return -1;
    }
    close(fd);
    return 0;
}

/* 渲染仪表盘 (与之前逻辑一致) */
void render_dashboard() {
    printf("\033[H"); 
    printf("======================================================\n");
    printf("[HeteroCore Gateway] IIO-Standard Dashboard          \n");
    printf("======================================================\n");
    printf("[SPI-H7]   AX: %6.3fg | AY: %6.3fg | AZ: %6.3fg       \n", latest_ax, latest_ay, latest_az);
    printf("[CAN-F103] Tire Pressure: %3d kPa                   \n", latest_tpms);
    printf("[CAN-F103] Door Status  : %s                         \n", 
           (latest_door == 1 ? "\033[33mOPEN\033[0m" : (latest_door == 0 ? "CLOSED" : "WAIT")));
    printf("------------------------------------------------------\n");
    if (alarm_active) printf("\033[31m[CRITICAL ALARM] Hardware Fault Detected!     \033[0m\n");
    else printf("\033[32m[SYSTEM STATUS ] All Nodes Normal             \033[0m\n");
    fflush(stdout);
}

int main() {
    int iio_fd, can_fd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    printf("\033[2J\033[HInitializing Gateway...\n");

    // 1. 配置 IIO 硬件
    if (setup_iio_buffer() < 0) return -1;
    iio_fd = open(IIO_DEV_PATH, O_RDONLY | O_NONBLOCK);
    if (iio_fd < 0) { perror("Open IIO device failed"); return -1; }

    // 2. 配置 CAN 总线
    system("ip link set can0 up type can bitrate 500000 2>/dev/null");
    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    strcpy(ifr.ifr_name, "can0");
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    int max_fd = (iio_fd > can_fd ? iio_fd : can_fd) + 1;
    printf("\033[2J");

    /* 3. 事件循环 */
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(iio_fd, &read_fds);
        FD_SET(can_fd, &read_fds);

        if (select(max_fd, &read_fds, NULL, NULL, NULL) < 0) break;

        // A. 处理 IIO 姿态数据
        if (FD_ISSET(iio_fd, &read_fds)) {
            /* IIO 数据包结构: s16 x 3 (6字节) + 填充 (2字节) + s64 时间戳 (8字节) = 16 字节 */
            struct {
                int16_t channels[3];
                uint16_t padding; 
                int64_t  timestamp;
            } data;

            if (read(iio_fd, &data, sizeof(data)) == sizeof(data)) {
                // IIO 已经在驱动里根据 Scale 处理了，或者应用层手动换算
                // 这里我们假设驱动传回的是 16bit 原始值，MPU6050 为 16384LSB/g
                latest_ax = data.channels[0] / 16384.0f;
                latest_ay = data.channels[1] / 16384.0f;
                latest_az = data.channels[2] / 16384.0f;
                render_dashboard();
            }
        }

        // B. 处理 CAN 数据
        if (FD_ISSET(can_fd, &read_fds)) {
            struct can_frame frame;
            if (read(can_fd, &frame, sizeof(frame)) > 0) {
                switch (frame.can_id) {
                    case 0x101: latest_door = frame.data[0]; break;
                    case 0x201: latest_tpms = (frame.data[0] << 8) | frame.data[1]; break;
                    case 0x050: alarm_active = (frame.data[0] != 0); break;
                }
                render_dashboard();
            }
        }
    }

    close(iio_fd);
    close(can_fd);
    return 0;
}