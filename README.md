# Dev environment setup on build machine

These steps were verified on Ubuntu 20.04 and 22.04.

```bash
# Dependency for sdbusplus
sudo apt install git meson libtool pkg-config g++ libsystemd-dev \
    python3 python3-pip python3-yaml python3-mako python3-inflection

# Dependency for fdr
sudo apt install cmake protobuf-compiler nlohmann-json3-dev libboost-all-dev \
    sqlite3 libsqlite3-dev libyaml-cpp-dev libcurl4-gnutls-dev python3-yaml \
    libspdlog-dev libfmt-dev
```

# Build (x86 native)

```bash
cmake -B build/
make -j24 -C build/
```
## Run

### Option 1: 
```bash
cd build
./fdr
```
### Option 2:
```bash
export PLATFORMS_PATH=/path/to/platforms
./fdr
```
FDR data would start landing under the path (LogsBasePath) provided in PPF.

**Note:** *PLATFORMS_PATH* gets precedence over *./platforms* in the working directory.
So, even if the working directory has *platforms* directory under it, if the *PLATFORMS_PATH* environment var
is set, FDR will use *PLATFORMS_PATH* for finding PPF.

# Build (ARM cross compile with bitbake)

## Add recipe to openbmc project

This is a one time step, and it would be no longer needed once it is committed to openbmc. 

```bash
git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/bmc/openbmc.git
cd openbmc

# Switch to the branch with FDR support
git checkout fdr-integration 

. setup hgx
```

## Building fdr

```bash
# Make sure you've activate `. setup hgx`
bitbake fdr
```

Once the command succeed:

- The rpm can be found under `openbmc/build/hgx/tmp/deploy/rpm/armv7ahf_vfpv4d16`
- The unpackage files(including the fdr binary) are loacated in `openbmc/build/hgx/tmp/work/armv7ahf-vfpv4d16-openbmc-linux-gnueabi/nvidia-fdr/git-r0/package` if you wish to scp them into openbmc

Otherwise, if errors occur, checkout the logs under `openbmc/build/hgx/tmp/work/armv7ahf-vfpv4d16-openbmc-linux-gnueabi/nvidia-fdr/git-r0/temp`

## Build fdr along with openbmc

```
bitbake obmc-phosphor-image
```

Run the image with qemu, and the fdr service should be running already.
```bash
# To check the service status
systemctl status nvidia-fdr.service

# If any changes are made in the service file (e.g. PLATFORMS_PATH has changed), or in the PPF,
# OR if the fdr binary has been replaced,
# restart the service using following:
systemctl restart nvidia-fdr.service
```

