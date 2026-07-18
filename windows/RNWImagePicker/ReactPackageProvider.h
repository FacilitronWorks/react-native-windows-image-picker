#pragma once

#include "ReactPackageProvider.g.h"

using namespace winrt::Microsoft::ReactNative;

namespace winrt::RNWImagePicker::implementation
{

struct ReactPackageProvider : ReactPackageProviderT<ReactPackageProvider>
{
    ReactPackageProvider() = default;

    void CreatePackage(IReactPackageBuilder const &packageBuilder) noexcept;
};

} // namespace winrt::RNWImagePicker::implementation

namespace winrt::RNWImagePicker::factory_implementation
{

struct ReactPackageProvider : ReactPackageProviderT<ReactPackageProvider, implementation::ReactPackageProvider> {};

} // namespace winrt::RNWImagePicker::factory_implementation
