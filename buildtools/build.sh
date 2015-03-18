#!/bin/bash
KERNELNAME="Donkey-"$1
KERNELVERSION=$1
TOOLCHAIN="/home/sub77/Android/toolchains/sabermod/SaberNaro-arm-eabi-4.9/bin/arm-eabi"
MODULES_DIR="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/modules"
ZIMAGE="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/arch/arm/boot/zImage"
KERNEL_DIR="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/dev/KERNEL"
MKBOOTIMG="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/tools/mkbootimg"
MKBOOTFS="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools//mkbootfs"
DTBTOOL="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/tools/dtbTool"
TEMP_DIR="/home/sub77/Roms/cm/cynanogen/kernel/samsung/matissewifi/buildtools/temp"
OUT_DIR="/home/sub77/dev/OUT"
BUILD_START=$(date +"%s")
CURRENTDATE=$(date +"%d%m")
if [ -a $ZIMAGE ];
then
echo "Cleaning Temp Folders"
rm -r $MODULES_DIR/*
rm $TEMP_DIR/boot.img
rm $TEMP_DIR/system/lib/modules/radio-iris-transport.ko
rm $TEMP_DIR/system/lib/modules/scsi_wait_scan.ko
rm $TEMP_DIR/system/lib/modules/pronto/pronto_wlan.ko
cd $KERNEL_DIR
find . -name '*.ko' -exec cp {} $MODULES_DIR/ \;
cd $MODULES_DIR
echo "Stripping modules for size"
$TOOLCHAIN-strip --strip-unneeded *.ko
cd $KERNEL_DIR
$DTBTOOL -o dt.img -s 2048 -p scripts/dtc/ arch/arm/boot/
$MKBOOTFS ramdisk/ > $KERNEL_DIR/ramdisk.cpio
cat $KERNEL_DIR/ramdisk.cpio | gzip > $KERNEL_DIR/root.fs
$MKBOOTIMG --kernel $ZIMAGE --ramdisk $KERNEL_DIR/root.fs --base 0x00000000 --cmdline "console=ttyHSL0,115200,n8 earlyprintk androidboot.console=ttyHSL0 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x37 vmalloc=400M utags.blkdev=/dev/block/platform/msm_sdcc.1/by-name/utags androidboot.write_protect=0 androidboot.selinux=permissive" --pagesize 2048 --dt dt.img -o $TEMP_DIR/boot.img
BUILD_END=$(date +"%s")
DIFF=$(($BUILD_END - $BUILD_START))
echo "Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
rm $KERNEL_DIR/dt.img 
rm $KERNEL_DIR/root.fs 
rm $KERNEL_DIR/ramdisk.cpio
else
echo "Compilation failed! Fix the errors!"
fi
# Packing into ZIP
read -p "Pack into ZIP? (s/n)? : " bchoice
	case "$bchoice" in
		y|Y|s|S)
			echo "Copying modules into temp folder"
			cp $MODULES_DIR/radio-iris-transport.ko $TEMP_DIR/system/lib/modules 
			cp $MODULES_DIR/scsi_wait_scan.ko $TEMP_DIR/system/lib/modules 
			cp $MODULES_DIR/wlan.ko $TEMP_DIR/system/lib/modules/pronto/pronto_wlan.ko 
			cd $TEMP_DIR
			zip -r ../$KERNELNAME-$CURRENTDATE.zip *
			mv ../$KERNELNAME-$CURRENTDATE.zip $OUT_DIR
			exit 0;;
		n|N )
			echo "Skipping...";;	
	esac

