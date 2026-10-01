## 源码下载

```plain
$ git clone https://github.com/Project-LemonLime/Project_LemonLime.git --recursive
```

### 下载的东西太大了？

`git clone` 的时候，使用 `--depth=1` 可以使下载下来的文件大小减少很多（因为默认情况下它会把所有历史记录全部下载下来）。

### 如果 Github 还是太慢…

你也许可以到 `码云（Gitee）` 去下载。

在很多地区，从 `码云` 下载的速度是从 `Github` 下载的速度的 100 倍。

[这个仓库在码云下的镜像](https://gitee.com/iotang/Project_LemonLime)

## Windows

去 `Releases` 下载就可以了。

### Scoop 用户

添加[第三方 bucket](https://github.com/ChungZH/peach) 即可快速安装与更新

```powershell
scoop bucket add peach https://github.com/ChungZH/peach
scoop install peach/lemon
```

当然如果你装有 Qt 5/6，也可以下载源码编译。

NOTE: XLS 导出是默认关闭的，如需使用，请编译时附加 `-DENABLE_XLS_EXPORT` 启用(Qt6 不可用)。

### 非常重要的提示

由于 Windows 的特殊性，请在下载 `Releases` 后检查 LemonLime 功能的完整性，包括能否探测程序的运行时间和使用内存。不过，如果使用源码构建 LemonLime，这一问题将不会出现，因此推荐使用源码构建 LemonLime。

> 在访问 GitHub 较慢或连接质量较差的地区，下载 Qt 的时间 + 安装 Qt 的时间 + 下载 LemonLime 源代码的时间 + 编译的时间 &lt; 从 Github 上下载可执行文件的时间。
>
> 请使用一个速度较快的国内镜像下载 Qt 。

## Linux

### Arch Linux 系

```bash
## 迅速安装 ##
yay -S lemon-lime # 稳定版本
yay -S lemon-lime-git # 开发版本（提前使用许多新功能！）
# 感谢 @CoelacanthusHex 的支持。

## 使用 CMake ##
sudo pacman -S gcc cmake qt6-base qt6-tools qt6-httpserver ninja 
cd 源代码的目录
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja 
ninja  # 获得可执行文件 lemon

## 使用 QtCreator ##
sudo pacman -S qtcreator
```

### Debian | Ubuntu 系

```bash
## 使用 CMake ##
sudo apt install build-essential ninja-build qt6-tools-dev-tools qt6-base-dev qt6-tools-dev qt6-l10n-tools libqt6httpserver6-dev libgl1-mesa-dev cmake # Qt6 依赖环境
cd 源代码的目录
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja 
ninja  # 获得可执行文件 lemon

# libqt6httpserver6-dev 只被内嵌的在线提交服务用到（默认 ENABLE_ONLINE_SERVER=ON）。
# 发行版里没有这个包（或不想编这一块）时，配置时加 -DENABLE_ONLINE_SERVER=OFF 即可，
# 其余功能不受影响，菜单里的“在线提交服务”会被隐藏。

ninja --install # 将其安装到系统中，默认安装位置位于 /usr/local
# 或者直接生成 DEB 包
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja -DBUILD_DEB=ON
ninja

## 使用 QtCreator ##
sudo apt install qtcreator
```

#### 官方镜像源版本无法支持编译的系统
 - Ubuntu 21.04 以前
 - Debian 11 之前

~~_arbiter 退出了群聊。_~~

### Fedora 系

```bash
## 使用 CMake ##
sudo dnf install cmake qt6-qtbase-devel qt6-qttools-devel qt6-qttools-linguist qt6-qtsvg-devel qt6-qthttpserver-devel desktop-file-utils ninja-build
cd 源代码的目录
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja
ninja # 获得可执行文件 lemon

ninja --install # 将其安装到系统中，默认安装位置位于 /usr/local

# 或者直接生成 RPM 包
sudo dnf install cmake qt5-qtbase-devel qt5-linguist qt5-qtsvg-devel desktop-file-utils ninja-build redhat-lsb-core fedora-packager rpmdevtools
cmake . -DCMAKE_BUILD_TYPE=Release -GNinja -DBUILD_RPM=ON
ninja
```

### openSUSE 系

```bash
## 使用 CMake ##
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

请使用 `watcher_macos.cpp` 编译 `watcher_unix`，否则内存限制会出问题。

设置 `CMAKE_PREFIX_PATH` 应设置为你的 `qt` 安装路径。

对于 Intel CPU 一般在 `/usr/local/Cellar/qt/` 目录下

如果使用 Apple Silicon，`CMAKE_PREFIX_PATH` 应为 `/opt/homebrew/Cellar/qt/<Your qt version>`

```bash
brew install cmake qt ninja
export CMAKE_PREFIX_PATH="/path/to/your/qt"
cmake -DCMAKE_BUILD_TYPE=Release -GNinja .
ninja
```
