# ohos_loaf

ohos_loaf 将 loaf（基于 openFrameworks 的 Lua 实时编码环境）迁移到 OpenHarmony / HarmonyOS PC。上游依赖的 openFrameworks 在鸿蒙上没有后端，本项目用 XComponent + EGL + OpenGL ES 3.0 重建渲染层，内嵌 Lua 5.4，保持 of.* / loaf.* / osc.* 脚本接口不变，并实现 FBO、GLSL ES 着色器、3D、OHAudio 音频、AVPlayer + NativeImage 视频、OSC 收发、串口/Firmata、摄像头桥接与 TCP 帧服务。支持拖拽导入、保存即热重载、错误浮层与示例库，现有 loaf 脚本无需改代码即可运行，面向实时视觉、互动装置与教学场景。
技术栈：ArkTS/ArkUI + C++/NAPI + EGL/GLES3 + Lua 5.4 + OHAudio + AVPlayer + CMake/Hvigor。
开源动机：补齐鸿蒙在创意编码与实时脚本工具上的生态空缺，并把适配经验回馈给 openFrameworks / loaf 社区。
社区计划：持续补齐上游 ofxLua 绑定、维护示例与文档、接受 issue/PR、跟随上游版本更新。