# 定义编译器
# $(CROSS_COMPILE) 是交叉编译工具链的前缀，比如 arm-buildroot-linux-gnueabihf-
# 系统会自动读取你刚才 export CROSS_COMPILE 设置的环境变量
# 加上 gcc 后，就拼成了完整的交叉编译器命令
CC = $(CROSS_COMPILE)gcc

# 编译参数
# -Wall 表示开启所有警告提示，帮助我们写出更规范的代码
# -O2 表示进行二级代码优化，让程序在开发板上跑得更快
CFLAGS = -Wall -O2

# 定义默认目标
# 当你在终端只输入 make 时，就会默认执行这个目标
# gateway_service 是纯用户态应用程序，不是内核驱动(.ko)，所以不需要 KERN_DIR
# 这里直接调用交叉编译器，把 .c 源码编译成板子能跑的 ARM 可执行文件
all:
	$(CC) $(CFLAGS) -o gateway_service gateway_service.c

# 定义清理目标
# 删除编译生成的可执行文件，保持目录整洁
clean:
	rm -f gateway_service