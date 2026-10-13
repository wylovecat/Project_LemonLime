# 从源码构建 LemonLime

## 源码下载

```plain
$ git clone https://github.com/Project-LemonLime/Project_LemonLime.git --recursive
```

### 下载的东西太大了？

`git clone` 的时候，使用 `--depth=1 --shallow-submodules` 可以使下载下来的文件大小减少很多（因为默认情况下它会把所有历史记录全部下载下来）。GitHub 自动生成的源码压缩包缺少子模块内容，应优先使用 `git clone`。

### 如果 Github 还是太慢…

你也许可以到 `码云（Gitee）` 去下载。

在很多地区，从 `码云` 下载的速度是从 `Github` 下载的速度的 100 倍。

[这个仓库在码云下的镜像](https://gitee.com/iotang/Project_LemonLime)

## 构建依赖

| 依赖           | 要求                                                  |
| -------------- | ----------------------------------------------------- |
| CMake          | 推荐 3.20 或更高；                                    |
| C++ 编译器     | 支持 C++17，使用与 Qt 工具包匹配的编译器              |
| Qt             | 6.8 或更高                                            |
| Qt 模块        | Core、Gui、Widgets、Network、Svg、LinguistTools、Test |
| Linux 附加模块 | Qt DBus，用于评测期间向系统申请阻止休眠               |
| 构建工具       | Ninja，或 CMake 支持的其他生成器                      |
| Git            | 用于获取源代码、子模块及构建版本号                    |

对于现有仓库，在切换分支或更新代码后同步子模块：

```bash
git submodule update --init --recursive
```

## Windows

安装 Visual Studio 的“使用 C++ 的桌面开发”组件、Windows SDK、CMake、Ninja，以及相应架构的 Qt MSVC 工具包。在 Visual Studio 的 **Developer PowerShell** 中执行：

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH="<your qt install path>/msvc2022_64"
cmake --build build --parallel
```

build 目录下会生成 lemon.exe。动态链接 Qt 时，使用 `windeployqt` 复制运行时依赖：

```powershell
& "<your qt install path>/msvc2022_64/bin/windeployqt.exe" --release build/lemon.exe
& ./build/lemon.exe
```

## Linux

安装发行版提供的 C++ 工具链、CMake、Ninja 和 Qt 开发包。

```bash
# Arch Linux
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools qt6-svg

# Debian 或 Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config lsb-release \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools qt6-svg-dev

# Fedora
sudo dnf install cmake qt6-qtbase-devel qt6-qttools-devel qt6-qttools-linguist qt6-qtsvg-devel desktop-file-utils ninja-build
cd 源代码的目录
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja
ninja # 获得可执行文件 lemon

ninja --install # 将其安装到系统中，默认安装位置位于 /usr/local

# 或者直接生成 RPM 包
sudo dnf install cmake qt6-qtbase-devel qt6-linguist qt6-qtsvg-devel desktop-file-utils ninja-build redhat-lsb-core fedora-packager rpmdevtools
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja -DBUILD_RPM=ON
ninja

# openSUSE
sudo zypper in qt6-base-devel qt6-tools qt6-svg-devel ninja qt6-linguist-devel  # Qt6 依赖环境
cd 源代码的目录
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja # 如使用 make 请删去 -GNinja
ninja # 获得可执行文件 lemon

ninja --install # 将其安装到系统中，默认安装位置位于 /usr/local

# 或者直接生成 RPM 包
sudo zypper in lsb-release rpm-build # RPM 依赖
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja -DBUILD_RPM=ON
ninja

```

### 本 fork 的额外依赖：内嵌在线提交服务

上面的依赖列表来自上游，只覆盖核心功能。本 fork 默认开启内嵌的在线提交服务
（`ENABLE_ONLINE_SERVER=ON`），它需要 Qt HttpServer，因此还要补：

```bash
# Debian / Ubuntu
sudo apt install libqt6httpserver6-dev
# Fedora
sudo dnf install qt6-qthttpserver-devel
```

发行版里没有这些包（或不想编这一块）时，配置时加 `-DENABLE_ONLINE_SERVER=OFF` 即可，
其余功能不受影响，菜单里的“在线提交服务”会被隐藏。

Debian/Ubuntu 还可以直接生成 deb 包：

```bash
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja -DBUILD_DEB=ON
ninja
```

## etc.

无AppImage可用

**现在是静态编译的时代**

### 静态单文件 / 老发行版（含 Ubuntu 20.04）

发行版仓库里的 Qt 太老时（Ubuntu 21.04 以前、Debian 11 之前没有 Qt 6.8），
可以用预编译的静态 Qt 直接打一个自包含的单文件 `lemon`：

```bash
wget https://github.com/Project-LemonLime/qt5ci/releases/latest/download/qt-6.9-static-linux.tar.gz
tar -zxf qt-6.9-static-linux.tar.gz   # 解出 qt6/
mkdir build && cd build
cmake ../Project_LemonLime -GNinja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/../qt6/bin" -DENABLE_ONLINE_SERVER=OFF
ninja   # 得到单文件 lemon
```

（这份静态 Qt 是用 ubuntu:20.04 容器构建的，没有 WebSockets 和 HttpServer 模块，
所以要么加 `-DENABLE_ONLINE_SERVER=OFF`（不含在线提交服务），要么按下一节把这两个
模块补编进去。

#### 让这份静态 Qt 带上在线提交服务（已实测）

Qt HttpServer 依赖 Qt WebSockets，而且模块源码版本不能高于静态 Qt 自身版本
（模块的 CMakeLists 会 `find_package(Qt6 ${PROJECT_VERSION})`）。
`qt-6.9-static-linux.tar.gz` 里的 Qt 是 6.9.3，所以取 6.9.3 的模块源码：

```bash
# 缺 brotli 会让 Qt6Network 找不到；缺 xcb-util 会报 Qt6Gui_FOUND=FALSE
# 并提示缺少导入目标 Qt6::XcbQpaPrivate
sudo apt install -y libbrotli-dev libxcb-util-dev libxcb-xkb-dev libxcb-xinput-dev \
                    libxcb-cursor0 x11-xkb-utils ccache

tar -zxf qt-6.9-static-linux.tar.gz   # 解出 qt6/，注意 qt6/bin 就是 Qt 安装前缀
for m in qtwebsockets qthttpserver; do
  curl -LO "https://download.qt.io/archive/qt/6.9/6.9.3/submodules/${m}-everywhere-src-6.9.3.tar.xz"
  tar -xf "${m}-everywhere-src-6.9.3.tar.xz"
  cmake -S "${m}-everywhere-src-6.9.3" -B "${m}-build" -GNinja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DQT_USE_CCACHE=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$PWD/qt6/bin" -DCMAKE_PREFIX_PATH="$PWD/qt6/bin"
  cmake --build "${m}-build" -j"$(nproc)" && cmake --install "${m}-build"
done
```

补编完成后，用上面同样的 cmake 命令（去掉 `-DENABLE_ONLINE_SERVER=OFF`）重新构建，
产物仍然只有一个 `lemon` 文件，Qt WebSockets / HttpServer 一并静态链接进去。
在 Ubuntu 20.04 上实测：`GET /login` 返回 200，`GET /api/tasks` 返回 401，
`GET /` 303 跳转到 `/login`。

产物 `lemon` 已经静态链接 Qt，目标机器不需要安装 Qt。跑判题还需要：

```bash
sudo apt install bubblewrap g++           # 沙箱与选手程序编译器
```

#### 关于 libxcb-cursor.so.0（老系统零依赖打包）

Qt 的 xcb 平台插件依赖 `libxcb-cursor.so.0`，而 Ubuntu 20.04 只有 universe 源里
才有 `libxcb-cursor0`，默认还不装；精简系统上直接运行会报

```
error while loading shared libraries: libxcb-cursor.so.0: cannot open shared object file
```

两种处理办法：

1. 让用户装一次：`sudo apt install libxcb-cursor0`（需要 universe 源）。
2. 把这一族运行库打进包里做兜底（推荐，已实测）——只是把 `.so` 放进目录是
   **没用的**，必须同时写入 runpath，否则动态链接器根本不会去那里找：

```bash
patchelf --set-rpath '$ORIGIN/lib' lemon
# DT_RUNPATH 不会传递给二级依赖，所以每个打进包的库也要单独设置
for f in lib/*; do patchelf --set-rpath '$ORIGIN' "$f"; done
```

验证：把系统里这些库临时移走再检查

```bash
ldd ./lemon | grep 'not found'    # 应该没有任何输出
```

本仓库交付的 Ubuntu 20.04 静态包就是这样做的：程序旁多一个 `lib/`
（约 1.3 MB，`libxcb-*` / `libxkbcommon*` / `libbrotli*` 共 21 个库），
在系统里这些库全部缺失的情况下仍能正常启动（20.04 实测通过）。

推荐使用 `-static` 编译参数，否则学生程序会缺 libstdc++。本仓库的
`.github/workflows/linux-static-qt6.yml` 就是这样构建的（容器即 ubuntu:20.04）。

## macOS

在没有 macOS 机子的情况下写 macOS 支持是一件非常滑稽的事。

安装 Xcode Command Line Tools 和 Homebrew，然后安装构建依赖：

```bash
xcode-select --install
brew install cmake ninja qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open build/lemon.app
```
