# DriverTest
add follows in drivers/Kconfig:
source "drivers/hello/Kconfig"

add follows in drivers/Makefile:
obj-$(CONFIG_HELLO)             += hello/

From KERNEL_DEFCONFIG defined in board android .mk, got default kconfig file and add follows:
CONFIG_HELLO=y

(ie, device/emdoor/em_tf100_l3mme/em_tf100_l3mme.mk:40:KERNEL_DEFCONFIG ?= em_tf100_l3mme_defconfig)

