# Luckfox SDK setup

Use this guide in the Ubuntu 22.04 WSL terminal. The workspace was inspected on
October 9, 2026 and reports Ubuntu 22.04.5 LTS, x86-64, and WSL 2. The SDK is now
checked out at `/home/wlsanderson/sdk/luckfox-pico`, at commit
`824b817f889c2cbff1d48fcdb18ab494a68f69d1`. The Pro/Max SD_CARD Buildroot
configuration is selected, and both supplied compilers report GCC 8.3.0.
The first image build stopped at Buildroot's PATH whitespace check. The user
subsequently reported successful completion, and generated firmware images were
independently inspected. Wolfvision also cross-compiled successfully with this
SDK; see section 6. Board execution remains unverified. The setup sections are
retained for reproducibility. The boards are still on order; these steps run
on the laptop.

The selected board is **Luckfox Pico Max (256 MB)**, with an SC3336 camera and
microSD boot. Use the official [LuckfoxTECH/luckfox-pico SDK](https://github.com/LuckfoxTECH/luckfox-pico).
[Luckfox's SDK guide](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/SDK/)
specifies Ubuntu 22.04 x86-64. Ubuntu is the build host; **Buildroot** is the
selected board operating system.

## 1. Install host dependencies

This is the vendor's SDK dependency set, including curl:

```sh
sudo apt update
sudo apt install -y git ssh make gcc gcc-multilib g++-multilib \
  module-assistant expect g++ gawk texinfo libssl-dev bison flex fakeroot \
  cmake unzip gperf autoconf device-tree-compiler libncurses5-dev pkg-config \
  bc python-is-python3 passwd openssl openssh-server openssh-client vim \
  file cpio rsync curl
```

These system packages support the vendor SDK. Wolfvision's Python tools still
use uv and the repository lockfile; see [development setup](DEVELOPMENT.md#setup)
to restore uv and media tools in the new distribution.

## 2. Clone outside Wolfvision

Keep the SDK on the WSL Linux filesystem, with a path containing no spaces:

```sh
mkdir -p "$HOME/sdk"
cd "$HOME/sdk"
git clone https://github.com/LuckfoxTECH/luckfox-pico.git
cd luckfox-pico
git rev-parse HEAD
git status --short
```

If that checkout already exists, inspect it before proceeding; do not replace or
update it automatically. Save the reported commit with later build results.
The default branch is an initial setup choice, not a verified revision matching
the downloaded `Luckfox_Pico_ProMax_MicroSD_250607` firmware. The image's date
does not identify an SDK commit.

## 3. Select the board configuration

From the SDK root:

```sh
./build.sh lunch
```

Select by the displayed names, since menu numbering can change:

1. `RV1106_Luckfox_Pico_Pro_Max`
2. `SD_CARD`
3. `Buildroot`

Inspect the resulting configuration:

```sh
readlink -f .BoardConfig.mk
./build.sh info
```

The expected shared Pro/Max configuration is
`BoardConfig-SD_CARD-Buildroot-RV1106_Luckfox_Pico_Pro_Max-IPC.mk`.
The [official configuration](https://github.com/LuckfoxTECH/luckfox-pico/blob/main/project/cfg/BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1106_Luckfox_Pico_Pro_Max-IPC.mk)
selects the `arm-rockchip830-linux-uclibcgnueabihf` cross toolchain.

## 4. Check the supplied compiler

Use the SDK toolchain for board applications. In the current terminal:

```sh
export PATH="$HOME/sdk/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH"
arm-rockchip830-linux-uclibcgnueabihf-gcc --version
arm-rockchip830-linux-uclibcgnueabihf-g++ --version
```

This PATH setting lasts for the current shell. The vendor also supplies an
`env_install_toolchain.sh` script that edits shell startup files; the explicit
export is sufficient for this initial compiler check.

## 5. Build the SDK image

After the configuration and compiler checks succeed, run from the SDK root:

```sh
./build.sh
```

Build as your ordinary user. The vendor documents generated firmware under
`output/image/`. If the build fails, retain the first error and nearby output
along with the SDK commit and selected configuration.

The first build reported that PATH contained spaces, tabs, or newlines. WSL
commonly imports Windows directory names containing spaces. Retry in a subshell
with only Linux system tools and the SDK compiler:

```sh
(
  cd "$HOME/sdk/luckfox-pico" || exit
  export PATH="$HOME/sdk/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
  ./build.sh
)
```

This replaces PATH only inside the parentheses. It preserves shell startup
files and does not remove downloaded packages or previous build outputs.
The clean PATH was checked to resolve make, host GCC/G++, and the SDK GCC/G++.
The user reported successful completion of the retry; `output/image/` contains
the boot, rootfs, loader, U-Boot, environment, userdata, and update images.
A connected board is unnecessary for SDK image compilation.

A completed SDK build establishes image compilation. Next, establish firmware,
kernel, runtime, and media-library compatibility and verify execution on the
board. Validate the vendor SC3336/H.264 example before
adding the live-camera adapter. These remain pending; flashing and board boot
are separate steps.

## 6. Cross-build the existing Wolfvision applications

The existing C++17 shared core and simulation applications compiled and linked
successfully with the SDK compiler on October 9, 2026. Reproduce from Wolfvision:

```sh
cd "$HOME/wolfvision"
cmake -S . -B build-luckfox \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_CXX_COMPILER="$HOME/sdk/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-g++"
cmake --build build-luckfox -j 2
file build-luckfox/simulation_tx build-luckfox/simulation_rx
```

This uses a separate build directory from the host `build`. Both executables
are ARM 32-bit EABI5 hard-float binaries using `/lib/ld-uClibc.so.0`. Their
required shared libraries (`libstdc++.so.6`, `libgcc_s.so.1`, `libc.so.0`) and
the loader are present in the SDK-generated rootfs staging directory. That
file inspection does not establish board runtime compatibility. Tests are
disabled for this build; no ARM executable was run on the x86-64 laptop.

These applications still use prerecorded H.264 and synthetic UART inputs.
Live camera capture/encoding and real UART adapters remain future work.

The next preparation step is recorded in [SC3336/H.264 adapter review](LUCKFOX_CAMERA.md):
vendor source/API mapping, a verified example cross-build, the proposed adapter
contract, and a future camera bring-up procedure.
