/**
 * Autolinking config for react-native-windows.
 *
 * Shape verified against the RNW CLI's dependency config parser at
 * node_modules/@react-native-windows/cli/lib-commonjs/commands/config/dependencyConfig.js:
 *
 *   - `sourceDir` is REQUIRED whenever `projects` is specified (the parser emits
 *     "Error: Source dir is required if projects are specified" otherwise). It is
 *     resolved relative to the package folder.
 *   - `projects[].projectFile` is resolved relative to `sourceDir`, and
 *     `projectFile` + `directDependency` are the only required per-project keys.
 *     projectName / projectLang / projectGuid / cppHeaders / cppPackageProviders
 *     are auto-derived by the parser from the .vcxproj, so they are omitted here.
 *   - `directDependency: true` is what makes the CLI emit the provider
 *     registration into the app's AutolinkedNativeModules.g.cpp. It derives
 *     `#include <winrt/RNWImagePicker.h>` and
 *     `winrt::RNWImagePicker::ReactPackageProvider()` from the <RootNamespace>
 *     in the .vcxproj -- which is why RootNamespace must stay `RNWImagePicker`.
 *   - `solutionFile: null` means "this package has no solution of its own";
 *     the app's solution consumes the .vcxproj via a generated ProjectReference.
 */
module.exports = {
  dependency: {
    platforms: {
      windows: {
        sourceDir: 'windows',
        solutionFile: null,
        projects: [
          {
            projectFile: 'RNWImagePicker/RNWImagePicker.vcxproj',
            directDependency: true,
          },
        ],
      },
    },
  },
};
