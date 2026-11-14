# Name of your module (matches the C filename without .c)
obj-m += jd9365tx_dsi.o

# Point to the active kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
