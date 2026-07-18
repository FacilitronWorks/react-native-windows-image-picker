# react-native-windows-image-picker

A native **image and document picker** TurboModule for **React Native Windows** on the **new architecture** (Fabric / Composition), backed by `Windows.Storage.Pickers.FileOpenPicker`.

> **Status: pre-release (`0.1.0-pre.0`), not yet published to npm.**
> The C++ implementation is extracted from a shipping production Windows app (RNW 0.83.2, Windows-on-ARM and x64) where it backs real image and file-attachment upload flows. The packaged form — standalone `.vcxproj`, `ReactPackageProvider`, and autolinking config — now exists, but **has not yet been compiled standalone or consumed end-to-end from an app**. An example app and CI build are still to do. Treat `0.1.0-pre.0` as "structurally complete, build-unverified"; wait for `0.1.0` before depending on it in production.

## Why this exists

There is no maintained file/image picker for React Native Windows. This isn't an oversight — every upstream door has been tried and closed:

- **`@react-native-documents/picker`** dropped Windows, and the maintainer [declined to take it back](https://github.com/react-native-documents/document-picker/issues/759): *"No one has stepped up to maintain it in years. I don't think it's a good idea to add it into this repo (because it will, again, be unmaintained)."*
- **React Native Windows core** [has no plan to ship one](https://github.com/microsoft/react-native-windows/issues/14488): *"AFAIK there's no plan to expose a built-in way to access windows file pickers for RNW... you'd probably need to expose the APIs yourself, like you said, with your own Turbo Module."*
- **`expo-image-picker`** has no Windows implementation, and Expo's [additional platform support](https://docs.expo.dev/modules/additional-platform-support/) covers only macOS and tvOS.
- **`react-native-image-picker`** has had open Windows requests since [2021](https://github.com/react-native-image-picker/react-native-image-picker/issues/1835) and [2023](https://github.com/react-native-image-picker/react-native-image-picker/issues/2136), both unanswered.

RNW's own [native module docs](https://github.com/microsoft/react-native-windows-samples/blob/main/docs/native-modules-setup.md) point at exactly this outcome: publish your own library when *"an existing library doesn't already exist (or you don't have permission to modify it)."*

### Related prior art

If you only need **document/folder** picking, look at [`@hbannro/rn-documents-picker-windows`](https://github.com/ClaudiuHBann/document-picker-windows) first — it's MIT, new-arch, mirrors the `@react-native-documents/picker` API, and solves the HWND problem correctly. It targets RNW 0.77 and has no image-specific surface (no dimensions, base64, or image-restricted filtering), which is why this package exists separately rather than as a PR to it.

## The hard part: `IInitializeWithWindow`

This is the trap that catches everyone, and the main reason this code is worth sharing.

On a **WinAppSDK / Win32 desktop** app there is no `CoreWindow`. A `FileOpenPicker` tries to parent itself to one, doesn't find it, and **throws** — usually surfacing as an opaque `E_FAIL` or a hang, with nothing pointing at the real cause. The picker must be explicitly associated with the app's top-level `HWND` through the classic COM interface `IInitializeWithWindow`:

```cpp
#include <shobjidl_core.h>

auto initWithWindow = picker.as<::IInitializeWithWindow>();
initWithWindow->Initialize(ResolveHostWindow());
```

Two further constraints that are easy to get wrong:

1. **The pick must run on the UI thread.** The dialog is modal and owned by the app window. This module hops onto the RNW dispatcher with a small `ResumeOnDispatcher` coroutine awaiter before creating or showing the picker.
2. **The `HWND` must be a real top-level window.** This module prefers a handle registered once from `WinMain`, then falls back to `GetActiveWindow()` and `GetForegroundWindow()` — both valid fallbacks precisely *because* the pick already runs on the thread that owns the app window.

## Installation

```sh
npm install react-native-windows-image-picker
# or: yarn add react-native-windows-image-picker
```

Then autolink and rebuild:

```sh
npx react-native autolink-windows
npx react-native run-windows
```

That is all the build wiring there is. The package ships a real
`windows/RNWImagePicker/RNWImagePicker.vcxproj` plus a `react-native.config.js`
declaring it as a direct Windows dependency, so `autolink-windows` adds the
`ProjectReference` to your app project and emits the provider registration into
your generated `AutolinkedNativeModules.g.cpp`. There is nothing to copy, and
nothing to add to your own `.vcxproj`.

Requires the **new architecture** — `<RnwNewArch>true</RnwNewArch>` in your app's
`windows/ExperimentalFeatures.props`. Developed against RNW 0.83.x.

## Integration

Autolinking registers the **module** — the attributed `REACT_MODULE` is picked up
by this library's own `ReactPackageProvider`, which calls
`AddAttributedModules(packageBuilder, true)` on your behalf. It is then reachable
from JS as `NativeModules.RNWImagePicker`, or through the typed wrappers in
`src/index.ts` (see [Usage](#usage)).

**One manual step remains: the host window.** Autolinking cannot register your
`HWND`, so this hook stays yours. Call it once from `WinMain`, after the app
window is created:

```cpp
// <winrt/RNWImagePicker.h> is already pulled in for you by AutolinkedNativeModules.g.h
winrt::RNWImagePicker::ImagePicker::SetHostWindow(reinterpret_cast<uint64_t>(hwnd));
```

`SetHostWindow` is projected as a static on an `ImagePicker` runtimeclass rather
than exposed as a plain C++ function, because autolinking hands your app this
library's **WinMD projection** (`<winrt/RNWImagePicker.h>`) but not its raw
headers — and the library links as its own DLL, whose exports are just the WinRT
activation entry points. The internal free function
`RNWImagePicker::SetHostWindow(void*)` still exists for anyone vendoring the
sources directly.

Why it matters: this is the one *fully* reliable source for the window the picker
must parent to — it is captured directly from the `AppWindow` the RNW host
created, rather than inferred. If you skip it the `GetActiveWindow` /
`GetForegroundWindow` fallbacks usually work, but "usually" is not what you want
behind a modal dialog.

> A future version will prefer `ReactCoreInjection::GetTopLevelWindowId(context.Properties().Handle())` so the `WinMain` hook becomes optional. The current fallback chain is the version that has actually shipped, so it is what is published here.

## Usage

```ts
import {
  launchImageLibraryAsync,
  launchDocumentPickerAsync,
  isAvailable,
} from 'react-native-windows-image-picker';

// Pick a single image
const result = await launchImageLibraryAsync({ base64: false });
if (!result.canceled) {
  const [asset] = result.assets!;
  console.log(asset.uri, asset.width, asset.height, asset.mimeType);
}

// Pick one or more arbitrary documents
const docs = await launchDocumentPickerAsync();
if (!docs.canceled) {
  for (const doc of docs.assets!) {
    console.log(doc.fileName, doc.fileSize, doc.mimeType);
  }
}
```

### Result shape

```ts
{
  canceled: false,
  assets: [
    {
      uri: 'file:///.../LocalState/PickedFiles/<name>',
      fileName: 'photo.jpg',
      mimeType: 'image/jpeg',
      fileSize: 1048576,
      type: 'image',   // 'file' for documents
      width: 4032,     // images only
      height: 3024,
      base64: undefined,  // only when requested
      exif: null
    }
  ]
}
```

Cancelling resolves `{ canceled: true, assets: null }` — it does **not** reject. Rejection is reserved for genuine failures.

### Why files are copied

Every picked file is copied into `LocalFolder\PickedFiles` and the returned `uri` points at that copy. A `StorageFile` handed back by the picker carries broker-mediated access that does not reliably survive being passed to other subsystems as a plain path; copying gives you a stable location you can read, upload, and retry against. The module sweeps stale copies once per process on init, since nothing else ever deletes them.

## API

| Function | Description |
| --- | --- |
| `launchImageLibraryAsync(options?)` | Pick a single image (image file types only). |
| `launchDocumentPickerAsync(options?)` | Pick one or more files of any type. |
| `requestMediaLibraryPermissionsAsync(writeOnly?)` | Always granted on desktop; for API symmetry. |
| `getMediaLibraryPermissionsAsync(writeOnly?)` | As above. |
| `isAvailable()` | `true` when running on Windows with the native module registered. |

`options.base64` is the only option honoured today. `quality` and `selectionLimit` are accepted and ignored by `launchImageLibraryAsync` (single-select).

## Roadmap

- [x] Standalone `.vcxproj` + `ReactPackageProvider` + `react-native.config.js` so this installs and autolinks as a normal package
- [ ] Verify the project actually compiles, and autolinks cleanly into a real app
- [ ] `ReactCoreInjection::GetTopLevelWindowId` to make the `WinMain` hook optional
- [ ] Example app + CI build
- [ ] Camera capture (currently a separate module)
- [ ] Folder picking / save picker
- [ ] Publish to npm and register in [react-native-community/directory](https://github.com/react-native-community/directory)

## Contributing

Issues and PRs welcome — particularly build-wiring and RNW version-compat reports. This is maintained as a byproduct of a production app, so the honest maintenance promise is: it will keep working on the RNW versions that app ships on.

## License

MIT — see [LICENSE](LICENSE).
