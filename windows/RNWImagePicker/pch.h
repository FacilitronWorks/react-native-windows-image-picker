#pragma once

#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1

#include <hstring.h>
#include <restrictederrorinfo.h>
#include <unknwn.h>
#include <windows.h>
#undef GetCurrentTime

#include <winrt/base.h>
#include <CppWinRTIncludes.h>
#if __has_include(<VersionMacros.h>)
#include <VersionMacros.h>
#endif

#include <winrt/Microsoft.ReactNative.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

// ImagePickerModule.cpp pulls in the Storage / Imaging / Cryptography
// projections itself, on purpose, so the module stays self-contained.

#include <string>
#include <string_view>

using namespace winrt::Windows::Foundation;
