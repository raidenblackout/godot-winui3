# WindowsEmbed Communication Layer — Design

Replaces the current `WindowsEmbedHostBridge` + `windows_windows_embed_interactor.gd` machinery
with a thread-safe message bus and an explicit engine-thread separation.

## Goals

- **One primitive** for all host↔engine messaging: signal-based, async, thread-safe.
- **Engine runs on its own thread** — not the WindowsEmbed UI dispatcher.
- **Zero synchronization code in userland.** No `call_deferred`, no pending counts,
  no timeout guards, no defer-or-AV gotchas. Connect a signal, send a message, done.
- **Public API stable across embeddings.** Same shape will work if/when we
  embed in other host frameworks (Win32, Qt, macOS NSView, etc.).

## Non-goals

- **Per-message request/response correlation.** Routing is by `(main_cmd, sub_cmd)`
  pub/sub. If a caller truly needs 1:1, they encode an ID in their JSON payload.
  Adding correlation IDs into the bridge would just push more API surface onto
  every embedder.
- **Synchronous RPC.** The bridge is async only. The legacy `set_pending_return`
  sync path stays for fast `get_*` reads (string lookups, etc.) but is marked
  "tight, side-effect-free reads only" and is not part of the new surface.
- **JSON parsing.** Payloads are opaque `PackedByteArray`s. Schema is the caller's
  problem.

## Public API surface

### Engine lifecycle — host side

```csharp
public static class EmbeddedEngine
{
    static bool Start();    // spawn engine thread; run Main::setup2/start on it
    static void Stop();     // signal exit + join
    static void Pause();    // suspend iteration (renderer + input queues keep buffering)
    static void Resume();   // wake from pause
    static bool IsRunning { get; }
    static bool IsPaused  { get; }
}
```

The engine thread is private. Host code never gets a thread handle. All
cross-thread interaction goes through the queues defined below.

### CommunicationManager — GDScript side

```gdscript
# Send a message to the host. Safe to call from any thread.
# Non-blocking — returns immediately after enqueue.
WindowsEmbed.send(main_cmd: StringName, sub_cmd: StringName, data: Variant) -> void

# Signal — emitted on the engine thread, during an active iteration.
# One emission per message. No coalescing, no defer required by the handler.
signal on_message(main_cmd: StringName, sub_cmd: StringName, data: Variant)
```

### CommunicationManager — host (C#) side

```csharp
public static class CommunicationManager
{
    // Send a message to the engine. Safe from any thread.
    public static void Send(string mainCmd, string subCmd, string json);

    // Subscribe with default routing (UI dispatcher — fine for UI updates).
    public static event EventHandler<MessageEventArgs> OnMessage;

    // Subscribe with explicit dispatch routing — for file I/O, network, etc.
    public static void SubscribePool(EventHandler<MessageEventArgs> handler);
    public static void SubscribeOn(SynchronizationContext ctx,
                                   EventHandler<MessageEventArgs> handler);
}

public sealed class MessageEventArgs : EventArgs
{
    public string MainCmd { get; init; }
    public string SubCmd  { get; init; }
    public string Json    { get; init; }
}
```

## Threading model

### Threads

| Name | Purpose | Owner |
| --- | --- | --- |
| **E** — Engine thread | `Main::iteration()`, GDScript execution, signal emissions, renderer work | Engine (spawned by `EmbeddedEngine.Start`) |
| **U** — UI thread | WindowsEmbed composition, XAML, dispatcher, pointer/key event source | App startup thread |
| **W** — Worker threads | Host-side I/O, network, decoding | `ThreadPool` / app's own pool |

### Flow

```
GDScript on E ──→ WindowsEmbed.send() ──→ [outbound queue] ──→ host drains on U or W
                                                         └→ OnMessage(handler)

host on U/W ──→ CommunicationManager.Send() ──→ [inbound queue] ──→ E drains at iter start
                                                                    └→ on_message(handler)
```

### Invariants (the contract that erases userland sync code)

1. **All `on_message` emissions happen on E during an active iteration's idle phase.**
   Handlers can mutate scene state, free resources, allocate nodes — anything legal
   inside a normal iteration. No AV possible because the bridge never invokes
   handlers from outside the iteration loop.

2. **`send()` is non-blocking and lock-light.** SPSC queue, atomic head/tail.
   Caller resumes within microseconds. No lock held across user code.

3. **Subscription is timing-independent.** GDScript `connect()` runs on E (because
   GDScript always runs on E), so Godot's existing single-threaded signal table is
   safe. No "subscribe too early" or "subscribe too late" failure modes.

4. **Pre-Start buffering.** Messages enqueued before `Start()` succeeds are kept.
   The first iteration drains them in order. Removes "did I register the handler
   in time?" anxiety.

5. **Post-Stop drop.** After `Stop()` returns, further `Send()` is a no-op with an
   optional WARN log. No use-after-free.

6. **One emission per message.** The bridge owns the only path from queue to
   signal. There is no parallel simulated/fallback path that can fire the same
   message twice.

### Pointer/key input

Input injection from U piggybacks on the same machinery:

```
PointerPressed (on U) ──→ EmbeddedEngine.InjectMouseButton(...) ──→ [input queue]
                                                                     └→ drained on E
                                                                        at iter top,
                                                                        injected into
                                                                        DisplayServer
```

Today's `_windows_embed_inject_*` static methods become enqueues. The engine drains
the queue at the top of each iteration before the message queue. Same invariants:
non-blocking, single emission, no defer needed.

## Internal design

### Queues

All three queues (inbound, outbound, input) use the same shape:

```cpp
struct BridgeMessage {
    StringName main_cmd;
    StringName sub_cmd;
    Vector<uint8_t> payload;   // opaque; UTF-8 JSON in practice
};

// LocklessSPSC because:
//   - inbound: one host producer (the dispatcher Send pump), one engine consumer
//   - outbound: one engine producer (GDScript on E), one host consumer
//   - input: one UI producer, one engine consumer
class BridgeQueue {
    SafeLocklessRing<BridgeMessage> ring;
    void push(BridgeMessage&&);   // non-blocking; returns false if full
    bool pop(BridgeMessage&);     // non-blocking; false if empty
};
```

If the host ever needs multiple producer threads (UI thread *and* a worker both
calling `Send`), wrap with a mutex or shard per-thread and merge at drain.
SPSC + thread_local sharding is a clean default; benchmark before optimizing.

### Drain points

- **Engine thread.** At the top of `Main::iteration()`, before script process tick:
  1. Drain input queue → inject events into `DisplayServer`.
  2. Drain inbound message queue → emit `on_message` for each, in order.

  Both happen inside the iteration scope, so scene access is safe.

- **Host side.** Default: a `DispatcherQueueTimer` on the UI thread (existing
  mechanism — already wired for `EngineIteration` calls today) drains the
  outbound queue and routes each message to the right subscriber via its
  declared `SynchronizationContext`.

### Lifecycle state machine

```
                 Start()
[stopped]  ─────────────────→ [starting] ─[Main::setup2 ok]─→ [running]
    ▲                                                              │
    │                                                       Pause()│
    │ stop()                                                       ▼
    │                                                          [paused]
    │                                                              │
    │                                                       Resume()│
    │                                                              ▼
    └───── stop() ──────────────────────────────────────────── [running]
                                  ▲                                │
                                  └────────── (running)────────────┘
```

`Send()` behavior per state:

| State | Behavior |
| --- | --- |
| `starting` | buffer in inbound queue (drained on first iteration) |
| `running` | enqueue normally |
| `paused` | enqueue normally (drained on resume) |
| `stopping` / `stopped` | drop with WARN |

### Engine-thread spawn — caveats

- `Main::setup` (early init) currently runs on the caller's thread (today: UI).
  We can keep it there if any of its work is UI-affine, then move `Main::setup2`
  + `start` + iteration onto E.
- `DisplayServerWindows` creates the swap chain and the window. The
  `ISwapChainPanelNative` interface binding (`SetSwapChain`) **must** happen on
  the same thread as the panel — i.e., U. The actual *rendering* into the swap
  chain (the engine's render loop) does not require U; D3D12 swap chains can
  Present from any thread that holds the command queue.
- Concrete plan: keep `AttachSurface()` on U, but move everything past
  `EngineStart` onto E. Validate that no D3D12 calls from E touch
  `ISwapChainPanelNative` itself (only the underlying `IDXGISwapChain`).

## Migration from current bridge

### Delete

- `_pending_responses` dict + `SIMULATED_TIMEOUT_SEC` timer in
  `windows_windows_embed_interactor.gd`.
- `SimulatedResponse` class (entire `simulated_response.gd`).
- The whole `Sender.PostDataCommand` / `register_handler("response", ...)`
  asymmetric callback pattern in `HostInteropSender.cs` / `HostInteropReceiver.cs`.
- `register_callbacks` / `unregister_callbacks` in the interactor.

### Replace

| Today | Tomorrow |
| --- | --- |
| `_host.call("send_to_host", "request_data", [...])` | `WindowsEmbed.send("request_data", sub_cmd, data)` |
| `_host.call("register_handler", "response", ...)` + GDScript dispatch | `WindowsEmbed.on_message.connect(_on_msg)` |
| `Sender.PostDataCommand("result_" + subCmd, json)` (in C#) | `CommunicationManager.Send("response", "result_" + subCmd, json)` |
| `Receiver.OnDataCommand += handler` | `CommunicationManager.OnMessage += handler` |

### What remains

- `libgodot_engine_setup/start/iteration/shutdown` C ABI — wrapped by the
  new `EmbeddedEngine` class on the C# side, untouched on the C++ side.
- Input injection helpers — refactored to enqueue, but same C ABI.
- Log callback — unchanged.

### Userland after migration

```gdscript
# windows_windows_embed_interactor.gd (or whatever replaces it)
extends Node

func _ready() -> void:
    WindowsEmbed.on_message.connect(_on_msg)
    WindowsEmbed.send("request_data", "get_indoor_map", {})

func _on_msg(main_cmd: StringName, sub_cmd: StringName, data: Variant) -> void:
    if main_cmd == "response":
        on_st_data_published.emit(sub_cmd, data)
    elif main_cmd == "error":
        push_error("Host error: %s / %s" % [sub_cmd, data])
```

```csharp
// MapViewPage.xaml.cs — equivalent surface
private void OnPanelLoaded(object sender, RoutedEventArgs e)
{
    CommunicationManager.OnMessage += OnHostMessage;
    EmbeddedEngine.Start();
}

private void OnHostMessage(object? s, MessageEventArgs e)
{
    if (e.MainCmd != "request_data") return;
    switch (e.SubCmd)
    {
        case "get_indoor_map":
            CommunicationManager.Send("response", "result_get_indoor_map",
                                      _viewModel.GetIndoorMap());
            break;
        // ...
    }
}
```

No threading code, no IDs, no timers — both sides.

## Open questions

1. **Timeout helper?** The bridge never timeouts on its own. Do we ship a
   userland convenience `WindowsEmbed.send_and_await(main_cmd, sub_cmd, data,
   timeout_ms)` that returns a one-shot signal? Probably yes as opt-in sugar.
2. **Backpressure.** If the host drains too slowly, outbound queue grows
   unbounded. Cap at N (e.g. 4096) with drop-oldest? Reject `send()`? Make it
   configurable via `EmbeddedEngine.QueueCapacity`.
3. **Signal argument shape.** `(main_cmd, sub_cmd, data)` triple vs.
   `(Message msg)` object. Object is cleaner for forward compat (adding
   timestamps, source thread, etc., later). Lean toward object.
4. **`Main::setup` thread placement.** Some early init may touch GPU drivers in
   ways that prefer the UI thread on Windows. Spike before committing.
5. **GDExtension parity.** This API is a Godot-side singleton (`WindowsEmbed`). For
   GDExtension callers (C#/Rust/etc.) we mirror the same shape via class methods.
   Verify the GDExtension binding generator handles `Variant` payloads cleanly
   across the FFI boundary.

## Acceptance criteria

The design is "right" when:

- Adding a new sub-command requires exactly **one** GDScript `match` arm and
  **one** C# `switch` case. No bridge changes, no synchronization code, no
  ordering knobs.
- Removing `SimulatedResponse` does not freeze the app — because real responses
  arrive deterministically through the single supported path.
- `EmbeddedEngine.Pause()` mid-frame doesn't drop input or messages; the queues
  buffer until `Resume()`.
- The current 1.16-second "deferred response delivery" gap in the
  `windows_windows_embed_interactor.gd` measurements collapses to one iteration
  (~16 ms), because there is no out-of-iteration callback path that has to be
  re-deferred.
