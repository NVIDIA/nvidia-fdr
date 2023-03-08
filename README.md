# Dev environment setup on build machine

These steps were verified on Ubuntu 20.04 and 22.04.

```bash
# Dependency for sdbusplus
sudo apt install git meson libtool pkg-config g++ libsystemd-dev \
    python3 python3-pip python3-yaml python3-mako python3-inflection

# Dependency for fdr
sudo apt install cmake protobuf-compiler nlohmann-json3-dev libboost-all-dev \
    sqlite3 libsqlite3-dev libyaml-cpp-dev libcurl4-gnutls-dev
```

# Build (x86 native)

```bash
cmake -B build/
make -j24 -C build/
```
## Run

```bash
./build/fdr
```

FDR data would start landing under /tmp/fdr/

# Build (ARM cross compile with bitbake)

## Add recipe to openbmc project

This is a one time step, and it would be no longer needed once it is committed to openbmc. 

```bash
git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/bmc/openbmc.git
cd openbmc

. setup hgx

# Copy the fdr_git.bb from this repo to openbmc, make necessary changes first
mkdir -p ../../meta-nvidia/recipes-nvidia/fdr
cp /path/to/nvidia-fdr/fdr_git.bb openbmc/meta-nvidia/recipes-nvidia/fdr
```

## Building fdr

```bash
# Make sure you've activate `. setup hgx`
bitbake fdr
```

Once the command succeed:

- The rpm can be found under `openbmc/build/hgx/tmp/deploy/rpm/armv7ahf_vfpv4d16`
- The unpackage files(including the fdr binary) are loacated in `openbmc/build/hgx/tmp/work/armv7ahf-vfpv4d16-openbmc-linux-gnueabi/fdr/git-r0/package` if you wish to scp them into openbmc

Otherwise, if errors occur, checkout the logs under `openbmc/build/hgx/tmp/work/armv7ahf-vfpv4d16-openbmc-linux-gnueabi/fdr/git-r0/temp`

## Build fdr along with openbmc

Append `IMAGE_INSTALL:append = "fdr"` to `openbmc/build/hgx/conf/local.conf`, then build the image

```
bitbake obmc-phosphor-image
```

Run the image with qemu, and the fdr command will be already there.
