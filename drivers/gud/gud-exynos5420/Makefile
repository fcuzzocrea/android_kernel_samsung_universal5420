#
# Makefile for the kernel mobicore drivers for Exynos5420
#
GUD_ROOT_FOLDER := $(srctree)/drivers/gud/gud-exynos5420

# add our modules to kernel.
obj-$(CONFIG_MOBICORE_API) += mcKernelApi.o
obj-$(CONFIG_MOBICORE_DRIVER) += mcDrvModule.o

mcDrvModule-objs := MobiCoreDriver/logging.o \
		MobiCoreDriver/ops.o \
		MobiCoreDriver/mem.o \
		MobiCoreDriver/api.o \
		MobiCoreDriver/pm.o \
		MobiCoreDriver/main.o

mcKernelApi-objs := MobiCoreKernelApi/main.o \
		MobiCoreKernelApi/clientlib.o \
		MobiCoreKernelApi/device.o \
		MobiCoreKernelApi/session.o \
		MobiCoreKernelApi/connection.o

# Include build_tag.h for main.c
CFLAGS_MobiCoreDriver/main.o := -include $(GUD_ROOT_FOLDER)/gud-exynos5420/build_tag.h

# Release mode by default
subdir-ccflags-y += -DNDEBUG
subdir-ccflags-y += -Wno-declaration-after-statement

subdir-ccflags-$(CONFIG_MOBICORE_DEBUG) += -DDEBUG
subdir-ccflags-$(CONFIG_MOBICORE_VERBOSE) += -DDEBUG_VERBOSE

# Hardcode platform here
MOBICORE_PLATFORM := EXYNOS_5420_STD

# Use the available platform folder
subdir-ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreDriver/platforms/$(MOBICORE_PLATFORM)
# MobiCore Driver includes
subdir-ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreDriver/public
# MobiCore KernelApi required incldes
subdir-ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreKernelApi/include \
		-I$(GUD_ROOT_FOLDER)/MobiCoreKernelApi/public
