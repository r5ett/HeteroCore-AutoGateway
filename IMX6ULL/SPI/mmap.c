#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <signal.h>
#include <errno.h>

#define MAX_FRAMES 100

/* mmap 共享结构体 (必须与驱动完全一致) */
struct h7_mmap_ctrl {
    volatile uint32_t head_idx;
    volatile uint32_t update_count;
    uint8_t tx_dummy[32];
    uint8_t buffer[MAX_FRAMES][32];
};

struct h7_mmap_ctrl *ctrl;
uint32_t last_count = 0;

/* --- 全局数据缓存 --- */
float latest_ax = 0.0, latest_ay = 0.0, latest_az = 0.0;
int latest_tpms = 0, latest_door = -1, alarm_active = 0;

/* --- 信号标志位 (必须加 volatile 和 sig_atomic_t 保证原子性) --- */
volatile sig_atomic_t h7_data_ready = 0;

/* ========================================================
 * 异步信号处理函数：只做最少的工作，绝不拖泥带水
 * ======================================================== */
void sigio_handler(int signum) {
    h7_data_ready = 1; // 告诉主循环：底层有新数据了！
}

/* ========================================================
 * 仪表盘渲染引擎
 * ======================================================== */
void render_dashboard(uint32_t count) {
    printf("\033[H"); 
    printf("======================================================\n");
    printf("[HeteroCore] FASYNC + Zero-Copy + CAN Dashboard\n");
    printf("======================================================\n");
    printf("[SPI-H7]   Update: %-8u | AX: %6.3fg | AY: %6.3fg | AZ: %6.3fg\n", count, latest_ax, latest_ay, latest_az);
    printf("[CAN-F103] Tire Pressure: %3d kPa                   \n", latest_tpms);
    printf("[CAN-F103] Door Status  : %s                         \n", 
           (latest_door == 1 ? "\033[33mOPEN\033[0m" : (latest_door == 0 ? "CLOSED" : "WAIT")));
    printf("------------------------------------------------------\n");
    if (alarm_active) printf("\033[31m[CRITICAL ALARM] Hardware Fault Detected!     \033[0m\n");
    else printf("\033[32m[SYSTEM STATUS ] All Nodes Normal             \033[0m\n");
    fflush(stdout);
}

int main() {
    int mmap_fd, can_fd, flags;
    struct sockaddr_can addr;
    struct ifreq ifr;

    printf("\033[2J\033[HInitializing Heterogeneous Gateway...\n");

    /* ----------------------------------------------------
     * 1. 初始化 Zero-Copy Mmap
     * ---------------------------------------------------- */
    mmap_fd = open("/dev/h7_mmap", O_RDWR);
    if (mmap_fd < 0) { perror("Open mmap failed"); return -1; }
    ctrl = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
    if (ctrl == MAP_FAILED) { perror("mmap failed"); return -1; }

    /* ----------------------------------------------------
     * 2. 绑定内核异步通知 (FASYNC)
     * ---------------------------------------------------- */
    signal(SIGIO, sigio_handler);
    fcntl(mmap_fd, F_SETOWN, getpid());
    flags = fcntl(mmap_fd, F_GETFL);
    fcntl(mmap_fd, F_SETFL, flags | FASYNC);

    /* ----------------------------------------------------
     * 3. 初始化 CAN 总线 (F103)
     * ---------------------------------------------------- */
    system("ip link set can0 up type can bitrate 500000 2>/dev/null");
    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    strcpy(ifr.ifr_name, "can0");
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    printf("\033[2J"); // 清屏

    /* ----------------------------------------------------
     * 4. 终极多路复用主循环
     * ---------------------------------------------------- */
    while (1) {
        int need_render = 0;

        /* A. 优先处理 H7 的极速异步中断 */
        if (h7_data_ready) {
            h7_data_ready = 0; // 立刻清零，准备迎接下一次中断
            if (ctrl->update_count != last_count) {
                uint32_t read_idx = (ctrl->head_idx == 0) ? (MAX_FRAMES - 1) : (ctrl->head_idx - 1);
                
                // 零拷贝直接读物理内存
                latest_ax = (int16_t)((ctrl->buffer[read_idx][0] << 8) | ctrl->buffer[read_idx][1]) / 16384.0f;
                latest_ay = (int16_t)((ctrl->buffer[read_idx][2] << 8) | ctrl->buffer[read_idx][3]) / 16384.0f;
                latest_az = (int16_t)((ctrl->buffer[read_idx][4] << 8) | ctrl->buffer[read_idx][5]) / 16384.0f;
                
                last_count = ctrl->update_count;
                need_render = 1;
            }
        }

        /* B. 处理 F103 的 CAN 报文 (带超时，防死锁) */
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(can_fd, &read_fds);
        
        // 设置 10ms 超时。这样即使 CAN 没有数据，程序也能快速循环回去检查 h7_data_ready
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; 

        int ret = select(can_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(can_fd, &read_fds)) {
            struct can_frame frame;
            if (read(can_fd, &frame, sizeof(frame)) > 0) {
                switch (frame.can_id) {
                    case 0x101: latest_door = frame.data[0]; break;
                    case 0x201: latest_tpms = (frame.data[0] << 8) | frame.data[1]; break;
                    case 0x050: alarm_active = (frame.data[0] != 0); break;
                }
                need_render = 1;
            }
        } 
        /* C. 捕获并包容被信号打断的情况 */
        else if (ret < 0 && errno != EINTR) {
            // 如果不是因为 SIGIO 打断了 select，而是发生了真实错误，才报错
            perror("select error");
        }

        /* D. 统一渲染屏幕 */
        if (need_render) {
            render_dashboard(last_count);
        }
    }

    /* 资源清理 */
    munmap(ctrl, 4096);
    close(mmap_fd);
    close(can_fd);
    return 0;
}