LemonLime 便携版（绿色版）使用说明
====================================

本目录已经是可直接运行的版本：拷贝到任意 Windows 10 (1809 及以上) 或
Windows 11 机器上，双击 lemon.exe 即可，无需安装 Qt，也无需预先安装
Visual Studio 运行库。

目录说明
--------
  lemon.exe     主程序
  LemonLime\    便携模式下自动生成，lemon.ini 里保存全部设置
  logs\         日志目录（lemonlime-log.txt，按天滚动，保留最近 30 天）
  compiler\     可选，放自带编译器（见下）
  portable      便携模式标记文件；删除它即恢复“设置写注册表、日志写用户目录”

在线提交服务
------------
  菜单 工具 → 在线提交服务…
  首次启动服务时如果 Windows 防火墙弹出提示，请选择“允许访问”（专用网络即可），
  否则同局域网的学生的浏览器连不上。
  学生用浏览器访问 http://老师机IP:8080

写题目、判题需要编译器（程序不内置）
------------------------------------
  Windows 下请使用 MinGW-w64 的 g++（推荐 https://winlibs.com 的单 zip 版本）：
    方式一：把整个 mingw64 目录放到本目录的 compiler\ 下，即
            compiler\mingw64\bin\g++.exe，重启 LemonLime 后会自动识别；
    方式二：自己装到任意位置，然后在
            LemonLime → 工具 → 设置 → 编译器 里把“编译器路径”指向 g++.exe。
  编译参数请保留 -static，否则学生程序在评测时会缺 libstdc++-6.dll 等运行库。

常见问题
--------
  * 杀毒软件 / SmartScreen 拦截：lemon.exe 没有数字签名，属正常现象；
    建议把本目录和比赛目录都加入 Windows Defender 的排除项。
  * 评测时报 “Failed to create process”：g++ 没配好，见上一节。
  * 若把本目录放到只读位置（例如 C:\Program Files），程序会自动退回
    “设置写注册表、日志写 %LOCALAPPDATA%\Lemonlime\logs” 的方式，仍可正常运行。
  * 自行编译与重新打包的步骤见仓库里的 BUILD-WINDOWS.txt。
