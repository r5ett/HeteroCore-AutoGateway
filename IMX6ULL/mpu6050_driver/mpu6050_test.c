#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * 使用方法: ./mpu6050_test /dev/mpu6050
 */
int main(int argc, char **argv) // 👈 修正了这里的 char **
{
    int fd;
    unsigned char buf[6]; // 👈 使用无符号字符，防止位移时符号扩展导致错误
    short x, y, z;        // 👈 MPU6050 的数据是 16 位有符号整数

    /* 1. 判断参数 */
    if (argc != 2) 
    {
        printf("Usage: %s <dev>\n", argv[0]);
        return -1;
    }

    /* 2. 打开文件 */
    fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        printf("Error: can not open file %s\n", argv[1]);
        return -1;
    }

    printf("MPU6050 opened successfully! Start reading data...\n");

    /* 3. 循环读取并打印数据 */
    while (1) 
    {
        if (read(fd, buf, 6) == 6) 
        {
            // 数据解析：MPU6050 是大端模式（高位在前，低位在后）
            x = (buf[0] << 8) | buf[1];
            y = (buf[2] << 8) | buf[3];
            z = (buf[4] << 8) | buf[5];

            // 打印原始加速度数据 (范围大概在 -32768 到 32767 之间)
            // 你可以用 \r 让输出始终保持在同一行，看起来像个仪表盘
            printf("Accel X: %6d | Y: %6d | Z: %6d\r", x, y, z);
            fflush(stdout); 
        } 
        else 
        {
            printf("\nRead data failed!\n");
            break;
        }

        usleep(100000); // 延时 100ms (1秒钟读10次)，防止刷屏太快看清
    }
    
    close(fd);
    return 0;
}