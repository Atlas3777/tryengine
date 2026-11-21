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
// live in `tryengine/Content`. Text can be consumed as a coroutine (`ReadText`) or as a
// pollable request (`RequestText`/`TextIsReady`/`TakeText`), which is what the loader and
// the game boot use.

require daslib/async_boost
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

// Drops a cached text so the next read comes from the current source. Reads in this
// module are always fresh; Save paths call this explicitly after writing.
def public Invalidate(path : string) {
    let id = ResolveAssetPath(AssetPath(path))
    if (id != 0ul) {
        InvalidateSourceText(id)
    }
}

// Editor-only: makes a loose file known to the registry when it came from a file dialog
// (an absolute path, or a file created after the last tree rebuild). The game never
// calls this: its paths are already in the pak.
def public RegisterPath(path : string) : uint64 {
    return RegisterAssetPath(AssetPath(path))
}

//! Starts an asynchronous text read and returns its asset id. `0` means the path is
//! unknown. The caller owns one reference and must finish with `TakeText` or
//! `CancelText`; this is the pollable form used by the loader and the game boot.
def public RequestText(path : string) : uint64 {
    let id = ResolveAssetPath(AssetPath(path))
    if (id == 0ul) {
        return 0ul
    }

    // Content is authored: a file may have been rewritten since the last read, and the
    // pak chunk is cheap enough to re-read. Never serve a stale document to a loader.
    InvalidateSourceText(id)
    RequestLoadSourceText(id)
    return id
}

def public TextIsReady(id : uint64) : bool {
    return id != 0ul && SourceTextIsReady(id)
}

def public TextIsFailed(id : uint64) : bool {
    return id != 0ul && SourceTextIsFailed(id)
}

//! Copies the text and releases the reference taken by `RequestText`.
def public TakeText(id : uint64) : string {
    if (id == 0ul) {
        return ""
    }
    var text = GetSourceText(id)
    ReleaseSourceText(id)
    return <- text
}

//! Releases a read that will not be consumed.
def public CancelText(id : uint64) {
    if (id == 0ul) {
        return
    }
    ReleaseSourceText(id)
    InvalidateSourceText(id)
}

// Full text of one asset. Returns "" when the path is unknown or the chunk failed:
// callers treat that as a missing/invalid file, never as an empty document.
[async]
def public ReadText(path : string) : string {
    let id = RequestText(path)
    if (id == 0ul) {
        yield ""
    }

    while (!TextIsReady(id)) {
        if (TextIsFailed(id)) {
            CancelText(id)
            yield ""
        }
        await_next_frame()
    }

    yield TakeText(id)
}
