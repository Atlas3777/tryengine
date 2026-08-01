// #pragma once
//
// #include <iostream>
// #include <string>
// #include <optional>
//
// #include "engine/async/Task.hpp"
// #include "engine/filesystem/FileSystem.hpp"
// #include "engine/filesystem/PathResolver.hpp"
// #include "engine/filesystem/VirtualPath.hpp"
// #include "engine/filesystem/NativePath.hpp"
// #include "engine/resources/JsonSerializer.hpp"
// #include "engine/resources/AddressablesTypes.hpp"
// #include "editor/EditorApp.hpp"
//
// namespace tryeditor {
//
// class AppBootstrap {
// public:
//     static tryengine::async::Task<void> CheckBaseProjectData(
//         tryengine::filesystem::FileSystem& fs,
//         tryengine::filesystem::PathResolver& resolver)
//     {
//         const auto addressables_dir = tryengine::filesystem::VirtualPath("project://addressables/");
//         const auto addressables_file = addressables_dir / "addressables.addressables";
//         const auto asset_groups_dir = addressables_dir / "asset_groups";
//
//         const auto native_dir = resolver.Resolve(addressables_dir);
//         const auto native_file = resolver.Resolve(addressables_file);
//         const auto native_groups_dir = resolver.Resolve(asset_groups_dir);
//
//         if (!native_dir || !native_file || !native_groups_dir)
//             co_return tryengine::core::Error("[Bootstrap]: Critical error: Failed to resolve virtual paths. Check mount points");
//
//
//         co_await fs.CreateDirectories(*native_dir);
//
//         if (co_await fs.Exists(*native_file)) {
//             std::cout << "[Bootstrap]: Addressables found!\n";
//         } else {
//             std::cerr << "[Bootstrap]: Addressables manifest not found. Creating default...\n";
//
//             tryengine::resources::AddressablesManifestAsset default_asset;
//             co_await fs.CreateDirectories(*native_groups_dir);
//
//         }
//     }
// };
//
// tryengine::async::Task<uint32_t> GetNumber() {
//     std::cout << "START TASK" << 42 << "\n";
//
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//
//     co_await tryengine::async::ExecutorSwitch{EditorApp::GetMain()};
//
//     std::cout << "END TASK" << 42 << "\n";
//
//     co_return 42;
// }
//
// }  // namespace tryeditor