#!/usr/bin/env python

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def assert_contains(text, needle, path):
    if needle not in text:
        raise AssertionError(f"{path} is missing {needle!r}")


def assert_not_contains(text, needle, path):
    if needle in text:
        raise AssertionError(f"{path} still contains {needle!r}")


def main():
    libgodot_h = read("core/extension/libgodot.h")
    native_cs = read("platform/windows/WinUI3_Project/Godot.WinUI3.Embedding/Interop/GodotWinUI3Native.cs")
    wrapper_cs = read("platform/windows/WinUI3_Project/Godot.WinUI3.Embedding/Interop/GodotWinUI3Embed.cs")

    required_header_symbols = [
        "libgodot_set_native_window",
        "libgodot_attach_surface",
        "libgodot_detach_surface",
        "libgodot_surface_set_size",
        "libgodot_surface_set_scale",
        "libgodot_inject_input_event",
        "LibGodotInputEvent",
    ]
    for symbol in required_header_symbols:
        assert_contains(libgodot_h, symbol, "core/extension/libgodot.h")

    required_imports = [
        "libgodot_attach_surface",
        "libgodot_detach_surface",
        "libgodot_surface_set_size",
        "libgodot_surface_set_scale",
        "libgodot_inject_input_event",
    ]
    for symbol in required_imports:
        assert_contains(native_cs, symbol, "GodotWinUI3Native.cs")

    for path, text in [
        ("GodotWinUI3Native.cs", native_cs),
        ("GodotWinUI3Embed.cs", wrapper_cs),
    ]:
        assert_not_contains(text, "godot_winui3_set_swap_chain_panel", path)
        assert_not_contains(text, "godot_winui3_inject_mouse_", path)
        assert_not_contains(text, "godot_winui3_inject_key", path)


if __name__ == "__main__":
    main()
