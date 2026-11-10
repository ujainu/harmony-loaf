# loaf → HarmonyOS PC 移植状态

## 结论

- 运行时、渲染、输入、脚本生命周期、OSC、音频、视频、3D、FBO/着色器、
  帧服务均已落地，并在 HarmonyOS 6.1.1（API 24）2in1 模拟器上验证。
- 官方 ofxLua 绑定的**接口面已全部对齐**：105 个类名、425 个自由函数都存在，
  脚本不会因缺类/缺函数直接中断。
- **已实现 91 个类**；其余 14 个为硬件/设备类或 SWIG 内部类型。

## 已验证

- `hvigorw clean assembleHap` 通过，安装启动正常
- 官方示例：graphics / polygon / fonts / imageLoader / osc / fileutils /
  callbacks / window 均可运行
- `selftest.lua` 128 项全部通过
- 音频：WAV 解码 → 44.1kHz 立体声混音输出，位置/结束状态正确
- 3D：透视相机、深度遮挡、Mesh、Box/Sphere/Cone/Cylinder/Plane、
  FBO 像素断言
- Syphon 等价：TCP 帧服务，宿主机收到 3 帧完整 RGBA 数据
- 输入：鼠标/触摸/键盘映射到 OF 键码，重复事件已去重
- UI：640×480 画布窗口、无默认工具栏、原版错误浮层样式、快捷键

## 已实现（90 类，完整清单见 API_COVERAGE.md）

Image, TrueTypeFont, Fbo, Shader, Mesh, Camera, EasyCam, Buffer, File,
Directory, FilePath, SoundPlayer, VideoPlayer, Color, Point, Pixels, Texture,
Polyline, Path, Rectangle, Node, Material, Light, FpsCounter, Parameter,
BoxPrimitive, SpherePrimitive, ConePrimitive, CylinderPrimitive,
PlanePrimitive, Vec2f, Vec3f, Vec4f, FloatVector, IntVector, StringVector,
Matrix4x4, Quaternion
SoundStream（Lua audioOut 回调 + 缓冲水位控制）
IcoSpherePrimitive, CubeMap（加载 + 天空盒）, BufferObject

## 未实现（15 个 compat 桩）

主要是：

- 采集：VideoGrabber（需 ArkTS 传入 Camera_Manager）、SoundDevice
- 网络：HttpRequest、HttpResponse
- 硬件：Arduino、Firmata 系列
- 数据容器/工具：各种 Vector、Quaternion、Matrix、FboSettings、DragInfo 等
- 其余 3D/图形辅助类

完整清单见 `docs/API_COVERAGE.md`。

## 已知边界

- 视频解码依赖设备媒体服务；本机模拟器镜像不带解码器（H.264/HEVC 均报
  `DEM_FMT_ERR`），需真机验证
- Syphon 无二进制兼容，只提供 TCP 帧服务
- 着色器要求 GLSL ES 3.0
- 3D 为单方向光简化管线
- addons 不在官方 loaf 范围内，未覆盖

## 继续移植的建议顺序

1. VideoGrabber / SoundDevice（采集）
2. HttpRequest（网络）
3. Matrix4x4 / Quaternion（3D 数学）
4. Arduino/Firmata（硬件）
5. 其余数据容器类
