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
#include "MQTTClient.h" 

/* ================= 配置区 ================= */
#define MQTT_ADDRESS   "tcp://bemfa.com:9501"
#define CLIENTID       "de415192691548c1a1178eccfb84c26c" // 你的UID
#define TOPIC          "AutoGateway" 
/* ========================================= */

#define MAX_FRAMES 100
struct h7_mmap_ctrl {
    volatile uint32_t head_idx;
    volatile uint32_t update_count;
    uint8_t tx_dummy[32];
    uint8_t buffer[MAX_FRAMES][32];
};

struct h7_mmap_ctrl *ctrl = NULL;
uint32_t last_processed_count = 0;

/* 全量数据变量 */
float ax = 0, ay = 0, az = 0;
int tpms = 0, door = -1, alarm_flag = 0;

volatile sig_atomic_t h7_data_ready = 0;
void sigio_handler(int signum) { h7_data_ready = 1; }

int main() {
    int mmap_fd, can_fd, flags;
    struct sockaddr_can addr;
    struct ifreq ifr;

    // 1. MQTT 初始化 (略，同之前)
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_create(&client, MQTT_ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleansession = 1;
    MQTTClient_connect(client, &conn_opts);

    // 2. 硬件初始化 (略，同之前)
    mmap_fd = open("/dev/h7_mmap", O_RDWR);
    ctrl = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
    signal(SIGIO, sigio_handler);
    fcntl(mmap_fd, F_SETOWN, getpid());
    flags = fcntl(mmap_fd, F_GETFL);
    fcntl(mmap_fd, F_SETFL, flags | FASYNC);

    system("ip link set can0 up type can bitrate 500000 2>/dev/null");
    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    strcpy(ifr.ifr_name, "can0");
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    printf("\033[2J\033[H Gateway Full-Data Running...\n");

    while (1) {
        int need_refresh = 0;

        /* --- 1. H7 数据解析 (AX, AY, AZ) --- */
        if (h7_data_ready) {
            h7_data_ready = 0;
            if (ctrl->update_count != last_processed_count) {
                uint32_t idx = (ctrl->head_idx == 0) ? (MAX_FRAMES - 1) : (ctrl->head_idx - 1);
                
                // 解析三个轴：每轴2字节 (16384 LSB/g)
                ax = (int16_t)((ctrl->buffer[idx][0] << 8) | ctrl->buffer[idx][1]) / 16384.0f;
                ay = (int16_t)((ctrl->buffer[idx][2] << 8) | ctrl->buffer[idx][3]) / 16384.0f;
                az = (int16_t)((ctrl->buffer[idx][4] << 8) | ctrl->buffer[idx][5]) / 16384.0f;
                
                last_processed_count = ctrl->update_count;
                need_refresh = 1;
            }
        }

        /* --- 2. F103 CAN 数据解析 (TPMS, Door, Alarm) --- */
        fd_set r_fds;
        struct timeval tv = {0, 10000};
        FD_ZERO(&r_fds); FD_SET(can_fd, &r_fds);
        if (select(can_fd + 1, &r_fds, NULL, NULL, &tv) > 0) {
            struct can_frame frame;
            if (read(can_fd, &frame, sizeof(frame)) > 0) {
                switch (frame.can_id) {
                    case 0x101: door = frame.data[0]; break;
                    case 0x201: tpms = (frame.data[0] << 8) | frame.data[1]; break;
                    case 0x050: alarm_flag = frame.data[0]; break; // 假设 0x050 是报警 ID
                }
                need_refresh = 1;
            }
        }

        /* --- 3. 全量数据打印与推送 --- */
        if (need_refresh) {
            // 本地显示：加上 AY, AZ 和 Alarm
            printf("\r[Update %u] ACC:%.2f,%.2f,%.2f | TPMS:%d | Door:%d | ALARM:%s ", 
                    last_processed_count, ax, ay, az, tpms, door, alarm_flag ? "\033[31mON\033[0m" : "OFF");
            fflush(stdout);

            // 每 10 次上一次云
            if (last_processed_count % 10 == 0 && MQTTClient_isConnected(client)) {
                char payload[256];
                snprintf(payload, sizeof(payload), 
                        "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"tpms\":%d,\"door\":%d,\"alarm\":%d}", 
                        ax, ay, az, tpms, door, alarm_flag);
                
                MQTTClient_message pubmsg = MQTTClient_message_initializer;
                pubmsg.payload = payload;
                pubmsg.payloadlen = (int)strlen(payload);
                pubmsg.qos = 0;
                MQTTClient_publishMessage(client, TOPIC, &pubmsg, NULL);
            }
        }
    }
    return 0;
}