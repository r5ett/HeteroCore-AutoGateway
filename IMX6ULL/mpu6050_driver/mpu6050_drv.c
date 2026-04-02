#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/i2c.h>

/* ================== 下面是全局变量和宏定义 ================== */
#define MPU6050_DEVICE_ID 0x68 /* MPU6050 的设备ID */
static struct i2c_client *mpu6050_client;/* I2C 客户端结构体指针 */
static int major = 0; /* 保存内核分配的主设备号 */
static struct class *mpu6050_class; /* 设备类 */

/* ================== 下面是和字符设备对接的核心 ================== */
/* 设备文件被读取时调用 */
static ssize_t mpu6050_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset){
    uint8_t data[6];
    int ret;

    // 1. 从 MPU6050 的 0x3B 寄存器开始，连续读 6 个字节 (加速度 X, Y, Z)
    // 内核帮手函数：直接读出一块数据
    ret = i2c_smbus_read_i2c_block_data(mpu6050_client, 0x3B, 6, data);
    if (ret < 0) {
        return -EIO;
    }

    // 2. 将内核空间读到的数据，安全地拷贝给应用层 (这就是大名鼎鼎的 copy_to_user)
    if (copy_to_user(buf, data, 6)) {
        return -EFAULT;
    }

    return 6; // 返回成功读取的字节数
}

/* 设备文件被写入时调用 */
static ssize_t mpu6050_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset){
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

/* 设备文件被打开时调用 */
static int mpu6050_drv_open(struct inode *node, struct file *file){
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

/* 设备文件被关闭时调用 */
static int mpu6050_drv_close(struct inode *node, struct file *file){
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

/* 定义字符设备的操作函数结构体 */
static struct file_operations mpu6050_drv ={
    .owner   = THIS_MODULE, /* 指定模块所有者 */
    .open    = mpu6050_drv_open,
    .read    = mpu6050_drv_read,
    .write   = mpu6050_drv_write,
    .release = mpu6050_drv_close,
};

/* ================== 下面是和 I2C 总线对接的核心 ================== */
/* I2C 设备被匹配到时调用 */
static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id){
    uint8_t who_am_i;
    uint8_t wake_up_cmd[2] = {0x6B, 0x00}; // 寄存器 0x6B，写入 0x00 解除休眠

    printk("MPU6050 matched! I2C Address: 0x%x\\n", client->addr);
    mpu6050_client = client; // 保存下来

    // 1. 验明正身：读取 0x75 (WHO_AM_I) 寄存器
    // i2c_smbus_read_byte_data 是内核提供的一个极其方便的帮手函数
    who_am_i = i2c_smbus_read_byte_data(client, 0x75);
    if (who_am_i != 0x68 && who_am_i != 0x70) {
        printk("Error: Not an MPU6050! WHO_AM_I = 0x%x\\n", who_am_i);
        return -ENODEV;
    }
    printk("MPU6050 Verified. WHO_AM_I = 0x%x\\n", who_am_i);

    // 2. 唤醒硬件：向 0x6B 寄存器写 0x00
    i2c_master_send(client, wake_up_cmd, 2);

    // 3. 注册字符设备，让用户空间可以通过 /dev/mpu6050 来访问这个设备
    major = register_chrdev(0, "mpu6050", &mpu6050_drv); // 0 代表让内核自动分配主设备号
    if (major < 0) {
        printk("Failed to register chrdev!\n");
        return major;
    }
    printk("MPU6050 chrdev registered with major %d\n", major);

    // 4. 创建设备节点 /dev/mpu6050
    mpu6050_class = class_create(THIS_MODULE, "mpu6050_class");
    if (IS_ERR(mpu6050_class)) {
        printk("Failed to create device class\n");
        unregister_chrdev(major, "mpu6050");
        return PTR_ERR(mpu6050_class);
    }
    device_create(mpu6050_class, NULL, MKDEV(major, 0), NULL, "mpu6050");
    return 0; // 返回 0 表示 probe 成功！
}

/* I2C 设备被卸载时调用 */
static int mpu6050_remove(struct i2c_client *client){
    printk("MPU6050 removed! I2C Address: 0x%x\\n", client->addr);
    unregister_chrdev(major, "mpu6050");
    device_destroy(mpu6050_class, MKDEV(major, 0));
    class_destroy(mpu6050_class);
    return 0;
}

/* 设备树匹配表 */
static const struct of_device_id mpu6050_of_match[] = {
    {.compatible = "r5ett,mpu6050"}, /* 设备树中的compatible属性 */
    {},
};

/* I2C 设备 ID 表 */
static const struct i2c_device_id mpu6050_id_table[] = {
    {"mpu6050", 0},
    {},
};

MODULE_DEVICE_TABLE(of, mpu6050_of_match);

/* 定义 I2C 驱动结构体 */
static struct i2c_driver mpu6050_driver = {
    .probe = mpu6050_probe, /* 当driver和device匹配时自动调用 */
    .remove = mpu6050_remove, /* 驱动卸载时调用 */
    .driver = {
        .name = "r5ett_mpu6050",
        .of_match_table = mpu6050_of_match,
        .owner = THIS_MODULE,
    },
    .id_table = mpu6050_id_table,
};

/* ================== 下面是模块的入口和出口函数 ================== */
/* 模块入口函数 */
static int mpu6050_init(void){
    int err;
    /* 向内核注册 I2C 驱动 */
    err = i2c_add_driver(&mpu6050_driver);
    if (err) {
        pr_err("Failed to register I2C driver\n");
        return err;
    }
    return 0;
}

/* 模块出口函数 */
static void mpu6050_exit(void){
    /* 从内核注销 I2C 驱动 */
    i2c_del_driver(&mpu6050_driver);
}

module_init(mpu6050_init);/* 指定入口函数 */
module_exit(mpu6050_exit);/* 指定出口函数 */
MODULE_LICENSE("GPL");/* 指定模块许可证 */