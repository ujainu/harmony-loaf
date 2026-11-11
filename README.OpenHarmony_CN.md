# loaf 的 OpenHarmony PC 适配记录

这份文档记录把 loaf（一个基于 openFrameworks 的 Lua 实时编码环境）迁移到
OpenHarmony / HarmonyOS PC 的全过程：工程怎么组织、代码改了哪些地方、HAP
怎么编出来、应用怎么跑起来，以及最后哪些能力真正可用、哪些按平台特点做了
取舍。

和常见的「桌面应用套壳」不同，loaf 依赖的 openFrameworks 在鸿蒙上没有后端，
也没有现成的 Electron 或 Qt 运行时可以复用。所以这次适配不是「打包搬运」，
而是用鸿蒙原生能力重建了 loaf 的运行时：EGL + OpenGL ES 3.0 渲染、Lua 5.4
解释器、OHAudio 音频、AVPlayer 视频、NativeImage 外部纹理、OSC 网络通信。
桌面端那一套 `of.*` / `loaf.*` / `osc.*` 脚本接口保持不变，脚本不需要修改。

仓库地址：<https://github.com/ujainu/harmony-loaf>

---

## 迁移能力总览

按照鸿蒙 PC 适配规范，能力分为 T0 / T1 / T2 三档：

| 阶段 | 范围 | 状态 |
| --- | --- | --- |
| **T0 基础能力** | 构建 HAP、安装启动、画布渲染、脚本加载、热重载、错误处理 | 完成（100%） |
| **T1 主要能力** | 2D 绘图、字体、图片、FBO、着色器、OSC、音频播放、输入、拖拽导入、官方示例 | 可用（约 95%） |
| **T2 增强能力** | 3D、实时音频、视频、帧服务、XML、launcher、导出、缩放、自测、摄像头、串口/Firmata | 可用（约 90%，硬件相关需真机验证） |

逐项状态见下文表格，类级清单见 `docs/API_COVERAGE.md`。

---

## 工程结构

```text
harmony-loaf/
├── AppScope/                     # 应用级配置与图标
├── entry/                        # 主模块
│   ├── src/main/cpp/
│   │   ├── napi_init.cpp         # NAPI 入口、XComponent 回调、JS 桥
│   │   ├── loaf/
│   │   │   ├── Runtime.*         # 渲染线程、EGL、脚本调度、热重载
│   │   │   ├── GLRenderer.*      # GLES3 2D/3D、FBO、着色器、字体
│   │   │   ├── LuaVM.*           # Lua 状态、回调、错误收集
│   │   │   ├── Bindings*.cpp     # of/loaf/osc 绑定
│   │   │   ├── Osc.*             # OSC 1.0 编解码与 UDP
│   │   │   ├── Audio.*           # OHAudio 输出与 WAV 解码
│   │   │   ├── AudioStream.*     # ofSoundStream 回调
│   │   │   ├── Video.*           # AVPlayer + NativeImage
│   │   │   ├── BindingsHardware.cpp  # Serial / Arduino(Firmata) / SoundDevice
│   │   │   ├── BindingsCamera.cpp    # ofVideoGrabber 的 NativeImage 表面
│   │   │   └── FrameServer.*     # Syphon 等价的 TCP 帧服务
│   │   └── third_party/          # Lua 5.4、stb_image
│   ├── src/main/ets/             # ArkUI 壳层（工具栏、文件选择、CameraBridge）
│   └── src/main/resources/
│       ├── rawfile/examples/     # loaf 官方示例
│       └── rawfile/selftest.lua  # 132 项自测
├── docs/                         # 使用文档与 API 覆盖
└── README.md                     # 项目总览
```

一句话概括适配策略：**脚本接口不变，运行时重建**。桌面版 loaf 用
openFrameworks 提供的窗口、绘图、音频、网络；鸿蒙版用 XComponent、EGL、
OHAudio、AVPlayer、UDP socket 重新实现同样的能力，再通过 NAPI 和 ArkUI 壳层
把它们组装成一个完整的应用。

---

## 环境要求

| 项目 | 要求 | 备注 |
| --- | --- | --- |
| DevEco Studio | 6.1.1 或更高 | 需要自带 SDK、hvigor 与 native 工具链 |
| compatibleSdkVersion | `6.1.1(24)` | 见 `build-profile.json5` |
| targetSdkVersion | `26.0.0` | 当前工程配置 |
| 目标设备 | `2in1`（arm64-v8a） | 鸿蒙 PC 真机或 2in1 模拟器 |
| 签名 | 可选 | 开发阶段可装未签名 HAP；发布需配置 `signingConfigs` |
| 宿主系统 | macOS / Windows / Linux | 本次开发与验证在 macOS 上完成 |

原生依赖全部来自鸿蒙 SDK：`libace_napi`、`libnative_window`、`libEGL`、
`libGLESv3`、`libnative_drawing`、`libohaudio`、`libavplayer`、
`libnative_image`，不需要额外下载第三方库。

---

## 编译与运行

### 1. 拉代码

```bash
git clone https://github.com/ujainu/harmony-loaf.git
cd harmony-loaf
```

### 2. 命令行构建

```bash
export DEVECO_SDK_HOME="/Applications/DevEco-Studio.app/Contents/sdk"
export PATH="/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin:$PATH"
hvigorw --mode module -p product=default assembleHap
```

产物在：

```text
entry/build/default/outputs/default/entry-default-unsigned.hap
```

`DEVECO_SDK_HOME` 必须指向 DevEco Studio 自带的 SDK 目录，否则 hvigor 会报
`Invalid value of 'DEVECO_SDK_HOME'`。另外，CMake 使用 `file(GLOB ...)` 收集
源文件，新增 `.cpp` 后需要改一下 `CMakeLists.txt` 触发重新配置，否则可能出现
「符号未定义」。

### 3. 用 DevEco Studio 跑（推荐）

1. 用 DevEco Studio 打开仓库根目录；
2. 等待 Sync 完成，选择 `entry` 模块；
3. 连接 2in1 设备或启动对应模拟器，点 Run。

### 4. 安装与启动

```bash
hdc install entry/build/default/outputs/default/entry-default-unsigned.hap
hdc shell aa start -a EntryAbility -b com.example.loaf_harmonypc
```

### 5. 开发循环

- 应用启动后会自动加载沙箱里的 `files/scripts/demo.lua`；
- 修改脚本后约 0.4 秒自动热重载；
- 也可以把 `.lua` 文件或含 `main.lua` 的文件夹拖进窗口；
- 仓库外提供了一个 `run.sh` 示例，用于把本地脚本推到模拟器并触发热重载。

---

## 适配了哪些地方

**渲染**。openFrameworks 的 2D API 是立即模式的，而 GLES 不支持
`glBegin/glEnd`。这里实现了一个批处理器：图元先累积到顶点数组，遇到纹理、
着色器、FBO 切换或帧结束时统一提交。坐标统一为「左上角原点、y 向下、逻辑
单位」，鼠标坐标按屏幕密度换算。

**字体**。没有 FreeType，改用系统 `OH_Drawing` 把文字栅格化成纹理并缓存。
`TrueTypeFont:load(path, size)` 会真正加载 TTF 文件，字号、行高、换行都生效。

**FBO 与着色器**。FBO 使用颜色纹理 + 深度 renderbuffer，`getPixel` 通过
`glReadPixels` 读回，便于自动化断言。`ofShader` 支持顶点/片元组合、纯片元、
uniform 设置；着色器需要 GLSL ES 3.0（`#version 300 es`）。

**3D**。EGL 配置开启 24 位深度缓冲，渲染器维护投影/视图/模型矩阵和矩阵栈，
提供 `of.Camera`、`of.Mesh`、`of.drawBox`/`drawSphere`、Cone/Cylinder/Plane/
IcoSphere 图元、`of.CubeMap` 天空盒。光照是单方向光的简化模型。

**Lua 运行时**。Lua 5.4 复用 ofxLua 的源码，每个脚本创建独立状态，注册
`of`/`loaf`/`osc` 三个全局表。回调包括 `setup/update/draw/exit`、
`oscReceived`、鼠标/键盘/窗口/拖拽，以及 `audioOut`。

**热重载**。每帧检查脚本文件的 `st_mtim`（纳秒）与大小，变化后重新加载。
早期只比较秒级时间戳，同一秒内保存两次不会触发，后来修正。

**输入**。鸿蒙键码映射到 openFrameworks 值（ASCII、方向键、修饰键），并在
原生层按「键值 + 按下状态 + 60ms」去重，避免 ArkUI 两条焦点路径各派发一次。

**窗口与缩放**。默认行为与 openFrameworks 一致：窗口变大只是画布变大。
另外提供 `loaf.setResolution(w, h)`，锁定逻辑尺寸后按 `min(宽比, 高比)`
等比缩放并居中，鼠标坐标反向映射；工具栏的「适配」按钮就是这个开关。

**音频**。`ofSoundPlayer` 解析 WAV、重采样到 44.1kHz 立体声并通过 OHAudio
播放；`ofSoundStream` 通过 Lua `audioOut` 回调生成样本，按缓冲水位补块。

**视频**。`AVPlayer` 解码到 `OH_NativeImage`，再绑定成
`GL_TEXTURE_EXTERNAL_OES` 纹理，直接绘制进画布。

**OSC 与帧服务**。完整 OSC 1.0 编解码（15 种参数、bundle），支持
`Message/Bundle/Sender/Receiver/ParameterSync`。Syphon 没有鸿蒙对应实现，
改为 TCP 帧服务，协议为 `"LOAF" | 宽 | 高 | 序号 | 字节数 | RGBA`。

**串口与 Firmata**。`ofSerial` 直接使用 POSIX `termios`（1200–230400 波特率，
非阻塞读写）；`ofArduino` 实现 Firmata 子集：引脚模式、数字输出、PWM、
模拟/数字上报，以及在渲染线程轮询解析后回调 `digitalPinChanged` /
`analogPinChanged`。设备列表通过扫描 `/dev/ttyUSB*`、`/dev/ttyACM*`、
`/dev/ttyS*` 得到。

**摄像头**。原生侧创建 `OH_NativeImage` 作为 GL 外部纹理，并把它的 surfaceId
通过事件发给 ArkTS；ArkTS 用 `@kit.CameraKit` 选择摄像头、匹配预览分辨率、
创建 `PreviewOutput` 并启动会话。画面回到原生侧后直接绘制进画布，可以和其它
图形混合。需要 `ohos.permission.CAMERA` 与真机摄像头。

**音频设备枚举**。`of.getSoundDeviceList()` 通过 OHAudio RoutingManager 列出
当前输入/输出设备，返回 `of.SoundDevice` 列表（名称、ID、类型）。

**兼容层**。官方 ofxLua 绑定有 105 个类、约 5354 个包装函数。未能真实实现的
类被安装为类型化桩，保证脚本不会因为缺类直接中断，同时把缺口公开在
`docs/API_COVERAGE.md` 中。

---

## 功能到底能用到什么程度

按 T0 / T1 / T2 三档说明。简单说：T0 是「能不能装、能不能起」，T1 是
「日常核心功能」，T2 是「桌面端的加分项」。

状态含义：

- **能用**：在本机 2in1 模拟器上实际运行并验证过。
- **部分能用**：功能已实现，但受设备/系统能力限制，验证不完整。
- **未适配**：平台没有对应能力，或按平台特点主动不做。

### T0 基础能力（能不能装、能不能起、能不能跑脚本）

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 编译打包成 HAP | 能用 | `hvigorw assembleHap` 通过，产物可安装 |
| 安装 / 启动 | 能用 | 模拟器与真机流程一致，启动无白屏 |
| 画布渲染 | 能用 | XComponent + EGL + GLES3，稳定出帧 |
| 脚本加载 | 能用 | 支持 `.lua` 与含 `main.lua` 的文件夹 |
| 热重载 | 能用 | 保存后约 0.4 秒生效，纳秒时间戳 |
| 错误处理 | 能用 | 语法/运行时错误显示浮层，修正后恢复 |

**小结**：T0 全部打通。HAP 能构建、能装、能起，脚本能加载、能热重载，
出错不会崩。

### T1 主要能力（日常创作核心）

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 2D 绘图（形状/变换/多边形） | 能用 | 兼容 openFrameworks 常用 API |
| 字体与文本 | 能用 | 系统排版 + 真实 TTF，支持行高/换行 |
| 图片加载与绘制 | 能用 | png/jpg/gif 等，支持缩放与取样 |
| FBO 离屏渲染 | 能用 | 分配、绘制、读回像素 |
| 着色器 | 能用 | GLSL ES 3.0，uniform 1f–4f/1i |
| OSC 收发 | 能用 | 15 种参数、bundle、Sender/Receiver |
| 音频播放 | 能用 | WAV 解码、重采样、混音、循环 |
| 鼠标 / 键盘 | 能用 | 键码映射、坐标换算、重复事件去重 |
| 拖拽导入 | 能用 | 文件与文件夹；无法识别时交给脚本 |
| 官方示例 | 能用 | 图形/多边形/字体/图片/OSC/文件/回调/窗口均运行 |
| 工具栏与文件选择 | 能用 | 打开、文件夹、重载、示例、自测 |

**小结**：T1 基本齐全。纯计算与绘图类能力稳定；输入和文件导入这类要跨系统
边界的功能也实际验证过。

### 硬件能力用法

串口与 Arduino（需要真实串口硬件）：

```lua
local arduino = of.Arduino()
arduino:setup("/dev/ttyUSB0", 57600)
arduino:sendDigitalPinMode(13, of.OUTPUT)
arduino:sendDigitalPin(13, 1)
arduino:reportAnalogPin(0, true)

function analogPinChanged(pin, value)
	print("A" .. pin, value)
end
```

摄像头（需要摄像头权限与真机）：

```lua
local camera

function setup()
	camera = of.VideoGrabber()
	camera:setup(640, 480)
end

function draw()
	camera:draw(0, 0, 640, 480)
end
```

音频设备枚举：

```lua
for _, device in ipairs(of.getSoundDeviceList()) do
	print(device:getDeviceName(), device:getDeviceID())
end
```

### T2 增强能力（桌面端加分项）

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 3D（相机/网格/图元/天空盒） | 部分能用 | 单方向光简化管线，无完整材质/阴影/后处理 |
| 实时音频（SoundStream） | 能用 | Lua `audioOut` 回调，约 58 块/秒 |
| 视频播放 | 部分能用 | AVPlayer + 外部纹理已实现；模拟器镜像缺解码器，需真机验证 |
| Syphon 等价帧服务 | 部分能用 | TCP 帧服务可被宿主机接收；不兼容 macOS Syphon 客户端 |
| XML 读写 | 能用 | 基本 DOM、子节点、属性、保存/加载 |
| launcher 脚本 | 能用 | 点击卡片通过 `/loaf/load` 切换示例 |
| 导出脚本 | 能用 | 系统保存对话框导出到桌面/文档 |
| 适配缩放 | 能用 | 锁定逻辑尺寸，窗口变化时等比缩放 |
| 自测 | 能用 | 132 项断言全部通过 |
| 摄像头（VideoGrabber） | 部分能用 | 原生创建 NativeImage 表面，ArkTS 侧用 `@kit.CameraKit` 启动预览；需要摄像头权限与真机 |
| Arduino / Firmata / Serial | 部分能用 | 已实现 POSIX 串口、Firmata 数字/模拟/PWM 与回调；需要真实串口硬件验证 |
| SoundDevice 枚举 | 能用 | 通过 OHAudio RoutingManager 列出输入/输出设备 |
| addons | 未适配 | 官方 loaf 本身也只带 ofxLua/ofxOsc/ofxSyphon |

**小结**：T2 的软件侧能力都已落地；摄像头、串口、视频解码需要真机硬件才能完成
最终验证，Syphon 仍只能功能等价。

---

## 整体到什么程度

把三张表合起来看：

- **T0**：构建、安装、启动、脚本、热重载全部到位，**100%**。
- **T1**：绘图、字体、图片、FBO、着色器、OSC、音频、输入、导入、示例都跑通，
  **约 95%**（差值主要是 openFrameworks 个别绘图函数的细节差异）。
- **T2**：13 项里 12 项已实现（8 项完整可用，3D/视频/Syphon/摄像头/串口
  4 项需真机或协议限制），addons 不在官方 loaf 范围内，**约 90%**。

官方绑定的 105 个类中，**102 个有真实语义**；剩余 3 个是 SWIG 内部类型
（`path`/`string`）和一个无 Lua 名字的基类，清单在 `docs/API_COVERAGE.md`。自测 132 项全部通过，
覆盖渲染、FBO、着色器、字体、音频、3D、OSC、文件、帧服务与兼容层。

**一句话总结**：loaf 的核心工作流——写脚本、保存、立刻看到画面、用 OSC
和别的工具通信——在鸿蒙 PC 上已经成立；FBO、着色器、3D、音频、视频、
帧服务这些桌面版能力也大多可用。没做的部分集中在摄像头、串口和 Syphon
二进制兼容，边界清楚，也有明确的后续路径。
