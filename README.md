# Cortex-M4 and Cortex-A53 Low Power Use Cases Application Note on i.MX8M Mini

This repository contains the ARM Trusted Firmware and Linux patches as well as the Cortex-M4 application to run the use cases described in the _Cortex-M4 and Cortex-A53 Low Power Use Cases_ application note.

This application note presents the power measurements performed for following three scenarios:
 - Cortex-A in Linux Suspend mode and Cortex-M in Stop mode waiting for a General Purpose Timer (GPT) interruption.
 - Cortex-A in Linux Suspend mode and Cortex-M periodically waking up to send a I2C frame.
 - Cortex-A in Linux Suspend mode and Cortex-M periodically waking up to perform a dummy load.

## Hardware and software setup
 - i.MX8MMini LPDDR4 EVK (8MMINILPD4-EVK)
 - BSP Linux 5.10.9_1.0.0 plus additional patches from this repository
 - MCUXpresso  SDK 2.9.1 plus specific application from this repository

## Build instructions
### BSP build
Please refer to _iMX Yocto Project User's Guide_ and _iMX Linux User's Guide_ documents included in the BSP release for BSP build and deployment instructions.
In order to patch the official BSP image, clone this repository and follow these instructions.
#### Standalone build
 - Move to the ARM Trusted Firmware (ATF) git folder you cloned (imx-atf) and apply the patch:
```
 git apply <path to low_power_usecases folder>/0001-imx-imx8m-gpc-Dont-override-clocks-used-by-MCore.patch
```
 - Rebuild ATF
```
make PLAT=imx8mm clean
make PLAT=imx8mm
```
 - Follow the _iMX Linux User's Guide_ document to build and deploy the boot image (flash.bin) using imx-mkimage
 - Move to the Linux git folder you cloned (linux-imx) and apply the patch:
```
 git apply <path to low_power_usecases folder>/0002-boot-dts-imx8mm-evk-rpmsg-Shared-memory-is-not-needed.patch
```
 - Recompile Linux dtbs
```
make dtbs
```
 - Copy the compiled _imx8mm-evk-rpmsg.dtb_ into the SDCard:
```
cp arch/arm64/boot/dts/freescale/imx8mm-evk-rpmsg.dtb /media/<username>/boot/
```
#### Yocto build
 - Move to the ARM Trusted Firmware (ATF) git folder
```
mv <yocto build folder>/tmp/work/cortexa53-crypto-mx8mp-poky-linux/imx-atf/2.4+gitAUTOINC+ba76d337e9-r0/git/
```
 - Apply the patch:
```
 git apply <path to low_power_usecases folder>/0001-imx-imx8m-gpc-Dont-override-clocks-used-by-MCore.patch
```
 - Force the compilation of imx-atf:
```
mv <yocto build folder>
bitbake -c compile -f imx-atf
```
- Move to the Linux git folder
```
mv <yocto build folder>/tmp/work/imx8mmevk-poky-linux/linux-imx/5.10.9+gitAUTOINC+32513c25d8-r0/git/
```
 - Apply the patch:
```
 git apply <path to low_power_usecases folder>/0002-boot-dts-imx8mm-evk-rpmsg-Shared-memory-is-not-needed.patch
```
 - Force the compilation of linux-imx:
```
mv <yocto build folder>
bitbake -c compile -f linux-imx
```
 - Create again the deploy images:
```
bitbake imx-image-multimedia
```

### MCUXpresso SDK build
Please refer to the _Getting Started with MCUXpresso SDK for EVK-MIMX8MM_ included in the official SDK package for SDK build and deployment instructions.
The Cortex-M4 application needed for this application note should be copied directly into the SDK's _rtos_examples_ folder:
```
cd <SDK path>/boards/evkmimx8mm/rtos_examples/
cp -r <path to low_power_usecases folder>/low_power_usecases/ .
```
Please notice that the application should be built using the _build_debug.sh_ script:
```
cd <SDK path>/boards/evkmimx8mm/rtos_examples/low_power_usecases/armgcc/
./build_debug.sh
```

## Running the application
When first booting the board, the boot needs to be stopped at u-boot in order to select the correct Linux device tree and load the Cortex-M4 application:
```
setenv fdt_file imx8mm-evk-rpmsg.dtb
setenv m4_image low_power_usecases.bin
setenv m4_loadaddr 0x7e0000
setenv m4_copyaddr 0x80000000
setenv m4_loadimage "fatload mmc '${mmcdev}':'${mmcpart}' '${m4_copyaddr}' '${m4_image}'; cp.b '${m4_copyaddr}' '${m4_loadaddr}' 0x20000"
setenv run_m4_image "run m4_loadimage; dcache flush; bootaux '${m4_loadaddr}'"
run run_m4_image 
```
Linux can then be booted:
```
boot
```
And set to Suspend mode:
```
echo mem > /sys/power/state 
```
Finally, the Cortex-M4 application can continue by pressing any key on the M4 console.