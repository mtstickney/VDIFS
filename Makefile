# VDIFS module makefile
obj-m := vdifs.o
vdifs-objs := inode.o super.o
KERNELDIR = /home/mts/dev/linux-2.6.36.2-uml
ARCH = um

default:
	make -C $(KERNELDIR) M=$(shell pwd) modules ARCH=$(ARCH)

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean ARCH=$(ARCH)

