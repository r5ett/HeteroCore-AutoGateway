KERN_DIR = /home/book/100ask_imx6ull-sdk/Linux-4.9.88

all:
	make -C $(KERN_DIR) M=$(shell pwd) modules 
	$(CROSS_COMPILE)gcc -o mpu6050_test mpu6050_test.c

clean:
	make -C $(KERN_DIR) M=$(shell pwd) modules clean
	rm -rf modules.order
	rm -f mpu6050_test

obj-m += mpu6050_drv.o
