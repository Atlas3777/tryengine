// Checks the glTF importer's up-axis handling without launching the editor: the root rotation
// that turns a Y-up (or X-up) file into the engine's Z-up, the name parsing the .meta setting
// goes through, and a byte-exact .meta round trip through the real
// MetaSettingsCodec<GltfImportSettings>.
//
//   tools/agent/gltf-probe.sh                                   # every committed model .meta
//   tools/agent/gltf-probe.sh game/assets/lance.glb.meta
//
// Built on demand into tools/agent/.tmp; never part of the game or the editor.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor/import/GltfImporter.hpp"
#include "editor/import/GltfUpAxis.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace {

using tryeditor::GltfImportSettings;
using tryeditor::GltfUpAxis;
using tryengine::Transform;

// sin(45°) = cos(45°) = √2/2: половина угла поворота на 90°.
constexpr float kHalfSqrt2 = 0.70710678118654752f;

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void CheckVec(das::float3 value, float x, float y, float z, const char* what) {
    const bool ok = std::fabs(value.x - x) < 1e-5f && std::fabs(value.y - y) < 1e-5f && std::fabs(value.z - z) < 1e-5f;
    if (!ok) {
        std::printf("FAIL: %s: got (%g %g %g), want (%g %g %g)\n", what, value.x, value.y, value.z, x, y, z);
        ++failures;
    }
}

void CheckQuat(das::float4 value, float x, float y, float z, float w, const char* what) {
    const bool ok = std::fabs(value.x - x) < 1e-5f && std::fabs(value.y - y) < 1e-5f &&
                    std::fabs(value.z - z) < 1e-5f && std::fabs(value.w - w) < 1e-5f;
    if (!ok) {
        std::printf("FAIL: %s: got (%g %g %g %g), want (%g %g %g %g)\n", what, value.x, value.y, value.z, value.w, x, y,
                    z, w);
        ++failures;
    }
}

// Rotation (x, y, z, w) is identity, scale is non-unit on purpose: the conversion must leave it
// alone and it is the scale that a wrong `ComposeWorld` order would visibly break.
Transform MakeTransform(float px, float py, float pz, float qx, float qy, float qz, float qw) {
    Transform transform;
    transform.position = das::float3(px, py, pz);
    transform.rotation = das::float4(qx, qy, qz, qw);
    transform.scale = das::float3(2.0f, 3.0f, 4.0f);
    return transform;
}

void CheckUpAxisMath() {
    // glTF is Y-up: +90° around X maps (x, y, z) to (x, -z, y), so source +Y becomes engine +Z.
    {
        Transform root = MakeTransform(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        tryeditor::ConvertGltfTransformToEngineUp(GltfUpAxis::Y, root);
        CheckVec(root.position, 1.0f, -3.0f, 2.0f, "y-up root position becomes (x, -z, y)");
        CheckQuat(root.rotation, kHalfSqrt2, 0.0f, 0.0f, kHalfSqrt2, "y-up root spins +90 degrees around X");
        CheckVec(root.scale, 2.0f, 3.0f, 4.0f, "y-up keeps the root scale");
    }

    // A file that is already Z-up must come through untouched, geometry and scale alike.
    {
        Transform root = MakeTransform(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, kHalfSqrt2, kHalfSqrt2);
        tryeditor::ConvertGltfTransformToEngineUp(GltfUpAxis::Z, root);
        CheckVec(root.position, 1.0f, 2.0f, 3.0f, "z-up keeps the root position");
        CheckQuat(root.rotation, 0.0f, 0.0f, kHalfSqrt2, kHalfSqrt2, "z-up keeps the root rotation");
        CheckVec(root.scale, 2.0f, 3.0f, 4.0f, "z-up keeps the root scale");
    }

    // X-up: -90° around Y maps (x, y, z) to (-z, y, x).
    {
        Transform root = MakeTransform(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        tryeditor::ConvertGltfTransformToEngineUp(GltfUpAxis::X, root);
        CheckVec(root.position, -3.0f, 2.0f, 1.0f, "x-up root position becomes (-z, y, x)");
        CheckQuat(root.rotation, 0.0f, -kHalfSqrt2, 0.0f, kHalfSqrt2, "x-up root spins -90 degrees around Y");
    }

    // The conversion composes on the left, so a node's own rotation stays under it: a root turned
    // 90° around Z (source +X pointing along source +Y) ends up pointing along engine +Z.
    {
        Transform root = MakeTransform(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kHalfSqrt2, kHalfSqrt2);
        tryeditor::ConvertGltfTransformToEngineUp(GltfUpAxis::Y, root);
        CheckQuat(root.rotation, 0.5f, -0.5f, 0.5f, 0.5f, "y-up composes with the node's own rotation");
    }

    // Non-unit input rotations are normalized the same way TransformSystem does it, so a file with
    // a scaled quaternion does not leak the scale into the hierarchy.
    {
        Transform root = MakeTransform(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 2.0f);
        tryeditor::ConvertGltfTransformToEngineUp(GltfUpAxis::Y, root);
        CheckQuat(root.rotation, 0.5f, -0.5f, 0.5f, 0.5f, "y-up normalizes a scaled quaternion");
    }
}

void CheckUpAxisNames() {
    // The .meta spelling is what daslang's `string(enum)` writes; the lowercase form a
    // hand-edited file may still carry from the previous version is accepted too.
    Check(tryeditor::GltfUpAxisName(GltfUpAxis::Y) == "Y", "Y is spelled Y");
    Check(tryeditor::GltfUpAxisName(GltfUpAxis::Z) == "Z", "Z is spelled Z");
    Check(tryeditor::GltfUpAxisName(GltfUpAxis::X) == "X", "X is spelled X");

    GltfUpAxis axis = GltfUpAxis::Z;
    for (const char* name : {"Y", "y"})
        Check(tryeditor::GltfUpAxisFromName(name, axis) && axis == GltfUpAxis::Y, "Y names the glTF up axis");
    for (const char* name : {"Z", "z"})
        Check(tryeditor::GltfUpAxisFromName(name, axis) && axis == GltfUpAxis::Z, "Z names an already Z-up file");
    for (const char* name : {"X", "x"})
        Check(tryeditor::GltfUpAxisFromName(name, axis) && axis == GltfUpAxis::X, "X names an X-up file");

    for (const char* bad : {"", "up", "Y ", "-z", "none"})
        Check(!tryeditor::GltfUpAxisFromName(bad, axis), "an unknown up axis is rejected");

    // An old .meta has no `up_axis` field at all; the default is what makes those models stand up.
    Check(GltfImportSettings{}.up_axis == GltfUpAxis::Y, "the missing up_axis defaults to the glTF norm");
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::vector<uint8_t> bytes;
    FILE* file = std::fopen(path, "rb");
    if (!file)
        return bytes;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        bytes.resize(static_cast<size_t>(size));
        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
            bytes.clear();
    }
    std::fclose(file);
    return bytes;
}

void PrintMetaError(const tryengine::core::Error& error) {
    const auto message = error.Message();
    std::printf("  meta error: %.*s\n", static_cast<int>(message.size()), message.data());
}

// A committed .meta must both parse into the settings the importer reads and match the writer
// byte for byte: otherwise the editor rewrites it on the next import and the git diff is noise.
void CheckMetaFile(const char* path) {
    std::printf("meta: %s\n", path);
    const std::vector<uint8_t> bytes = ReadFile(path);
    if (bytes.empty()) {
        std::printf("FAIL: %s cannot be read\n", path);
        ++failures;
        return;
    }

    auto parsed = tryeditor::ParseAssetMeta<GltfImportSettings>(eastl::span<const uint8_t>(bytes.data(), bytes.size()));
    if (!parsed.has_value()) {
        PrintMetaError(parsed.error());
        parsed.error().Discard();
        std::printf("FAIL: %s parses as glTF import settings\n", path);
        ++failures;
        return;
    }

    const auto& meta = *parsed;
    const eastl::string_view axis_name = tryeditor::GltfUpAxisName(meta.settings.up_axis);
    std::printf("  importer %s  guid %llu  up_axis %.*s\n", meta.header.importer_type.c_str(),
                static_cast<unsigned long long>(meta.header.guid), static_cast<int>(axis_name.size()),
                axis_name.data());
    Check(meta.header.importer_type == "GltfImporter", "the .meta names the glTF importer");
    Check(meta.header.guid != 0, "the .meta carries a guid");
    Check(!meta.header.sub_assets.empty(), "the .meta carries sub-assets");

    const eastl::vector<uint8_t> rewritten = tryeditor::WriteAssetMeta(meta);
    const bool same =
        rewritten.size() == bytes.size() && std::memcmp(rewritten.data(), bytes.data(), bytes.size()) == 0;
    Check(same, "the .meta is written back byte for byte");

    tryeditor::GltfImporter importer;
    Check(importer.ValidateMeta(eastl::span<const uint8_t>(bytes.data(), bytes.size())),
          "the importer accepts the .meta");

    // A field of the wrong type or an unknown axis name is an import error, never a silent
    // default: the importer would otherwise re-import the model in the wrong orientation.
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const size_t field = text.find("up_axis: string = ");
    if (field != std::string::npos) {
        std::string wrong_type = text;
        wrong_type.replace(field, std::strlen("up_axis: string"), "up_axis: u64   ");
        Check(!importer.ValidateMeta(
                  eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(wrong_type.data()), wrong_type.size())),
              "a non-string up_axis is rejected");

        std::string unknown_name = text;
        const size_t quote = unknown_name.find('"', field);
        if (quote != std::string::npos && quote + 1 < unknown_name.size()) {
            unknown_name.replace(quote + 1, 1, "q");
            Check(!importer.ValidateMeta(eastl::span<const uint8_t>(
                      reinterpret_cast<const uint8_t*>(unknown_name.data()), unknown_name.size())),
                  "an unknown up axis name is rejected");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    CheckUpAxisNames();
    CheckUpAxisMath();

    if (argc < 2) {
        std::printf("FAIL: no .meta paths given\n");
        ++failures;
    }
    for (int i = 1; i < argc; ++i)
        CheckMetaFile(argv[i]);

    if (failures != 0) {
        std::printf("GLTF PROBE FAILED (%d checks)\n", failures);
        return 1;
    }
    std::printf("GLTF PROBE OK\n");
    return 0;
}
