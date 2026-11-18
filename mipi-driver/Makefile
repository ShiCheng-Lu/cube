# The module's object list is in Kbuild; the kernel build reads that, not this file.

OVERLAY  = vc4-kms-dsi-ht05s-30mi-a1
DTS_FILE = $(OVERLAY)-overlay.dts
MODULE   = panel-jadard-jd9365tx.ko

RPI_BUILD = /rpi-build
POOL      = http://archive.raspberrypi.com/debian/pool/main/l/linux
# The Pi's `uname -r` for cross-compiling; bootstrap-wsl saves it in .pi-release
PI_REL   ?= $(shell cat .pi-release 2>/dev/null)
# Only needed when the archive has more than one build of PI_REL:
# PI_OS=trixie|bookworm (what the Pi runs) or DEB_VER=<exact Debian version>
PI_OS    ?=
DEB_VER  ?=
BUILD     = $(RPI_BUILD)/$(PI_REL)

# Cross-compile against the tree from bootstrap-wsl if there is one,
# otherwise build natively against the running kernel (on the Pi)
ifneq ($(and $(PI_REL),$(wildcard $(BUILD)/linux/Module.symvers)),)
KDIR  ?= $(BUILD)/linux
CROSS  = ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
else
KDIR  ?= /lib/modules/$(shell uname -r)/build
CROSS  =
endif

WSL_PKGS = bc bison flex libssl-dev libelf-dev libncurses-dev make \
           crossbuild-essential-arm64 device-tree-compiler dpkg-dev kmod \
           curl xz-utils

# For the kernel tree's make inside bootstrap-wsl. Not written as $$(MAKE) there,
# because make runs any recipe that mentions it even for `make -n`.
KMAKE = $(MAKE)

SHELL := /bin/bash
.ONESHELL:
.SHELLFLAGS := -eo pipefail -c

all: module overlay

module:
	@echo "Building for $(if $(CROSS),$(PI_REL) (cross),the running kernel)"
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(CROSS) modules
	if [ -n "$(CROSS)" ]; then
	    # The Pi rejects the module unless this starts with its exact uname -r
	    modinfo -F vermagic $(MODULE)
	    modinfo -F vermagic $(MODULE) | grep -q '^$(PI_REL) ' || { echo "vermagic doesn't match $(PI_REL)"; exit 1; }
	fi

overlay: $(OVERLAY).dtbo

$(OVERLAY).dtbo: $(DTS_FILE)
	cpp -nostdinc -I $(KDIR)/include -undef -x assembler-with-cpp $< $(OVERLAY).pre.dts
	dtc -@ -I dts -O dtb -o $@ $(OVERLAY).pre.dts

# Doesn't use KDIR, so it works without a kernel tree
clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd .*.o Module.symvers modules.order
	rm -f $(OVERLAY).pre.dts $(OVERLAY).dtbo


# ---- Runs bootstrap-wsl or bootstrap-rpi depending on where it's run
#      (WSL kernels are "...-microsoft-standard-WSL2", Pi kernels "...+rpt-rpi-...")
bootstrap:
	@case "$$(uname -r)" in
	    *[Mm]icrosoft*) $(MAKE) bootstrap-wsl ;;
	    *+rpt-*)        $(MAKE) bootstrap-rpi ;;
	    *) echo "Not WSL or Raspberry Pi OS ($$(uname -r)); run 'make bootstrap-wsl' or 'make bootstrap-rpi'"
	       exit 1 ;;
	esac

# ---- On the Pi: compiler, dtc and headers for the running kernel
bootstrap-rpi:
	@rel=$$(uname -r)
	flavour=$${rel##*+rpt-}            # e.g. rpi-2712
	sudo apt update
	sudo apt full-upgrade -y
	# The meta package keeps the headers in step with future kernel upgrades
	sudo apt install -y build-essential device-tree-compiler linux-headers-$$flavour
	if [ -f /lib/modules/$$rel/build/Makefile ]; then
	    echo "Ready: run 'make'"
	else
	    echo "No headers for the running kernel $$rel; the upgrade probably"
	    echo "installed a newer kernel. Run 'sudo reboot', then 'make'."
	fi


# ---- In WSL: download and prepare the Pi kernel's exact source in $(BUILD)
#      make bootstrap-wsl PI_REL=<the Pi's uname -r>
bootstrap-wsl:
	@PI_REL='$(PI_REL)'
	[ -n "$$PI_REL" ] || { echo "Add the Pi's kernel release: PI_REL=<output of uname -r on the Pi>"; exit 1; }
	pi_ver=$${PI_REL%%+*}              # e.g. 6.18.50
	#
	# Tools (only asks for sudo if something is missing)
	missing=$$(for p in $(WSL_PKGS); do
	    dpkg-query -W -f='$${Status}\n' $$p 2>/dev/null | grep -q 'ok installed' || echo $$p
	done)
	if [ -n "$$missing" ]; then
	    sudo apt update
	    sudo apt install -y $$missing
	fi
	if [ ! -w $(RPI_BUILD) ]; then
	    sudo mkdir -p $(RPI_BUILD)
	    sudo chown "$$USER:" $(RPI_BUILD)
	fi
	#
	# Debian version of the package (Trixie and Bookworm builds of the same
	# kernel have different symbol CRCs, so it must be the Pi's one)
	deb_ver='$(DEB_VER)'
	if [ -z "$$deb_ver" ]; then
	    found=$$(curl -fsS $(POOL)/ | grep -o "linux-headers-$${PI_REL}_[^\"]*_arm64.deb" \
	             | sed "s/^linux-headers-$${PI_REL}_//; s/_arm64.deb$$//" | sort -u || true)
	    case '$(PI_OS)' in
	        bookworm) found=$$(grep bookworm <<<"$$found" || true) ;;
	        trixie)   found=$$(grep -v bookworm <<<"$$found" || true) ;;
	    esac
	    n=$$(grep -c . <<<"$$found" || true)
	    if [ "$$n" = 0 ]; then
	        echo "No headers package for $$PI_REL in the archive (typo, or not a Raspberry Pi OS kernel?)"
	        exit 1
	    elif [ "$$n" != 1 ]; then
	        echo "Several builds of $$PI_REL exist:"; sed 's/^/    /' <<<"$$found"
	        echo "Add PI_OS=trixie or PI_OS=bookworm (what the Pi runs), or DEB_VER=<one of the above>."
	        echo "The Pi's 'uname -v' names the exact one."
	        exit 1
	    fi
	    deb_ver=$$found
	fi
	echo "Using $$PI_REL, package version $$deb_ver"
	mkdir -p $(BUILD)
	cd $(BUILD)
	#
	# Downloads (skipped if already present)
	for f in linux-headers-$${PI_REL}_$${deb_ver}_arm64.deb linux_$${pi_ver}.orig.tar.xz \
	         linux_$${deb_ver}.debian.tar.xz linux_$${deb_ver}.dsc; do
	    if [ ! -f "$$f" ]; then
	        curl -fL -o "$$f.part" "$(POOL)/$$f"
	        mv "$$f.part" "$$f"
	    fi
	done
	#
	# .config, Module.symvers and kernel.release of the Pi's kernel
	rm -rf headers
	dpkg-deb -x linux-headers-$${PI_REL}_$${deb_ver}_arm64.deb headers
	hdr=$$PWD/headers/usr/src/linux-headers-$$PI_REL
	[ "$$(cat $$hdr/include/config/kernel.release)" = "$$PI_REL" ] || { echo "headers package is for a different release"; exit 1; }
	#
	# Kernel source + Debian/Raspberry Pi patches. Skipped if every patch in
	# the series is applied; redone if a previous run was interrupted.
	series() { grep -v '^\s*#' linux/debian/patches/series | sed 's/\s.*//' | grep .; }
	if [ -f linux/.pc/applied-patches ] && diff -q <(series) linux/.pc/applied-patches >/dev/null; then
	    echo "Kernel source already extracted and patched"
	else
	    rm -rf linux linux_*.tmp-extract.*
	    echo "Extracting and patching the kernel source (a minute or two, no output)..."
	    dpkg-source -x linux_$${deb_ver}.dsc linux | { grep -v '^dpkg-source: info: applying' || true; }
	fi
	#
	# Prepare the tree for building external modules
	cd linux
	cp $$hdr/.config .config
	# The Pi's .config carries a stale CONFIG_LOCALVERSION; Debian sets the real
	# suffix (e.g. "+rpt-rpi-2712") at package build time.
	./scripts/config --set-str LOCALVERSION "$${PI_REL#$$pi_ver}" --disable LOCALVERSION_AUTO
	cross='ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-'
	$(KMAKE) --no-print-directory $$cross olddefconfig
	$(KMAKE) --no-print-directory $$cross KERNELRELEASE=$$PI_REL modules_prepare
	cp $$hdr/Module.symvers .
	#
	# The Pi rejects the module unless the release string matches exactly
	[ "$$(cat include/config/kernel.release)" = "$$PI_REL" ] || { echo "kernel.release mismatch"; exit 1; }
	grep -q "\"$$PI_REL\"" include/generated/utsrelease.h || { echo "UTS_RELEASE mismatch"; exit 1; }
	[ "$$($(KMAKE) -s --no-print-directory $$cross LOCALVERSION= kernelrelease)" = "$$PI_REL" ] || { echo "kernelrelease mismatch"; exit 1; }
	#
	echo "$$PI_REL" > $(CURDIR)/.pi-release
	echo "Ready: run 'make' to cross-compile for $$PI_REL"

.PHONY: all module overlay clean bootstrap bootstrap-rpi bootstrap-wsl
