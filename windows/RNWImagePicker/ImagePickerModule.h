#pragma once
#include "NativeModules.h"

using namespace winrt::Microsoft::ReactNative;

// ============================================================================
// ImagePickerModule - a REAL react-native-windows image picker TurboModule.
//
// WHY: expo-image-picker has no native module for react-native-windows, so the
// FIT app ships windows-shims/expo-image-picker.windows.ts. Until now that shim
// returned { canceled: true } unconditionally, so the profile-photo picker (and
// every "choose from library" flow) silently did nothing on Windows. This module
// backs the shim with a native Windows.Storage.Pickers.FileOpenPicker so the app
// can actually pick an image from disk.
//
// ARCHITECTURE: attributed C++/WinRT module (same REACT_MODULE style as
// works-windows' SyncKvModule), auto-registered by AddAttributedModules(builder,
// true) inside your app's IReactPackageProvider. Exposed to JS as
// NativeModules.RNWImagePicker.
//
// The single hardest part on a WinAppSDK / Win32 desktop app is that a
// FileOpenPicker has no CoreWindow to parent itself to and therefore throws
// unless it is associated with the app's top-level HWND via
// IInitializeWithWindow::Initialize(hwnd). See ImagePickerModule.cpp for how the
// HWND is acquired (registered from WinMain, with GetActiveWindow fallback) and
// why the whole pick runs on the UI-thread dispatcher.
//
// App coupling is deliberately limited to a single SetHostWindow() hook so this
// module can drop into any RNW app unchanged.
// ============================================================================

namespace winrt::RNWImagePicker::implementation
{
    REACT_MODULE(ImagePickerModule, L"RNWImagePicker");
    struct ImagePickerModule
    {
        REACT_INIT(Initialize);
        void Initialize(ReactContext const &reactContext) noexcept;

        // Async: opens the OS file picker and resolves an expo-image-picker
        // -shaped result:
        //   canceled -> { canceled: true,  assets: null }
        //   success  -> { canceled: false, assets: [ { uri, fileName, mimeType,
        //                 fileSize, width, height, type: "image", base64, exif } ] }
        // Rejects (with a message) only on an unexpected failure. `options` mirrors
        // ImagePickerOptions; only `base64` is honored today (quality/selectionLimit
        // are single-select no-ops for now).
        REACT_METHOD(launchImageLibrary);
        void launchImageLibrary(JSValueObject options, ReactPromise<JSValue> promise) noexcept;

        // Async: opens the OS file picker for ARBITRARY documents (PDF, Office
        // docs, images, text, ...) and resolves a multi-asset result:
        //   canceled -> { canceled: true,  assets: null }
        //   success  -> { canceled: false, assets: [ { uri, fileName, mimeType,
        //                 fileSize, type: "file", base64? }, ... ] }
        // Backs the WO "Add Files" flow on Windows, which must accept any file
        // type — not just images (which launchImageLibrary is restricted to).
        // Uses PickMultipleFilesAsync with an all-files ("*") FileTypeFilter and
        // copies each pick into LocalFolder\PickedFiles for a stable upload path.
        // `options.base64` is honored (default false: documents can be large and
        // the WO multipart upload streams from the file uri, not base64).
        REACT_METHOD(launchDocumentPicker);
        void launchDocumentPicker(JSValueObject options, ReactPromise<JSValue> promise) noexcept;

        // Desktop file picking needs no runtime capability, so permissions always
        // resolve granted. Exposed for API completeness / direct native consumers;
        // the JS shim also carries local fallbacks.
        REACT_METHOD(requestMediaLibraryPermissions);
        void requestMediaLibraryPermissions(bool writeOnly, ReactPromise<JSValue> promise) noexcept;

        REACT_METHOD(getMediaLibraryPermissions);
        void getMediaLibraryPermissions(bool writeOnly, ReactPromise<JSValue> promise) noexcept;

    private:
        ReactContext m_context{nullptr};
    };
}

// ----------------------------------------------------------------------------
// Host-window registration hook.
//
// Call once from WinMain with the app's top-level HWND (see README: Integration). This
// is the reliable source for the window the picker must be parented to. The
// parameter is void* (not HWND) so this header does not force a <windows.h>
// include order on consumers; the app passes an HWND, which converts implicitly.
// ----------------------------------------------------------------------------
namespace RNWImagePicker
{
    void SetHostWindow(void *hwnd) noexcept;

    // Best-effort, once-per-process cleanup of stale LocalFolder\PickedImages
    // copies (the picker copies every picked file there and nothing ever deleted
    // them, so the folder grew forever). Kicked off from the module's REACT_INIT;
    // the sweep runs on a background thread and never throws.
    void CleanupStalePickedImages() noexcept;
}
