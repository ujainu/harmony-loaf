# loaf for HarmonyOS PC

把 [loaf](https://github.com/danomatika/loaf)（Lua + OSC + openFrameworks 的实时编码
运行时）移植到鸿蒙 PC（2in1）。openFrameworks 没有鸿蒙后端，因此这里用鸿蒙原生能力
重建了运行时，并保持 loaf 的脚本接口不变。

```
┌────────────────────────── ArkUI (Index.ets) ──────────────────────────────┐
│ 工具栏：打开 / 文件夹 / 重载 / 示例库 / 自测   拖拽导入   错误浮层   快捷键 │
├────────────────────────── XComponent (SURFACE) ────────────────────────────┤
│ EGL + OpenGL ES 3.0  GLRenderer：批处理 2D、系统字体文本、纹理/图片        │
│ LuaVM 5.4  of.* / loaf.* / osc.* 绑定   OSC(UDP) 收发   热重载轮询          │
└────────────────────────────────────────────────────────────────────────────┘
```

## 目录

| 路径 | 说明 |
| --- | --- |
| `entry/src/main/cpp/napi_init.cpp` | NAPI 入口、XComponent 回调（surface/触摸/键盘）、JS 桥 |
| `entry/src/main/cpp/loaf/Runtime.*` | 渲染线程、EGL 生命周期、脚本调度、热重载、事件分发 |
| `entry/src/main/cpp/loaf/GLRenderer.*` | GLES3 2D 渲染器（批次、变换栈、多边形、系统字体字形缓存） |
| `entry/src/main/cpp/loaf/LuaVM.*` | Lua 状态、脚本加载、回调调用、print 转发、错误收集 |
| `entry/src/main/cpp/loaf/BindingsOf.cpp` | `of.*` 绘图 / 图像 / 字体 / 事件 / 工具函数 |
| `entry/src/main/cpp/loaf/BindingsFile.cpp` | `of.Buffer` `of.File` `of.Directory` `of.FilePath` |
| `entry/src/main/cpp/loaf/BindingsLoaf.cpp` | `loaf.*`、`osc.*`（Message/Bundle/Sender/Receiver/Settings） |
| `entry/src/main/cpp/loaf/Bindings3D.cpp` | `of.Camera` `of.Mesh` `of.drawBox` `of.drawSphere` |
| `entry/src/main/cpp/loaf/BindingsAudio.cpp` | `of.SoundPlayer`（OHAudio） |
| `entry/src/main/cpp/loaf/BindingsVideo.cpp` | `of.VideoPlayer`（AVPlayer + NativeImage） |
| `entry/src/main/cpp/loaf/FrameServer.*` | Syphon 等价的本地帧服务（TCP） |
| `entry/src/main/cpp/loaf/Math3D.h` | 4x4 矩阵 / 透视 / lookAt |
| `entry/src/main/cpp/loaf/Osc.*` | OSC 1.0 编解码，全部 15 种参数类型 + bundle |
| `entry/src/main/cpp/third_party/lua` | Lua 5.4（来自 `loaf/src_ofxLua/libs/lua`） |
| `entry/src/main/ets/loaf/ScriptStore.ets` | 脚本/文件夹导入、示例库解包 |
| `entry/src/main/resources/rawfile/examples` | loaf 官方示例（原样收录） |
| `entry/src/main/resources/rawfile/selftest.lua` | 128 项接口自测 |

## 构建

```bash
export DEVECO_SDK_HOME="/Applications/DevEco-Studio.app/Contents/sdk"
export PATH="/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin:$PATH"
hvigorw --mode module -p product=default assembleHap
```

产物：`entry/build/default/outputs/default/entry-default-unsigned.hap`

安装并启动：

```bash
hdc install entry/build/default/outputs/default/entry-default-unsigned.hap
hdc shell aa start -a EntryAbility -b com.example.loaf_harmonypc
```

正式分发前需在 `build-profile.json5` 配置 `signingConfigs`。

## 使用

- 默认窗口 640×480，界面与原版 loaf 一致：只有画布，没有工具栏
- 快捷键与原版一致：`Ctrl+R` 重载、`Ctrl+Shift+R` 清空、`Ctrl+F` 全屏；
  `Ctrl+O` 打开脚本、`Ctrl+Shift+P` 显示附加面板（本移植的额外入口）
- 拖入 `.lua` 或含 `main.lua` 的文件夹即载入；无法识别时交给脚本的 `dragEvent`
- 启动自动载入内置示例；「示例」按钮打开官方示例库（图形、多边形、字体、图片、
  OSC、文件工具、回调、窗口、自测）
- 「打开」选择 `.lua`；「文件夹」选择含 `main.lua` 的工程；也可把文件/文件夹拖进窗口
- 脚本保存后约 0.4s 自动热重载；`Ctrl+R` 重载，`Ctrl+O` 打开，`Esc` 停止
- OSC 默认监听 `9990`、发送到 `127.0.0.1:8880`
- 回调：`setup` `update` `draw` `exit` `gotMessage` `oscReceived`
  `mousePressed` `mouseReleased` `mouseMoved` `mouseDragged`
  `keyPressed` `keyReleased` `windowResized`

## 接口覆盖

`loaf.*`：完整，包含 `setVerbose` `isVerbose` `setUseDefaultKeys` `useDefaultKeys`
`isBundled` `getStartDirectory` `getCurrentDirectory` `setCurrentDirectory`
`getHostname` `reloadScript` `clearScript` `set/getListenPort`
`start/stopListening` `isListening` `set/getSendHost` `set/getSendPort`
`sendMessage` `sendBundle` `sendBang` `sendFloat` `sendInt` `sendString`
`send`（变参）、Syphon 系列（`startSyphon/stopSyphon/isSyphonPublishing/
setSyphonName/getSyphonName/setSyphonPort/getSyphonPort/getSyphonFrameCount`）。
`osc.ParameterSync` 提供参数发布
（`addFloat/addInt/addBool/addString`、`set/get`、`send`、`handle`、`setCallback`）。

`osc.*`：`Message` / `Bundle` / `Sender` / `Receiver` / `ReceiverSettings` /
`SenderSettings`，消息支持 int32、int64、float、double、string、symbol、char、
bool、trigger、timetag、blob、midi、none、rgba，含读写、拷贝、类型串、远端端点，
收发支持 bundle 与嵌套 bundle。

`of.*` 绘图：`background clear setColor setHexColor fill noFill setLineWidth
setCircleResolution drawRectangle drawRectangleOutline drawCircle drawEllipse
drawLine drawTriangle drawPolyline drawPolygon drawRectRounded drawArc
beginShape vertex curveVertex bezierVertex nextContour endShape
pushMatrix popMatrix pushStyle popStyle pushView popView translate rotate
rotateDeg scale`

文本/图像：`drawBitmapString drawBitmapStringHighlight drawString stringWidth
loadFont TrueTypeFont loadImage Image drawImage Point Fbo Shader`

- `of.Fbo`：`allocate begin endFbo/finish draw clear getWidth getHeight
  isAllocated getPixel`，用于离屏渲染与后处理
- `of.Shader`：`load(vert, frag)` / `load(vert)` / `loadFromSource` /
  `loadFragmentSource`、`begin` / `endShader`、`setUniform1f..4f`、`setUniform1i`
- `of.SoundPlayer`（`of.SoundStream` 同义）：`load play stop pause resume
  setPaused isPlaying isLoaded setLoop isLooping setVolume getVolume
  getDuration getPosition setPosition`；WAV 解码后统一重采样为 44.1kHz 立体声
  并由 OHAudio 混音输出
- `of.VideoPlayer`：`load play pause stop setLoop isLooping isPlaying isLoaded
  setVolume getDuration getPosition setPosition getWidth getHeight
  getFrameCount getError draw`；AVPlayer 解码到 `OH_NativeImage` 外部纹理，
  由 GLES 直接绘制进画布（可与其它图形混合）
- `of.Camera`：`setPosition lookAt/setTarget setFov setNearClip setFarClip
  begin endCamera`；`of.Mesh`：`addVertex addColor addNormal addIndex
  addTriangle getNumVertices getNumIndices draw drawWireframe`；
  另有 `of.drawBox` `of.drawSphere` 与 `rotateXDeg/rotateYDeg/rotateZDeg`
- 字体：`loadFont` / `TrueTypeFont:load` 通过 `OH_Drawing_TypefaceCreateFromFile`
  真正加载 TTF，字号、行高、换行均生效

运行时：`getWidth getHeight getWindowWidth getWindowHeight getScreenWidth
getScreenHeight getElapsedTimef getElapsedTimeMillis getFrameNum
getLastFrameTime getFrameRate setFrameRate getMouseX getMouseY getMousePressed
getKeyPressed setWindowTitle setWindowShape setWindowPosition setFullscreen
toggleFullscreen setWindowMode getWindowMode setBackgroundAuto
setEscapeQuitsApp random randomSeed noise signedNoise map clamp lerp dist
distSquared toString toInt toFloat degToRad radToDeg sendMessage` 与
`PI TWO_PI HALF_PI POLY_WINDING_* IMAGE_GRAYSCALE` 等常量。

文件工具：`of.Buffer`（size/clear/set/append/getData/getText/isEmpty）、
`of.File`（读写/复制/移动/删除/大小/权限/类型判断/路径分解）、
`of.Directory`（创建/列举/存在/路径）、`of.FilePath`（join/取目录/取扩展名等）。

## 与桌面版 loaf 的差异

- 着色器需使用 GLSL ES 3.0（`#version 300 es`），桌面 GLSL 需改写
- `osc.ParameterSync` 是简化语义：不自动绑定 `ofParameter`，需显式 `send()` 与
  `handle(message)`（可在 `oscReceived` 中调用）
- 音频仅支持 WAV 文件播放（PCM 8/16/24/32-bit 与 float32），暂无采集/输入流与
  mp3/ogg 解码
- 视频解码依赖设备的媒体解码服务：本机模拟器镜像不带解码器，H.264/HEVC 均返回
  `DEM_FMT_ERR ... unsupport interface`，因此解码链路只能在真机验证
- 3D 为单方向光 + 顶点法线的简化管线，没有 OF 的材质/阴影/后期链路
- Syphon 无二进制兼容：`startSyphon()` 现在启动本地 TCP 帧服务，协议为
  `"LOAF" | u32 宽 | u32 高 | u32 序号 | u32 字节数 | RGBA`，默认端口 9999、
  10fps、1/2 降采样；需要配套接收端，macOS 的 Syphon 客户端不能直连
- 尚未实现：openFrameworks addons
- `of.Image` 的灰度/像素变换为兼容占位，`getColor` 返回真实像素与颜色度量

## 已验证

在 HarmonyOS 6.1.1（API 24）2in1 模拟器（3120×2080，密度 2.0）上：

- 原生 GLES3 渲染、系统字体、图片（png/jpg/gif）显示正常
- Lua 回调链路、热重载、错误浮层、print 转发正常
- 触摸/鼠标/键盘事件到达脚本（逻辑坐标换算正确）
- 音频：脚本内生成 22050Hz 单声道 WAV，加载、重采样、播放、音量、结束状态均验证
- OSC 回环收发、消息全类型、bundle、手动 Receiver 正常
- 官方示例 `graphicsExample` `polygonExample` `fontsExample`
  `imageLoaderExample` `osc.lua` `fileutils.lua` `callbacks.lua` `window.lua`
  均可运行；`selftest.lua` 128 项全部通过（含 FBO、着色器、ParameterSync、TTF、
  音频、视频接口、3D、帧服务、Pixels/Texture/Polyline/Path、Rectangle/Node/
  Material/Light/图元、Vec2f/3f、Color、兼容层）

## API 覆盖

对照官方 ofxLua 绑定（105 个类、约 5354 个包装函数）：**91 个类为原生/等价实现**，
其余 14 个类为硬件/设备类或 SWIG 内部类型，由 `CompatStubs.h` 安装为兼容桩——
类名、方法名与官方一致，调用不会因 nil 中断，但返回默认值。

逐类状态见 [docs/API_COVERAGE.md](docs/API_COVERAGE.md)。
