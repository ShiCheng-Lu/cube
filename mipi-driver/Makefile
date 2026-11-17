obj-m += panel-jadard-jd9365tx.o

KDIR ?= /lib/modules/$(shell uname -r)/build

OVERLAY  = vc4-kms-dsi-ht05s-30mi-a1
DTS_FILE = $(OVERLAY)-overlay.dts

all: module overlay

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

overlay: $(OVERLAY).dtbo

$(OVERLAY).dtbo: $(DTS_FILE)
	cpp -nostdinc -I $(KDIR)/include -undef -x assembler-with-cpp $< $(OVERLAY).pre.dts
	dtc -@ -I dts -O dtb -o $@ $(OVERLAY).pre.dts

# Doesn't use KDIR, so it works without a kernel tree (e.g. in WSL)
clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd .*.o Module.symvers modules.order
	rm -f $(OVERLAY).pre.dts $(OVERLAY).dtbo

.PHONY: all module overlay clean
