# VDIFS module makefile
obj-m := vdifs.o
vdifs-objs := inode.o super.o
KERNELDIR = /home/mts/dev/linux-2.6.36.2-uml

default:
	make -C $(KERNELDIR) M=$(shell pwd) modules ARCH=um

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean ARCH=um

