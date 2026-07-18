#include "pch.h"
#include "ImagePickerModule.h"

// WinRT projections used by the picker implementation. These are pulled in here
// (not in pch.h) to keep the module self-contained and easy to lift into a library.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Security.Cryptography.h>

// IInitializeWithWindow (classic COM) - the piece that lets a WinAppSDK/Win32
// desktop app parent a FileOpenPicker to its window.
#include <shobjidl_core.h>

#include <atomic>
#include <cctype>
#include <coroutine>
#include <string>

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Security::Cryptography;

namespace
{
    // ------------------------------------------------------------------------
    // HWND acquisition.
    //
    // Primary source: the app's top-level HWND, registered once from WinMain via
    // RNWImagePicker::SetHostWindow (see README: Integration). This is the only 100%
    // reliable handle because it is captured directly from the AppWindow the RNW
    // host created. Stored in an atomic since it is written on the UI thread at
    // startup and read from the picker coroutine.
    // ------------------------------------------------------------------------
    std::atomic<HWND> g_hostWindow{nullptr};

    // Resolve the window to parent the picker to. Prefer the registered host
    // window; fall back to the active/foreground window (valid because the pick
    // runs on the UI thread that owns the app window - see ResumeOnDispatcher).
    HWND ResolveHostWindow() noexcept
    {
        if (HWND hwnd = g_hostWindow.load())
        {
            return hwnd;
        }
        if (HWND hwnd = ::GetActiveWindow())
        {
            return hwnd;
        }
        return ::GetForegroundWindow();
    }

    // Awaiter that resumes the coroutine on an IReactDispatcher's thread. Used to
    // hop onto the RNW UI thread before creating/showing the modal picker.
    struct ResumeOnDispatcher
    {
        winrt::Microsoft::ReactNative::IReactDispatcher dispatcher;

        bool await_ready() const noexcept
        {
            return dispatcher && dispatcher.HasThreadAccess();
        }
        void await_suspend(std::coroutine_handle<> resume) const
        {
            if (!dispatcher)
            {
                // Instance tearing down / dispatcher not attached: resuming inline on
                // the current thread beats a guaranteed null-vtable call.
                resume();
                return;
            }
            dispatcher.Post([resume]() noexcept { resume(); });
        }
        void await_resume() const noexcept {}
    };

    // Build a percent-encoded file:/// URI from a Win32 path. User profile paths
    // (which LocalFolder lives under) can contain spaces and non-ASCII, so encode
    // everything outside the RFC 3986 unreserved set (plus '/' and ':' kept for
    // the drive + separators). Works on the UTF-8 bytes so non-ASCII is safe.
    std::string MakeFileUri(winrt::hstring const &path)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        const std::string utf8 = winrt::to_string(path);
        std::string uri = "file:///";
        uri.reserve(utf8.size() + 8);
        for (char raw : utf8)
        {
            const unsigned char ch = static_cast<unsigned char>(raw);
            if (ch == '\\')
            {
                uri.push_back('/');
            }
            else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') || ch == '/' || ch == ':' ||
                     ch == '.' || ch == '-' || ch == '_' || ch == '~')
            {
                uri.push_back(static_cast<char>(ch));
            }
            else
            {
                uri.push_back('%');
                uri.push_back(kHex[ch >> 4]);
                uri.push_back(kHex[ch & 0x0F]);
            }
        }
        return uri;
    }

    // Best-effort MIME type from a file extension, used only if StorageFile does
    // not report a ContentType.
    std::string MimeFromName(winrt::hstring const &name)
    {
        std::string n = winrt::to_string(name);
        auto dot = n.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : n.substr(dot + 1);
        for (auto &c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "png") return "image/png";
        if (ext == "gif") return "image/gif";
        if (ext == "bmp") return "image/bmp";
        if (ext == "webp") return "image/webp";
        if (ext == "heic" || ext == "heif") return "image/heic";
        if (ext == "tif" || ext == "tiff") return "image/tiff";
        if (ext == "svg") return "image/svg+xml";
        // Documents (used by launchDocumentPicker / the WO "Add Files" flow).
        if (ext == "pdf") return "application/pdf";
        if (ext == "txt" || ext == "log") return "text/plain";
        if (ext == "csv") return "text/csv";
        if (ext == "rtf") return "application/rtf";
        if (ext == "doc") return "application/msword";
        if (ext == "docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (ext == "xls") return "application/vnd.ms-excel";
        if (ext == "xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (ext == "ppt") return "application/vnd.ms-powerpoint";
        if (ext == "pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        if (ext == "zip") return "application/zip";
        return "application/octet-stream";
    }

    // ------------------------------------------------------------------------
    // Stale-file cleanup for the picker's LocalFolder copy folders.
    //
    // Every image pick copies the chosen file into PickedImages (step 5 of
    // LaunchPickerAsync) and every document pick copies into PickedFiles, so the
    // app keeps a stable, broker-independent path — but nothing ever deleted
    // those copies, so the folders grew forever. The copies are only needed
    // transiently (upload / preview right after the pick), so on module init we
    // delete entries older than kPickedFileMaxAgeDays. Best effort on a
    // background thread; a file still in use simply survives until the next pass.
    // ------------------------------------------------------------------------
    constexpr uint64_t kPickedFileMaxAgeDays = 7;
    constexpr uint64_t kPickedFileMaxAge100ns =
        kPickedFileMaxAgeDays * 24ULL * 60ULL * 60ULL * 10'000'000ULL;

    // Sweep one LocalFolder subfolder of stale copies. `now` is the current time
    // as a 100ns FILETIME so a single GetSystemTimeAsFileTime covers every folder.
    void SweepStaleCopyFolder(std::wstring const &localFolderPath,
                              wchar_t const *subFolder, ULARGE_INTEGER now) noexcept
    {
        const std::wstring dir = localFolderPath + L"\\" + subFolder;
        WIN32_FIND_DATAW ffd{};
        const std::wstring pattern = dir + L"\\*";
        HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            return; // folder absent (nothing picked yet) - nothing to clean
        }
        do
        {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                continue;
            }
            ULARGE_INTEGER written{};
            written.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
            written.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
            if (written.QuadPart != 0 && now.QuadPart > written.QuadPart &&
                (now.QuadPart - written.QuadPart) > kPickedFileMaxAge100ns)
            {
                const std::wstring stale = dir + L"\\" + ffd.cFileName;
                ::DeleteFileW(stale.c_str()); // best effort; in-use files just stay
            }
        } while (::FindNextFileW(hFind, &ffd));
        ::FindClose(hFind);
    }

    winrt::fire_and_forget CleanupStalePickedImagesAsync() noexcept
    {
        try
        {
            co_await winrt::resume_background();

            const std::wstring localFolder{
                ApplicationData::Current().LocalFolder().Path().c_str()};

            FILETIME nowFt{};
            ::GetSystemTimeAsFileTime(&nowFt);
            ULARGE_INTEGER now{};
            now.LowPart = nowFt.dwLowDateTime;
            now.HighPart = nowFt.dwHighDateTime;

            SweepStaleCopyFolder(localFolder, L"PickedImages", now);
            SweepStaleCopyFolder(localFolder, L"PickedFiles", now);
        }
        catch (...)
        {
            // Cleanup must never take anything down.
        }
    }

    JSValueObject MakeGrantedPermission()
    {
        JSValueObject perm;
        perm["granted"] = true;
        perm["status"] = "granted";
        perm["canAskAgain"] = true;
        perm["expires"] = "never";
        perm["accessPrivileges"] = "all";
        return perm;
    }

    // The picker coroutine. Kept as a free fire_and_forget (rather than a member)
    // so it owns everything it touches by value and does not depend on module
    // lifetime across suspension points. `context` and `promise` are cheap,
    // copyable handles that are safe to carry across co_await.
    winrt::fire_and_forget LaunchPickerAsync(
        winrt::Microsoft::ReactNative::ReactContext context,
        bool wantBase64,
        winrt::Microsoft::ReactNative::ReactPromise<JSValue> promise) noexcept
    {
        try
        {
            // 1) Hop onto the UI thread. The picker is modal to the app window and
            //    must be created/shown from the thread that owns that window.
            co_await ResumeOnDispatcher{context.Handle().UIDispatcher()};

            HWND hwnd = ResolveHostWindow();

            // 2) Configure the picker.
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::Thumbnail);
            picker.SuggestedStartLocation(PickerLocationId::PicturesLibrary);
            auto filters = picker.FileTypeFilter();
            filters.Append(L".jpg");
            filters.Append(L".jpeg");
            filters.Append(L".png");
            filters.Append(L".gif");
            filters.Append(L".bmp");
            filters.Append(L".webp");
            filters.Append(L".heic");

            // 3) CRITICAL: associate the picker with the app HWND. Without this a
            //    FileOpenPicker throws on a WinAppSDK/Win32 desktop app because it
            //    has no CoreWindow to parent to. This single QI + Initialize is the
            //    difference between "picker opens" and "picker throws E_FAIL".
            {
                auto initWithWindow = picker.as<::IInitializeWithWindow>();
                winrt::check_hresult(initWithWindow->Initialize(hwnd));
            }

            // 4) Show it. co_await may resume off the UI thread; everything below
            //    (file IO / decode) is thread-agnostic, so that is fine.
            StorageFile file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                JSValueObject canceled;
                canceled["canceled"] = true;
                canceled["assets"] = nullptr;
                promise.Resolve(JSValue{std::move(canceled)});
                co_return;
            }

            // 5) Copy into LocalFolder\PickedImages so the app has a stable path it
            //    can read later, independent of the picker's transient broker grant.
            //    (LocalFolder is the same store SyncKvModule uses.)
            auto localFolder = ApplicationData::Current().LocalFolder();
            auto destFolder = co_await localFolder.CreateFolderAsync(
                L"PickedImages", CreationCollisionOption::OpenIfExists);
            StorageFile localCopy = co_await file.CopyAsync(
                destFolder, file.Name(), NameCollisionOption::GenerateUniqueName);

            // 6) Metadata.
            auto basicProps = co_await localCopy.GetBasicPropertiesAsync();
            const uint64_t fileSize = basicProps.Size();
            const winrt::hstring fileName = localCopy.Name();
            const winrt::hstring contentType = localCopy.ContentType();

            // 7) Dimensions (best-effort; 0 on failure per spec).
            uint32_t width = 0, height = 0;
            try
            {
                auto stream = co_await localCopy.OpenAsync(FileAccessMode::Read);
                auto decoder = co_await BitmapDecoder::CreateAsync(stream);
                width = decoder.PixelWidth();
                height = decoder.PixelHeight();
            }
            catch (...)
            {
                // leave 0/0
            }

            // 8) Optional base64. The FIT profile upload and photo-size validation
            //    read asset.base64, and every caller passes base64: true.
            std::string base64;
            bool haveBase64 = false;
            if (wantBase64)
            {
                try
                {
                    auto buffer = co_await FileIO::ReadBufferAsync(localCopy);
                    base64 = winrt::to_string(CryptographicBuffer::EncodeToBase64String(buffer));
                    haveBase64 = true;
                }
                catch (...)
                {
                    // omit base64 on failure
                }
            }

            // 9) Build the expo-image-picker asset shape.
            JSValueObject asset;
            asset["uri"] = MakeFileUri(localCopy.Path());
            asset["fileName"] = winrt::to_string(fileName);
            asset["mimeType"] = contentType.empty() ? MimeFromName(fileName)
                                                    : winrt::to_string(contentType);
            asset["fileSize"] = static_cast<double>(fileSize);
            asset["width"] = static_cast<double>(width);
            asset["height"] = static_cast<double>(height);
            asset["type"] = "image";
            // exif is intentionally null: helpers.convertToJpeg() keys off
            // exif.Orientation and, when it is set, routes through the no-op
            // expo-image-manipulator RNW shim - which would wipe base64/dimensions.
            asset["exif"] = nullptr;
            asset["duration"] = nullptr;
            asset["assetId"] = nullptr;
            asset["base64"] = haveBase64 ? JSValue{std::move(base64)} : JSValue{nullptr};

            JSValueArray assets;
            assets.push_back(std::move(asset));

            JSValueObject result;
            result["canceled"] = false;
            result["assets"] = std::move(assets);
            promise.Resolve(JSValue{std::move(result)});
        }
        catch (winrt::hresult_error const &e)
        {
            promise.Reject(winrt::to_string(e.message()).c_str());
        }
        catch (...)
        {
            promise.Reject("RNWImagePicker: unexpected error while picking image");
        }
    }

    // The document-picker coroutine — the multi-file, any-type sibling of
    // LaunchPickerAsync. Same free fire_and_forget model (owns everything by
    // value so it survives across co_await independent of module lifetime), same
    // HWND-parenting requirement, but uses PickMultipleFilesAsync and an all-files
    // ("*") filter so the WO "Add Files" flow can attach arbitrary documents.
    winrt::fire_and_forget LaunchDocumentPickerAsync(
        winrt::Microsoft::ReactNative::ReactContext context,
        bool wantBase64,
        winrt::Microsoft::ReactNative::ReactPromise<JSValue> promise) noexcept
    {
        try
        {
            // 1) Hop onto the UI thread — the picker is modal to the app window
            //    and must be created/shown from the thread that owns it.
            co_await ResumeOnDispatcher{context.Handle().UIDispatcher()};

            HWND hwnd = ResolveHostWindow();

            // 2) Configure the picker for arbitrary documents. A FileOpenPicker
            //    requires a non-empty FileTypeFilter; "*" makes it show ALL file
            //    types (PDF, Office docs, images, text, ...). "*" must be the only
            //    entry — it cannot be mixed with specific extensions.
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");

            // 3) CRITICAL: parent the picker to the app HWND (same as the image
            //    picker) or it throws E_FAIL on a WinAppSDK/Win32 desktop app.
            {
                auto initWithWindow = picker.as<::IInitializeWithWindow>();
                winrt::check_hresult(initWithWindow->Initialize(hwnd));
            }

            // 4) Show it and collect the (possibly multiple) picks.
            auto files = co_await picker.PickMultipleFilesAsync();
            if (!files || files.Size() == 0)
            {
                JSValueObject canceled;
                canceled["canceled"] = true;
                canceled["assets"] = nullptr;
                promise.Resolve(JSValue{std::move(canceled)});
                co_return;
            }

            // 5) Ensure the stable LocalFolder\PickedFiles copy folder exists.
            auto localFolder = ApplicationData::Current().LocalFolder();
            auto destFolder = co_await localFolder.CreateFolderAsync(
                L"PickedFiles", CreationCollisionOption::OpenIfExists);

            JSValueArray assets;
            for (StorageFile const &file : files)
            {
                if (!file)
                {
                    continue;
                }

                // Copy into PickedFiles so the app has a stable path it can read
                // later, independent of the picker's transient broker grant.
                StorageFile localCopy = co_await file.CopyAsync(
                    destFolder, file.Name(), NameCollisionOption::GenerateUniqueName);

                auto basicProps = co_await localCopy.GetBasicPropertiesAsync();
                const uint64_t fileSize = basicProps.Size();
                const winrt::hstring fileName = localCopy.Name();
                const winrt::hstring contentType = localCopy.ContentType();

                // Optional base64 (default off — documents can be large and the WO
                // multipart upload streams straight from the file uri).
                std::string base64;
                bool haveBase64 = false;
                if (wantBase64)
                {
                    try
                    {
                        auto buffer = co_await FileIO::ReadBufferAsync(localCopy);
                        base64 = winrt::to_string(
                            CryptographicBuffer::EncodeToBase64String(buffer));
                        haveBase64 = true;
                    }
                    catch (...)
                    {
                        // omit base64 on failure
                    }
                }

                JSValueObject asset;
                asset["uri"] = MakeFileUri(localCopy.Path());
                asset["fileName"] = winrt::to_string(fileName);
                asset["name"] = winrt::to_string(fileName);
                asset["mimeType"] = contentType.empty()
                                        ? MimeFromName(fileName)
                                        : winrt::to_string(contentType);
                asset["fileSize"] = static_cast<double>(fileSize);
                asset["type"] = "file";
                asset["base64"] = haveBase64 ? JSValue{std::move(base64)} : JSValue{nullptr};
                assets.push_back(std::move(asset));
            }

            if (assets.empty())
            {
                // Every copy failed — treat as canceled rather than a hard error.
                JSValueObject canceled;
                canceled["canceled"] = true;
                canceled["assets"] = nullptr;
                promise.Resolve(JSValue{std::move(canceled)});
                co_return;
            }

            JSValueObject result;
            result["canceled"] = false;
            result["assets"] = std::move(assets);
            promise.Resolve(JSValue{std::move(result)});
        }
        catch (winrt::hresult_error const &e)
        {
            promise.Reject(winrt::to_string(e.message()).c_str());
        }
        catch (...)
        {
            promise.Reject("RNWImagePicker: unexpected error while picking documents");
        }
    }
}

namespace winrt::RNWImagePicker::implementation
{
    void ImagePickerModule::Initialize(ReactContext const &reactContext) noexcept
    {
        m_context = reactContext;
        // Sweep stale PickedImages copies (once per process, background thread).
        RNWImagePicker::CleanupStalePickedImages();
    }

    void ImagePickerModule::launchImageLibrary(JSValueObject options, ReactPromise<JSValue> promise) noexcept
    {
        bool wantBase64 = false;
        if (auto it = options.find("base64"); it != options.end())
        {
            if (auto const *b = it->second.TryGetBoolean())
            {
                wantBase64 = *b;
            }
        }

        // Kick off the coroutine. launchImageLibrary itself returns void (required
        // for a REACT_METHOD promise method); the actual async work lives in the
        // fire_and_forget which resolves/rejects `promise`.
        LaunchPickerAsync(m_context, wantBase64, promise);
    }

    void ImagePickerModule::launchDocumentPicker(JSValueObject options, ReactPromise<JSValue> promise) noexcept
    {
        // Documents default to NO base64 (they can be large; the WO upload streams
        // from the file uri). Callers that genuinely need bytes pass base64: true.
        bool wantBase64 = false;
        if (auto it = options.find("base64"); it != options.end())
        {
            if (auto const *b = it->second.TryGetBoolean())
            {
                wantBase64 = *b;
            }
        }

        LaunchDocumentPickerAsync(m_context, wantBase64, promise);
    }

    void ImagePickerModule::requestMediaLibraryPermissions(bool /*writeOnly*/, ReactPromise<JSValue> promise) noexcept
    {
        promise.Resolve(JSValue{MakeGrantedPermission()});
    }

    void ImagePickerModule::getMediaLibraryPermissions(bool /*writeOnly*/, ReactPromise<JSValue> promise) noexcept
    {
        promise.Resolve(JSValue{MakeGrantedPermission()});
    }
}

namespace RNWImagePicker
{
    void SetHostWindow(void *hwnd) noexcept
    {
        g_hostWindow.store(static_cast<HWND>(hwnd));
    }

    void CleanupStalePickedImages() noexcept
    {
        // Once per process: React instances (and thus REACT_INIT) recur on every
        // Metro reload, but one sweep per app run is plenty.
        static std::atomic<bool> started{false};
        if (started.exchange(true))
        {
            return;
        }
        CleanupStalePickedImagesAsync();
    }
}
