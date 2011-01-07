# VDIFS module makefile
obj-m := vdifs.o
vdifs-y := inode.o vdifs.o
KERNELDIR = /home/mts/dev/linux-2.6.36.2-uml

default:
	make -C $(KERNELDIR) M=$(shell pwd) modules ARCH=um

clean:
	rm -f  rm -f procinfo.o procinfo.ko procinfo.mod.c procinfo.mod.o Module.symvers	
