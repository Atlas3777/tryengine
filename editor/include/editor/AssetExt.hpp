#pragma once

#include <EASTL/string_view.h>
#include <cstdint>
#include <cstring>

namespace tryeditor {

// Ключ расширения файла — 8 байт, впритык в узел дерева (UiFile::ext). Расширение нужно
// дереву, чтобы отвечать на вопросы вида "все .mat в game/data": сравнение ключей — одно
// сравнение uint64 на файл, без работы со строками. Строкой расширение не храним: у
// авторитетных файлов (content, data) нет ни .meta, ни guid, а лишняя интернированная
// строка на каждый файл — только лишняя память и refcount.
//
// Байты упакованы в нижнем регистре вместе с точкой, младший байт — первый символ:
// ".mat" => 0x74616D2E. Расширения длиннее 8 символов обрезаются по 8 байтам.
inline constexpr uint64_t kNoExt = 0;

inline uint64_t PackExt(eastl::string_view bytes) {
    uint64_t key = 0;
    const size_t n = bytes.size() < 8 ? bytes.size() : 8;
    for (size_t i = 0; i < n; ++i) {
        char c = bytes[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        key |= static_cast<uint64_t>(static_cast<uint8_t>(c)) << (i * 8);
    }
    return key;
}

// Имя файла, путь или само расширение -> ключ: "hero.png", "game/data/a.mat", ".mat" и
// "mat" дают один и тот же ключ. Точка в ключ входит всегда, поэтому запрос не зависит от
// того, как его записали.
inline uint64_t ExtKey(eastl::string_view name_or_path) {
    const size_t slash = name_or_path.find_last_of("\\/");
    if (slash != eastl::string_view::npos)
        name_or_path = name_or_path.substr(slash + 1);

    const size_t dot = name_or_path.find_last_of('.');
    const eastl::string_view ext = (dot == eastl::string_view::npos) ? name_or_path : name_or_path.substr(dot);
    if (ext.empty())
        return kNoExt;
    if (ext[0] == '.')
        return PackExt(ext);

    char buf[9];
    buf[0] = '.';
    const size_t n = ext.size() < 8 ? ext.size() : 8;
    std::memcpy(buf + 1, ext.data(), n);
    return PackExt(eastl::string_view(buf, n + 1));
}

}  // namespace tryeditor
