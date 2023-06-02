# Recipe created by recipetool
# This is the basis of a recipe and may need further editing in order to be fully functional.
# (Feel free to remove these comments when editing.)

# Unable to find any files that looked like license statements. Check the accompanying
# documentation and source headers and set LICENSE and LIC_FILES_CHKSUM accordingly.
#
# NOTE: LICENSE is being set to "CLOSED" to allow you to at least start building - if
# this is not accurate with respect to the licensing of the software being built (it
# will not be in most cases) you must specify the correct value before using this
# recipe for anything other than initial testing/development!
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

# Use below if local development repo is prefered, please commit the change first.
#SRC_URI = "git:///home/skandi/flight-data-recorder/nvidia-fdr;protocol=file;branch=skandi/book-of-errors"
SRC_URI = "git://git@gitlab-master.nvidia.com:12051/dgx/nvidia-fdr.git;protocol=ssh;branch=master"

# Modify these as desired
#PV = "1.0+git${SRCPV}"
#SRCREV = "3db4c0d8ec4c8593346ec080283709342edaf973"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# NOTE: unable to map the following CMake package dependencies: Protobuf
inherit cmake

# Specify any options you want to pass to cmake using EXTRA_OECMAKE:
EXTRA_OECMAKE = "-DYOCTO=ON -DCMAKE_INSTALL_PREFIX:PATH=/usr"

DEPENDS = "yaml-cpp protobuf protobuf-native sqlite3 systemd sdbusplus nlohmann-json curl spdlog python3 python3-pyyaml"
RDEPENDS_${PN} = "yaml-cpp protobuf sqlite3 systemd sdbusplus curl spdlog"
RDEPENDS:${PN} += "bash"
