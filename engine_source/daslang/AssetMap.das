options gen2
options relaxed_assign = false

module AssetMap shared public

//! Runtime alias table of one screen: a flat `key -> (kind, id)` map. This is the only
//! asset-facing object game and UI code see; the authored `Bundle` (paths, refs, parents,
//! prefabs) stays inside the loader.
//!
//! The map is produced once per request by `Bundle::Flatten`, after the bundle hierarchy is
//! linked, and registered in the process-owned registry below. `AssetMap` itself is a small
//! copyable handle, so a screen stack can store it and a widget can call `assets.Texture("logo")`
//! without looking a path up and without copying the table. A typed getter returns `0` for an
//! unknown key or a key of another kind; the caller reports the typo.

//! One resolved alias.
struct public AssetEntry {
    kind : string
    id : uint64
}

//! Handle of one registered alias table. `0` is the empty map.
struct public AssetMap {
    id : uint64 = 0ul
}

//! Stored alias table; the public `AssetMap` is only the handle.
struct private AssetTable {
    by_name : table<string; AssetEntry>
}

var private g_tables : table<uint64; AssetTable>
var private g_next : uint64 = 1ul

//! Registers a flattened alias table and returns its handle. The table is moved into the
//! registry, so the caller must not use it afterwards.
def public Register(var aliases : table<string; AssetEntry>) : AssetMap {
    let id = g_next
    g_next = g_next + 1ul
    var stored : AssetTable
    stored.by_name <- aliases
    g_tables[id] <- stored
    return AssetMap(id = id)
}

def private Lookup(self : AssetMap; key : string; kind : string) : uint64 {
    if (self.id == 0ul || !key_exists(g_tables, self.id)) {
        return 0ul
    }
    var entry : AssetEntry
    var found = false
    unsafe {
        let value = g_tables[self.id].by_name?[key]
        if (value != null) {
            entry = *value
            found = true
        }
    }
    if (!found || entry.kind != kind) {
        return 0ul
    }
    return entry.id
}

//! Runtime id of a texture asset, `0` when the key is missing or is not a texture.
def public Texture(self : AssetMap; key : string) : uint64 {
    return Lookup(self, key, "texture")
}

//! Runtime id of a font asset.
def public Font(self : AssetMap; key : string) : uint64 {
    return Lookup(self, key, "font")
}

//! Runtime id of a material asset.
def public Material(self : AssetMap; key : string) : uint64 {
    return Lookup(self, key, "material")
}

//! Runtime id of a mesh asset.
def public Mesh(self : AssetMap; key : string) : uint64 {
    return Lookup(self, key, "mesh")
}

//! True when the key exists, whatever its kind.
def public Has(self : AssetMap; key : string) : bool {
    if (self.id == 0ul || !key_exists(g_tables, self.id)) {
        return false
    }
    return key_exists(g_tables[self.id].by_name, key)
}

//! Kind of the key (`""` when absent); diagnostics and pickers.
def public KindOf(self : AssetMap; key : string) : string {
    if (self.id == 0ul || !key_exists(g_tables, self.id)) {
        return ""
    }
    var entry : AssetEntry
    var found = false
    unsafe {
        let value = g_tables[self.id].by_name?[key]
        if (value != null) {
            entry = *value
            found = true
        }
    }
    if (!found) {
        return ""
    }
    return entry.kind
}

//! Number of aliases in the map.
def public Count(self : AssetMap) : int {
    if (self.id == 0ul || !key_exists(g_tables, self.id)) {
        return 0
    }
    return length(g_tables[self.id].by_name)
}
