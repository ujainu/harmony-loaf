# loaf for HarmonyOS PC 使用文档

## 1. 安装

构建产物：

```bash
export DEVECO_SDK_HOME="/Applications/DevEco-Studio.app/Contents/sdk"
export PATH="/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin:$PATH"
hvigorw --mode module -p product=default assembleHap
```

```bash
hdc install entry/build/default/outputs/default/entry-default-unsigned.hap
hdc shell aa start -a EntryAbility -b com.example.loaf_harmonypc
```

正式分发前在 `build-profile.json5` 配置 `signingConfigs`。

## 2. 界面

默认窗口 640×480，包含一条工具栏和下方画布：

- 打开：选择一个 `.lua` 脚本
- 文件夹：选择包含 `main.lua` 的工程目录
- 重载：重新加载当前脚本
- 示例：打开内置的 loaf 官方示例库
- 自测：运行 128 项接口自测
- 导出脚本：把当前脚本通过系统保存对话框导出到桌面/文档
- 适配：锁定当前逻辑尺寸，窗口缩放时画面等比放大
- ?：快捷键与使用说明

工具栏可以用 `Ctrl+Shift+P` 显示/隐藏，`×` 关闭。

## 3. 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+R` | 重载当前脚本 |
| `Ctrl+Shift+R` | 清空当前脚本 |
| `Ctrl+O` | 打开脚本 |
| `Ctrl+F` | 全屏切换 |
| `Ctrl+Shift+P` | 显示/隐藏工具栏 |
| `Esc` | 交给脚本处理（示例脚本常用于清空） |

按键会映射为 openFrameworks 键码：字母为 ASCII，`Esc=27`，回车 `13`，
方向键为 `0xFF00`–`0xFF03`。

## 4. 载入脚本

三种方式：

1. 拖拽单个 `.lua` 文件到窗口
2. 拖拽包含 `main.lua` 的文件夹到窗口
3. 工具栏「打开 / 文件夹」

脚本会被复制到应用沙箱的 `files/scripts/` 下运行，相对路径的数据文件
（图片、字体等）放在脚本同目录即可。

## 5. 编写脚本

```lua
local t = 0

function setup()
	of.setWindowTitle("hello")
	of.background(20)
	of.setCircleResolution(64)
end

function update()
	t = t + of.getLastFrameTime()
end

function draw()
	of.setColor(255, 180, 40)
	of.drawCircle(of.getWidth() / 2, of.getHeight() / 2, 80 + 20 * math.sin(t))
end
```

可用回调：

`setup` `update` `draw` `exit` `gotMessage` `oscReceived`
`mousePressed` `mouseReleased` `mouseMoved` `mouseDragged`
`keyPressed` `keyReleased` `windowResized` `dragEvent` `audioOut`

保存脚本后约 0.4 秒自动热重载。

## 6. 示例

工具栏「示例」内置 loaf 官方示例：图形、多边形、字体、图片、OSC、文件工具、
回调、窗口，以及自测脚本。示例会被解包到 `files/scripts/examples/`。

## 7. 音频

```lua
local tone

function setup()
	tone = of.SoundPlayer()
	tone:load("beep.wav")
	tone:setVolume(0.6)
	tone:play()
end
```

实时合成使用 `of.SoundStream` 与全局 `audioOut`：

```lua
local phase = 0
function setup()
	local s = of.SoundStream()
	s:setup(2, 0, 44100, 735)
	s:start()
end

function audioOut(buffer)
	local frames = buffer:getNumFrames()
	local channels = buffer:getNumChannels()
	for i = 0, frames - 1 do
		local v = math.sin(phase) * 0.2
		for c = 0, channels - 1 do
			buffer:setSample(i * channels + c, v)
		end
		phase = phase + 2 * math.pi * 220 / 44100
	end
end
```

支持 WAV（8/16/24/32-bit PCM、float32），统一重采样为 44.1kHz 立体声。

## 8. OSC

```lua
function setup()
	loaf.setListenPort(9990)
	loaf.startListening()
	loaf.setSendHost("127.0.0.1")
	loaf.setSendPort(8880)
	loaf.send("/hello", 1, 2.5, "text")
end

function oscReceived(message)
	print(message:getAddress(), message:getArgAsFloat(0))
end
```

控制消息 `/loaf/load <绝对路径>` 会直接切换脚本（launcher 就使用它）。
`osc.ParameterSync` 提供参数发布与回调。

## 9. 3D

```lua
local camera = of.Camera()
camera:setPosition(0, 0, 400)
camera:lookAt(0, 0, 0)

function draw()
	of.background(12)
	camera:begin()
	of.setColor(240, 200, 40)
	of.drawBox(0, 0, 0, 120)
	of.drawSphere(0, 160, 0, 50)
	camera:endCamera()
end
```

可用：`of.Camera`、`of.EasyCam`、`of.Mesh`、`of.drawBox/drawSphere`、
`BoxPrimitive`/`SpherePrimitive`/`ConePrimitive`/`CylinderPrimitive`/
`PlanePrimitive`/`IcoSpherePrimitive`、`of.Material`、`of.Light`、
`of.CubeMap`。

## 10. 离屏渲染与着色器

```lua
local fbo = of.Fbo()
fbo:allocate(512, 512)

fbo:begin()
of.background(0, 0, 0)
of.setColor(255, 80, 60)
of.drawCircle(256, 256, 120)
fbo:endFbo()

function draw()
	of.background(18)
	fbo:draw(40, 40)
end
```

`of.Shader` 支持 `load(vert, frag)`、`loadFromSource`、`loadFragmentSource`、
`setUniform1f..4f`、`setUniform1i`。着色器需使用 GLSL ES 3.0（`#version 300 es`）。

## 11. 帧服务（Syphon 等价）

```lua
loaf.setSyphonPort(9999)
loaf.startSyphon("my-loaf")
```

通过 TCP 推送画布：`"LOAF" | u32 宽 | u32 高 | u32 序号 | u32 字节数 | RGBA`，
默认 10fps、1/2 降采样。可用 `hdc fport tcp:9999 tcp:9999` 转发到宿主机接收。

## 12. 缩放与窗口

默认行为与 openFrameworks 一致：窗口变大只是画布变大，内容按像素坐标绘制。
点击工具栏「适配」会在脚本中执行 `loaf.setResolution(当前宽, 当前高)`，
之后窗口变化时画面等比缩放，鼠标坐标会反向映射。

脚本里也可以直接调用：

```lua
loaf.setResolution(960, 600)
loaf.clearResolution()
```

## 13. 自测与诊断

```bash
hdc shell "uitest uiInput click <x> <y>"   # 点「自测」
hdc shell "hilog -x | grep loafcore"
```

自测覆盖渲染、FBO、着色器、字体、音频、3D、OSC、文件、帧服务等。

常见问题：

- 视频无法播放：模拟器镜像可能缺少解码服务，需真机验证
- 脚本报错：画面会显示白底红字的错误浮层，修正后自动恢复
- 热重载不触发：确认文件已写入脚本所在目录（沙箱 `files/scripts`）
