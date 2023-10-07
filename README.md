# Dev environment setup on build machine

These steps were verified on Ubuntu 22.04.

## Dependencies for sdbusplus

```bash
sudo apt install git meson libtool pkg-config g++-12 libsystemd-dev \
    python3 python3-pip python3-yaml python3-mako python3-inflection
```

## Dependencies for NVIDIA FDR

```bash
sudo apt install cmake clang-tools nlohmann-json3-dev \
    libyaml-cpp-dev libcurl4-gnutls-dev python3-yaml \
    libspdlog-dev libfmt-dev clang-tools libgtest-dev libgmock-dev
```

## Protobuf

protobuf3 bundled with the distro is not new engouh to support `optional` keyword

```bash
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.20.3/protobuf-all-3.20.3.tar.gz
tar zxvf protobuf-all-3.20.3.tar.gz
cd protobuf-3.20.3/
./configure --prefix=/usr
make
sudo make install
```

## Set default gcc/g++ version

Some header files os `sdbusplus` requires g++12 to compile. If you have multiple gcc/g++ installed on the system (most likely you'll have both 11 and 12 now), set the default version to 12.

```bash
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 10
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 10
```

> Note: `10` at the end of command is the priority.

# Build & Test(amd64 native)

## Configure

```bash
meson setup --reconfigure build

# Or use following for debug build, these are the same options used in Docker unit test.
meson setup --reconfigure build -Db_colorout=never -Dwerror=true -Dwarning_level=3 -Ddebug=true -Doptimization=g 
```

## Build

```bash
meson compile -C build

# or use ninja
ninja -C build
```

## Running FDR

### Configure Systemd to allow name registration

```bash
cat > /etc/dbus-1/system.d/xyz.openbmc_project.FDR.conf << EOF
<!DOCTYPE busconfig PUBLIC
 "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="xyz.openbmc_project.FDR"/>
  </policy>
</busconfig>
EOF

sudo systemctl reload dbus
```

### Running FDR

```bash
cd <build dir>
sudo ./nvidia-fdr
```

FDR data would start landing under the path (LogsBasePath) provided in PPF.

## Validation

### Unit Test

```bash
meson setup --reconfigure build_coverage -Db_coverage=True
meson test -v -C build_coverage
```

### Static analysis

```bash
ninja scan-build -C builddir
```

## Code Coverage (Docker)

```bash
./coverage/coverage.sh
```

The result should be availab in the following location:

- text (also print on the output): ./build_coverage/meson-logs/coverage.txt
- html: ./build_coverage/meson-logs/coverage.html

## Run the CI pipeline locally

See https://gitlab-master.nvidia.com/dgx/bmc/docs/-/blob/develop/testing/local-ci-build.md


# Build (ARM cross compile with bitbake)

## Checkout the OpenBMC repo

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

## Code Coverage

TODO
