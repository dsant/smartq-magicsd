#
# Include the make variables (CC, etc...)
#

CPU=s3c6410

#CROSS_PATH=/usr/local/software/toolchain-arm-jaunty.rgs1
#CROSS_COMPILE=${CROSS_PATH}/bin/arm-none-linux-gnueabi-

#CROSS_PATH=/usr/local/software/arm-2010q1-188-arm-none-eabi
#CROSS_COMPILE=${CROSS_PATH}/bin/arm-none-eabi-

CROSS_PATH=/usr/local/software/arm-2010q1-188-arm-gnueabi
CROSS_COMPILE=${CROSS_PATH}/bin/arm-none-linux-gnueabi-

#CROSS_PATH=/usr/local/software/toolchain-arm-karmic.rgs0
#CROSS_COMPILE=${CROSS_PATH}/bin/arm-none-linux-gnueabi-

COMPILER_LIB_PATH_PRE=${CROSS_PATH}/lib/gcc/arm-none-linux-gnueabi
COMPILER_LIB_PATH=${COMPILER_LIB_PATH_PRE}/`ls ${COMPILER_LIB_PATH_PRE}`

AS	= $(CROSS_COMPILE)as
LD	= $(CROSS_COMPILE)ld
CC	= $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
HOSTCC	= gcc

# we need the mkudfu tool from U-Boot build
#MKUDFU = ../uboot/u-boot/tools/mkudfu

export CROSS_COMPILE AD LD CC OBJCOPY OBJDUMP MKUDFU