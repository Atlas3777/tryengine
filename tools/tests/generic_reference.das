options gen2
options relaxed_assign = false

require daslib/enum_trait

//! `var x : auto(T)` is a VALUE parameter in daslang: writes go into a copy and the caller keeps
//! its old value. A generic helper that must change the caller's object needs `auto(T)&`.
//!
//! The Import Settings form was written with `var field : auto(T)` / `var value : auto(T)`, so the
//! form drew every widget but silently dropped every edit: the combo box looked dead. This test
//! pins the language rule the form now depends on, including the enum path of the up-axis combo.

enum Axis {
    Y
    Z
    X
}

struct Settings {
    text   : string = "keep"
    number : int = 7
    axis   : Axis = Axis.Y
}

var g_failures = 0

def private Check(condition : bool; what : string) {
    if (!condition) {
        print("FAIL: {what}\n")
        g_failures++
    }
}

[generic]
def private ClearByValue(var field : auto(TT)) {
    field = default<TT>
}

[generic]
def private ClearByRef(var field : auto(TT)&) {
    field = default<TT>
}

//! The shape the form uses: a generic over a whole struct by reference, then a field by reference.
[generic]
def private ClearNumber(var value : auto(TT)&) {
    ClearByRef(value.number)
}

//! The enum branch of the form: walk the values with `each` and write the picked one through.
[generic]
def private PickEnum(var field : auto(TT)&; index : int) {
    var i = 0
    for (value in each(field)) {
        if (i == index) {
            field = value
        }
        i++
    }
}

[export]
def main() : int {
    var by_value = Settings()
    ClearByValue(by_value.text)
    Check(by_value.text == "keep", "a `var x : auto(T)` parameter is a copy, the caller keeps its value")

    var by_ref = Settings()
    ClearByRef(by_ref.text)
    Check(by_ref.text == "", "a `var x : auto(T)&` parameter writes through to the caller")

    ClearByRef(by_ref.number)
    Check(by_ref.number == 0, "references work for numeric fields")

    by_ref.axis = Axis.X
    ClearByRef(by_ref.axis)
    Check(by_ref.axis == Axis.Y, "references work for enum fields")

    var nested = Settings()
    ClearNumber(nested)
    Check(nested.number == 0, "a field reference stays a reference through a generic struct parameter")

    var picked = Settings()
    PickEnum(picked.axis, 2)
    Check(picked.axis == Axis.X, "the enum combo path writes the picked value into the setting")

    if (g_failures != 0) {
        return 1
    }
    print("GENERIC REFERENCE OK\n")
    return 0
}
