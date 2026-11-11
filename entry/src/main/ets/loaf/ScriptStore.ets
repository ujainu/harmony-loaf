import { common } from '@kit.AbilityKit';
import { fileIo as fs, picker } from '@kit.CoreFileKit';
import { util } from '@kit.ArkTS';

/**
 * Keeps the working copy of loaf scripts inside the app sandbox so the native
 * watcher can reload them and relative data files resolve next to main.lua.
 */
export class ScriptStore {

  static scriptsDir(context: common.UIAbilityContext): string {
    const dir = context.filesDir + '/scripts';
    if (!fs.accessSync(dir)) {
      fs.mkdirSync(dir, true);
    }
    return dir;
  }

  static ensureDemo(context: common.UIAbilityContext): string {
    return ScriptStore.ensureRaw(context, 'demo.lua');
  }

  /// returns the bundled demo script if the user already has one (loaf's bundled mode)
  static demoPath(context: common.UIAbilityContext): string {
    const path = ScriptStore.scriptsDir(context) + '/demo.lua';
    return fs.accessSync(path) ? path : '';
  }

  static ensureRaw(context: common.UIAbilityContext, name: string): string {
    const target = ScriptStore.scriptsDir(context) + '/' + name;
    if (!fs.accessSync(target)) {
      const raw = context.resourceManager.getRawFileContentSync(name);
      const text = util.TextDecoder.create('utf-8').decodeToString(raw);
      ScriptStore.writeText(target, text);
    }
    return target;
  }

  /// copies the bundled example tree from rawfile into the sandbox once
  static ensureExamples(context: common.UIAbilityContext): string {
    const root = ScriptStore.scriptsDir(context) + '/examples';
    const marker = root + '/.complete6';
    if (fs.accessSync(marker)) {
      return root;
    }
    const directories: string[] = [
      'examples',
      'examples/helloworld',
      'examples/tests',
      'examples/tests/module',
      'examples/tests/module/hello',
      'examples/tests/directory',
      'examples/of',
      'examples/of/images',
      'examples/of/fonts',
      'examples/video',
      'examples/dance'
    ];
    for (const rawDir of directories) {
      const targetDir = ScriptStore.scriptsDir(context) + '/' + rawDir;
      try {
        if (!fs.accessSync(targetDir)) {
          fs.mkdirSync(targetDir, true);
        }
      } catch (err) {
        console.error(`loaf: mkdir failed ${targetDir} ${JSON.stringify(err)}`);
        continue;
      }
      let entries: string[] = [];
      try {
        entries = context.resourceManager.getRawFileListSync(rawDir);
      } catch (err) {
        console.error(`loaf: list raw ${rawDir} failed ${JSON.stringify(err)}`);
        continue;
      }
      for (const entry of entries) {
        const childDir = rawDir + '/' + entry;
        if (directories.indexOf(childDir) >= 0) {
          continue;
        }
        try {
          const content = context.resourceManager.getRawFileContentSync(childDir);
          const file = fs.openSync(targetDir + '/' + entry,
            fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
          try {
            fs.writeSync(file.fd, content.buffer as ArrayBuffer);
          } finally {
            fs.closeSync(file);
          }
        } catch (err) {
          console.error(`loaf: copy raw ${childDir} failed ${JSON.stringify(err)}`);
        }
      }
    }
    ScriptStore.writeText(marker, 'ok');
    return root;
  }

  static writeText(path: string, text: string): void {
    const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
    try {
      fs.writeSync(file.fd, text);
    } finally {
      fs.closeSync(file);
    }
  }

  static async pickScript(context: common.UIAbilityContext): Promise<string | null> {
    const documentPicker = new picker.DocumentViewPicker(context);
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = 1;
    options.fileSuffixFilters = ['.lua'];
    const uris = await documentPicker.select(options);
    if (!uris || uris.length === 0) {
      return null;
    }
    return ScriptStore.importFile(context, uris[0]);
  }

  static async pickFolder(context: common.UIAbilityContext): Promise<string | null> {
    const documentPicker = new picker.DocumentViewPicker(context);
    const options = new picker.DocumentSelectOptions();
    options.selectMode = picker.DocumentSelectMode.FOLDER;
    options.maxSelectNumber = 1;
    const uris = await documentPicker.select(options);
    if (!uris || uris.length === 0) {
      return null;
    }
    return ScriptStore.importFolder(context, uris[0]);
  }

  /// lets the user export a sandbox script to a location they pick (e.g. Desktop)
  static async exportScript(context: common.UIAbilityContext, sourcePath: string,
                            suggestedName: string): Promise<boolean> {
    const file = fs.openSync(sourcePath, fs.OpenMode.READ_ONLY);
    let content: ArrayBuffer;
    try {
      const size = fs.statSync(file.fd).size;
      content = new ArrayBuffer(size);
      fs.readSync(file.fd, content);
    } finally {
      fs.closeSync(file);
    }
    const documentPicker = new picker.DocumentViewPicker(context);
    const options = new picker.DocumentSaveOptions();
    options.newFileNames = [suggestedName];
    options.fileSuffixChoices = ['.lua'];
    const uris = await documentPicker.save(options);
    if (!uris || uris.length === 0) {
      return false;
    }
    const target = fs.openSync(uris[0], fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
    try {
      fs.writeSync(target.fd, content);
    } finally {
      fs.closeSync(target);
    }
    return true;
  }

  static importFile(context: common.UIAbilityContext, uri: string): string {
    const name = ScriptStore.baseName(uri);
    const target = ScriptStore.scriptsDir(context) + '/' + name;
    try {
      fs.copyFileSync(uri, target);
    } catch (err) {
      // drag & drop grants a temporary uri permission; some sources only allow fd reads
      const source = fs.openSync(uri, fs.OpenMode.READ_ONLY);
      try {
        const size = fs.statSync(source.fd).size;
        const buffer = new ArrayBuffer(size);
        fs.readSync(source.fd, buffer);
        const destination = fs.openSync(target, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
        try {
          fs.writeSync(destination.fd, buffer);
        } finally {
          fs.closeSync(destination);
        }
      } finally {
        fs.closeSync(source);
      }
    }
    return target;
  }

  static importFolder(context: common.UIAbilityContext, uri: string): string | null {
    const name = ScriptStore.baseName(uri);
    const target = ScriptStore.scriptsDir(context) + '/' + name;
    if (!fs.accessSync(target)) {
      fs.mkdirSync(target, true);
    }
    ScriptStore.copyDirectory(uri, target);
    const main = target + '/main.lua';
    return fs.accessSync(main) ? main : null;
  }

  private static copyDirectory(source: string, target: string): void {
    const names = fs.listFileSync(source);
    for (const name of names) {
      const sourcePath = source + '/' + name;
      const targetPath = target + '/' + name;
      const stat = fs.statSync(sourcePath);
      if (stat.isDirectory()) {
        if (!fs.accessSync(targetPath)) {
          fs.mkdirSync(targetPath, true);
        }
        ScriptStore.copyDirectory(sourcePath, targetPath);
      } else {
        fs.copyFileSync(sourcePath, targetPath);
      }
    }
  }

  private static baseName(uri: string): string {
    const trimmed = uri.endsWith('/') ? uri.substring(0, uri.length - 1) : uri;
    const index = trimmed.lastIndexOf('/');
    return decodeURIComponent(index >= 0 ? trimmed.substring(index + 1) : trimmed);
  }
}
