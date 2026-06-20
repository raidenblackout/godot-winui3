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
    native_cs = read("platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding/Interop/GodotWindowsEmbedNative.cs")
    wrapper_cs = read("platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding/Interop/GodotWindowsEmbedEmbed.cs")
    enums_cs = read("platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding/Interop/GodotWindowsEmbedEnums.cs")
    engine_host_cs = read("platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding/GodotEngineHost.cs")
    receiver_cs = read("platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding/Communication/EngineMessageReceiver.cs")
    map_page_cs = read("platform/windows/WindowsEmbed_Project/windows_embed_sample/Views/MapViewPage.xaml.cs")
    platform_scsub = read("platform/windows/SCsub")
    d3d12_scsub = read("drivers/d3d12/SCsub")
    display_server_windows_cpp = read("platform/windows/display_server_windows.cpp")
    legacy_feature = "win" + "ui3"
    legacy_pascal = "Win" + "UI3"
    legacy_macro = "WIN" + "UI3"
    legacy_abi_prefix = "godot_" + legacy_feature

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
        assert_contains(native_cs, symbol, "GodotWindowsEmbedNative.cs")

    for path, text in [
        ("GodotWindowsEmbedNative.cs", native_cs),
        ("GodotWindowsEmbedEmbed.cs", wrapper_cs),
        ("GodotWindowsEmbedEnums.cs", enums_cs),
        ("GodotEngineHost.cs", engine_host_cs),
        ("EngineMessageReceiver.cs", receiver_cs),
        ("MapViewPage.xaml.cs", map_page_cs),
        ("platform/windows/SCsub", platform_scsub),
        ("drivers/d3d12/SCsub", d3d12_scsub),
    ]:
        assert_not_contains(text, legacy_feature, path)
        assert_not_contains(text, legacy_pascal, path)
        assert_not_contains(text, legacy_macro, path)
        assert_not_contains(text, legacy_abi_prefix + "_set_swap_chain_panel", path)
        assert_not_contains(text, legacy_abi_prefix + "_inject_mouse_", path)
        assert_not_contains(text, legacy_abi_prefix + "_inject_key", path)
        assert_not_contains(text, "godot_windows_embed_embed", path)
        assert_not_contains(text, "windows_" + "embed_host_bridge", path)

    assert_contains(display_server_windows_cpp, "_windows_embed_uses_xaml_input()", "display_server_windows.cpp")
    assert_contains(
        display_server_windows_cpp,
        "if (p_mode == DisplayServerEnums::MOUSE_MODE_CAPTURED && !windows_embed_xaml_input)",
        "display_server_windows.cpp",
    )
    assert_contains(enums_cs, "GodotWindowsEmbedInputMode", "GodotWindowsEmbedEnums.cs")
    assert_contains(engine_host_cs, "SetInputMode(GodotWindowsEmbedInputMode.Xaml)", "GodotEngineHost.cs")
    assert_contains(receiver_cs, "OnSynchronousMessage", "EngineMessageReceiver.cs")
    assert_contains(map_page_cs, "OnSynchronousHostMessage", "MapViewPage.xaml.cs")
    assert_contains(map_page_cs, 'case "get_devices":', "MapViewPage.xaml.cs")


if __name__ == "__main__":
    main()
