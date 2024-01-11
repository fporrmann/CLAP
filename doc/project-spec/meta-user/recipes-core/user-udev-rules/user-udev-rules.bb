# inspired by https://support.xilinx.com/s/question/0D54U00007Oh1E3SAJ/petalinux-permissions-ownership-of-device-nodes-in-dev?language=en_US

SUMMARY = "User udev rules files for Linux drivers"
DESCRIPTION = "User udev rules recipe for Xilinx Linux in tree drivers"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
 
SRC_URI = "\
    file://99-uio-device.rules \
"
 
S = "${WORKDIR}"
 
COMPATIBLE_MACHINE ?= "^$"
COMPATIBLE_MACHINE:zynq = ".*"
COMPATIBLE_MACHINE:zynqmp = ".*"
COMPATIBLE_MACHINE:microblaze = ".*"
COMPATIBLE_MACHINE:versal = ".*"
 
do_configure[noexec] = '1'
do_compile[noexec] = '1'
 
do_install () {
    install -d ${D}${sysconfdir}/udev/rules.d
    for rule in $(find ${WORKDIR} -maxdepth 1 -type f -name "*.rules"); do
        install -m 0644 $rule ${D}${sysconfdir}/udev/rules.d/
    done
}

FILES:${PN} += "${sysconfdir}/udev/rules.d/*"