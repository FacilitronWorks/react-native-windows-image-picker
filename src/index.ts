import { NativeModules, Platform } from 'react-native';

/**
 * The native module, exposed as `NativeModules.RNWImagePicker` once the
 * package provider is registered (see README: Integration).
 */
const Native = (NativeModules as Record<string, any>).RNWImagePicker;

export type PickedAssetType = 'image' | 'file';

export interface PickedAsset {
  /** `file:///` URI of a stable copy in `LocalFolder\PickedFiles`. */
  uri: string;
  fileName: string;
  mimeType: string;
  fileSize: number;
  type: PickedAssetType;
  /** Present only for images. */
  width?: number;
  height?: number;
  /** Present only when `base64: true` was requested. */
  base64?: string;
  exif?: Record<string, unknown> | null;
}

export interface PickerResult {
  canceled: boolean;
  assets: PickedAsset[] | null;
}

export interface PickerOptions {
  /**
   * Also return the file contents as a base64 string.
   *
   * Defaults to `false` for documents: they can be large, and uploads should
   * normally stream from the returned `uri` rather than carrying the bytes
   * through the bridge.
   */
  base64?: boolean;
}

const CANCELED: PickerResult = { canceled: true, assets: null };

function assertAvailable(): void {
  if (Platform.OS !== 'windows') {
    throw new Error(
      'react-native-windows-image-picker only runs on Windows. Guard your call ' +
        "with Platform.OS === 'windows' or use Platform.select.",
    );
  }
  if (!Native) {
    throw new Error(
      'The RNWImagePicker native module was not found. Register the package ' +
        'provider in your app (see README: Integration) and rebuild — a Metro ' +
        'reload alone will not pick up a new native module.',
    );
  }
}

/**
 * Open the OS picker restricted to image file types and return a single image.
 *
 * Resolves `{ canceled: true, assets: null }` when the user dismisses the
 * dialog; rejects only on an unexpected failure.
 */
export async function launchImageLibraryAsync(
  options: PickerOptions = {},
): Promise<PickerResult> {
  assertAvailable();
  const result = await Native.launchImageLibrary(options);
  return result ?? CANCELED;
}

/**
 * Open the OS picker for arbitrary documents (PDF, Office, images, text, ...)
 * and return one or more assets. Uses `PickMultipleFilesAsync` with an
 * all-files filter.
 */
export async function launchDocumentPickerAsync(
  options: PickerOptions = {},
): Promise<PickerResult> {
  assertAvailable();
  const result = await Native.launchDocumentPicker(options);
  return result ?? CANCELED;
}

/**
 * Always resolves granted: picking a file on desktop Windows requires no
 * runtime capability. Provided for API symmetry with mobile pickers so callers
 * can share a code path.
 */
export async function requestMediaLibraryPermissionsAsync(
  writeOnly = false,
): Promise<{ granted: boolean; status: string; canAskAgain: boolean }> {
  assertAvailable();
  return Native.requestMediaLibraryPermissions(writeOnly);
}

export async function getMediaLibraryPermissionsAsync(
  writeOnly = false,
): Promise<{ granted: boolean; status: string; canAskAgain: boolean }> {
  assertAvailable();
  return Native.getMediaLibraryPermissions(writeOnly);
}

/** True when the native module is present and usable on this platform. */
export function isAvailable(): boolean {
  return Platform.OS === 'windows' && Boolean(Native);
}
