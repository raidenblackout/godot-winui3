# WinUI3 Host Bridge — message passing between host and engine

A bidirectional, JSON-on-the-wire message bus between an embedded Godot
engine and a WinUI3 host process. Used for higher-level commands like
`set_device_info`, `get_device_info`, or any custom RPC — **not** for input
events (mouse / keyboard / wheel) which have their own dedicated path via
`godot_winui3_inject_*`.

This sits alongside the engine-lifecycle and input APIs documented in
[godot_winui3_embed.h](godot_winui3_embed.h) and [godot_winui3_embed.cs](godot_winui3_embed.cs).
The implementation is in
[winui3_host_bridge.h](winui3_host_bridge.h) /
[.cpp](winui3_host_bridge.cpp).

## Why it exists

Other platforms have analogous bridges:

| Platform | Bridge |
|---|---|
| Android | `GodotPlugin` + `JNISingleton` (Java methods/signals exposed as a Godot `Object`) |
| Web | `JavaScriptBridge` singleton (`eval`, `create_callback`, wrapped JS objects) |
| iOS | `apple_embedded` plugins (Obj-C methods/signals exposed as a singleton) |

WinUI3 follows the same shape: the engine side gets a Godot `Object` named
`WinUI3Host` registered as an Engine singleton; the host side gets a small
C ABI that talks to that singleton.

## Architecture at a glance

```
┌──────────────────────────────────────────────────────────────────┐
│  WinUI3 host process (.exe, links the Godot DLL)                 │
│                                                                  │
│   ┌──────────────────┐         GodotWinUI3Embed.CallEngine(…)    │
│   │ MainWindow.cs    │ ─────▶ godot_winui3_call_engine(…) ─┐     │
│   │ (your code)      │                                     │     │
│   │                  │ ◀──── HostMessageHandler delegate ──┼─┐   │
│   └──────────────────┘                                     │ │   │
│                                                            │ │   │
│  ─────────────────────── DLL boundary ───────────────────  │ │   │
│                                                            │ │   │
│   ┌──────────────────┐                                     │ │   │
│   │ WinUI3HostBridge │   ◀──────── dispatch_host_call ─────┘ │   │
│   │  (singleton)     │   ─────── invoke host_callback ───────┘   │
│   │                  │                                            │
│   └──────────────────┘                                            │
│       ▲       │                                                   │
│       │       │ register_handler / send_to_host                   │
│       │       ▼                                                   │
│   ┌──────────────────┐                                            │
│   │ GDScript / C#    │  WinUI3Host.register_handler(…)            │
│   │ scene scripts    │  WinUI3Host.send_to_host(…)                │
│   └──────────────────┘                                            │
└───────────────────────────────────────────────────────────────────┘
```

Two directions, two patterns:

- **Host → Engine** — host calls `CallEngine(method, argsJson)`. The bridge
  emits the `host_message_received` signal (catch-all) and, if a handler is
  registered for that method, invokes it via `Callable.callv()`. Return value
  is JSON-encoded and shipped back.
- **Engine → Host** — GDScript calls
  `WinUI3Host.send_to_host(method, args)`. The bridge JSON-encodes the args
  and invokes the registered C callback. The host populates the return value
  from inside the callback via `godot_winui3_set_call_return(json)`.

## Wire format

JSON, UTF-8. Args travel as a JSON **array** (each element = one positional
argument). Return values travel as **any** JSON value.

GDScript handlers receive typed `Variant`s thanks to in-engine
`JSON.parse_string` / `JSON.stringify` — so a JSON object becomes a
`Dictionary`, a JSON array becomes an `Array`, and so on. Type mapping:

| JSON | Godot Variant |
|---|---|
| `null` | `Variant::NIL` |
| `true` / `false` | `Variant::BOOL` |
| number (no decimal) | `Variant::INT` |
| number (with decimal) | `Variant::FLOAT` |
| string | `Variant::STRING` |
| array | `Variant::ARRAY` |
| object | `Variant::DICTIONARY` |

JSON does not represent Godot-specific types (`Vector2`, `NodePath`,
`Resource`, packed arrays, …) directly. If you need those, encode them
yourself (e.g. `{"x": 1.0, "y": 2.0}` ↔ `Vector2`) at the boundary.

## Lifecycle and ordering

```
1. godot_winui3_set_log_callback(...)            ; optional, for capturing setup errors
2. godot_winui3_set_embedded_parent_hwnd(hwnd)
3. godot_winui3_engine_setup(argc, argv)         ; ◀── WinUI3Host singleton becomes available here
4. GodotWinUI3Embed.SetHostMessageHandler(...)   ; engine→host receiver — register before engine_start
5. godot_winui3_set_swap_chain_panel(0, panel)
6. godot_winui3_engine_start()                   ; runs scripts; _ready() registers GDScript handlers
7. (per frame) godot_winui3_engine_iteration()
8. (anytime) GodotWinUI3Embed.CallEngine(...)    ; works once step 6 has run
```

- The `WinUI3Host` Object is registered with `ClassDB` during platform-API
  registration inside `Main::setup()` (called by step 3). Singleton
  registration happens at the same time, so the singleton exists *before*
  any GDScript runs.
- `SetHostMessageHandler` should be installed **before** `EngineStart` so
  that messages emitted from `_ready()` are not dropped.
- `CallEngine` only finds a handler after the relevant GDScript has run
  `WinUI3Host.register_handler(...)` (typically in `_ready`).

## Threading model

**The bridge is single-threaded.** All calls in both directions must run on
the engine iteration thread — the same thread that drives
`godot_winui3_engine_iteration`. In a typical WinUI3 host that is the UI
thread (driven by a `DispatcherQueueTimer`).

If you receive an event on a worker thread (e.g. a Bluetooth callback) and
need to call `CallEngine`, marshal first:

```csharp
dispatcherQueue.TryEnqueue(() =>
{
    GodotWinUI3Embed.CallEngine("device_connected", "[42]");
});
```

The bridge does not queue or buffer — calls execute synchronously. There is
exactly one shared "pending return value" slot per host callback; nested
host callbacks are not supported.

## GDScript API — `WinUI3Host` singleton

```gdscript
# Send a message to the host. args is positional — one element per host argument.
# Returns whatever the host writes back via godot_winui3_set_call_return,
# or null if the host did not set a return value.
WinUI3Host.send_to_host(method: StringName, args: Array = []) -> Variant

# Register a Callable to handle host→engine calls for `method`.
# Replaces any existing handler for that method.
WinUI3Host.register_handler(method: StringName, handler: Callable) -> void

# Remove the handler for `method`. No-op if none was registered.
WinUI3Host.unregister_handler(method: StringName) -> void

# True if a handler is currently registered for `method`.
WinUI3Host.has_handler(method: StringName) -> bool

# Catch-all signal. Fires for EVERY host→engine call, regardless of whether
# a handler is registered for that method. Useful for logging / debugging.
signal host_message_received(method: StringName, args: Array)
```

Handlers are invoked via `Callable.callv(args)`, so the signature must
accept positional arguments matching whatever the host sends. Return value
is stringified back to JSON; a return type of `void` (nothing) becomes
`null` on the host side.

## C# API — `GodotWinUI3Embed`

```csharp
public delegate string? HostMessageHandler(string method, string argsJson);

// Engine → Host. Pass null to clear.
// Call BEFORE EngineStart so messages emitted during _ready are not dropped.
public static void SetHostMessageHandler(HostMessageHandler? handler);

// Host → Engine. Returns the JSON-encoded return value, or null if the
// handler returned nothing or no handler was registered.
public static string? CallEngine(string method, string? argsJson = null);
```

The handler is invoked on the engine iteration thread. Exceptions thrown
inside the handler are caught and logged via `Debug.WriteLine` — they do
**not** propagate across the native boundary.

## C ABI — for non-C# hosts

Defined in [godot_winui3_embed.h](godot_winui3_embed.h):

```c
typedef void (*godot_winui3_host_msg_func)(
    const char *p_method,
    const char *p_args_json);

void godot_winui3_set_host_message_callback(godot_winui3_host_msg_func cb);
void godot_winui3_set_call_return(const char *p_json);
int32_t godot_winui3_call_engine(
    const char *p_method,
    const char *p_args_json,
    char **r_ret_json);
void godot_winui3_free_string(char *p_str);
```

Memory ownership:

- `p_method`, `p_args_json` passed to the callback are owned by the engine
  and only valid for the duration of the call. Copy them if you need to
  defer work.
- `*r_ret_json` returned by `godot_winui3_call_engine` is allocated by the
  engine. **Caller must free with `godot_winui3_free_string`.** Pass `NULL`
  for that argument if you don't need the return value.
- The string passed to `godot_winui3_set_call_return` is copied internally;
  the caller may free it after the call.

Return value of `godot_winui3_call_engine`:
- `1` on success — `*r_ret_json` is either `NULL` (no return / no handler)
  or a heap-allocated UTF-8 JSON string.
- `0` on failure — engine not initialised or singleton not registered.

## Examples

### Recipe: get_device_info / set_device_info

GDScript handler side:

```gdscript
extends Node

var device_info := {}

func _ready() -> void:
    WinUI3Host.register_handler("set_device_info", _on_set_device_info)
    WinUI3Host.register_handler("get_device_info", _on_get_device_info)
    # Tell the host we are ready to receive commands.
    WinUI3Host.send_to_host("scene_ready", [{"scene": "main"}])

func _on_set_device_info(info: Dictionary) -> Dictionary:
    device_info = info
    return {"ok": true}

func _on_get_device_info() -> Dictionary:
    return device_info
```

C# host side:

```csharp
using Godot.WinUI3;
using System.Text.Json;

GodotWinUI3Embed.SetHostMessageHandler((method, argsJson) =>
{
    if (method == "scene_ready")
    {
        // args is `[{"scene":"main"}]`
        return null;
    }
    return null;
});

// After EngineStart() and at least one iteration, the GDScript handlers exist:

var ackJson = GodotWinUI3Embed.CallEngine(
    "set_device_info",
    JsonSerializer.Serialize(new[] {
        new { vendor = "Foo", model = "Bar" }
    }));
// ackJson => {"ok":true}

var infoJson = GodotWinUI3Embed.CallEngine("get_device_info", "[]");
// infoJson => {"vendor":"Foo","model":"Bar"}
```

### Recipe: long-running work — fire-and-forget

If you don't need a return value, just don't call `set_call_return` (host
side) or return `null` from the handler (engine side). The bridge always
returns synchronously, but the receiver can kick off async work and return
immediately:

```gdscript
func _on_start_telemetry_upload(payload: Dictionary) -> void:
    # Don't block the iteration — schedule the upload and return.
    _telemetry_queue.append(payload)
```

```csharp
GodotWinUI3Embed.SetHostMessageHandler((method, argsJson) =>
{
    if (method == "log_event")
    {
        _ = Task.Run(() => UploadAsync(argsJson));
        return null;            // host gets `null` back immediately
    }
    return null;
});
```

### Recipe: catch-all logging

```gdscript
func _ready() -> void:
    WinUI3Host.host_message_received.connect(
        func(method: StringName, args: Array):
            print("[host→engine] ", method, " ", args)
    )
```

This signal fires for **every** host-initiated call, regardless of whether
a specific handler was registered. Useful for diagnostics during
integration.

## Error handling

| Failure mode | Visible behaviour |
|---|---|
| `send_to_host` called with no host callback registered | Warning printed once per method name; returns `null`. |
| `CallEngine` called before engine setup | Returns `0` from the C ABI / throws `InvalidOperationException` from C#. |
| `CallEngine` for an unknown method | Signal still fires; returns `null` (no error). The host cannot distinguish "no handler" from "handler returned null". |
| Handler `Callable` throws / errors | Logged via Godot's error handler; `CallEngine` returns `null`. |
| Malformed args JSON | Argument decodes to `Variant::NIL`; handler is called with empty args. (No structured error is reported.) |
| C# host handler throws | Caught and logged via `Debug.WriteLine`; engine sees `null` return. |

## Limitations

- **Single-threaded.** No queuing, no cross-thread marshalling. Caller
  responsibility.
- **JSON only.** No native pass-through for Godot Variant types beyond what
  JSON expresses (no Vector2/3, NodePath, Resource references, packed
  arrays, etc.). Encode such payloads yourself.
- **One handler per method.** `register_handler` replaces the previous one.
  Use the catch-all signal if you need multiple listeners.
- **Synchronous only.** The bridge does not natively support async/await;
  build that on top yourself if needed (e.g. with a request-id +
  completion-message protocol).

## Files

- [winui3_host_bridge.h](winui3_host_bridge.h) — singleton class declaration
- [winui3_host_bridge.cpp](winui3_host_bridge.cpp) — singleton + dispatch
- [godot_winui3_embed.h](godot_winui3_embed.h) — C ABI
- [godot_winui3_embed.cpp](godot_winui3_embed.cpp) — C ABI implementation
- [godot_winui3_embed.cs](godot_winui3_embed.cs) — C# P/Invoke wrapper
- [api/api.cpp](api/api.cpp) — singleton registration entry point
