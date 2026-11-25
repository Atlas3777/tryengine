#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include "engine/core/MakeError.hpp"
#include "engine/resources/ConfigFormat.hpp"

namespace tryeditor {

using tryengine::Result;

//! Идентичность ассета. `guid` — то, чем ассет адресуют материалы, сцены и пак;
//! `importer_type` выбирает импортёр; `sub_assets` генерирует импортёр (меши, материалы и
//! текстуры glTF-модели живут под тем же guid отдельными resource id).
struct AssetMetaHeader {
    uint64_t guid = 0;
    eastl::string importer_type;
    eastl::vector<uint64_t> sub_assets;
};

template <typename TSettings>
struct AssetMeta {
    AssetMetaHeader header;
    TSettings settings;
};

//! Разбор и запись блока `settings` для одного вида настроек импорта.
//!
//! Специализация объявляется рядом с самим типом настроек (editor/include/editor/import),
//! поэтому поле, добавленное в C++, и его чтение из .meta лежат в одном файле. Незнакомые
//! поля игнорируются: .meta может быть новее кода, а отсутствующие берут значение по
//! умолчанию структуры.
template <typename TSettings>
struct MetaSettingsCodec;

namespace meta {

using tryengine::resources::ConfigArray;
using tryengine::resources::ConfigField;
using tryengine::resources::ConfigObject;
using tryengine::resources::ConfigValue;

inline ConfigField MakeU64Field(eastl::string name, uint64_t value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "u64";
    field.value = value;
    return field;
}

inline ConfigField MakeU32Field(eastl::string name, uint32_t value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "u32";
    field.value = value;
    return field;
}

inline ConfigField MakeU8Field(eastl::string name, uint8_t value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "u8";
    field.value = value;
    return field;
}

inline ConfigField MakeFloatField(eastl::string name, float value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "f32";
    field.value = value;
    return field;
}

inline ConfigField MakeBoolField(eastl::string name, bool value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "bool";
    field.value = value;
    return field;
}

inline ConfigField MakeStringField(eastl::string name, eastl::string value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "string";
    field.value = eastl::move(value);
    return field;
}

inline ConfigField MakeU64ArrayField(eastl::string name, const eastl::vector<uint64_t>& values) {
    ConfigArray array;
    array.elements.reserve(values.size());
    for (const uint64_t value : values)
        array.elements.push_back(MakeU64Field(eastl::string(), value));

    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = "u64[]";
    field.value = eastl::make_unique<ConfigArray>(eastl::move(array));
    return field;
}

//! Значение поля как uint64. Принимает любой целочисленный вариант и десятичную строку:
//! руками правленный .meta не обязан повторять ровно тот вариант, который пишет C++.
inline bool ReadUnsigned(const ConfigValue& value, uint64_t& out) {
    if (const auto* v = eastl::get_if<uint64_t>(&value)) {
        out = *v;
        return true;
    }
    if (const auto* v = eastl::get_if<uint32_t>(&value)) {
        out = *v;
        return true;
    }
    if (const auto* v = eastl::get_if<uint16_t>(&value)) {
        out = *v;
        return true;
    }
    if (const auto* v = eastl::get_if<uint8_t>(&value)) {
        out = *v;
        return true;
    }
    if (const auto* v = eastl::get_if<int64_t>(&value); v && *v >= 0) {
        out = static_cast<uint64_t>(*v);
        return true;
    }
    if (const auto* v = eastl::get_if<int32_t>(&value); v && *v >= 0) {
        out = static_cast<uint64_t>(*v);
        return true;
    }
    return false;
}

template <typename T>
Result<T> ReadFieldError(eastl::string_view block, eastl::string_view name, const char* expected) {
    return LogAndMakeError(".meta: field '{}.{}' must be {}", block, name, expected);
}

inline Result<uint64_t> ReadU64Field(const ConfigObject& object, eastl::string_view block, eastl::string_view name,
                                     uint64_t fallback) {
    const ConfigField* field = tryengine::resources::FindField(object, name);
    if (!field)
        return fallback;
    uint64_t value = 0;
    if (!ReadUnsigned(field->value, value))
        return ReadFieldError<uint64_t>(block, name, "an unsigned integer");
    return value;
}

inline Result<uint32_t> ReadU32Field(const ConfigObject& object, eastl::string_view block, eastl::string_view name,
                                     uint32_t fallback) {
    auto value = ReadU64Field(object, block, name, fallback);
    if (!value.has_value())
        return eastl::move(value.error());
    if (*value > UINT32_MAX)
        return LogAndMakeError(".meta: field '{}.{}' does not fit u32", block, name);
    return static_cast<uint32_t>(*value);
}

inline Result<uint8_t> ReadU8Field(const ConfigObject& object, eastl::string_view block, eastl::string_view name,
                                   uint8_t fallback) {
    auto value = ReadU64Field(object, block, name, fallback);
    if (!value.has_value())
        return eastl::move(value.error());
    if (*value > UINT8_MAX)
        return LogAndMakeError(".meta: field '{}.{}' does not fit u8", block, name);
    return static_cast<uint8_t>(*value);
}

inline Result<float> ReadFloatField(const ConfigObject& object, eastl::string_view block, eastl::string_view name,
                                    float fallback) {
    const ConfigField* field = tryengine::resources::FindField(object, name);
    if (!field)
        return fallback;
    if (const auto* value = eastl::get_if<float>(&field->value))
        return *value;
    if (const auto* value = eastl::get_if<double>(&field->value))
        return static_cast<float>(*value);
    return ReadFieldError<float>(block, name, "a number");
}

inline Result<bool> ReadBoolField(const ConfigObject& object, eastl::string_view block, eastl::string_view name,
                                  bool fallback) {
    const ConfigField* field = tryengine::resources::FindField(object, name);
    if (!field)
        return fallback;
    if (const auto* value = eastl::get_if<bool>(&field->value))
        return *value;
    return ReadFieldError<bool>(block, name, "a bool");
}

inline Result<eastl::string> ReadStringField(const ConfigObject& object, eastl::string_view block,
                                             eastl::string_view name, eastl::string_view fallback) {
    const ConfigField* field = tryengine::resources::FindField(object, name);
    if (!field)
        return eastl::string(fallback.data(), fallback.size());
    if (const auto* value = eastl::get_if<eastl::string>(&field->value))
        return *value;
    return ReadFieldError<eastl::string>(block, name, "a string");
}

inline Result<eastl::vector<uint64_t>> ReadU64ArrayField(const ConfigObject& object, eastl::string_view block,
                                                         eastl::string_view name) {
    eastl::vector<uint64_t> values;
    const ConfigField* field = tryengine::resources::FindField(object, name);
    if (!field)
        return values;
    const ConfigArray* array = tryengine::resources::GetArray(field->value);
    if (!array)
        return ReadFieldError<eastl::vector<uint64_t>>(block, name, "an array of u64");
    values.reserve(array->elements.size());
    for (const ConfigField& element : array->elements) {
        uint64_t value = 0;
        if (!ReadUnsigned(element.value, value))
            return ReadFieldError<eastl::vector<uint64_t>>(block, name, "an array of u64");
        values.push_back(value);
    }
    return values;
}

//! Блок `header` обязателен: без guid ассет нельзя адресовать, без importer_type — импортировать.
inline Result<AssetMetaHeader> ReadHeader(const ConfigObject& root) {
    const ConfigField* field = tryengine::resources::FindField(root, "header");
    if (!field)
        return LogAndMakeError(".meta: 'header' block is missing");
    const ConfigObject* header = tryengine::resources::GetObject(field->value);
    if (!header)
        return LogAndMakeError(".meta: 'header' must be an object");

    AssetMetaHeader result;
    auto guid = ReadU64Field(*header, "header", "guid", 0);
    if (!guid.has_value())
        return eastl::move(guid.error());
    if (*guid == 0)
        return LogAndMakeError(".meta: 'header.guid' is missing or zero");
    result.guid = *guid;

    auto importer = ReadStringField(*header, "header", "importer_type", "");
    if (!importer.has_value())
        return eastl::move(importer.error());
    if (importer->empty())
        return LogAndMakeError(".meta: 'header.importer_type' is missing");
    result.importer_type = eastl::move(*importer);

    auto sub_assets = ReadU64ArrayField(*header, "header", "sub_assets");
    if (!sub_assets.has_value())
        return eastl::move(sub_assets.error());
    result.sub_assets = eastl::move(*sub_assets);
    return result;
}

inline ConfigObject MakeHeader(const AssetMetaHeader& header) {
    ConfigObject object;
    object.fields.push_back(MakeU64Field("guid", header.guid));
    object.fields.push_back(MakeStringField("importer_type", header.importer_type));
    object.fields.push_back(MakeU64ArrayField("sub_assets", header.sub_assets));
    return object;
}

}  // namespace meta

//! Читает только идентичность .meta. Нужна конвейеру: он узнаёт guid и импортёра до того,
//! как выберет, каким кодеком разбирать настройки.
inline Result<AssetMetaHeader> ParseMetaHeader(eastl::span<const uint8_t> bytes) {
    auto parsed = tryengine::resources::ParseConfig(bytes);
    if (!parsed.has_value())
        return eastl::move(parsed.error());
    return meta::ReadHeader(*parsed);
}

inline Result<AssetMetaHeader> ParseMetaHeader(eastl::string_view text) {
    auto parsed = tryengine::resources::ParseConfig(text);
    if (!parsed.has_value())
        return eastl::move(parsed.error());
    return meta::ReadHeader(*parsed);
}

//! .meta — документ ConfigFormat из двух блоков: `header` (идентичность, редактировать
//! нельзя) и `settings` (настройки импорта, раскладываются на поля в панели Import Settings).
template <typename TSettings>
Result<AssetMeta<TSettings>> ParseAssetMeta(eastl::span<const uint8_t> bytes) {
    auto parsed = tryengine::resources::ParseConfig(bytes);
    if (!parsed.has_value())
        return eastl::move(parsed.error());

    AssetMeta<TSettings> meta;
    auto header = meta::ReadHeader(*parsed);
    if (!header.has_value())
        return eastl::move(header.error());
    meta.header = eastl::move(*header);

    const tryengine::resources::ConfigField* settings = tryengine::resources::FindField(*parsed, "settings");
    if (settings) {
        const tryengine::resources::ConfigObject* object = tryengine::resources::GetObject(settings->value);
        if (!object)
            return LogAndMakeError(".meta: 'settings' must be an object");
        auto decoded = MetaSettingsCodec<TSettings>::Read(*object);
        if (!decoded.has_value())
            return eastl::move(decoded.error());
        meta.settings = eastl::move(*decoded);
    }
    return meta;
}

template <typename TSettings>
Result<AssetMeta<TSettings>> ParseAssetMeta(eastl::string_view text) {
    return ParseAssetMeta<TSettings>(
        eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

template <typename TSettings>
bool ValidateAssetMeta(eastl::span<const uint8_t> bytes) {
    auto parsed = ParseAssetMeta<TSettings>(bytes);
    if (!parsed.has_value()) {
        parsed.error().Discard();
        return false;
    }
    return true;
}

template <typename TSettings>
eastl::vector<uint8_t> WriteAssetMeta(const AssetMeta<TSettings>& meta) {
    tryengine::resources::ConfigObject root;

    tryengine::resources::ConfigField header;
    header.name = "header";
    header.type_name = "AssetMetaHeader";
    header.value = eastl::make_unique<tryengine::resources::ConfigObject>(meta::MakeHeader(meta.header));
    root.fields.push_back(eastl::move(header));

    tryengine::resources::ConfigObject settings;
    MetaSettingsCodec<TSettings>::Write(settings, meta.settings);

    tryengine::resources::ConfigField settings_field;
    settings_field.name = "settings";
    settings_field.type_name = MetaSettingsCodec<TSettings>::kTypeName;
    settings_field.value = eastl::make_unique<tryengine::resources::ConfigObject>(eastl::move(settings));
    root.fields.push_back(eastl::move(settings_field));

    const eastl::string text = tryengine::resources::WriteConfig(root);
    eastl::vector<uint8_t> bytes;
    bytes.assign(text.begin(), text.end());
    return bytes;
}

}  // namespace tryeditor
