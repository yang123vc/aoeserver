
#Driver directory 
DRIVER_D = linux/drivers/block/aoeserver


# Get the kernel version
KVER := $(shell uname -r)

# kernel sources for building third-party modules should be found here
KDIR := /lib/modules/$(KVER)/build

#  where to put the aoeserver module
INSTDIR := /lib/modules/$(KVER)/kernel/drivers/block/aoeserver

PWD	:= $(shell pwd)
KMAK_FLAGS := \
	  CONFIG_ATA_OVER_ETH_SERVER=m \
	  KDIR=${KDIR}

default: 
	$(MAKE) -C $(KDIR) $(KMAK_FLAGS) SUBDIRS="$(PWD)/$(DRIVER_D)" modules


clean:
	cd $(DRIVER_D) && rm -f *.o *.ko core


realclean: clean
	cd $(DRIVER_D) && rm -rf .tmp_versions .aoe*.*o.cmd .aoe*.*o.d aoeserver.mod.c

install: default
	@echo "Install directory is $(INSTDIR)"
	mkdir -p $(INSTDIR)
	install -m 644 $(DRIVER_D)/aoeserver.ko $(INSTDIR)

