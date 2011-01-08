# VDIFS module makefile
obj-m := vdifs.o
vdifs-y := inode.o
KERNELDIR = /home/mts/dev/linux-2.6.36.2-uml

default:
	make -C $(KERNELDIR) M=$(shell pwd) modules ARCH=um

clean:
	rm -f  rm -f *.o *.ko *.mod.c Module.symvers modules.order
