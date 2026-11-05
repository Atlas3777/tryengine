#include "engine/core/GameScriptFileAccess.hpp"

#include <EASTL/hash_set.h>
#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::core {

namespace {

constexpr const char* kRootMark = "%/";

eastl::string Stem(const eastl::string& path) {
    size_t slash = path.find_last_of('/');
    eastl::string name = slash == eastl::string::npos ? path : path.substr(slash + 1);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".das")
        name.resize(name.size() - 4);
    return name;
}

bool EndsWith(const eastl::string& s, const eastl::string& suf) {
    return s.size() >= suf.size() && std::memcmp(s.data() + s.size() - suf.size(), suf.data(), suf.size()) == 0;
}

// "daslib/strings_boost" -> "/daslib/strings_boost.das"; "./a/b.das" -> "/a/b.das"
eastl::string ReqSuffix(const eastl::string& req) {
    size_t i = 0;
    while (i < req.size() && (req[i] == '.' || req[i] == '%' || req[i] == '/')) {
        // пропускаем только ведущие "./", "../", "%/"
        if (req[i] == '.' && i + 1 < req.size() && req[i + 1] != '.' && req[i + 1] != '/')
            break;
        ++i;
    }
    eastl::string s = req.substr(i);
    for (auto& c : s)
        if (c == '.')
            c = '/';
    // вернуть расширение, если оно было в исходном req
    if (EndsWith(req, ".das")) {
        s.resize(s.size() - 4);  // "x/das" -> "x"
        s.append(".das");
    } else {
        s.append(".das");
    }
    return eastl::string("/") + s;
}

}  // namespace

eastl::string ToPakName(const das::string& path) {
    const das::string root = das::getDasRoot() + "/";
    if (path.size() > root.size() && path.compare(0, root.size(), root) == 0)
        return eastl::string(kRootMark) + (path.c_str() + root.size());
    return eastl::string(path.c_str());
}

das::string FromPakName(const eastl::string& n) {
    if (n.size() >= 2 && n[0] == '%' && n[1] == '/')
        return das::getDasRoot() + "/" + (n.c_str() + 2);
    return das::string(n.c_str());
}

Result<ScriptPak> CollectScriptPak(das::FileAccess& access, das::ModuleGroup& group,
                                   const das::string& entry,
                                   const eastl::vector<das::string>& extra = {}) {
    eastl::vector<das::string> names;
    names.push_back(entry);
    for (auto* mod : group.getModules())
        if (!mod->fileName.empty())
            names.push_back(mod->fileName);
    for (const auto& e : extra)          // дубликаты отсеет hash_set seen
        names.push_back(e);

    ScriptPak pak;
    pak.entry_index = 0;
    eastl::hash_set<eastl::string> seen;

    for (const auto& name : names) {
        eastl::string pak_name = ToPakName(name);
        if (!seen.insert(pak_name).second)
            continue;

        das::FileInfo* info = access.getFileInfo(name);
        if (!info)
            return LogAndMakeError("CollectScriptPak: не удалось прочитать '{}'", name.c_str());

        const char* src = nullptr;
        uint32_t len = 0;
        info->getSourceAndLength(src, len);

        ScriptPakFile f;
        f.name = eastl::move(pak_name);
        f.data.assign(reinterpret_cast<const uint8_t*>(src), reinterpret_cast<const uint8_t*>(src) + len);

        LogInfo("Collect f.name: {}", f.name);
        pak.files.push_back(eastl::move(f));
    }
    return pak;
}

PackedFileAccess::PackedFileAccess(ScriptPak pak) : pak_(eastl::move(pak)) {
    paths_.reserve(pak_.files.size());
    for (size_t i = 0; i < pak_.files.size(); ++i) {
        eastl::string full = FromPakName(pak_.files[i].name).c_str();
        by_path_[full] = i;
        by_stem_[Stem(full)].push_back(i);
        paths_.push_back(eastl::move(full));
    }
    entry_path_ = paths_[pak_.entry_index];
}

das::ModuleInfo PackedFileAccess::getModuleInfo(const das::string& req, const das::string& from) const {
    das::ModuleInfo info;

    eastl::string r(req.c_str());
    size_t cut = r.find_last_of("./");
    eastl::string mod_name = cut == eastl::string::npos ? r : r.substr(cut + 1);
    if (EndsWith(r, ".das"))
        mod_name = Stem(r);

    auto it = by_stem_.find(mod_name);
    if (it == by_stem_.end())
        return das::FileAccess::getModuleInfo(req, from);

    size_t pick = it->second.front();
    if (it->second.size() > 1) {
        const eastl::string suf = ReqSuffix(r);
        for (size_t idx : it->second)
            if (EndsWith(paths_[idx], suf)) {
                pick = idx;
                break;
            }
    }

    // LogInfo("info.moduleName = {}", mod_name.c_str());
    // LogInfo("info.fileName = {}", paths_[pick].c_str());

    info.moduleName = mod_name.c_str();
    info.fileName = paths_[pick].c_str();
    return info;
}

das::FileInfo* PackedFileAccess::getNewFileInfo(const das::string& file_name) {
    if (locked)
        return nullptr;
    auto it = by_path_.find(eastl::string(file_name.c_str()));
    if (it == by_path_.end())
        return nullptr;

    const auto& d = pak_.files[it->second].data;
    if (d.empty())
        return setFileInfo(file_name, das::FileInfoPtr(new das::TextFileInfo("", 0, false)));

    // TextFileInfo владеет выровненной копией (как в FsFileSystem)
    char* src = static_cast<char*>(das_aligned_alloc16(d.size()));
    std::memcpy(src, d.data(), d.size());
    return setFileInfo(file_name, das::FileInfoPtr(new das::TextFileInfo(src, uint32_t(d.size()), true)));
}

}  // namespace tryengine::core