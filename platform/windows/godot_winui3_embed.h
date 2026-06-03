/**************************************************************************/
/*  godot_winui3_embed.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef GODOT_WINUI3_EMBED_H
#define GODOT_WINUI3_EMBED_H

#ifdef WINUI3_ENABLED

#include <stdint.h>

/* Export macro — matches the pattern used by libgodot.h */
#if defined(_MSC_VER) || defined(__MINGW32__)
#define GODOT_WINUI3_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define GODOT_WINUI3_API __attribute__((visibility("default")))
#else
#define GODOT_WINUI3_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Input routing mode passed to godot_winui3_set_input_mode().
 *
 * NATIVE (0, default): Win32 WM_* messages are processed normally by Godot's
 *   WndProc. Mouse events are routed to the child HWND by Win32 hit-testing;
 *   keyboard events require a separate hook or InputKeyboardSource wiring.
 *
 * XAML  (1): WndProc suppresses all mouse/keyboard WM_* messages. The host is
 *   responsible for injecting every input event via godot_winui3_inject_*.
 *   Use when handling pointer/key events on the SwapChainPanel in XAML.
 */
#define GODOT_WINUI3_INPUT_NATIVE 0
#define GODOT_WINUI3_INPUT_XAML   1

/**
 * Log levels passed to the log callback set by godot_winui3_set_log_callback.
 *   0 = normal print
 *   1 = warning
 *   2 = error / script error / shader error
 */
typedef void (*godot_winui3_log_func)(const char *p_message, int32_t p_level);

/**
 * Install a log callback that receives all Godot print/warning/error output.
 *
 * Call this BEFORE godot_winui3_engine_setup() to capture setup failures.
 * Pass nullptr to uninstall. Only one callback can be active at a time;
 * calling again replaces the previous one.
 *
 * The callback is invoked on the thread that triggered the log event.
 * It must not re-enter any godot_winui3_* function.
 */
GODOT_WINUI3_API void godot_winui3_set_log_callback(godot_winui3_log_func p_callback);

/**
 * Set the host HWND that the engine's main window should be re-parented into.
 *
 * Call BEFORE godot_winui3_engine_setup() — this is read during DisplayServer
 * construction, so it must be set first. Use the HWND of your WinUI3 window
 * (obtained via WindowNative.GetWindowHandle in C#).
 *
 * @param p_hwnd  HWND of the host window, cast to void*. Pass nullptr to clear.
 */
GODOT_WINUI3_API void godot_winui3_set_embedded_parent_hwnd(void *p_hwnd);

/**
 * Initialise the Godot engine in-process (calls Main::setup internally).
 *
 * Call ONCE per process, after godot_winui3_set_embedded_parent_hwnd().
 * After this returns 1, call godot_winui3_set_swap_chain_panel() and
 * godot_winui3_engine_start() before driving frames with
 * godot_winui3_engine_iteration().
 *
 * @param p_argc  Number of arguments (including argv[0] program name).
 * @param p_argv  UTF-8 argument strings. argv[0] is treated as the program
 *                name; the rest are forwarded to Godot (e.g. "--path",
 *                "C:/proj", "--rendering-driver", "d3d12").
 *
 * @return 1 on success, 0 on failure.
 */
GODOT_WINUI3_API int32_t godot_winui3_engine_setup(int32_t p_argc, char **p_argv);

/**
 * Start the loaded project (calls Main::setup2 + Main::start internally).
 *
 * Call after godot_winui3_engine_setup() and after the swap chain panel has
 * been bound via godot_winui3_set_swap_chain_panel().
 *
 * @return 1 on success, 0 on failure.
 */
GODOT_WINUI3_API int32_t godot_winui3_engine_start();

/**
 * Run a single frame of the engine main loop.
 *
 * Call this from a 60Hz timer (DispatcherQueueTimer) on your UI thread.
 * Returns non-zero when the engine wants to exit (the user-side host should
 * stop the timer and call godot_winui3_engine_shutdown()).
 *
 * @return 0 to keep running, non-zero when the engine wants to quit.
 */
GODOT_WINUI3_API int32_t godot_winui3_engine_iteration();

/**
 * Shut down the engine and release all resources. Call once before exit.
 */
GODOT_WINUI3_API void godot_winui3_engine_shutdown();

/**
 * Pass the ISwapChainPanelNative* for a window to the engine.
 *
 * Call after engine initialisation, before the first rendered frame.
 *
 * @param p_window_id  Godot window ID (use DisplayServer.MAIN_WINDOW_ID = 0 for
 *                     the primary window).
 * @param p_panel_native  Pointer obtained in C# via
 *                        panel.As<ISwapChainPanelNative>(), marshalled as IntPtr.
 */
GODOT_WINUI3_API void godot_winui3_set_swap_chain_panel(int32_t p_window_id, void *p_panel_native);

/**
 * A unit of work the engine needs run on the host UI thread.
 * p_ctx is engine-owned and only valid for the duration of the call.
 */
typedef void (*godot_winui3_work_func)(void *p_ctx);

/**
 * Host-supplied dispatcher: run p_work(p_ctx) on the thread that owns the
 * SwapChainPanel (the host UI thread) and return only once it has completed
 * (i.e. SYNCHRONOUSLY — block the calling engine thread until done).
 */
typedef void (*godot_winui3_ui_dispatch_func)(godot_winui3_work_func p_work, void *p_ctx);

/**
 * Install a dispatcher used to marshal the few rendering calls that have UI-thread
 * affinity — specifically ISwapChainPanelNative::SetSwapChain — onto the UI thread.
 *
 * REQUIRED when the engine is driven from a dedicated thread (i.e. when
 * godot_winui3_engine_setup/start/iteration run on a thread other than the one
 * that created the SwapChainPanel). Binding the swap chain to the panel's
 * composition visual must happen on the panel's thread; off-thread it silently
 * returns S_OK but never attaches, leaving the panel blank.
 *
 * Call BEFORE godot_winui3_engine_start() (the swap chain is created during
 * start). Pass NULL to clear, in which case the binding runs inline on the
 * calling thread — correct only when that thread already owns the panel.
 */
GODOT_WINUI3_API void godot_winui3_set_ui_dispatcher(godot_winui3_ui_dispatch_func p_dispatch);

/**
 * Notify the engine that the SwapChainPanel was resized.
 *
 * Call from the SwapChainPanel.SizeChanged handler in the WinUI3 host.
 *
 * @param p_window_id  Godot window ID.
 * @param p_width      New width in physical pixels.
 * @param p_height     New height in physical pixels.
 */
GODOT_WINUI3_API void godot_winui3_notify_panel_resize(int32_t p_window_id, int32_t p_width, int32_t p_height);

/**
 * Set the panel's composition scale (physical pixels per DIP).
 *
 * Without this call, a swap chain sized in physical pixels would render past
 * the panel's bounds because SwapChainPanel maps 1 swap-chain pixel to 1 DIP
 * by default. Internally the engine inverts the scale on the swap chain via
 * IDXGISwapChain2::SetMatrixTransform so the buffer fits the panel exactly.
 *
 * Call this BEFORE godot_winui3_engine_start() with the panel's initial
 * CompositionScaleX/Y, then again from the SwapChainPanel.CompositionScaleChanged
 * handler in the WinUI3 host whenever DPI or any ancestor visual transform
 * changes. Pair with a fresh godot_winui3_notify_panel_resize call so the
 * physical-pixel buffer size matches the new scale.
 *
 * @param p_window_id  Godot window ID.
 * @param p_scale_x    Horizontal composition scale (e.g. 1.5 at 150% DPI).
 * @param p_scale_y    Vertical composition scale.
 */
GODOT_WINUI3_API void godot_winui3_set_composition_scale(int32_t p_window_id, float p_scale_x, float p_scale_y);

/**
 * Inject a mouse button press or release event.
 *
 * @param p_window_id  Godot window ID the event targets.
 * @param p_button     Godot MouseButton enum value (1=left, 2=right, 3=middle).
 * @param p_pressed    Non-zero for press, zero for release.
 * @param p_x          Panel-local X coordinate in pixels.
 * @param p_y          Panel-local Y coordinate in pixels.
 */
GODOT_WINUI3_API void godot_winui3_inject_mouse_button(int32_t p_window_id, int32_t p_button, int32_t p_pressed, float p_x, float p_y);

/**
 * Inject a mouse motion event.
 *
 * All coordinates are panel-local pixels.
 *
 * @param p_window_id  Godot window ID the event targets.
 * @param p_x          Current X position.
 * @param p_y          Current Y position.
 * @param p_rel_x      X delta since the last motion event.
 * @param p_rel_y      Y delta since the last motion event.
 */
GODOT_WINUI3_API void godot_winui3_inject_mouse_motion(int32_t p_window_id, float p_x, float p_y, float p_rel_x, float p_rel_y);

/**
 * Inject a key press or release event.
 *
 * Modifier state (Shift, Ctrl, Alt, Meta) is read automatically from the
 * current Win32 keyboard state via GetKeyboardState(), so the host does not
 * need to pass them explicitly.
 *
 * @param p_window_id  Godot window ID the event targets.
 * @param p_keycode    Godot Key enum value.
 * @param p_pressed    Non-zero for press, zero for release.
 * @param p_echo       Non-zero if this is an auto-repeat (echo) event.
 * @param p_char       Unicode codepoint of the character produced, or 0.
 */
GODOT_WINUI3_API void godot_winui3_inject_key(int32_t p_window_id, int32_t p_keycode, int32_t p_pressed, int32_t p_echo, uint32_t p_char);

/**
 * Inject a scroll-wheel event.
 *
 * @param p_window_id  Godot window ID the event targets.
 * @param p_x          Panel-local X position in pixels at the time of the scroll.
 * @param p_y          Panel-local Y position in pixels at the time of the scroll.
 * @param p_delta_x    Horizontal scroll amount in "notches" (positive = right).
 *                     Pass e.GetCurrentPoint(panel).Properties.MouseWheelDelta / 120.0f
 *                     for WinUI3 PointerWheelChanged events (note: WinUI3 negates the
 *                     vertical delta relative to Win32, so negate p_delta_y accordingly).
 * @param p_delta_y    Vertical scroll amount in notches (positive = up / away from user).
 */
GODOT_WINUI3_API void godot_winui3_inject_mouse_wheel(int32_t p_window_id, float p_x, float p_y, float p_delta_x, float p_delta_y);

/**
 * Set the input routing mode for the WinUI3 embed.
 *
 * @param p_mode  GODOT_WINUI3_INPUT_NATIVE (0) or GODOT_WINUI3_INPUT_XAML (1).
 *                Defaults to NATIVE. Call before the first frame.
 */
GODOT_WINUI3_API void godot_winui3_set_input_mode(int32_t p_mode);

/* -----------------------------------------------------------------------
 * Host <-> Engine messaging (WinUI3Host singleton).
 *
 * Mirrors the convention used by Android's GodotPlugin and Web's
 * JavaScriptBridge: a Godot Object is exposed as an Engine singleton
 * named "WinUI3Host", and host code talks to it through this C ABI.
 *
 * Wire format on the boundary is JSON (UTF-8). Args are encoded as a
 * JSON array; return values are any JSON value. The engine converts
 * to/from Variant internally so GDScript handlers / signal listeners
 * see typed Dictionary / Array / primitives.
 *
 * Direction matrix:
 *
 *   GDScript  ─ WinUI3Host.send_to_host(method, args) ──▶  host callback
 *   Host      ─ godot_winui3_call_engine(method, args) ──▶ GDScript handler
 *
 * Threading: all calls in both directions must happen on the engine
 * iteration thread (the WinUI3 UI thread when using a DispatcherQueueTimer).
 * The engine is not thread-safe — marshal cross-thread callers with
 * dispatcher.TryEnqueue() before invoking these functions.
 * -----------------------------------------------------------------------
 */

/**
 * Engine -> Host callback signature.
 *
 * Invoked when GDScript runs WinUI3Host.send_to_host(method, args).
 *   p_method     — UTF-8 method name (e.g. "device_info_changed").
 *   p_args_json  — UTF-8 JSON array of arguments. Never NULL; "[]" if empty.
 *
 * To return a value, call godot_winui3_set_call_return() from inside the
 * callback. Skipping that call is equivalent to returning null.
 *
 * Pointers are owned by the engine and only valid for the duration of the
 * callback — copy them out if you need to defer work.
 */
typedef void (*godot_winui3_host_msg_func)(const char *p_method, const char *p_args_json);

/**
 * Install the engine -> host callback. Pass NULL to clear.
 * Call before godot_winui3_engine_start() so messages emitted during
 * GDScript _ready() are not dropped.
 */
GODOT_WINUI3_API void godot_winui3_set_host_message_callback(godot_winui3_host_msg_func p_callback);

/**
 * Set the JSON return value for the currently-running host callback.
 * Only valid to call from inside a godot_winui3_host_msg_func invocation;
 * outside that scope it is silently ignored. The bytes are copied internally,
 * so the caller may free p_json after this returns.
 *
 * Pass NULL or an empty string for "no return value" (Variant::NIL on the
 * GDScript side).
 */
GODOT_WINUI3_API void godot_winui3_set_call_return(const char *p_json);

/**
 * Host -> Engine call. Invokes the GDScript handler registered via
 * WinUI3Host.register_handler(p_method, callable) and also fires the
 * WinUI3Host.host_message_received signal.
 *
 *   p_method     — UTF-8 method name to dispatch.
 *   p_args_json  — UTF-8 JSON array of arguments, or NULL / "" for no args.
 *   r_ret_json   — receives a heap-allocated UTF-8 JSON return value, or
 *                  NULL if the handler returned null / no handler matched.
 *                  When non-NULL, caller must free with godot_winui3_free_string().
 *
 * Returns 1 on success (including the "no handler" case — that returns
 * NULL in *r_ret_json), 0 if the engine is not running or the bridge
 * singleton is missing.
 */
GODOT_WINUI3_API int32_t godot_winui3_call_engine(const char *p_method, const char *p_args_json, char **r_ret_json);

/**
 * Free a string returned by godot_winui3_call_engine().
 * Safe to call with NULL.
 */
GODOT_WINUI3_API void godot_winui3_free_string(char *p_str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WINUI3_ENABLED */

#endif /* GODOT_WINUI3_EMBED_H */
