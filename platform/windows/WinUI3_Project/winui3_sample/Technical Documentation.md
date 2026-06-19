# Godot × WinUI3 Embedding — Technical Documentation

## 1. How Godot Normally Renders on Windows

To understand why every part of this integration exists, it helps to start with what Godot does in the default case.

When Godot starts on Windows it asks the OS for a **Win32 window** — an `HWND`. That handle is the root of everything: input arrives through it via `WndProc`, and the D3D12 backend creates a swap chain tied directly to it using `CreateSwapChainForHwnd`. DXGI then owns the screen region covered by that HWND and presents rendered frames straight to the desktop compositor (DWM).

```
Normal Godot
─────────────────────────────────────────────────
  OS desktop (DWM)
    └─ HWND  ◄── WndProc (input: WM_KEYDOWN, WM_LBUTTONDOWN …)
         └─ IDXGISwapChain (bound to HWND via CreateSwapChainForHwnd)
               └─ D3D12 render target → Present()
```

This is a clean, tight loop: one HWND, one swap chain, one path for input.

---

## 2. What WinUI3 Is (and Why It Complicates Things)

WinUI3 is Microsoft's modern desktop UI framework. Unlike older Win32 or WinForms apps, WinUI3 does not render controls by drawing into HWNDs. Instead it builds a **XAML visual tree** — a retained-mode scene graph — that is composited by the Windows composition engine (Windows.UI.Composition, which runs on top of DirectComposition and ultimately DWM).

The WinUI3 window still *has* a Win32 HWND underneath, but XAML controls live in a conceptual layer *above* it. The DWM sees one HWND; XAML sees a tree of UIElement nodes. Input flows through both systems simultaneously: Win32 messages reach the HWND, and WinUI3's own PointerRouting / FocusManager layer handles XAML-level event dispatch.

```
WinUI3 App
─────────────────────────────────────────────────
  OS desktop (DWM)
    └─ WinUI3 HWND
         └─ Windows.UI.Composition visual tree
              ├─ XAML layout root
              │    ├─ Button, TextBlock, Grid …
              │    └─ SwapChainPanel  ← ← ← this is our hook
              └─ WinUI3 FocusManager (keyboard routing)
```

The critical point: **you cannot simply embed a Godot HWND as a XAML child.** XAML does not know about HWNDs; it deals in visuals and layout elements. If you put a raw Win32 child window inside a XAML layout it is drawn on a separate DWM layer that does not participate in XAML composition, clipping, or DPI handling.

---

## 3. SwapChainPanel — the Bridge

Microsoft designed `SwapChainPanel` specifically to solve the "I want to render my own frames inside a XAML layout" problem. It is a XAML UIElement that acts as a viewport into a DXGI swap chain you supply.

The mechanism has two sides:

**XAML side:** `SwapChainPanel` participates in layout normally. It has a size, position, transforms, opacity, can be placed inside a Grid, animated, etc. From XAML's perspective it is just another element.

**D3D side:** It exposes a COM interface called `ISwapChainPanelNative`. You call `SetSwapChain(IDXGISwapChain1*)` on that interface and hand it your swap chain. From that point, whenever you call `Present()` on the swap chain, the composition engine picks up the new frame and blends it into the XAML visual tree at the panel's position.

The key constraint: the swap chain must be created with **`CreateSwapChainForComposition`** instead of `CreateSwapChainForHwnd`. A swap chain created for an HWND is owned by DXGI/DWM and cannot be handed to the composition engine. `ForComposition` produces a "headless" swap chain with no HWND attachment; its output is only visible once you connect it to a visual or a SwapChainPanel.

```
SwapChainPanel rendering path
─────────────────────────────────────────────────
  D3D12 render pass → IDXGISwapChain1::Present()
                            │
                    ISwapChainPanelNative::SetSwapChain()
                            │
              Windows.UI.Composition visual tree
                            │
              DWM composites it into the WinUI3 window
                            │
                     What the user sees
```

---

## 4. Why the Display Server Had to Change

Godot's `DisplayServer` is the OS abstraction layer. It owns window creation (HWNDs, styles, sizes), surface creation (handing data to the D3D12 backend), resize/DPI notifications, and input dispatch. Every one of these needed adjustment for WinUI3.

### 4a. Surface Creation

The D3D12 backend's swap chain creation previously assumed it was always working against an HWND. It called `CreateSwapChainForHwnd` unconditionally, then called `MakeWindowAssociation` (which registers Alt+Enter fullscreen handling — only meaningful for HWND-bound swap chains) and set up DirectComposition for window transparency.

For SwapChainPanel embedding all three assumptions are wrong:

1. Call `CreateSwapChainForComposition` instead of `CreateSwapChainForHwnd`
2. Skip `MakeWindowAssociation` — there is no HWND binding to associate
3. Skip DirectComposition setup — SwapChainPanel and DirectComposition are mutually exclusive presentation paths; the panel *is* the composition step

### 4b. Window Style

We still create a Win32 child HWND for Godot's window. The window style matters critically:

| Flag | Reason |
|------|--------|
| `WS_CHILD` | Makes the HWND a true Win32 child of the WinUI3 window. Win32 then delivers mouse messages directly to it via hit-testing (the cursor is over the HWND → `WM_LBUTTONDOWN` goes there). |
| `WS_EX_NOREDIRECTIONBITMAP` | Tells DWM not to create a redirection surface for this window. Without it, DWM would try to composite the HWND's own pixel content into the desktop, conflicting with the SwapChainPanel's composition path and causing visual artifacts. |
| Remove `WS_EX_WINDOWEDGE` | Avoids a visible border artifact on the embedded child. |

Previously, Godot's embedded windows used `WS_POPUP` — a borderless floating window. `WS_POPUP` windows are not true children; Win32 does not route mouse messages to them via the parent's hit-test tree. We had to switch to `WS_CHILD` for native mouse routing to work.

### 4c. Initialization Order

The WinUI3 host creates its XAML objects (including the SwapChainPanel) before calling `EngineSetup()`. But the DisplayServer — and therefore the D3D12 surface — does not exist until `Main::setup2()` runs inside `EngineStart()`. There is a structural gap:

```
Timeline
────────────────────────────────────────────────────────────────────
  Host creates SwapChainPanel in XAML
  Host calls SetSwapChainPanel()  ← DisplayServer does not exist yet
  Host calls EngineSetup()        ← Main::setup() runs, still no DisplayServer
  Host calls EngineStart()        ← Main::setup2() creates DisplayServer
                                     surface is created HERE
                                     ← pending panel is picked up automatically
  Host calls EngineIteration()    ← first frame rendered into panel
```

To handle the gap we use a static "pending" slot on `DisplayServerWindows`. `set_pending_swap_chain_panel()` stores the panel reference (with a COM AddRef) before the DisplayServer exists. During surface creation inside `setup2()`, the DisplayServer checks for a pending panel and picks it up. No destroy-and-recreate cycle is needed, which matters because the panel must be bound before the first `Present()` call.

### 4d. Resize

XAML layout is independent of Win32 geometry. When the user resizes the WinUI3 window, XAML reflows and the SwapChainPanel gets a new logical size — but no `WM_SIZE` message is sent to Godot's child HWND, because Win32 does not know the XAML panel's size changed. The host must explicitly call `NotifyPanelResize()` from the XAML `SizeChanged` event handler, which in turn resizes the DXGI swap chain buffers.

---

## 5. The Input Routing Problem

Input in this architecture runs through two separate, parallel subsystems that must not conflict.

**Win32 input** flows through `WndProc` via `WM_*` messages. Mouse messages go to whichever HWND the cursor is physically over (Win32 hit-testing). Keyboard messages go to whichever HWND currently has Win32 keyboard focus.

**XAML input** flows through WinUI3's own routing system. The `FocusManager` tracks which UIElement has logical focus. `PointerPressed`, `KeyDown`, etc. fire on UIElements in the XAML tree, independent of Win32 focus.

The conflict: if Godot's child HWND steals Win32 keyboard focus (via `WM_MOUSEACTIVATE` returning `MA_ACTIVATE`), WinUI3's `FocusManager` loses track of the focused element. From that point, `KeyDown` events stop firing on the SwapChainPanel, breaking keyboard input in XAML mode.

Solution: return `MA_NOACTIVATE` from `WM_MOUSEACTIVATE` when running in WinUI3 embed mode. The child HWND receives mouse messages (via hit-testing) but never claims Win32 keyboard focus. Keyboard input is handled separately through whichever input mode the host selects.

### The Two Input Modes

Because of the above, there are two valid ways to deliver input to Godot:

```
NATIVE mode
────────────────────────────────────────────────────────────────────
  Mouse: Win32 hit-tests child HWND → WM_LBUTTONDOWN → WndProc ✓
  Keyboard: host hooks parent HWND or uses InputKeyboardSource
  WndProc processes everything normally — no changes needed

XAML mode
────────────────────────────────────────────────────────────────────
  Mouse: SwapChainPanel.PointerPressed → inject_mouse_button() → Godot
  Keyboard: SwapChainPanel.KeyDown → inject_key() → Godot
  WndProc suppresses WM_MOUSE* and WM_KEY* to prevent double-processing
```

The flag is runtime-configurable because neither mode is universally better:

- **Native** leverages Win32's battle-tested mouse routing with zero extra wiring. Keyboard requires either a `SetWindowSubclass` hook on the parent or a `Microsoft.UI.Input.InputKeyboardSource` subscription (Windows App SDK 1.2+).
- **XAML** is simpler to set up end-to-end from C# — all events come from the panel — but every input type (scroll, right-click, middle-click, modifier keys) must be explicitly wired and injected, and modifier state must be read from Win32 `GetKeyboardState()` since XAML events do not carry full modifier information.

The suppression guard in `WndProc` is what makes the modes mutually exclusive. Without it, XAML mode would double-fire every event: once from the XAML injection and again from the Win32 message the OS still delivers to the child HWND.

---

## 6. The C API Boundary

The embedding layer exposes a flat C API (`extern "C"`, `__declspec(dllexport)`) rather than a C++ class API. This is deliberate:

- A C ABI is stable across MSVC versions, compiler settings, and C runtime configurations. A C++ class ABI is not — vtable layout, name mangling, and exception handling all vary between builds.
- The host application is a C# WinUI3 app using P/Invoke. P/Invoke binds directly to C exported functions without any COM interop overhead or type library requirements.
- The C API wraps Godot's internal C++ types (`DisplayServerWindows` static methods, `GodotInstance` lifecycle) so the host is fully insulated from Godot's internal type system.

---

## 7. Full Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│  WinUI3 Host Application (C#)                                        │
│                                                                      │
│  ┌──────────────────┐  XAML events   ┌──────────────────────────┐   │
│  │  MainWindow.xaml │ ─────────────► │  GodotWinUI3Embed (C#)  │   │
│  │  SwapChainPanel  │  SizeChanged   │  typed wrapper class     │   │
│  │  PointerPressed  │  KeyDown etc.  └────────────┬─────────────┘   │
│  └──────────────────┘                             │ P/Invoke         │
└───────────────────────────────────────────────────┼──────────────────┘
                                                    │
                                       ┌────────────▼─────────────────────────┐
                                       │  godot_winui3_embed  (C ABI / DLL)   │
                                       │                                       │
                                       │  engine_setup / start / iteration     │
                                       │  set_input_mode (NATIVE | XAML)       │
                                       │  set_swap_chain_panel                 │
                                       │  notify_panel_resize                  │
                                       │  inject_mouse_button / motion / wheel │
                                       │  inject_key                           │
                                       └────────────┬─────────────────────────┘
                                                    │
                                       ┌────────────▼─────────────────────────┐
                                       │  DisplayServerWindows                 │
                                       │                                       │
                                       │  Pre-init statics:                    │
                                       │    _embedded_parent_hwnd              │
                                       │    _pending_swap_chain_panel          │
                                       │                                       │
                                       │  Per-window:                          │
                                       │    hWnd (WS_CHILD +                   │
                                       │           WS_EX_NOREDIRECTIONBITMAP)  │
                                       │    parent_hwnd → WinUI3 HWND          │
                                       │    swap_chain_panel pointer           │
                                       │                                       │
                                       │  WndProc:                             │
                                       │    MA_NOACTIVATE on WM_MOUSEACTIVATE  │
                                       │    suppresses WM_MOUSE* + WM_KEY*     │
                                       │    when input_mode == XAML            │
                                       └────────────┬─────────────────────────┘
                                                    │
                                       ┌────────────▼─────────────────────────┐
                                       │  D3D12 Backend                        │
                                       │                                       │
                                       │  swap_chain_resize():                 │
                                       │  ┌─ swap_chain_panel present?         │
                                       │  │   YES → CreateSwapChainForComposit │
                                       │  │         ISwapChainPanelNative      │
                                       │  │           ::SetSwapChain()         │
                                       │  │   NO  → CreateSwapChainForHwnd     │
                                       │  └────────────────────────────────── │
                                       └────────────┬─────────────────────────┘
                                                    │
┌───────────────────────────────────────────────────▼──────────────────┐
│  Windows OS                                                           │
│                                                                       │
│  WinUI3 HWND                                                          │
│  └─ Godot child HWND (WS_CHILD, WS_EX_NOREDIRECTIONBITMAP)           │
│       Win32 delivers WM_MOUSE* here via hit-testing (NATIVE mode)    │
│                                                                       │
│  Windows.UI.Composition visual tree                                   │
│  └─ SwapChainPanel visual ◄── IDXGISwapChain1::Present()             │
│       DWM composites this into the WinUI3 window at panel position    │
└───────────────────────────────────────────────────────────────────────┘
```

---

## 8. Why Not Just Use an HWND Directly?

A natural question: could we skip SwapChainPanel and make Godot's HWND a Win32 child of the WinUI3 window, rendering straight to it?

The HWND hierarchy works, but rendering does not compose correctly. When DWM composites the WinUI3 window, a child HWND's content is drawn in a separate composition band from the XAML content. You lose:

- **Correct z-ordering** — a XAML popup or tooltip can never appear in front of the child HWND
- **XAML effects** — opacity, transforms, and blur effects on the panel have no effect on the HWND's content
- **DPI scaling** — XAML handles fractional DPI scaling natively; HWNDs snap to integer scale factors, causing blurry rendering on non-integer DPI settings
- **Clipping** — XAML clipping masks (e.g., border radius) do not clip the child HWND

`SwapChainPanel` solves all of these by bringing Godot's rendered output *into* the XAML visual tree as a first-class participant. The composition engine treats a Godot frame as just another visual node, indistinguishable from a XAML Rectangle filled with an image.

---

## 9. Why Not Use CoreWindow or a Single Input Subscription?

In UWP (the predecessor to WinUI3 desktop), the input root was `CoreWindow`. A renderer like Unity could call `SetCoreWindowEvents(CoreWindow*)` to subscribe to all input events in a single call — keys, pointer, wheel, everything. WinUI3 desktop removes this: `CoreWindow.GetForCurrentThread()` returns null in WinUI3 desktop apps; the API is deprecated and unsupported.

WinUI3 does offer `Microsoft.UI.Input.InputPointerSource` and `InputKeyboardSource` (Windows App SDK 1.2+) as modern replacements. These operate at the window level — not per UIElement — and cover all pointer and keyboard events in one subscription point. They are the closest analog to the old `CoreWindow` model and are the recommended approach for the NATIVE + keyboard scenario.

The per-element XAML approach used in the sample is simpler for a demonstration but requires manually wiring every event type on the SwapChainPanel. The input mode flag was introduced precisely because the right choice depends on the host app's requirements — without recompiling the Godot DLL.

---

## 10. Summary of Design Decisions

| Decision | Alternative considered | Why this way |
|---|---|---|
| `CreateSwapChainForComposition` + `ISwapChainPanelNative::SetSwapChain` | `CreateSwapChainForHwnd` | Only ForComposition output participates in XAML visual tree composition |
| Keep a child HWND alongside the SwapChainPanel | Panel only, no HWND | HWND is needed for Win32 message routing; WndProc is Godot's entire input pipeline |
| `WS_CHILD` window style | `WS_POPUP` (previous default for embedded windows) | WS_CHILD makes Win32 deliver mouse messages via hit-testing; WS_POPUP does not |
| `WS_EX_NOREDIRECTIONBITMAP` | Default (no flag) | Prevents DWM from creating a conflicting redirection surface alongside the panel |
| `MA_NOACTIVATE` on `WM_MOUSEACTIVATE` | Allow Win32 activation | Prevents the child HWND from stealing keyboard focus and breaking WinUI3 FocusManager |
| Static "pending" panel slot | Bind panel after DisplayServer init | Avoids surface destroy+recreate; panel must be bound before the first Present() call |
| Runtime input mode flag | Compile-time `#ifdef` | Host picks the right input strategy for its use case without rebuilding the engine DLL |
| Flat C ABI for the host-facing API | C++ class or COM interface | Stable across compilers; directly P/Invoke-able from C# with no COM overhead |
| `winui3=yes` SCons opt-in build flag | Always compiled in | The required Windows SDK headers may not be present in all build environments |
