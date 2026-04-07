#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int fd;
    uint8_t rx_buffer[32] = {0};
    int16_t raw_x, raw_y, raw_z;
    float ax, ay, az;

    fd = open("/dev/h7_data", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /dev/h7_data");
        exit(1);
    }

    printf("Application started. Waiting for MPU6050 data...\n");
    printf("Format: [Raw Value] -> [Acceleration in g]\n");
    printf("----------------------------------------------\n");

    while (1) {
        // 阻塞读取 32 字节数据
        int bytes_read = read(fd, rx_buffer, 32);

        if (bytes_read == 32) { // 严格校验长度
            
            // ================== [新增] 数据校验拦截 ==================
            uint8_t checksum = 0;
            for(int i = 0; i < 31; i++) {
                checksum += rx_buffer[i];
            }
            
            if (checksum != rx_buffer[31]) {
                // 如果发现错误，不仅报错，还把 32 个字节全部打印出来！
                printf("\n\033[31m[WARN] Checksum Error! Expected: %02X, Got: %02X\033[0m\n", checksum, rx_buffer[31]);
                printf("RAW: ");
                for(int i=0; i<32; i++) printf("%02X ", rx_buffer[i]);
                printf("\n");
                continue; 
            }
            // ==========================================================

            /* 1. 校验通过！这部分数据绝对安全可靠，开始解析 */
            raw_x = (int16_t)((rx_buffer[0] << 8) | rx_buffer[1]);
            raw_y = (int16_t)((rx_buffer[2] << 8) | rx_buffer[3]);
            raw_z = (int16_t)((rx_buffer[4] << 8) | rx_buffer[5]);

            /* 2. 换算为重力加速度 g */
            ax = raw_x / 16384.0f;
            ay = raw_y / 16384.0f;
            az = raw_z / 16384.0f;

            /* 3. 实时刷新打印 */
            printf("X: %6d => %6.3fg | ", raw_x, ax);
            printf("Y: %6d => %6.3fg | ", raw_y, ay);
            printf("Z: %6d => %6.3fg\r",  raw_z, az); 
            fflush(stdout);
            
        } else if (bytes_read < 0) {
            perror("\nRead error");
            break;
        }
    }

    close(fd);
    return 0;
}