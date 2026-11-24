options gen2
options relaxed_assign = false

module EditorCommands shared public

//! Selection-bound editor commands: undo/redo, delete, copy, cut and paste.
//!
//! The history itself lives in `tryeditor/UndoRedo`, which knows nothing about the current
//! selection; this module is the only place that combines the two: every command here reads
//! `selected_eid`, and every command that changes the world moves the selection to whatever
//! the history reports.
//!
//! Hotkeys are registered once from `[init]`, the same way `Hotkey` reload is, so the Edit
//! menu and the keyboard run exactly the same function.

require daslib/decs
require tryengine/Input
require tryeditor/EditorHotkeys
require tryeditor/Hierarchy
require tryeditor/UndoRedo

def private ApplyFocus(focus : EntityId) {
    if (focus != INVALID_ENTITY_ID) {
        selected_eid = focus
    } elif (!decs::is_alive(selected_eid)) {
        // The undo deleted the selected entity (or its record is gone): leave the Inspector
        // with nothing selected instead of a dead id.
        selected_eid = INVALID_ENTITY_ID
    }
}

def public CanModifySelection() : bool => !UndoRedo::IsSuspended() && decs::is_alive(selected_eid)

def public UndoCommand() {
    ApplyFocus(UndoRedo::Undo())
}

def public RedoCommand() {
    ApplyFocus(UndoRedo::Redo())
}

def public CopyCommand() {
    if (CanModifySelection()) {
        UndoRedo::CopyEntity(selected_eid)
    }
}

//! Copy and delete in one step. The copy itself is not an undo step, so one Ctrl+Z brings the
//! entity back.
def public CutCommand() {
    if (CanModifySelection() && UndoRedo::CopyEntity(selected_eid)) {
        if (UndoRedo::DeleteSubtree(selected_eid)) {
            selected_eid = INVALID_ENTITY_ID
        }
    }
}

def public PasteCommand() {
    let eid = UndoRedo::PasteClipboard()
    if (eid != INVALID_ENTITY_ID) {
        selected_eid = eid
    }
}

def public DeleteCommand() {
    if (UndoRedo::DeleteSubtree(selected_eid)) {
        selected_eid = INVALID_ENTITY_ID
    }
}

[init]
def private RegisterCommandHotkeys() {
    RegisterHotkey(Key.Z, @@UndoCommand, "Undo", ctrl = true)
    RegisterHotkey(Key.Y, @@RedoCommand, "Redo", ctrl = true)
    RegisterHotkey(Key.Z, @@RedoCommand, "Redo", ctrl = true, shift = true)
    RegisterHotkey(Key.C, @@CopyCommand, "Copy entity", ctrl = true)
    RegisterHotkey(Key.V, @@PasteCommand, "Paste entity", ctrl = true)
    RegisterHotkey(Key.X, @@CutCommand, "Cut entity", ctrl = true)
    RegisterHotkey(Key.Delete, @@DeleteCommand, "Delete entity")
}
