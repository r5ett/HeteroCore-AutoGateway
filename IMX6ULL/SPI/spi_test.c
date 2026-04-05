#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int fd;
    uint8_t rx_buffer[32] = {0};

    // 1. 打开你专属的字符设备
    fd = open("/dev/h7_data", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /dev/h7_data");
        exit(1);
    }

    printf("Application started. Waiting for H7 interrupt (GPIO drop)...\n");

    // 2. 循环等待数据
    while (1) {
        /* * 核心魔法：这一行会“阻塞”（卡住睡觉），完全不占 CPU。
         * 直到 H7 把中断线拉低，底层驱动执行 spi_sync 并 wake_up，
         * 这里才会苏醒，并拿到 32 个字节的数据！
         */
        int bytes_read = read(fd, rx_buffer, 32);

        if (bytes_read > 0) {
            printf("Received %d bytes: ", bytes_read);
            // 打印前 4 个字节看看对不对
            for (int i = 0; i < 4; i++) {
                printf("%02X ", rx_buffer[i]);
            }
            printf("...\n");
        } else {
            perror("Read error");
            break;
        }
    }

    close(fd);
    return 0;
}