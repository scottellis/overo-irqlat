# cross-compile module makefile

ifneq ($(KERNELRELEASE),)
    obj-m := irqlat.o
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules 
endif

install:
	sudo cp irqlat.ko /exports/overo/home/root

clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .irqlat* .tmp_versions

endif

