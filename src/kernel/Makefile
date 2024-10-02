
ifeq ($(shell grep -Fq "ubuntu" "/etc/os-release" && echo 1 || echo 0), 1)
	EXTRA_CFLAGS += -DPSOS_UBUNTU
else ifeq ($(shell grep -Fq "rhel" "/etc/os-release" && echo 1 || echo 0), 1)
	EXTRA_CFLAGS += -DPSOS_RHEL
endif

obj-m := psdaq.o

path := $(shell uname -r)

all:
	make -C /lib/modules/$(path)/build M=$(shell pwd)  modules
clean:
	make -C /lib/modules/$(path)/build M=$(shell pwd) clean
