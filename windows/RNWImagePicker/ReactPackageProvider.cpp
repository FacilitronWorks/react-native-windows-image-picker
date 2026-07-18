#include "pch.h"

#include "ReactPackageProvider.h"
#if __has_include("ReactPackageProvider.g.cpp")
#include "ReactPackageProvider.g.cpp"
#endif

#include "ImagePickerModule.h"

using namespace winrt::Microsoft::ReactNative;

namespace winrt::RNWImagePicker::implementation
{

void ReactPackageProvider::CreatePackage(IReactPackageBuilder const &packageBuilder) noexcept
{
    // AddAttributedModules discovers the REACT_MODULE(ImagePickerModule,
    // L"RNWImagePicker") registration in ImagePickerModule.h. The second
    // argument opts the module into TurboModule registration, which the new
    // architecture requires.
    //
    // RNW_NEW_ARCH is defined by PropertySheets\ReactNativeArchitecture.props
    // whenever RnwNewArch == true. (Note: some published libraries test
    // `#ifdef RnwNewArch`, which is an MSBuild property name and never a
    // preprocessor define -- that check silently never fires.)
#ifdef RNW_NEW_ARCH
    AddAttributedModules(packageBuilder, true);
#else
    AddAttributedModules(packageBuilder);
#endif
}

} // namespace winrt::RNWImagePicker::implementation
