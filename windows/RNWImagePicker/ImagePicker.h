#pragma once

#include "ImagePicker.g.h"

namespace winrt::RNWImagePicker::implementation
{

// Statics-only runtimeclass: per the C++/WinRT convention for a MIDL
// `static runtimeclass`, the implementation type is a plain struct carrying the
// static methods, and only the factory derives from the generated _T template.
struct ImagePicker
{
    ImagePicker() = default;

    static void SetHostWindow(uint64_t hwnd) noexcept;
    static void CleanupStalePickedImages() noexcept;
};

} // namespace winrt::RNWImagePicker::implementation

namespace winrt::RNWImagePicker::factory_implementation
{

struct ImagePicker : ImagePickerT<ImagePicker, implementation::ImagePicker> {};

} // namespace winrt::RNWImagePicker::factory_implementation
