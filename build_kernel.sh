#!/bin/sh

export CROSS_COMPILE=/home/fcuzzocrea/Documenti/LineageOS/lineage-17.1/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.8/bin/arm-linux-androideabi-

make ARCH=arm -j8 ANDROID_MAJOR_VERSION=m klimtwifi_00_defconfig
make ARCH=arm -j8 ANDROID_MAJOR_VERSION=m

