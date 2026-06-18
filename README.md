# Godot WinUI3 SwapChainPanel Fork

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

This fork of [Godot Engine](https://godotengine.org) adds an experimental
Windows embedding path for hosting Godot inside a WinUI3
`SwapChainPanel`.

The upstream Godot engine normally presents its D3D12 swap chain directly to a
Win32 `HWND`. This fork adds a WinUI3-specific path where a WinUI3 host process
loads Godot as a DLL, passes an `ISwapChainPanelNative*` to the engine, and lets
Godot render into a DXGI composition swap chain owned by the XAML visual tree.

## What This Fork Adds

Compared with upstream Godot master, this branch adds:

- A Windows SCons option: `winui3=yes`.
- D3D12 SwapChainPanel presentation using `CreateSwapChainForComposition` and
  `ISwapChainPanelNative::SetSwapChain`.
- Composition scale handling through `IDXGISwapChain2::SetMatrixTransform`, so
  a physical-pixel backbuffer fits the WinUI3 panel's DIP-sized layout area.
- A host-facing C ABI in `platform/windows/godot_winui3_embed.h` for engine
  lifecycle, panel binding, resize/DPI updates, input injection, logging, and
  host/engine calls.
- DisplayServer support for an embedded parent `HWND`, child-window sizing, and
  optional XAML-routed input.
- A `WinUI3Host` Godot singleton for JSON message passing between GDScript and
  the native WinUI3 host.
- A C# WinUI3 sample app under `platform/windows/winui3_sample`.

## Current Scope

This is not a general replacement for Godot's normal Windows platform port. It
is a fork for applications that need to embed a running Godot project inside a
WinUI3 XAML UI.

Supported path:

- Windows desktop.
- MSVC builds.
- D3D12 rendering.
- Godot built as a shared library.
- WinUI3 host apps that can pass an `ISwapChainPanelNative` pointer.

Important constraints:

- `winui3=yes` requires `library_type=shared_library`.
- `winui3=yes` requires D3D12.
- MinGW does not enable this feature; the build option is ignored there.
- Pre-initialization panel binding is intended for the main Godot window
  (`window_id = 0`).
- Host calls, engine calls, input injection, and `EngineIteration` should run
  on the same thread, normally the WinUI3 UI thread.

## Building

Install the normal Godot Windows build prerequisites, including the D3D12
dependencies:

```powershell
python misc/scripts/install_d3d12_sdk_windows.py
```

Build the Windows template DLL with WinUI3 support:

```powershell
scons platform=windows target=template_release arch=x86_64 d3d12=yes library_type=shared_library winui3=yes disable_path_overrides=no
```

`disable_path_overrides=no` is required when you want the host to launch the
engine with `--path <project_dir>` (loading a Godot project directory directly
instead of a packed `.pck`). It defaults to `yes` for `template_release`
builds, which strips that CLI argument out. Omit it if your host always passes
`--main-pack <file.pck>`.

The sample project expects the resulting DLL at:

```text
bin/godot.windows.template_release.x86_64.dll
```

and copies it next to the WinUI3 executable as `godot.dll`.

## Host Integration

The C ABI is declared in:

```text
platform/windows/godot_winui3_embed.h
```

The basic host order is:

1. Install an optional log callback with `godot_winui3_set_log_callback`.
2. Pass the WinUI3 window `HWND` with `godot_winui3_set_embedded_parent_hwnd`.
3. Call `godot_winui3_engine_setup`.
4. Pass the panel's `ISwapChainPanelNative*` with
   `godot_winui3_set_swap_chain_panel`.
5. Set initial composition scale and physical-pixel size with
   `godot_winui3_set_composition_scale` and
   `godot_winui3_notify_panel_resize`.
6. Call `godot_winui3_engine_start`.
7. Drive frames by calling `godot_winui3_engine_iteration`.
8. Call `godot_winui3_engine_shutdown` before the host exits.

For a C# host, the sample obtains the panel pointer with:

```csharp
var panelPtr = Marshal.GetComInterfaceForObject(
    GodotPanel,
    typeof(ISwapChainPanelNative));
```

## Input Modes

The fork exposes two input modes through `godot_winui3_set_input_mode`:

- `GODOT_WINUI3_INPUT_NATIVE`: Godot's Windows `WndProc` handles normal Win32
  mouse and keyboard messages. This is the default.
- `GODOT_WINUI3_INPUT_XAML`: Godot suppresses Win32 mouse/key messages for the
  embedded window. The WinUI3 host forwards XAML pointer/key events through
  `godot_winui3_inject_mouse_button`, `godot_winui3_inject_mouse_motion`,
  `godot_winui3_inject_mouse_wheel`, and `godot_winui3_inject_key`.

The sample app demonstrates the XAML injection path on a `SwapChainPanel`.

## Host And Engine Messages

This fork registers a Godot singleton named `WinUI3Host` when `winui3=yes` is
enabled. It provides a JSON message bridge:

```gdscript
WinUI3Host.send_to_host(method: StringName, args: Array = []) -> Variant
WinUI3Host.register_handler(method: StringName, handler: Callable) -> void
WinUI3Host.unregister_handler(method: StringName) -> void
WinUI3Host.has_handler(method: StringName) -> bool

signal host_message_received(method: StringName, args: Array)
```

On the host side, use `godot_winui3_call_engine` to call registered GDScript
handlers, and `godot_winui3_set_host_message_callback` to receive
`WinUI3Host.send_to_host` messages.

Payloads cross the native boundary as UTF-8 JSON. Return strings allocated by
`godot_winui3_call_engine` must be released with `godot_winui3_free_string`.

## Sample App

The sample WinUI3 host lives in:

```text
platform/windows/winui3_sample
```

It is a .NET 8 WinUI3 app using Windows App SDK 1.6. It hosts Godot in
`Views/MapViewPage.xaml` using a `SwapChainPanel`, drives the engine from a
`DispatcherQueueTimer`, forwards pointer/key events, and demonstrates JSON
message passing between the host and the Godot project.

After building the Godot DLL, open or build:

```text
platform/windows/winui3_sample/WinUI3Sample.csproj
```

The project copies `bin/godot.windows.template_release.x86_64.dll` as
`godot.dll` when that file exists.

## Project side-implementation:

You can use `Engine.get_singleton("WinUI3Host")` to retrieve a singleton object, that can be used to pass/receive message to/from the host.
```code
extends BaseInteractor
class_name WindowsWinUI3Interactor

var _host: Object = null

func _ready() -> void:
	if Engine.has_singleton("WinUI3Host"):
		_host = Engine.get_singleton("WinUI3Host")
		register_callbacks()
	else:
		push_warning("WindowsWinUI3Interactor: WinUI3Host singleton not found — running without host bridge")

func register_callbacks() -> void:
	if _host == null:
		return
	_host.call("register_handler", "response", _on_winui3_response)
	_host.call("register_handler", "error", _on_winui3_error)
	_host.call("register_handler", "config_change", _on_winui3_config_change)

func unregister_callbacks() -> void:
	if _host == null:
		return
	_host.call("unregister_handler", "response")
	_host.call("unregister_handler", "error")
	_host.call("unregister_handler", "config_change")

func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		unregister_callbacks()

func _on_winui3_response(mainCmd: String, subCmd: String, json: String) -> void:
	_on_response(mainCmd, subCmd, json)

func _on_winui3_error(mainCmd: String, subCmd: String, json: String) -> void:
	_on_error(mainCmd, subCmd, json)

func _on_winui3_config_change(json: String) -> void:
	_on_configuration_changed(json)

func request_data(mainCmd: String, subCmd: String, json: String) -> void:
	if _host == null:
		return
	_host.call("send_to_host", "request_data", [mainCmd, subCmd, json])

func get_string(id: String, args: Array) -> String:
	if _host == null:
		return ""
	var result = _host.call("send_to_host", "get_string", [id, args])
	if result is String:
		return result
	return ""

func get_quantity_string(id: String, quantity: int, args: Array) -> String:
	if _host == null:
		return ""
	var result = _host.call("send_to_host", "get_quantity_string", [id, quantity, args])
	if result is String:
		return result
	return ""

func print_log(level: String, tag: String, message: String) -> void:
	if _host == null:
		return
	_host.call("send_to_host", "print_log", [level, tag, message])
```

## Key Files

- `platform/windows/godot_winui3_embed.h`
- `platform/windows/godot_winui3_embed.cpp`
- `platform/windows/winui3_host_bridge.h`
- `platform/windows/winui3_host_bridge.cpp`
- `platform/windows/display_server_windows.*`
- `drivers/d3d12/rendering_context_driver_d3d12.*`
- `drivers/d3d12/rendering_device_driver_d3d12.cpp`
- `platform/windows/winui3_sample/Interop/godot_winui3_embed.cs`
- `platform/windows/winui3_sample/Views/MapViewPage.xaml.cs`

## Upstream Godot

Godot is a feature-packed, cross-platform 2D and 3D game engine distributed
under the permissive [MIT license](https://godotengine.org/license).

For general Godot documentation, builds, community links, and contribution
guidelines, see:

- [Godot website](https://godotengine.org)
- [Godot documentation](https://docs.godotengine.org)
- [Contributing guide](CONTRIBUTING.md)
- [Official Godot repository](https://github.com/godotengine/godot)
