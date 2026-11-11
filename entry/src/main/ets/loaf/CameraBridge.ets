import { camera } from '@kit.CameraKit';
import { common } from '@kit.AbilityKit';

/**
 * Bridges loaf's ofVideoGrabber to the HarmonyOS camera stack.
 * The native side creates a NativeImage and hands us its surface id;
 * we start a preview output that renders into that surface.
 */
export class CameraBridge {
  private static session: camera.Session | undefined = undefined;
  private static input: camera.CameraInput | undefined = undefined;
  private static output: camera.PreviewOutput | undefined = undefined;

  static async start(context: common.UIAbilityContext, surfaceId: string,
                     width: number, height: number): Promise<string> {
    await CameraBridge.stop();
    try {
      const manager = camera.getCameraManager(context);
      const devices = manager.getSupportedCameras();
      if (!devices || devices.length === 0) {
        return '没有检测到摄像头';
      }
      let device = devices[0];
      for (const candidate of devices) {
        if (candidate.cameraPosition === camera.CameraPosition.CAMERA_POSITION_BACK) {
          device = candidate;
          break;
        }
      }
      const capability = manager.getSupportedOutputCapability(device, camera.SceneMode.NORMAL_VIDEO);
      const profiles = capability.previewProfiles;
      if (!profiles || profiles.length === 0) {
        return '摄像头不支持预览输出';
      }
      let profile = profiles[0];
      for (const candidate of profiles) {
        if (candidate.size.width === width && candidate.size.height === height) {
          profile = candidate;
          break;
        }
      }
      const input = manager.createCameraInput(device);
      await input.open();
      const output = manager.createPreviewOutput(profile, surfaceId);
      const session = manager.createSession(camera.SceneMode.NORMAL_VIDEO);
      session.beginConfig();
      session.addInput(input);
      session.addOutput(output);
      await session.commitConfig();
      await session.start();
      CameraBridge.session = session;
      CameraBridge.input = input;
      CameraBridge.output = output;
      return '';
    } catch (err) {
      await CameraBridge.stop();
      return `摄像头启动失败: ${JSON.stringify(err)}`;
    }
  }

  static async stop(): Promise<void> {
    try {
      if (CameraBridge.session) {
        await CameraBridge.session.stop();
      }
    } catch (err) {
      console.error('loaf: camera session stop');
    }
    try {
      if (CameraBridge.session) {
        await CameraBridge.session.release();
      }
    } catch (err) {
      console.error('loaf: camera session release');
    }
    try {
      if (CameraBridge.input) {
        await CameraBridge.input.close();
      }
    } catch (err) {
      console.error('loaf: camera input close');
    }
    try {
      if (CameraBridge.output) {
        await CameraBridge.output.release();
      }
    } catch (err) {
      console.error('loaf: camera output release');
    }
    CameraBridge.session = undefined;
    CameraBridge.input = undefined;
    CameraBridge.output = undefined;
  }
}
