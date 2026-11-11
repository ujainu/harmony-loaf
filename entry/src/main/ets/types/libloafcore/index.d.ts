export interface LoafEvent {
  type: 'ready' | 'script' | 'error' | 'info' | 'camera';
  message?: string;
  path?: string;
  reload?: boolean;
  action?: string;
  surfaceId?: string;
  width?: number;
  height?: number;
}

export interface LoafInfo {
  running: boolean;
  script: string;
  frameRate: number;
  width: number;
  height: number;
  listening: boolean;
  listenPort: number;
  error: string;
}

export const loadScript: (path: string) => boolean;
export const loadScriptText: (name: string, text: string, dir: string) => boolean;
export const reload: () => boolean;
export const stop: () => void;
export const evaluate: (text: string) => boolean;
export const setListenPort: (port: number) => boolean;
export const startListening: () => boolean;
export const stopListening: () => void;
export const setSendHost: (host: string) => void;
export const setSendPort: (port: number) => boolean;
export const sendOsc: (address: string, type: string, value: string) => void;
export const setVerbose: (verbose: boolean) => void;
export const getInfo: () => string;
export const onEvent: (callback: (eventJson: string) => void) => void;
export const setDensity: (density: number) => void;
export const pointerEvent: (x: number, y: number, button: number, action: number) => void;
export const keyEvent: (code: number, down: boolean) => void;
export const dragEvent: (paths: string[]) => void;
