# Godot WindowsEmbed Fork

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

This fork of [Godot Engine](https://godotengine.org) adds an experimental
Windows embedding path. It is not tied to one Windows UI framework: any Windows
host that can provide the required native handles can load Godot as a DLL,
drive the engine, and render a Godot project inside a host-owned surface.

The current sample uses a XAML `SwapChainPanel`, but the branch is named
WindowsEmbed because the engine-side feature is general to Windows embedding.

## What This Fork Adds

Compared with upstream Godot, this branch adds:

- A Windows SCons option: `windows_embed=yes`.
- A shared-library Windows build that exposes the `libgodot_*` embedding ABI.
- D3D12 composition swap-chain presentation for host-owned surfaces.
- `ISwapChainPanelNative` binding for XAML hosts.
- Composition scale handling through `IDXGISwapChain2::SetMatrixTransform`, so
  a physical-pixel backbuffer fits a DIP-sized XAML layout area.
- DisplayServer support for an embedded parent window, surface resize/DPI
  updates, and optional host-routed input.
- A `WindowsEmbedHost` Godot singleton for bidirectional JSON messaging between
  GDScript and the native Windows host.
- A .NET 8 sample host under
  `platform/windows/WindowsEmbed_Project`.

## Current Scope

This is not a replacement for Godot's normal Windows platform port. It is for
applications that need to embed a running Godot project inside a native Windows
application.

Supported path:

- Windows desktop.
- MSVC builds.
- D3D12 rendering.
- Godot built as a shared library.
- A host application that can supply a native parent window and a render
  surface. The included sample uses a XAML `SwapChainPanel`.

Important constraints:

- `windows_embed=yes` requires `library_type=shared_library`.
- `windows_embed=yes` requires D3D12.
- MinGW does not enable this feature; the option is ignored there.
- The pre-start surface binding path is intended for the main Godot window
  (`window_id = 0`).
- Engine lifecycle, rendering iterations, input injection, and host/engine
  calls are run on the sample's dedicated engine thread via `GodotEngineHost`.

## Building

Install the normal Godot Windows build prerequisites, including the D3D12
dependencies:

```powershell
python misc/scripts/install_d3d12_sdk_windows.py
```

Build the Windows template DLL with WindowsEmbed support:

```powershell
python -m SCons platform=windows target=template_release arch=x86_64 d3d12=yes library_type=shared_library windows_embed=yes disable_path_overrides=no
```

`disable_path_overrides=no` is useful when the host launches the engine with a
project directory. If the host only passes `--main-pack <file.pck>`, this flag is
less important, but the sample/test workflow in this branch uses it.

The sample expects the engine DLL at:

```text
bin/godot.windows.template_release.x86_64.dll
```

Build the sample host with:

```powershell
dotnet build platform/windows/WindowsEmbed_Project/WindowsEmbedSample.sln -p:GodotWindowsEmbedEngineDll=E:/Projects/2dog-4.7/bin/godot.windows.template_release.x86_64.dll
```

Adjust the absolute DLL path if your checkout is somewhere else. The sample
copies the engine DLL next to the executable as `godot.dll`.

## Host Architecture

The C ABI is declared in `core/extension/libgodot.h` and included by
`platform/windows/godot_windows_embed.h`. The C# wrapper lives in:

```text
platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding
```

The sample host uses `GodotEngineHost` to run Godot on a dedicated background
thread. The sample page owns the XAML controls; engine work is queued to the
engine thread before each `libgodot_engine_iteration()` call.

Basic host order:

1. Install a log callback with `libgodot_set_log_callback`.
2. Pass the host window with `libgodot_set_embedded_parent_window`.
3. Install the UI dispatcher with `libgodot_set_ui_dispatcher`.
4. Select XAML-routed input with `libgodot_set_input_mode`.
5. Call `libgodot_engine_setup`.
6. Attach the render surface with `libgodot_attach_surface`.
7. Set the initial surface scale and size.
8. Call `libgodot_engine_start`.
9. Drive frames with `libgodot_engine_iteration`.
10. Call `libgodot_engine_shutdown` before the host exits.

For a C# XAML host, the sample obtains the panel pointer with:

```csharp
var panelPtr = Marshal.GetComInterfaceForObject(
    GodotPanel,
    typeof(ISwapChainPanelNative));
```

## Input Modes

The fork exposes two input modes through `libgodot_set_input_mode`:

- `GODOT_WINDOWS_EMBED_INPUT_NATIVE`: Godot's Windows `WndProc` handles normal
  Win32 mouse and keyboard messages. This is the default.
- `GODOT_WINDOWS_EMBED_INPUT_XAML`: Godot suppresses Win32 mouse/key messages
  for the embedded window. The host forwards pointer/key events through
  `libgodot_inject_input_event`.

The sample uses XAML-routed input. This matters for right-drag camera controls:
embedded projects should not switch Godot to `Input.MOUSE_MODE_CAPTURED` while
the XAML host is injecting absolute pointer positions. The native side also
avoids captured-mouse recentering in XAML input mode, which prevents the cursor
from snapping to the center of the screen during right-click drag.

## Host And Engine Messages

When `windows_embed=yes` is enabled, Godot registers an Engine singleton named
`WindowsEmbedHost`:

```gdscript
WindowsEmbedHost.send_to_host(method: StringName, args: Array = []) -> Variant
WindowsEmbedHost.register_handler(method: StringName, handler: Callable) -> void
WindowsEmbedHost.unregister_handler(method: StringName) -> void
WindowsEmbedHost.has_handler(method: StringName) -> bool

signal host_message_received(method: StringName, args: Array)
```

Payloads cross the native boundary as UTF-8 JSON arrays. Return values are JSON
values. The bridge supports two useful patterns.

### Direct RPC-style calls

GDScript can call the host and use the return value immediately:

```gdscript
var devices: Variant = WindowsEmbedHost.send_to_host("get_devices", [])
```

The sample host handles this through `EngineMessageReceiver.OnSynchronousMessage`
and returns JSON for direct commands such as `get_devices`, `get_rooms`,
`get_locations`, and `get_host_time`.

### Async request/response calls

For project-side interactors, GDScript can register response handlers and send a
request to the host:

```gdscript
extends BaseInteractor
class_name WindowsEmbedInteractor

var _host: Object = null

func _ready() -> void:
	if Engine.has_singleton("WindowsEmbedHost"):
		_host = Engine.get_singleton("WindowsEmbedHost")
		register_callbacks()
	else:
		push_warning("WindowsEmbedInteractor: WindowsEmbedHost singleton not found; running without host bridge")

func register_callbacks() -> void:
	if _host == null:
		return
	_host.call("register_handler", "response", _on_windows_embed_response)
	_host.call("register_handler", "error", _on_windows_embed_error)
	_host.call("register_handler", "config_change", _on_windows_embed_config_change)

func unregister_callbacks() -> void:
	if _host == null:
		return
	_host.call("unregister_handler", "response")
	_host.call("unregister_handler", "error")
	_host.call("unregister_handler", "config_change")

func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		unregister_callbacks()

func _on_windows_embed_response(main_cmd: String, sub_cmd: String, json: String) -> void:
	_on_response(main_cmd, sub_cmd, json)

func _on_windows_embed_error(main_cmd: String, sub_cmd: String, json: String) -> void:
	_on_error(main_cmd, sub_cmd, json)

func _on_windows_embed_config_change(json: String) -> void:
	_on_configuration_changed(json)

func request_data(main_cmd: String, sub_cmd: String, json: String) -> void:
	if _host == null:
		return
	_host.call("send_to_host", "request_data", [main_cmd, sub_cmd, json])

func get_string(id: String, args: Array) -> String:
	if _host == null:
		return ""
	var result: Variant = _host.call("send_to_host", "get_string", [id, args])
	return result if result is String else ""

func get_quantity_string(id: String, quantity: int, args: Array) -> String:
	if _host == null:
		return ""
	var result: Variant = _host.call("send_to_host", "get_quantity_string", [id, quantity, args])
	return result if result is String else ""

func print_log(level: String, tag: String, message: String) -> void:
	if _host == null:
		return
	_host.call("send_to_host", "print_log", [level, tag, message])
```

The sample host receives `request_data` through `EngineMessageReceiver.OnDataCommand`.
It replies by calling the engine handler named `response` with:

```text
["st_data", "result_<sub_command>", "<json payload>"]
```

## Sample App

The sample Windows host lives in:

```text
platform/windows/WindowsEmbed_Project
```

Important files:

- `Godot.WindowsEmbed.Embedding/GodotEngineHost.cs`: owns the engine thread and
  queues work into the iteration loop.
- `Godot.WindowsEmbed.Embedding/Interop/GodotWindowsEmbedEmbed.cs`: managed
  wrapper over the `libgodot_*` ABI.
- `Godot.WindowsEmbed.Embedding/Communication/EngineMessageReceiver.cs`: receives
  `WindowsEmbedHost.send_to_host` calls.
- `Godot.WindowsEmbed.Embedding/Communication/EngineMessageSender.cs`: calls
  GDScript handlers registered through `WindowsEmbedHost.register_handler`.
- `windows_embed_sample/Views/MapViewPage.xaml.cs`: attaches the `SwapChainPanel`,
  forwards XAML input, and serves sample JSON data.
- `windows_embed_sample/Assets/TestProject.pck`: bundled sample Godot project.

The sample defaults to the bundled `.pck` if present. Development builds can
change `MapViewPage.ResolveProjectPath()` or the sample run script to load a
project directory instead.

## Key Engine Files

- `core/extension/libgodot.h`
- `platform/windows/godot_windows_embed.h`
- `platform/windows/godot_windows_embed.cpp`
- `platform/windows/windows_host_bridge.h`
- `platform/windows/windows_host_bridge.cpp`
- `platform/windows/display_server_windows.*`
- `drivers/d3d12/rendering_context_driver_d3d12.*`
- `drivers/d3d12/rendering_device_driver_d3d12.cpp`
- `platform/windows/WindowsEmbed_Project/Godot.WindowsEmbed.Embedding`
- `platform/windows/WindowsEmbed_Project/windows_embed_sample`

## Checks

The focused regression checker for this branch is:

```powershell
python misc/scripts/check_libgodot_embedding_api.py
```

It verifies the renamed WindowsEmbed API surface, the XAML input-mode cursor
guard, and the direct host reply path.

## Upstream Godot

Godot is a feature-packed, cross-platform 2D and 3D game engine distributed
under the permissive [MIT license](https://godotengine.org/license).

For general Godot documentation, builds, community links, and contribution
guidelines, see:

- [Godot website](https://godotengine.org)
- [Godot documentation](https://docs.godotengine.org)
- [Contributing guide](CONTRIBUTING.md)
- [Official Godot repository](https://github.com/godotengine/godot)
