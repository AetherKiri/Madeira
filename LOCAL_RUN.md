# Madeira 本机运行记录

验证日期：2026-09-16。环境：Apple M4、macOS 15.6.1、Xcode 26.3。

## 项目结构

Madeira 的目标是在 iOS 上运行 Windows 游戏。主要链路是：

```text
Windows 游戏 → Wine（Windows API）→ DXMT（D3D11 → Metal）→ GPU
                  ↑
             FEX（x86-64 → ARM64）
```

- `app/Madeira/`：SwiftUI/UIKit 界面、JIT 管理、Wine/FEX 桥接、游戏输入和日志。
- `FEX/`、`wine/`、`research/dxmt/`：带 iOS 改造的固定版本子模块。
- `build/`：Wine、DXMT、字体和加密库的原生编译脚本。
- `research/remote-metal/`：可独立运行的 macOS Metal 服务、协议和测试客户端。

## 已运行的 Mac 图形后端

在仓库根目录执行：

```sh
./scripts/run-macos-demo.sh start
./scripts/run-macos-demo.sh status
./scripts/run-macos-demo.sh test
./scripts/run-macos-demo.sh stop
```

`start` 编译并启动原生窗口，通过 TCP 向 Apple M4 GPU 发送渲染命令，
播放 600 帧三角形动画。动画完成后服务继续运行，窗口保留最后一帧。
再次执行 `start` 会重建并重启这个工作目录的服务。

服务仅监听 `127.0.0.1:47821`。日志位于
`build/DerivedData/remote-metal/host.log` 和 `render.log`；同目录的
`token` 是自动生成的本机连接口令，属于忽略提交的构建产物。

实际验证结果：

- 协议校验：63 项通过；命令打包：25 项通过。
- 真实客户端检查通过，包括自行编译的 Metal 着色器加载。
- 窗口呈现：600/600 帧；没有丢失 drawable。
- 离屏三角形：1352/4096 个像素有颜色，校验和 `0x31d709c5`。
- 打包命令回放成功，GPU 渲染结果通过像素检查。

这部分验证图形传输和渲染，不代表 Windows 游戏已经运行。

## 完整应用的 Mac 兼容模式

Xcode 实际列出了 `My Mac — Designed for [iPad,iPhone]` 运行目标。
因此不能仅凭项目使用 UIKit 或关闭 Mac Catalyst 就断定它无法在 Mac 上运行。
[Apple 的说明](https://developer.apple.com/documentation/apple-silicon/running-your-ios-apps-in-macos)
介绍了这条兼容路径；Madeira 的 JIT 和游戏执行仍须实际验证。

本次已完成：

- 下载所有递归子模块，并保持仓库指定的提交版本。
- 找到完整 Xcode；通过 `DEVELOPER_DIR` 使用它，没有修改系统的 `xcode-select`。
- 安装 Apple Metal Toolchain。
- 从 [Microsoft 官方发行包](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
  提取所需的 12 个 x64 VC++ DLL，保留原始字节并检查证书数据完整。
  文件位于 `app/Madeira/x86_64-vcruntime/`，按仓库规则忽略提交。
- 在关闭签名的诊断构建中，应用源码完成编译，并进入链接阶段。

尚未解决的完整应用阻塞点：

1. 缺少 `libFEXCore.a` 等原生静态库。FEX 本次 iOS 原生构建在
   `Core.cpp` 的 `IosFfsBypassLog` / `IosCbEntryLog` 声明处失败，需进一步
   核对该分支的原生构建配置与 ARM64EC 构建配置。
2. Wine/DXMT 构建链不完整：例如 `build/wineserver/build.sh` 要求已有
   `libwineserver.a`，首次运行直接报 `No base libwineserver.a found`；
   DXMT 还需要文档指定的 LLVM 15 iOS 交叉编译产物。
3. 本机没有有效开发签名身份。项目仍使用原作者的 Team/Bundle ID，
   正常构建报找不到 `com.willfaust.mythicemu` 的开发描述文件。
   安装调试完整应用需要配置自己的开发团队和签名。

构建日志保存在 `build/DerivedData/remote-metal/` 下的
`app-build.log`、`signing-check.log`、`fex-configure.log` 和 `fex-build.log`。
目前完整 Madeira 应用和 Windows 游戏均未启动。
