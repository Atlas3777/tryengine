options gen2
options relaxed_assign = false

module Assets shared public

// Unified content storage. The script always asks with an authored path; the engine
// turns it into PathId(path) and finds either a loose file (editor) or a pak chunk
// (game). No script reads the disk directly, so the storage can be swapped later
// without touching Content/Scene/Prefab/Game.
//
// Authored paths are relative to the game directory: `content/...` and `data/...`, so
// `game/` is never written by game code. `game/...`, absolute paths and virtual
// `tryengine/...` entries are accepted as they are. Extensions are always explicit.
//
// This module is storage only: it resolves paths, exposes the text asynchronously and
// registers loose files from editor dialogs. Parsed documents and their dependencies
// live in `tryengine/Content`. Text is never cached: a request owns its text until
// `TakeText` or `CancelText`, because the caller parses it once and the parsed form is
// what stays. The pollable form (`RequestText`/`TextIsReady`/`TakeText`) is what the
// loader and the game boot use.

require strings
require tryResources

// Absolute path that contains `/game/content/` or `/game/data/` becomes the repo-relative
// path; an absolute path outside the game stays absolute.
def private RebaseAbsoluteGamePath(path : string; root : string) : string {
    let marker = "/{root}/"
    let marker_index = rfind(path, marker)
    if (marker_index < 0) {
        return path
    }
    return "{root}/{slice(path, marker_index + length(marker), length(path))}"
}

// Authored path -> repo-relative registry path. `content/...` and `data/...` are relative
// to the game directory, `game/...` and absolute paths are kept. A bare path is resolved
// against `fallback_root` ("content" or "data"); an empty root leaves it unresolved.
// The extension is never guessed or appended.
def public ResolveGamePath(path : string; fallback_root : string) : string {
    if (empty(path)) {
        return ""
    }

    var result = replace(path, "\\", "/")
    while (starts_with(result, "./")) {
        result = slice(result, 2, length(result))
    }

    result = RebaseAbsoluteGamePath(result, "game/content")
    result = RebaseAbsoluteGamePath(result, "game/data")

    if (starts_with(result, "game/") || starts_with(result, "tryengine/") || starts_with(result, "/")) {
        return result
    }
    if (starts_with(result, "content/") || starts_with(result, "data/")) {
        return "game/{result}"
    }
    if (empty(fallback_root)) {
        return result
    }
    return "game/{fallback_root}/{result}"
}

// Path the registry knows. Domain-relative `content/...` and `data/...` are accepted; a
// bare name is left alone because only a document kind knows its root (Content does that).
def private AssetPath(path : string) : string {
    return ResolveGamePath(path, "")
}

def public Exists(path : string) : bool {
    return ResolveAssetPath(AssetPath(path)) != 0ul
}

def public IdOf(path : string) : uint64 {
    return ResolveAssetPath(AssetPath(path))
}

// Editor-only: makes a loose file known to the registry when it came from a file dialog
// (an absolute path, or a file created after the last tree rebuild). The game never
// calls this: its paths are already in the pak.
def public RegisterPath(path : string) : uint64 {
    return RegisterAssetPath(AssetPath(path))
}

//! Starts an asynchronous text read and returns its request handle. `0` means the path
//! is unknown. The caller owns the text and must finish with `TakeText` or `CancelText`;
//! the text is freed with the request, so nothing survives the parse.
def public RequestText(path : string) : uint64 {
    let id = ResolveAssetPath(AssetPath(path))
    if (id == 0ul) {
        return 0ul
    }

    return RequestLoadText(id)
}

def public TextIsReady(handle : uint64) : bool {
    return handle != 0ul && TextRequestIsReady(handle)
}

def public TextIsFailed(handle : uint64) : bool {
    return handle != 0ul && TextRequestIsFailed(handle)
}

//! Copies the text and releases the request.
def public TakeText(handle : uint64) : string {
    if (handle == 0ul) {
        return ""
    }
    return TakeAssetText(handle)
}

//! Releases a request whose text will not be consumed.
def public CancelText(handle : uint64) {
    if (handle != 0ul) {
        CancelTextRequest(handle)
    }
}
