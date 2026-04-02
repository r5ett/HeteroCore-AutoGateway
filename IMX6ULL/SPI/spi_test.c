#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// 默认设备节点，执行时可通过传参覆盖
static const char *device = "/dev/spidev1.0"; 
static uint32_t mode = SPI_MODE_0; // 对应 H7 的 CPOL=LOW, CPHA=1EDGE
static uint8_t bits = 8;           // 对应 H7 的 8 Bits
static uint32_t speed = 1000000;   // 前期测试先用 1MHz

static void transfer(int fd)
{
    int ret;
    // 主机准备发给 H7 的测试数据
    uint8_t tx[] = {0xAA, 0xBB, 0xCC, 0xDD};
    // 用于存放 H7 发回来的数据
    uint8_t rx[ARRAY_SIZE(tx)] = {0, };

    // 核心：全双工传输结构体
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = 0,
        .speed_hz = speed,
        .bits_per_word = bits,
    };

    // 执行 SPI 数据传输 (SPI_IOC_MESSAGE(1) 表示执行 1 个 tr 结构体的传输)
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1) {
        perror("can't send spi message");
        return;
    }

    // 打印接收到的数据
    printf("RX from H7: ");
    for (ret = 0; ret < ARRAY_SIZE(tx); ret++) {
        printf("%02X ", rx[ret]);
    }
    printf("\n");
}

/*
 * ./spi_test /dev/spidev0.0
 */
int main(int argc, char *argv[])
{
    int ret = 0;
    int fd;

    if (argc > 1) {
        device = argv[1];
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("can't open device");
        exit(1);
    }

    /* 1. 设置 SPI 模式 */
    ret = ioctl(fd, SPI_IOC_WR_MODE32, &mode);
    if (ret == -1) {
        perror("can't set spi mode");
        exit(1);
    }

    /* 2. 设置 数据位宽 */
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1) {
        perror("can't set bits per word");
        exit(1);
    }

    /* 3. 设置 最大通信频率 */
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1) {
        perror("can't set max speed hz");
        exit(1);
    }

    printf("SPI settings: Mode %d, %d bits, %d Hz\n", mode, bits, speed);
    printf("Sending  : AA BB CC DD\n");

    /* 4. 发起全双工通信 */
    transfer(fd);

    close(fd);
    return ret;
}