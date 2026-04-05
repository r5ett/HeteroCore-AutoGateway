KERN_DIR = /home/book/100ask_imx6ull-sdk/Linux-4.9.88

all:
	make -C $(KERN_DIR) M=$(shell pwd) modules 
	$(CROSS_COMPILE)gcc -o spi_test spi_test.c

clean:
	make -C $(KERN_DIR) M=$(shell pwd) modules clean
	rm -rf modules.order
	rm -f spi_test

obj-m += spi_drv.o
obj-m += spi_drv_iio.o
obj-m += spi_drv_dma.o