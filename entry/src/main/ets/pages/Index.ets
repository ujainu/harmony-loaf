import { common } from '@kit.AbilityKit';
import { display } from '@kit.ArkUI';
import { window } from '@kit.ArkUI';
import { KeyCode } from '@kit.InputKit';
import { CameraBridge } from '../loaf/CameraBridge';
import { unifiedDataChannel } from '@kit.ArkData';
import loafcore, { LoafEvent, LoafInfo } from 'libloafcore.so';

import { ScriptStore } from '../loaf/ScriptStore';

@Entry
@Component
struct Index {
  @State scriptName: string = '未载入脚本';
  @State status: string = '就绪';
  @State errorText: string = '';
  @State showHelp: boolean = false;
  @State showExamples: boolean = false;
  @State showPanel: boolean = true;
  @State frameRate: number = 60;
  @State surfaceInfo: string = '';
  @State dragActive: boolean = false;
  @State fullscreen: boolean = false;
  @State fitMode: boolean = false;

  private context: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
  private infoTimer: number = -1;
  private lastKeyCode: number = -1;
  private lastKeyTime: number = 0;
  private xController: XComponentController = new XComponentController();

  aboutToAppear(): void {
    try {
      const info = display.getDefaultDisplaySync();
      loafcore.setDensity(info.densityPixels);
    } catch (err) {
      console.error('loaf: could not read display density', JSON.stringify(err));
    }

    loafcore.onEvent((raw: string) => {
      try {
        const event = JSON.parse(raw) as LoafEvent;
        this.handleEvent(event);
      } catch (err) {
        console.error('loaf: bad event', raw);
      }
    });

    this.infoTimer = setInterval(() => {
      this.refreshInfo();
    }, 1000);

    setTimeout(() => {
      try {
        ScriptStore.ensureExamples(this.context);
      } catch (err) {
        console.error('loaf: examples unpack failed', JSON.stringify(err));
      }
      this.syncFromRuntime();
      const demo: string = ScriptStore.demoPath(this.context);
      if (this.scriptName === '未载入脚本' && demo.length > 0) {
        loafcore.loadScript(demo);
      }
    }, 400);
  }

  private syncFromRuntime(): void {
    try {
      const info = JSON.parse(loafcore.getInfo()) as LoafInfo;
      if (info.script && info.script.length > 0) {
        const parts: string[] = info.script.split('/');
        this.scriptName = parts[parts.length - 1];
        this.status = info.error && info.error.length > 0 ? '脚本错误' : '运行中';
        this.errorText = info.error ?? '';
      }
    } catch (err) {
      console.error('loaf: sync failed', JSON.stringify(err));
    }
  }

  aboutToDisappear(): void {
    if (this.infoTimer !== -1) {
      clearInterval(this.infoTimer);
      this.infoTimer = -1;
    }
  }

  private handleEvent(event: LoafEvent): void {
    if (event.type === 'error') {
      this.errorText = event.message ?? '未知错误';
      this.status = '脚本错误';
    } else if (event.type === 'script' && event.path) {
      this.errorText = '';
      this.status = event.reload ? '已热重载' : '运行中';
      const scriptPath: string = event.path;
      const parts: string[] = scriptPath.split('/');
      this.scriptName = parts[parts.length - 1];
    } else if (event.type === 'info' && event.message) {
      this.status = event.message;
    } else if (event.type === 'ready') {
      this.status = '运行时就绪';
    } else if (event.type === 'camera') {
      if (event.action === 'start' && event.surfaceId) {
        CameraBridge.start(this.context, event.surfaceId, event.width ?? 640, event.height ?? 480)
          .then((error: string) => {
            console.info(`loaf: camera start result "${error}"`);
            if (error.length > 0) {
              this.errorText = error;
            }
          });
      } else if (event.action === 'stop') {
        CameraBridge.stop();
      }
    }
  }

  private refreshInfo(): void {
    try {
      const info = JSON.parse(loafcore.getInfo()) as LoafInfo;
      this.frameRate = info.frameRate;
      this.surfaceInfo = `${info.width} × ${info.height}`;
      if (this.scriptName === '未载入脚本' && info.script && info.script.length > 0) {
        const parts: string[] = info.script.split('/');
        this.scriptName = parts[parts.length - 1];
        this.status = info.error && info.error.length > 0 ? '脚本错误' : '运行中';
      }
    } catch (err) {
      console.error('loaf: info parse failed', JSON.stringify(err));
    }
  }

  private runDemo(): void {
    try {
      const path = ScriptStore.ensureDemo(this.context);
      loafcore.loadScript(path);
    } catch (err) {
      this.errorText = `无法写入示例脚本: ${JSON.stringify(err)}`;
    }
  }

  private runSelfTest(): void {
    try {
      const path = ScriptStore.ensureRaw(this.context, 'selftest.lua');
      loafcore.loadScript(path);
    } catch (err) {
      this.errorText = `无法写入自测脚本: ${JSON.stringify(err)}`;
    }
  }

  private openExample(relative: string): void {
    try {
      if (relative === 'demo.lua') {
        this.showExamples = false;
        this.runDemo();
        return;
      }
      if (relative === 'selftest.lua') {
        this.showExamples = false;
        this.runSelfTest();
        return;
      }
      const root = ScriptStore.ensureExamples(this.context);
      loafcore.loadScript(root + '/' + relative);
      this.showExamples = false;
    } catch (err) {
      this.errorText = `示例加载失败: ${JSON.stringify(err)}`;
    }
  }

  private async openScript(): Promise<void> {
    try {
      const path = await ScriptStore.pickScript(this.context);
      if (path) {
        loafcore.loadScript(path);
      }
    } catch (err) {
      this.errorText = `打开脚本失败: ${JSON.stringify(err)}`;
    }
  }

  private async openFolder(): Promise<void> {
    try {
      const path = await ScriptStore.pickFolder(this.context);
      if (path) {
        loafcore.loadScript(path);
      } else {
        this.errorText = '所选文件夹中缺少 main.lua';
      }
    } catch (err) {
      this.errorText = `打开文件夹失败: ${JSON.stringify(err)}`;
    }
  }

  private async exportLauncher(): Promise<void> {
    try {
      const demo = ScriptStore.demoPath(this.context);
      if (demo.length === 0) {
        this.errorText = '当前没有可导出的脚本';
        return;
      }
      const saved = await ScriptStore.exportScript(this.context, demo, 'loaf-gallery.lua');
      this.status = saved ? '已导出脚本' : '已取消导出';
    } catch (err) {
      this.errorText = `导出失败: ${JSON.stringify(err)}`;
    }
  }

  private toggleFit(): void {
    try {
      const info = JSON.parse(loafcore.getInfo()) as LoafInfo;
      if (!this.fitMode) {
        loafcore.evaluate(`loaf.setResolution(${info.width}, ${info.height})`);
        this.fitMode = true;
        this.status = '已锁定当前尺寸，窗口变化时等比缩放';
      } else {
        loafcore.evaluate('loaf.clearResolution()');
        this.fitMode = false;
        this.status = '已关闭等比缩放';
      }
    } catch (err) {
      this.errorText = `切换缩放失败: ${JSON.stringify(err)}`;
    }
  }

  build() {
    Stack({ alignContent: Alignment.TopStart }) {
      XComponent({
        id: 'loaf_surface',
        type: XComponentType.SURFACE,
        libraryname: 'loafcore',
        controller: this.xController
      })
        .width('100%')
        .height('100%')
        .onMouse((event: MouseEvent) => {
          if (event.action === MouseAction.Move) {
            loafcore.pointerEvent(event.x, event.y, 0, 0);
          } else if (event.action === MouseAction.Press) {
            loafcore.pointerEvent(event.x, event.y, event.button, 1);
          } else if (event.action === MouseAction.Release) {
            loafcore.pointerEvent(event.x, event.y, event.button, 2);
          }
        })

      if (this.showPanel) {
        this.Toolbar()
      }

      if (this.errorText.length > 0) {
        Scroll() {
          Text(this.errorText)
            .fontSize(14)
            .fontColor('#ffe7e7')
            .fontFamily('HarmonyOS Sans Mono')
            .width('100%')
        }
        .width('92%')
        .constraintSize({ maxHeight: 240 })
        .backgroundColor('#e02a0d14')
        .borderRadius(10)
        .padding(18)
        .position({ x: '4%', y: '76%' })
        .onClick(() => { this.errorText = ''; })
      }

      if (this.showHelp) {
        this.HelpOverlay()
      }

      if (this.showExamples) {
        this.ExampleGallery()
      }

      if (this.dragActive) {
        this.DropHint()
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#0b0d12')
    .focusable(true)
    .defaultFocus(true)
    .onKeyEvent((event: KeyEvent) => {
      if (event.stopPropagation) {
        event.stopPropagation();
      }
      if (event.type !== KeyType.Down) {
        loafcore.keyEvent(event.keyCode, false);
        return;
      }
      const now: number = Date.now();
      if (this.lastKeyCode === event.keyCode && now - this.lastKeyTime < 40) {
        return;
      }
      this.lastKeyCode = event.keyCode;
      this.lastKeyTime = now;
      loafcore.keyEvent(event.keyCode, true);
      const ctrl: boolean = event.getModifierKeyState ? event.getModifierKeyState(['Ctrl']) : false;
      const shift: boolean = event.getModifierKeyState ? event.getModifierKeyState(['Shift']) : false;
      if (event.keyCode === KeyCode.KEYCODE_R && ctrl && shift) {
        loafcore.stop();
      } else if (event.keyCode === KeyCode.KEYCODE_R && ctrl) {
        loafcore.reload();
      } else if (event.keyCode === KeyCode.KEYCODE_F && ctrl) {
        this.toggleFullscreen();
      } else if (event.keyCode === KeyCode.KEYCODE_P && ctrl && shift) {
        this.showPanel = !this.showPanel;
      } else if (event.keyCode === KeyCode.KEYCODE_O && ctrl) {
        this.openScript();
      } else {
        loafcore.keyEvent(event.keyCode, true);
      }
    })
    .onDragEnter(() => {
      this.dragActive = true;
    })
    .onDragLeave(() => {
      this.dragActive = false;
    })
    .onDrop((event: DragEvent) => {
      this.dragActive = false;
      this.handleDrop(event);
    })
  }

  private toggleFullscreen(): void {
    try {
      const mainWindow = window.getLastWindow(this.context);
      mainWindow.then((win: window.Window) => {
        win.setWindowLayoutFullScreen(!this.fullscreen);
        this.fullscreen = !this.fullscreen;
      });
    } catch (err) {
      console.error('loaf: fullscreen failed', JSON.stringify(err));
    }
  }

  @Builder
  DropHint() {
    Column() {
      Text('松开以载入')
        .fontSize(26)
        .fontColor('#eaf2ff')
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor('#662a6cff')
    .border({ width: 3, color: '#7ba7ff' })
    .hitTestBehavior(HitTestMode.None)
  }

  private handleDrop(event: DragEvent): void {
    try {
      const records: unifiedDataChannel.UnifiedRecord[] = event.getData().getRecords();
      let loaded = false;
      let droppedUri: string = '';
      for (const record of records) {
        if (record instanceof unifiedDataChannel.Folder) {
          const folder = record as unifiedDataChannel.Folder;
          droppedUri = folder.uri;
          const main = ScriptStore.importFolder(this.context, folder.uri);
          if (main) {
            loafcore.loadScript(main);
            loaded = true;
          } else {
            this.errorText = '拖入的文件夹里没有 main.lua';
          }
          break;
        }
        if (record instanceof unifiedDataChannel.File) {
          const file = record as unifiedDataChannel.File;
          droppedUri = file.uri;
          const path = ScriptStore.importFile(this.context, file.uri);
          loafcore.loadScript(path);
          loaded = true;
          break;
        }
      }
      if (!loaded && this.errorText.length === 0) {
        loafcore.dragEvent(droppedUri.length > 0 ? [droppedUri] : []);
      }
    } catch (err) {
      this.errorText = `拖入的文件无法载入: ${JSON.stringify(err)}`;
    }
  }

  @Builder
  Toolbar() {
    Row({ space: 10 }) {
      Column({ space: 2 }) {
        Text('loaf')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .fontColor('#f2f5fb')
        Text(`${this.scriptName} · ${this.status}`)
          .fontSize(12)
          .fontColor('#9aa6bd')
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      if (this.surfaceInfo.length > 0) {
        Text(this.surfaceInfo)
          .fontSize(11)
          .fontColor('#7f8ba3')
      }

      Button('打开')
        .fontSize(13)
        .height(32)
        .backgroundColor('#243049')
        .fontColor('#e6ecf8')
        .onClick(() => { this.openScript(); })
      Button('文件夹')
        .fontSize(13)
        .height(32)
        .backgroundColor('#243049')
        .fontColor('#e6ecf8')
        .onClick(() => { this.openFolder(); })
      Button('重载')
        .fontSize(13)
        .height(32)
        .backgroundColor('#2b3a58')
        .fontColor('#e6ecf8')
        .onClick(() => { loafcore.reload(); })
      Button('示例')
        .fontSize(13)
        .height(32)
        .backgroundColor('#2f6f5f')
        .fontColor('#eafff8')
        .onClick(() => { this.showExamples = !this.showExamples; })
      Button('自测')
        .fontSize(13)
        .height(32)
        .backgroundColor('#3a4a6b')
        .fontColor('#e6ecf8')
        .onClick(() => { this.runSelfTest(); })
      Button('导出脚本')
        .fontSize(13)
        .height(32)
        .backgroundColor('#3a4a6b')
        .fontColor('#e6ecf8')
        .onClick(() => { this.exportLauncher(); })
      Button(this.fitMode ? '适配中' : '适配')
        .fontSize(13)
        .height(32)
        .backgroundColor(this.fitMode ? '#2f6f5f' : '#243049')
        .fontColor('#e6ecf8')
        .onClick(() => { this.toggleFit(); })
      Button('?')
        .fontSize(14)
        .height(32)
        .width(36)
        .backgroundColor('#243049')
        .fontColor('#e6ecf8')
        .onClick(() => { this.showHelp = !this.showHelp; })
      Button('×')
        .fontSize(14)
        .height(32)
        .width(36)
        .backgroundColor('#1b2130')
        .fontColor('#9aa6bd')
        .onClick(() => { this.showPanel = false; })
    }
    .width('100%')
    .padding({ left: 20, right: 20, top: 14, bottom: 14 })
    .backgroundColor('#ee121722')
  }

  @Builder
  HelpOverlay() {
    Column({ space: 14 }) {
      Text('loaf on HarmonyOS PC')
        .fontSize(22)
        .fontWeight(FontWeight.Medium)
        .fontColor('#f2f5fb')
      Text('把 .lua 文件或包含 main.lua 的文件夹拖进窗口，或用“打开/文件夹”按钮导入。')
        .fontSize(14)
        .fontColor('#c3ccdd')
      Text('快捷键')
        .fontSize(13)
        .fontColor('#8fa0bd')
      Text('Esc  停止当前脚本\nCtrl+R  重新载入\nCtrl+O  打开脚本')
        .fontSize(13)
        .lineHeight(22)
        .fontColor('#dfe6f3')
      Text('脚本保存后会自动热重载；OSC 默认监听 9990，发送 8880。')
        .fontSize(13)
        .fontColor('#c3ccdd')
      Button('知道了')
        .fontSize(13)
        .height(32)
        .backgroundColor('#2b3a58')
        .fontColor('#e6ecf8')
        .onClick(() => { this.showHelp = false; })
    }
    .alignItems(HorizontalAlign.Start)
    .width(460)
    .padding(26)
    .backgroundColor('#f2141a26')
    .borderRadius(14)
    .position({ x: '50%', y: '50%' })
    .markAnchor({ x: 230, y: 190 })
    .onClick(() => { this.showHelp = false; })
  }

  @Builder
  ExampleGallery() {
    Column({ space: 10 }) {
      Row() {
        Text('内置示例')
          .fontSize(18)
          .fontWeight(FontWeight.Medium)
          .fontColor('#f2f5fb')
          .layoutWeight(1)
        Button('关闭')
          .fontSize(12)
          .height(28)
          .backgroundColor('#243049')
          .fontColor('#c3ccdd')
          .onClick(() => { this.showExamples = false; })
      }
      .width('100%')

      this.ExampleRow('动画 demo', 'loaf 内置实时演示', 'demo.lua')
      this.ExampleRow('启动器 launcher', '六张示例卡片与拖拽入口', 'launcher.lua')
      this.ExampleRow('双模块 gallery', '旋转三角体 / 跳舞女孩', 'loaf-gallery.lua')
      this.ExampleRow('图形 graphics', '形状、颜色、变换、曲线', 'of/graphicsExample.lua')
      this.ExampleRow('多边形 polygon', 'beginShape / vertex / 两种绕数', 'of/polygonExample.lua')
      this.ExampleRow('龙形曲线 dragon', 'FBO、点运算与迭代绘制', 'dragoncurve.lua')
      this.ExampleRow('字体 fonts', 'TrueTypeFont 与文本绘制', 'of/fontsExample.lua')
      this.ExampleRow('图片 imageLoader', '加载 png/jpg/gif 并绘制', 'of/imageLoaderExample.lua')
      this.ExampleRow('OSC 测试', '消息类型、bundle、收发', 'tests/osc.lua')
      this.ExampleRow('文件工具', 'of.File / Directory / Buffer', 'tests/fileutils.lua')
      this.ExampleRow('回调 callbacks', '鼠标键盘窗口事件', 'tests/callbacks.lua')
      this.ExampleRow('窗口 window', '窗口与帧率控制', 'tests/window.lua')
      this.ExampleRow('自测 selftest', '全部接口回归', 'selftest.lua')
    }
    .alignItems(HorizontalAlign.Start)
    .width(560)
    .padding(24)
    .backgroundColor('#f2141a26')
    .borderRadius(14)
    .position({ x: '50%', y: '50%' })
    .markAnchor({ x: 280, y: 260 })
  }

  @Builder
  ExampleRow(title: string, detail: string, path: string) {
    Row({ space: 12 }) {
      Column({ space: 2 }) {
        Text(title)
          .fontSize(14)
          .fontColor('#e8eefb')
        Text(detail)
          .fontSize(11)
          .fontColor('#8fa0bd')
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      Button('运行')
        .fontSize(12)
        .height(28)
        .backgroundColor('#2b3a58')
        .fontColor('#e6ecf8')
        .onClick(() => { this.openExample(path); })
    }
    .width('100%')
    .padding({ top: 6, bottom: 6 })
  }

}
