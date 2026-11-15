#include "engine/resources/ConfigFormat.hpp"

#include <charconv>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

#include "engine/core/FormatUtils.h"

namespace tryengine::resources {

namespace {

using tryengine::core::Error;

constexpr bool IsAlpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

constexpr bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

constexpr bool IsIdentifierChar(char ch) {
    return IsAlpha(ch) || IsDigit(ch) || ch == '_';
}

bool ParseDouble(eastl::string_view token, double& out) {
    if (!token.empty() && token.front() == '+')
        token.remove_prefix(1);
    if (token.empty())
        return false;

    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
    return ec == std::errc{} && ptr == token.data() + token.size();
}

bool ParseInt64(eastl::string_view token, int64_t& out) {
    if (!token.empty() && token.front() == '+')
        token.remove_prefix(1);
    if (token.empty())
        return false;

    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
    return ec == std::errc{} && ptr == token.data() + token.size();
}

bool ParseUInt64(eastl::string_view token, uint64_t& out) {
    if (!token.empty() && token.front() == '+')
        token.remove_prefix(1);
    if (token.empty() || token.front() == '-')
        return false;

    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
    return ec == std::errc{} && ptr == token.data() + token.size();
}

class ConfigParser {
public:
    explicit ConfigParser(eastl::string_view text) : text_(text) {}

    Result<ConfigObject> Parse() {
        ConfigObject root;
        while (true) {
            SkipWhitespaceAndComments();
            if (AtEnd())
                break;

            ConfigField field;
            if (!ParseField(field))
                return std::unexpected(Error(eastl::move(error_)));
            UpsertField(root, eastl::move(field));
        }

        if (!error_.empty())
            return std::unexpected(Error(eastl::move(error_)));
        return root;
    }

private:
    bool AtEnd() const {
        return cursor_ >= text_.size();
    }

    char Peek() const {
        return AtEnd() ? '\0' : text_[cursor_];
    }

    char Get() {
        if (AtEnd())
            return '\0';

        const char ch = text_[cursor_++];
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return ch;
    }

    bool Fail(eastl::string_view message) {
        if (!error_.empty())
            return false;
        error_ = tryengine::fmt::format("{} at {}:{}", message, line_, column_);
        return false;
    }

    void SkipWhitespaceAndComments() {
        while (!AtEnd()) {
            const char ch = Peek();
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                Get();
                continue;
            }
            if (ch != '/')
                return;

            if (cursor_ + 1 >= text_.size())
                return;
            const char next = text_[cursor_ + 1];
            if (next == '/') {
                Get();
                Get();
                while (!AtEnd() && Peek() != '\n')
                    Get();
                continue;
            }
            if (next == '*') {
                Get();
                Get();
                while (!AtEnd()) {
                    const char comment_ch = Get();
                    if (comment_ch == '*' && Peek() == '/') {
                        Get();
                        break;
                    }
                }
                continue;
            }
            return;
        }
    }

    bool Expect(char symbol) {
        SkipWhitespaceAndComments();
        if (AtEnd())
            return Fail(tryengine::fmt::format("unexpected end of config, expected '{}'", symbol));
        if (Peek() != symbol)
            return Fail(tryengine::fmt::format("unexpected character '{}', expected '{}'", Peek(), symbol));
        Get();
        return true;
    }

    bool ReadIdentifier(eastl::string& out) {
        SkipWhitespaceAndComments();
        out.clear();
        while (!AtEnd() && IsIdentifierChar(Peek()))
            out.push_back(Get());
        if (out.empty())
            return Fail("expected identifier");
        return true;
    }

    bool ReadQuotedString(eastl::string& out) {
        if (!Expect('"'))
            return false;

        out.clear();
        while (!AtEnd()) {
            char ch = Get();
            if (ch == '"')
                return true;
            if (ch == '\\') {
                if (AtEnd())
                    return Fail("unterminated string escape");
                const char escaped = Get();
                switch (escaped) {
                    case 'n':
                        ch = '\n';
                        break;
                    case 'r':
                        ch = '\r';
                        break;
                    case 't':
                        ch = '\t';
                        break;
                    case '"':
                        ch = '"';
                        break;
                    case '\\':
                        ch = '\\';
                        break;
                    default:
                        ch = escaped;
                        break;
                }
            }
            out.push_back(ch);
        }

        return Fail("unterminated string");
    }

    bool ReadNumberToken(eastl::string& out) {
        SkipWhitespaceAndComments();
        out.clear();

        const char first = Peek();
        if (first != '-' && first != '+' && !IsDigit(first))
            return Fail("expected number");
        out.push_back(Get());

        while (!AtEnd()) {
            const char ch = Peek();
            if (IsDigit(ch) || ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-')
                out.push_back(Get());
            else
                break;
        }
        return true;
    }

    template <typename T, size_t N>
    bool ReadFloatVector(ConfigValue& out) {
        if (!Expect('['))
            return false;

        eastl::array<float, N> values{};
        for (size_t i = 0; i < N; ++i) {
            eastl::string token;
            if (!ReadNumberToken(token))
                return false;

            double value = 0.0;
            if (!ParseDouble(token, value))
                return Fail(tryengine::fmt::format("invalid number '{}'", token));
            values[i] = static_cast<float>(value);
        }

        if (!Expect(']'))
            return false;
        out = values;
        return true;
    }

    template <typename T, size_t N>
    bool ReadIntVector(ConfigValue& out) {
        if (!Expect('['))
            return false;

        eastl::array<T, N> values{};
        for (size_t i = 0; i < N; ++i) {
            eastl::string token;
            if (!ReadNumberToken(token))
                return false;

            int64_t value = 0;
            if (!ParseInt64(token, value))
                return Fail(tryengine::fmt::format("invalid number '{}'", token));
            values[i] = static_cast<T>(value);
        }

        if (!Expect(']'))
            return false;
        out = values;
        return true;
    }

    template <typename T, size_t N>
    bool ReadUIntVector(ConfigValue& out) {
        if (!Expect('['))
            return false;

        eastl::array<T, N> values{};
        for (size_t i = 0; i < N; ++i) {
            eastl::string token;
            if (!ReadNumberToken(token))
                return false;

            uint64_t value = 0;
            if (!ParseUInt64(token, value))
                return Fail(tryengine::fmt::format("invalid number '{}'", token));
            values[i] = static_cast<T>(value);
        }

        if (!Expect(']'))
            return false;
        out = values;
        return true;
    }

    bool ReadObject(ConfigValue& out) {
        if (!Expect('{'))
            return false;

        auto object = eastl::make_unique<ConfigObject>();
        while (true) {
            SkipWhitespaceAndComments();
            if (AtEnd())
                return Fail("unexpected end of config in object");
            if (Peek() == '}') {
                Get();
                break;
            }

            ConfigField field;
            if (!ParseField(field))
                return false;
            UpsertField(*object, eastl::move(field));

            SkipWhitespaceAndComments();
            if (Peek() == ',')
                Get();
        }

        out = eastl::move(object);
        return true;
    }

    bool ParseValueByType(eastl::string_view type_name, ConfigValue& out) {
        SkipWhitespaceAndComments();

        if (type_name.size() >= 2 && type_name.substr(type_name.size() - 2) == "[]") {
            if (!Expect('['))
                return false;

            auto array = eastl::make_unique<ConfigArray>();
            const eastl::string_view element_type = type_name.substr(0, type_name.size() - 2);
            while (true) {
                SkipWhitespaceAndComments();
                if (AtEnd())
                    return Fail("unexpected end of config in array");
                if (Peek() == ']') {
                    Get();
                    break;
                }

                ConfigField element;
                element.type_name = eastl::string(element_type.data(), element_type.size());
                if (!ParseValueByType(element_type, element.value))
                    return false;
                array->elements.push_back(eastl::move(element));

                SkipWhitespaceAndComments();
                if (Peek() == ',')
                    Get();
            }

            out = eastl::move(array);
            return true;
        }

        if (type_name == "f32x2")
            return ReadFloatVector<float, 2>(out);
        if (type_name == "f32x3")
            return ReadFloatVector<float, 3>(out);
        if (type_name == "f32x4")
            return ReadFloatVector<float, 4>(out);
        if (type_name == "i32x2")
            return ReadIntVector<int32_t, 2>(out);
        if (type_name == "i32x3")
            return ReadIntVector<int32_t, 3>(out);
        if (type_name == "i32x4")
            return ReadIntVector<int32_t, 4>(out);
        if (type_name == "u32x2")
            return ReadUIntVector<uint32_t, 2>(out);
        if (type_name == "u32x3")
            return ReadUIntVector<uint32_t, 3>(out);
        if (type_name == "u32x4")
            return ReadUIntVector<uint32_t, 4>(out);
        if (type_name == "f32m3x3")
            return ReadFloatVector<float, 9>(out);
        if (type_name == "f32m3x4")
            return ReadFloatVector<float, 12>(out);
        if (type_name == "f32m4x4")
            return ReadFloatVector<float, 16>(out);

        // Объект может иметь произвольное имя типа (имя структуры из daslang).
        if (Peek() == '{')
            return ReadObject(out);

        if (type_name == "string") {
            eastl::string value;
            if (!ReadQuotedString(value))
                return false;
            out = eastl::move(value);
            return true;
        }
        if (type_name == "bool") {
            eastl::string value;
            if (!ReadIdentifier(value))
                return false;
            if (value == "true") {
                out = true;
                return true;
            }
            if (value == "false") {
                out = false;
                return true;
            }
            return Fail(tryengine::fmt::format("invalid bool value '{}'", value));
        }

        if (type_name == "u64" || type_name == "uint64") {
            eastl::string token;
            uint64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseUInt64(token, value))
                return Fail(tryengine::fmt::format("invalid u64 value '{}'", token));
            out = value;
            return true;
        }
        if (type_name == "u32" || type_name == "uint") {
            eastl::string token;
            uint64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseUInt64(token, value))
                return Fail(tryengine::fmt::format("invalid u32 value '{}'", token));
            out = static_cast<uint32_t>(value);
            return true;
        }
        if (type_name == "u16" || type_name == "uint16") {
            eastl::string token;
            uint64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseUInt64(token, value))
                return Fail(tryengine::fmt::format("invalid u16 value '{}'", token));
            out = static_cast<uint16_t>(value);
            return true;
        }
        if (type_name == "u8" || type_name == "uint8") {
            eastl::string token;
            uint64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseUInt64(token, value))
                return Fail(tryengine::fmt::format("invalid u8 value '{}'", token));
            out = static_cast<uint8_t>(value);
            return true;
        }

        if (type_name == "i64" || type_name == "int64") {
            eastl::string token;
            int64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseInt64(token, value))
                return Fail(tryengine::fmt::format("invalid i64 value '{}'", token));
            out = value;
            return true;
        }
        if (type_name == "i32" || type_name == "int") {
            eastl::string token;
            int64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseInt64(token, value))
                return Fail(tryengine::fmt::format("invalid i32 value '{}'", token));
            out = static_cast<int32_t>(value);
            return true;
        }
        if (type_name == "i16" || type_name == "int16") {
            eastl::string token;
            int64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseInt64(token, value))
                return Fail(tryengine::fmt::format("invalid i16 value '{}'", token));
            out = static_cast<int16_t>(value);
            return true;
        }
        if (type_name == "i8" || type_name == "int8") {
            eastl::string token;
            int64_t value = 0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseInt64(token, value))
                return Fail(tryengine::fmt::format("invalid i8 value '{}'", token));
            out = static_cast<int8_t>(value);
            return true;
        }

        if (type_name == "f32" || type_name == "float") {
            eastl::string token;
            double value = 0.0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseDouble(token, value))
                return Fail(tryengine::fmt::format("invalid f32 value '{}'", token));
            out = static_cast<float>(value);
            return true;
        }
        if (type_name == "f64" || type_name == "double") {
            eastl::string token;
            double value = 0.0;
            if (!ReadNumberToken(token))
                return false;
            if (!ParseDouble(token, value))
                return Fail(tryengine::fmt::format("invalid f64 value '{}'", token));
            out = value;
            return true;
        }

        return Fail(tryengine::fmt::format("unknown type '{}'", type_name));
    }

    bool ParseField(ConfigField& out) {
        SkipWhitespaceAndComments();
        if (AtEnd())
            return Fail("unexpected end of config, expected field");
        if (Peek() == '}')
            return Fail("unexpected '}'");

        if (!ReadIdentifier(out.name))
            return false;
        if (!Expect(':'))
            return false;
        if (!ReadIdentifier(out.type_name))
            return false;

        SkipWhitespaceAndComments();
        while (!AtEnd() && Peek() == '[') {
            Get();
            if (!Expect(']'))
                return false;
            out.type_name += "[]";
            SkipWhitespaceAndComments();
        }

        if (!Expect('='))
            return false;
        return ParseValueByType(out.type_name, out.value);
    }

    static void UpsertField(ConfigObject& object, ConfigField&& field) {
        for (auto& existing : object.fields) {
            if (existing.name == field.name) {
                existing = eastl::move(field);
                return;
            }
        }
        object.fields.push_back(eastl::move(field));
    }

    eastl::string_view text_;
    size_t cursor_ = 0;
    int line_ = 1;
    int column_ = 1;
    eastl::string error_;
};

void AppendTabs(eastl::string& out, int depth) {
    for (int i = 0; i < depth; ++i)
        out.push_back('\t');
}

void AppendQuotedString(eastl::string& out, eastl::string_view value) {
    out.push_back('"');
    for (const char ch : value) {
        switch (ch) {
            case '"':
                out.append("\\\"");
                break;
            case '\\':
                out.append("\\\\");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    out.push_back('"');
}

template <typename T, size_t N>
void AppendFloatArray(eastl::string& out, const eastl::array<T, N>& values) {
    out.push_back('[');
    for (size_t i = 0; i < N; ++i) {
        if (i > 0)
            out.push_back(' ');
        tryengine::fmt::format_append(out, "{}", values[i]);
    }
    out.push_back(']');
}

template <typename T, size_t N>
void AppendIntegerArray(eastl::string& out, const eastl::array<T, N>& values) {
    out.push_back('[');
    for (size_t i = 0; i < N; ++i) {
        if (i > 0)
            out.push_back(' ');
        tryengine::fmt::format_append(out, "{}", static_cast<int64_t>(values[i]));
    }
    out.push_back(']');
}

void WriteField(eastl::string& out, const ConfigField& field, int depth);

void WriteValue(eastl::string& out, const ConfigValue& value, int depth) {
    eastl::visit(
        [&](const auto& alternative) {
            using T = eastl::decay_t<decltype(alternative)>;
            if constexpr (eastl::is_same_v<T, eastl::monostate>) {
                out.append("null");
            } else if constexpr (eastl::is_same_v<T, eastl::string>) {
                AppendQuotedString(out, alternative);
            } else if constexpr (eastl::is_same_v<T, bool>) {
                out.append(alternative ? "true" : "false");
            } else if constexpr (eastl::is_same_v<T, int8_t>) {
                tryengine::fmt::format_append(out, "{}", static_cast<int32_t>(alternative));
            } else if constexpr (eastl::is_same_v<T, uint8_t>) {
                tryengine::fmt::format_append(out, "{}", static_cast<uint32_t>(alternative));
            } else if constexpr (eastl::is_same_v<T, float>) {
                tryengine::fmt::format_append(out, "{}", alternative);
            } else if constexpr (eastl::is_same_v<T, double>) {
                tryengine::fmt::format_append(out, "{}", alternative);
            } else if constexpr (eastl::is_same_v<T, ConfigFloat2> || eastl::is_same_v<T, ConfigFloat3> ||
                                 eastl::is_same_v<T, ConfigFloat4> || eastl::is_same_v<T, ConfigFloat3x3> ||
                                 eastl::is_same_v<T, ConfigFloat3x4> || eastl::is_same_v<T, ConfigFloat4x4>) {
                AppendFloatArray(out, alternative);
            } else if constexpr (eastl::is_same_v<T, ConfigInt2> || eastl::is_same_v<T, ConfigInt3> ||
                                 eastl::is_same_v<T, ConfigInt4>) {
                AppendIntegerArray(out, alternative);
            } else if constexpr (eastl::is_same_v<T, ConfigUInt2> || eastl::is_same_v<T, ConfigUInt3> ||
                                 eastl::is_same_v<T, ConfigUInt4>) {
                AppendIntegerArray(out, alternative);
            } else if constexpr (eastl::is_same_v<T, eastl::unique_ptr<ConfigObject>>) {
                out.append("{\n");
                if (alternative) {
                    for (const ConfigField& child : alternative->fields) {
                        WriteField(out, child, depth + 1);
                        out.push_back('\n');
                    }
                }
                AppendTabs(out, depth);
                out.push_back('}');
            } else if constexpr (eastl::is_same_v<T, eastl::unique_ptr<ConfigArray>>) {
                out.append("[\n");
                if (alternative) {
                    for (const ConfigField& element : alternative->elements) {
                        WriteField(out, element, depth + 1);
                        out.push_back('\n');
                    }
                }
                AppendTabs(out, depth);
                out.push_back(']');
            } else {
                tryengine::fmt::format_append(out, "{}", alternative);
            }
        },
        value);
}

void WriteField(eastl::string& out, const ConfigField& field, int depth) {
    AppendTabs(out, depth);
    if (!field.name.empty()) {
        out.append(field.name);
        out.append(": ");
        out.append(field.type_name);
        out.append(" = ");
    }
    WriteValue(out, field.value, depth);
}

}  // namespace

Result<ConfigObject> ParseConfig(eastl::string_view text) {
    ConfigParser parser(text);
    return parser.Parse();
}

Result<ConfigObject> ParseConfig(eastl::span<const uint8_t> bytes) {
    return ParseConfig(eastl::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

eastl::string WriteConfig(const ConfigObject& root) {
    eastl::string out;
    for (const ConfigField& field : root.fields) {
        WriteField(out, field, 0);
        out.push_back('\n');
    }
    return out;
}

const ConfigField* FindField(const ConfigObject& object, eastl::string_view name) {
    for (const ConfigField& field : object.fields)
        if (field.name == name)
            return &field;
    return nullptr;
}

ConfigField* FindField(ConfigObject& object, eastl::string_view name) {
    for (ConfigField& field : object.fields)
        if (field.name == name)
            return &field;
    return nullptr;
}

namespace {

ConfigValue CloneValue(const ConfigValue& value) {
    return eastl::visit(
        [](const auto& alternative) -> ConfigValue {
            using T = eastl::decay_t<decltype(alternative)>;
            if constexpr (eastl::is_same_v<T, eastl::unique_ptr<ConfigObject>>) {
                if (!alternative)
                    return eastl::unique_ptr<ConfigObject>{};
                return eastl::make_unique<ConfigObject>(*alternative);
            } else if constexpr (eastl::is_same_v<T, eastl::unique_ptr<ConfigArray>>) {
                if (!alternative)
                    return eastl::unique_ptr<ConfigArray>{};
                return eastl::make_unique<ConfigArray>(*alternative);
            } else {
                return alternative;
            }
        },
        value);
}

}  // namespace

ConfigField::ConfigField(const ConfigField& other)
    : name(other.name), type_name(other.type_name), value(CloneValue(other.value)) {}

ConfigField& ConfigField::operator=(const ConfigField& other) {
    if (this != &other) {
        name = other.name;
        type_name = other.type_name;
        value = CloneValue(other.value);
    }
    return *this;
}

ConfigObject::ConfigObject(const ConfigObject& other) {
    fields.reserve(other.fields.size());
    for (const ConfigField& field : other.fields)
        fields.push_back(field);
}

ConfigObject& ConfigObject::operator=(const ConfigObject& other) {
    if (this != &other) {
        fields.clear();
        fields.reserve(other.fields.size());
        for (const ConfigField& field : other.fields)
            fields.push_back(field);
    }
    return *this;
}

ConfigArray::ConfigArray(const ConfigArray& other) {
    elements.reserve(other.elements.size());
    for (const ConfigField& element : other.elements)
        elements.push_back(element);
}

ConfigArray& ConfigArray::operator=(const ConfigArray& other) {
    if (this != &other) {
        elements.clear();
        elements.reserve(other.elements.size());
        for (const ConfigField& element : other.elements)
            elements.push_back(element);
    }
    return *this;
}

bool IsObject(const ConfigValue& value) {
    return eastl::holds_alternative<eastl::unique_ptr<ConfigObject>>(value);
}

bool IsArray(const ConfigValue& value) {
    return eastl::holds_alternative<eastl::unique_ptr<ConfigArray>>(value);
}

const ConfigObject* GetObject(const ConfigValue& value) {
    const auto* object = eastl::get_if<eastl::unique_ptr<ConfigObject>>(&value);
    return object ? object->get() : nullptr;
}

const ConfigArray* GetArray(const ConfigValue& value) {
    const auto* array = eastl::get_if<eastl::unique_ptr<ConfigArray>>(&value);
    return array ? array->get() : nullptr;
}

ConfigObject* GetObject(ConfigValue& value) {
    auto* object = eastl::get_if<eastl::unique_ptr<ConfigObject>>(&value);
    return object ? object->get() : nullptr;
}

ConfigArray* GetArray(ConfigValue& value) {
    auto* array = eastl::get_if<eastl::unique_ptr<ConfigArray>>(&value);
    return array ? array->get() : nullptr;
}

}  // namespace tryengine::resources
