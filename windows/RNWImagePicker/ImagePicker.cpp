#include "pch.h"

#include "ImagePicker.h"
#if __has_include("ImagePicker.g.cpp")
#include "ImagePicker.g.cpp"
#endif

// Declares the plain C++ free functions in the GLOBAL ::RNWImagePicker
// namespace, which is distinct from the WinRT winrt::RNWImagePicker namespace
// opened below -- hence the leading :: on every call.
#include "ImagePickerModule.h"

namespace winrt::RNWImagePicker::implementation
{

void ImagePicker::SetHostWindow(uint64_t hwnd) noexcept
{
    ::RNWImagePicker::SetHostWindow(reinterpret_cast<void *>(static_cast<uintptr_t>(hwnd)));
}

void ImagePicker::CleanupStalePickedImages() noexcept
{
    ::RNWImagePicker::CleanupStalePickedImages();
}

} // namespace winrt::RNWImagePicker::implementation
