options gen2
options relaxed_assign = false

module AssetCache shared public

//! Ready (or almost ready) content values: one shared ledger of paths plus one typed value
//! table per asset kind.
//!
//! `g_meta` holds the state machine (read text once, parse, compile), the dependencies and
//! the text-request handle for every asset, whatever its type. The value itself lives in
//! the kind's own `table<string; T?>`, so nothing is erased or cast: the kind passes its
//! table to the generic `Advance`/`Store`/`Clear`.
//!
//! The kind owns its own public entry points (`Scene::Request`, `Prefab::Request`,
//! `Bundle::Request` and the matching `Advance`/`Cancel`); there is no registry of kinds and
//! no string dispatch. A caller that needs every kind names them, and a new kind is a
//! compile error until it is named.
//!
//! The value type is inferred from the table (`table<string; auto(TT)?>`), so no
//! `type<...>` expression is passed at runtime; that keeps the lint rule "a type tag may
//! be passed but not read" out of every call.

require daslib/async_boost
require strings
require tryengine/Assets
require tryengine/Content

//! State of one cached asset. `Loading` means the text is being read or the value is being
//! compiled; `Ready` means the kind's table has the value; `Failed` is terminal until a new
//! `Request` (or `Refresh`) retries it.
enum public AssetState {
    Loading
    Ready
    Failed
}

//! One ledger row: kind for diagnostics and for `Clear`, the state machine and the
//! dependencies collected while parsing. Written only by this module; kind modules just own
//! the value tables.
struct public AssetMeta {
    kind   : string
    state  : AssetState
    deps   : Content::Dependencies
    handle : uint64
}

// One row per canonical path. Paths identify assets uniquely: the domains (`content`,
// `data`) and the extensions do not overlap between kinds.
var private g_meta : table<string; AssetMeta>

//! Starts loading `path` unless the ledger already knows it. Returns false only when the
//! storage layer does not know the path; a failed record is retried, because a read error
//! may be transient and nothing was stored for it. `kind` is a label: it is what `Clear`
//! filters by and what a diagnostic prints.
def public Request(kind : string; path : string) : bool {
    if (key_exists(g_meta, path)) {
        if (g_meta[path].state != AssetState.Failed) {
            return true
        }
        erase(g_meta, path)
    }

    var meta : AssetMeta
    meta.kind = kind
    let handle = Assets::RequestText(path)
    if (handle == 0ul) {
        meta.state = AssetState.Failed
        g_meta[path] <- meta
        return false
    }

    meta.state = AssetState.Loading
    meta.handle = handle
    g_meta[path] <- meta
    return true
}

//! Drops a cached record and starts a fresh read. The editor uses this after a file was
//! overwritten outside the save path.
def public Refresh(var values : table<string; auto(TT)?>; kind : string; path : string) : bool {
    Invalidate(values, path)
    return Request(kind, path)
}

//! One step of the state machine: takes the text when it arrived, parses the document and
//! compiles the value into the kind's table. Safe to call every frame; a `Ready`/`Failed`
//! record is unchanged. `items_field` and `compile` belong to the kind and are passed by its
//! `Advance` entry point.
def public Advance(var values : table<string; auto(TT)?>; path : string; items_field : string;
                   compile : function<(var document : Content::Document?) : TT?>) : AssetState {
    if (!key_exists(g_meta, path)) {
        return AssetState.Failed
    }
    if (g_meta[path].state != AssetState.Loading) {
        return g_meta[path].state
    }

    if (Assets::TextIsFailed(g_meta[path].handle)) {
        Assets::CancelText(g_meta[path].handle)
        g_meta[path].handle = 0ul
        g_meta[path].state = AssetState.Failed
        return AssetState.Failed
    }
    if (!Assets::TextIsReady(g_meta[path].handle)) {
        return AssetState.Loading
    }

    let text = Assets::TakeText(g_meta[path].handle)
    g_meta[path].handle = 0ul

    var document = Content::Parse(path, text, items_field)
    if (document == null) {
        g_meta[path].state = AssetState.Failed
        return AssetState.Failed
    }

    // Dependencies are copied before `compile` takes the document: a prefab's compiled value
    // consumes its tree, and the ledger still has to answer `Dependencies`.
    clone(g_meta[path].deps.meshes, document.deps.meshes)
    clone(g_meta[path].deps.materials, document.deps.materials)
    clone(g_meta[path].deps.textures, document.deps.textures)
    clone(g_meta[path].deps.fonts, document.deps.fonts)

    var value = invoke(compile, document)
    if (value == null) {
        g_meta[path].state = AssetState.Failed
        return AssetState.Failed
    }

    values[path] <- value
    g_meta[path].state = AssetState.Ready
    return AssetState.Ready
}

//! Stores a value that was just produced synchronously (editor save paths), so a following
//! read sees it without a read. `value` is moved into the table.
def public Store(var values : table<string; auto(TT)?>; kind : string; path : string; var value : TT?;
                 deps : Content::Dependencies) {
    var meta : AssetMeta
    meta.kind = kind
    meta.state = value != null ? AssetState.Ready : AssetState.Failed
    clone(meta.deps.meshes, deps.meshes)
    clone(meta.deps.materials, deps.materials)
    clone(meta.deps.textures, deps.textures)
    clone(meta.deps.fonts, deps.fonts)
    g_meta[path] <- meta
    values[path] <- value
}

def public State(path : string) : AssetState {
    if (!key_exists(g_meta, path)) {
        return AssetState.Failed
    }
    return g_meta[path].state
}

def public Has(path : string) : bool {
    return key_exists(g_meta, path)
}

def public Dependencies(path : string) : Content::Dependencies {
    var deps : Content::Dependencies
    if (key_exists(g_meta, path)) {
        clone(deps.meshes, g_meta[path].deps.meshes)
        clone(deps.materials, g_meta[path].deps.materials)
        clone(deps.textures, g_meta[path].deps.textures)
        clone(deps.fonts, g_meta[path].deps.fonts)
    }
    return <- deps
}

//! Drops a record (loaded or not). A pending text request is cancelled.
def public Invalidate(var values : table<string; auto(TT)?>; path : string) {
    if (!key_exists(g_meta, path)) {
        return
    }

    if (g_meta[path].handle != 0ul) {
        Assets::CancelText(g_meta[path].handle)
        g_meta[path].handle = 0ul
    }
    erase(g_meta, path)
    erase(values, path)
}

//! Drops only an asset that is still loading: one that already became ready stays cached
//! when a request is aborted. This is what `Loader::Abort` needs.
def public Cancel(var values : table<string; auto(TT)?>; path : string) {
    if (!key_exists(g_meta, path)) {
        return
    }

    if (g_meta[path].state != AssetState.Loading) {
        return
    }
    if (g_meta[path].handle != 0ul) {
        Assets::CancelText(g_meta[path].handle)
        g_meta[path].handle = 0ul
    }
    erase(g_meta, path)
    erase(values, path)
}

//! Drops every record of one kind (ready or failed) from the ledger and its value table.
def public Clear(var values : table<string; auto(TT)?>; kind : string) {
    for (key in keys(g_meta)) {
        if (g_meta[key].kind == kind) {
            Invalidate(values, key)
        }
    }
}
