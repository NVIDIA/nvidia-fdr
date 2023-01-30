# Dev environment setup on build machine:

    sudo apt install nlohmann-json3-dev
    sudo apt install libboost-all-dev
    sudo apt install libsystemd-dev
    sudo apt install sqlite3
    sudo apt install libsqlite3-dev

Install sdbusplus library from sources (Binary not available from distro)

    git clone https://github.com/openbmc/sdbusplus.git
    cd sdbusplus/
    git checkout -b temp 1778b12b1d304b759fd6b86f25f8efb74870a953
    meson build -Dtests=disabled -Dexamples=disabled -Ddefault_library=static
    cd build
    ninja
    ninja test
    sudo ninja install


Install protobuf from sources (Version from distro is too old for us)

    wget https://github.com/protocolbuffers/protobuf/archive/refs/tags/v21.12.tar.gz
    tar xzf v21.12.tar.gz
    cd protobuf-21.12/
    cmake . -Dprotobuf_BUILD_TESTS:BOOL=OFF
    cmake --build . --parallel 10
    sudo make install

# Additional Dev Setup for cross-compiling :
## Build and install ARM cross compiler toolchain for OpenBMC

    git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/bmc/openbmc.git
    cd openbmc
    . setup hgx
    bitbake obmc-phosphor-image
    bitbake obmc-phosphor-image -c populate_sdk

# Build (x86 native)

    cmake -B build/
    make -j24 -C build/

# Build (ARM cross compile)

    . /usr/local/oecore-x86_64/environment-setup-armv7ahf-vfpv4d16-openbmc-linux-gnueabi
    cmake -B build/
    make -j24 -C build/

# Run

    ./build/fdr

FDR data would start landing under /tmp/fdr/



