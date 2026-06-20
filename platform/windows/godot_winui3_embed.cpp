/**************************************************************************/
/*  godot_winui3_embed.cpp                                                */
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

#ifdef WINUI3_ENABLED

#include "godot_winui3_embed.h"

#include "display_server_windows.h"
#include "winui3_host_bridge.h"

#include "core/error/error_macros.h"
#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/input/input.h"
#include "core/os/memory.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "servers/display/display_server.h"
#include "servers/display/display_server_enums.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Engine lifecycle
//
// Thin wrappers around libgodot_create_godot_instance / GodotInstance so that
// a WinUI3 host can drive the engine without dealing with GDExtension interop.
// ---------------------------------------------------------------------------

static GodotInstance *g_winui3_instance = nullptr;

// ---------------------------------------------------------------------------
// Log / error callback
// ---------------------------------------------------------------------------

static libgodot_log_func s_log_callback = nullptr;
static PrintHandlerList s_print_handler;
static ErrorHandlerList s_error_handler;

static void _winui3_print_handler(void *, const String &p_message, bool p_error, bool p_rich) {
	if (!s_log_callback) {
		return;
	}
	CharString cs = p_message.utf8();
	s_log_callback(cs.get_data(), p_error ? 2 : 0);
}

static void _winui3_error_handler(void *, const char *p_func, const char *p_file, int p_line,
		const char *p_err, const char *p_descr, bool p_editor_notify, ErrorHandlerType p_type) {
	(void)p_editor_notify;
	if (!s_log_callback) {
		return;
	}
	int32_t level = (p_type == ERR_HANDLER_WARNING) ? 1 : 2;
	static const char *const types[] = { "ERROR", "WARNING", "SCRIPT", "SHADER" };
	const char *type_str = (p_type < 4) ? types[p_type] : "ERROR";
	String msg;
	if (p_descr && p_descr[0] != '\0') {
		msg = vformat("[%s] %s @ %s:%d\n  %s: %s",
				type_str, String(p_func), String(p_file), p_line, String(p_err), String(p_descr));
	} else {
		msg = vformat("[%s] %s @ %s:%d\n  %s",
				type_str, String(p_func), String(p_file), p_line, String(p_err));
	}
	CharString cs = msg.utf8();
	s_log_callback(cs.get_data(), level);
}

void libgodot_set_log_callback(libgodot_log_func p_callback) {
	if (s_log_callback) {
		remove_print_handler(&s_print_handler);
		remove_error_handler(&s_error_handler);
	}
	s_log_callback = p_callback;
	if (p_callback) {
		s_print_handler.printfunc = _winui3_print_handler;
		s_print_handler.userdata = nullptr;
		add_print_handler(&s_print_handler);

		s_error_handler.errfunc = _winui3_error_handler;
		s_error_handler.userdata = nullptr;
		add_error_handler(&s_error_handler);
	}
}

// Stub GDExtension init function. The host application is not itself a
// GDExtension — it just embeds the engine — so we register an empty extension
// that satisfies libgodot's bookkeeping without registering any classes.
static void _winui3_stub_noop(void *, GDExtensionInitializationLevel) {}

static GDExtensionBool _winui3_stub_extension_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	(void)p_get_proc_address;
	(void)p_library;
	r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
	r_initialization->userdata = nullptr;
	r_initialization->initialize = _winui3_stub_noop;
	r_initialization->deinitialize = _winui3_stub_noop;
	return 1;
}

void libgodot_set_embedded_parent_window(void *p_hwnd) {
	DisplayServerWindows::set_embedded_parent_hwnd(p_hwnd);
}

int32_t libgodot_engine_setup(int32_t p_argc, char **p_argv) {
	ERR_FAIL_COND_V_MSG(g_winui3_instance != nullptr, 0, "Godot engine is already initialized.");
	ERR_FAIL_COND_V(p_argc < 1, 0);
	ERR_FAIL_NULL_V(p_argv, 0);

	GDExtensionObjectPtr ptr = libgodot_create_godot_instance(p_argc, p_argv, &_winui3_stub_extension_init);
	if (ptr == nullptr) {
		return 0;
	}
	g_winui3_instance = (GodotInstance *)ptr;
	return 1;
}

// Deferred resize state — populated before DisplayServer exists,
// replayed by libgodot_engine_start() after Main::setup2() creates it.
// (The swap chain panel uses a different mechanism: DisplayServerWindows::_pending_swap_chain_panel.)
struct DeferredResizeState {
	int32_t window_id = 0;
	int32_t width = 0;
	int32_t height = 0;
	bool pending = false;
};
static DeferredResizeState s_deferred_resize;

int32_t libgodot_engine_start() {
	ERR_FAIL_NULL_V_MSG(g_winui3_instance, 0, "Call libgodot_engine_setup() first.");
	bool ok = g_winui3_instance->start();
	if (ok) {
		// Main::setup2() (called inside start()) creates the DisplayServer.
		// Replay any resize call that arrived before it existed.
		// (The swap chain panel was applied earlier via _pending_swap_chain_panel.)
		DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
		if (ds != nullptr && s_deferred_resize.pending) {
			ds->window_notify_panel_resize(
					DisplayServerEnums::WindowID(s_deferred_resize.window_id),
					s_deferred_resize.width,
					s_deferred_resize.height);
			s_deferred_resize.pending = false;
		}
	}
	return ok ? 1 : 0;
}

int32_t libgodot_engine_iteration() {
	ERR_FAIL_NULL_V_MSG(g_winui3_instance, 1, "Engine not initialized — caller should stop iterating.");
	// GodotInstance::iteration() returns true when the main loop wants to quit.
	return g_winui3_instance->iteration() ? 1 : 0;
}

void libgodot_engine_shutdown() {
	if (g_winui3_instance == nullptr) {
		return;
	}
	DisplayServerWindows::set_pending_swap_chain_panel(nullptr);
	DisplayServerWindows::set_pending_composition_scale(1.0f, 1.0f);
	libgodot_destroy_godot_instance((GDExtensionObjectPtr)g_winui3_instance);
	g_winui3_instance = nullptr;
}

// ---------------------------------------------------------------------------
// Panel / swap chain helpers
// ---------------------------------------------------------------------------

void libgodot_set_native_window(void *p_native_window) {
	libgodot_attach_surface(DisplayServerEnums::MAIN_WINDOW_ID, p_native_window);
}

void libgodot_attach_surface(int32_t p_window_id, void *p_native_surface) {
	DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
	if (ds == nullptr) {
		// Store on DisplayServerWindows so it is applied during _create_rendering_context_window,
		// avoiding a destroy+create cycle that would leave a dangling Surface pointer in the
		// RenderingDevice's SwapChain. Only the main window (id 0) is supported at pre-init time.
		ERR_FAIL_COND_MSG(p_window_id != DisplayServerEnums::MAIN_WINDOW_ID, "Only the main window can receive a native surface before DisplayServer initialization.");
		DisplayServerWindows::set_pending_swap_chain_panel(
				static_cast<ISwapChainPanelNative *>(p_native_surface));
		return;
	}
	ds->window_set_swap_chain_panel(
			DisplayServerEnums::WindowID(p_window_id),
			p_native_surface);
}

void libgodot_detach_surface(int32_t p_window_id) {
	libgodot_attach_surface(p_window_id, nullptr);
}

void libgodot_set_ui_dispatcher(libgodot_ui_dispatch_func p_dispatch) {
	// Signatures are identical; the cast bridges the C ABI typedef and the
	// DisplayServer-side typedef without coupling the two headers.
	DisplayServerWindows::set_ui_dispatcher(
			reinterpret_cast<DisplayServerWindows::WinUI3UIDispatchFunc>(p_dispatch));
}

void libgodot_surface_set_size(int32_t p_window_id, int32_t p_width, int32_t p_height) {
	DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
	if (ds == nullptr) {
		s_deferred_resize = { p_window_id, p_width, p_height, true };
		return;
	}
	ds->window_notify_panel_resize(
			DisplayServerEnums::WindowID(p_window_id),
			p_width,
			p_height);
}

void libgodot_surface_set_scale(int32_t p_window_id, float p_scale_x, float p_scale_y) {
	DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
	if (ds == nullptr) {
		// Stash on DisplayServerWindows so it is applied to the WindowData during DisplayServer
		// construction, before _create_rendering_context_window builds the Surface.
		// Only the main window is supported pre-init.
		DisplayServerWindows::set_pending_composition_scale(p_scale_x, p_scale_y);
		return;
	}
	ds->window_set_composition_scale(
			DisplayServerEnums::WindowID(p_window_id),
			p_scale_x,
			p_scale_y);
}

// ---------------------------------------------------------------------------
// Input injection helpers
//
// Delegate to the static inject methods on DisplayServerWindows so that all
// button-mask tracking, velocity computation, and unicode fixup are handled
// consistently in a single place.
// ---------------------------------------------------------------------------

static void _libgodot_apply_modifiers(InputEventWithModifiers *p_event, uint32_t p_modifiers) {
	p_event->set_shift_pressed((p_modifiers & LIBGODOT_INPUT_MODIFIER_SHIFT) != 0);
	p_event->set_ctrl_pressed((p_modifiers & LIBGODOT_INPUT_MODIFIER_CTRL) != 0);
	p_event->set_alt_pressed((p_modifiers & LIBGODOT_INPUT_MODIFIER_ALT) != 0);
	p_event->set_meta_pressed((p_modifiers & LIBGODOT_INPUT_MODIFIER_META) != 0);
}

int32_t libgodot_inject_input_event(const LibGodotInputEvent *p_event) {
	ERR_FAIL_NULL_V(p_event, 0);
	ERR_FAIL_COND_V_MSG(p_event->size < sizeof(LibGodotInputEvent), 0, "LibGodotInputEvent::size is smaller than the ABI struct.");

	DisplayServerEnums::WindowID window_id = DisplayServerEnums::WindowID(p_event->window_id);

	switch (p_event->type) {
		case LIBGODOT_INPUT_EVENT_MOUSE_BUTTON: {
			const LibGodotMouseButtonEvent &src = p_event->data.mouse_button;
			Ref<InputEventMouseButton> mb;
			mb.instantiate();
			mb->set_window_id(window_id);
			mb->set_button_index(MouseButton(src.button));
			mb->set_pressed(src.pressed != 0);
			mb->set_position(Vector2(src.x, src.y));
			mb->set_global_position(Vector2(src.x, src.y));
			mb->set_button_mask(Input::get_singleton()->get_mouse_button_mask());
			_libgodot_apply_modifiers(*mb, p_event->modifiers);
			Input::get_singleton()->parse_input_event(mb);
			return 1;
		}
		case LIBGODOT_INPUT_EVENT_MOUSE_MOTION: {
			const LibGodotMouseMotionEvent &src = p_event->data.mouse_motion;
			Ref<InputEventMouseMotion> mm;
			mm.instantiate();
			mm->set_window_id(window_id);
			mm->set_position(Vector2(src.x, src.y));
			mm->set_global_position(Vector2(src.x, src.y));
			mm->set_relative(Vector2(src.relative_x, src.relative_y));
			mm->set_relative_screen_position(Vector2(src.relative_x, src.relative_y));
			mm->set_velocity(Input::get_singleton()->get_last_mouse_velocity());
			mm->set_screen_velocity(mm->get_velocity());
			mm->set_button_mask(Input::get_singleton()->get_mouse_button_mask());
			_libgodot_apply_modifiers(*mm, p_event->modifiers);
			Input::get_singleton()->parse_input_event(mm);
			Input::get_singleton()->set_mouse_position(Point2i(src.x, src.y));
			return 1;
		}
		case LIBGODOT_INPUT_EVENT_MOUSE_WHEEL: {
			const LibGodotMouseWheelEvent &src = p_event->data.mouse_wheel;
			DisplayServerWindows::_winui3_inject_mouse_wheel(window_id, src.x, src.y, src.delta_x, src.delta_y);
			return 1;
		}
		case LIBGODOT_INPUT_EVENT_KEY: {
			const LibGodotKeyEvent &src = p_event->data.key;
			Ref<InputEventKey> k;
			k.instantiate();
			k->set_window_id(window_id);
			k->set_keycode(Key(src.keycode));
			k->set_physical_keycode(Key(src.keycode));
			k->set_key_label(Key(src.keycode));
			k->set_pressed(src.pressed != 0);
			k->set_echo(src.echo != 0);
			_libgodot_apply_modifiers(*k, p_event->modifiers);
			if (src.unicode != 0) {
				k->set_unicode(char32_t(src.unicode));
			}
			Input::get_singleton()->parse_input_event(k);
			return 1;
		}
		case LIBGODOT_INPUT_EVENT_SCREEN_TOUCH: {
			const LibGodotScreenTouchEvent &src = p_event->data.screen_touch;
			Ref<InputEventScreenTouch> st;
			st.instantiate();
			st->set_window_id(window_id);
			st->set_index(src.index);
			st->set_position(Vector2(src.x, src.y));
			st->set_pressed(src.pressed != 0);
			st->set_canceled(src.canceled != 0);
			st->set_double_tap(src.double_tap != 0);
			Input::get_singleton()->parse_input_event(st);
			return 1;
		}
		case LIBGODOT_INPUT_EVENT_SCREEN_DRAG: {
			const LibGodotScreenDragEvent &src = p_event->data.screen_drag;
			Ref<InputEventScreenDrag> sd;
			sd.instantiate();
			sd->set_window_id(window_id);
			sd->set_index(src.index);
			sd->set_position(Vector2(src.x, src.y));
			sd->set_relative(Vector2(src.relative_x, src.relative_y));
			sd->set_relative_screen_position(Vector2(src.relative_x, src.relative_y));
			sd->set_velocity(Vector2(src.velocity_x, src.velocity_y));
			sd->set_screen_velocity(Vector2(src.velocity_x, src.velocity_y));
			sd->set_pressure(src.pressure);
			Input::get_singleton()->parse_input_event(sd);
			return 1;
		}
		default:
			ERR_FAIL_V_MSG(0, "Unsupported LibGodotInputEvent type.");
	}
}

void libgodot_set_input_mode(int32_t p_mode) {
	DisplayServerWindows::set_winui3_input_mode(p_mode);
}

// ---------------------------------------------------------------------------
// Host <-> Engine messaging
//
// JSON-on-the-wire bridge backed by the WinUI3HostBridge singleton. See
// winui3_host_bridge.h for the engine-side surface and the .h above for
// the documented host-facing contract.
// ---------------------------------------------------------------------------

// Stash for a host callback registered before the bridge singleton exists.
// The host is allowed to call libgodot_set_host_message_callback() at any
// time after libgodot_set_log_callback() but before EngineSetup completes.
// register_winui3_host_bridge() calls godot_winui3_apply_pending_host_callback()
// immediately after constructing the bridge so the callback is never dropped.
static libgodot_host_msg_func s_pending_host_callback = nullptr;

// Called by register_winui3_host_bridge() (winui3_host_bridge.cpp) once the
// bridge singleton is live. Applies any callback stashed before setup finished.
void godot_winui3_apply_pending_host_callback(WinUI3HostBridge *p_bridge) {
	if (p_bridge != nullptr && s_pending_host_callback != nullptr) {
		p_bridge->set_host_callback(reinterpret_cast<WinUI3HostBridge::HostMessageFunc>(s_pending_host_callback));
		s_pending_host_callback = nullptr;
	}
}

void libgodot_set_host_message_callback(libgodot_host_msg_func p_callback) {
	WinUI3HostBridge *bridge = WinUI3HostBridge::get_singleton();
	if (bridge == nullptr) {
		// Bridge not created yet — stash and apply in register_winui3_host_bridge().
		s_pending_host_callback = p_callback;
		return;
	}
	s_pending_host_callback = nullptr;
	bridge->set_host_callback(reinterpret_cast<WinUI3HostBridge::HostMessageFunc>(p_callback));
}

void libgodot_set_call_return(const char *p_json) {
	WinUI3HostBridge *bridge = WinUI3HostBridge::get_singleton();
	if (bridge == nullptr) {
		return;
	}
	String s;
	if (p_json != nullptr) {
		s.append_utf8(p_json);
	}
	bridge->set_pending_return(s);
}

int32_t libgodot_call_engine(const char *p_method, const char *p_args_json, char **r_ret_json) {
	if (r_ret_json != nullptr) {
		*r_ret_json = nullptr;
	}
	ERR_FAIL_NULL_V(p_method, 0);

	WinUI3HostBridge *bridge = WinUI3HostBridge::get_singleton();
	if (bridge == nullptr) {
		return 0;
	}

	String method;
	method.append_utf8(p_method);

	String args_json;
	if (p_args_json != nullptr) {
		args_json.append_utf8(p_args_json);
	}

	String ret = bridge->dispatch_host_call(method, args_json);

	if (!ret.is_empty() && r_ret_json != nullptr) {
		CharString utf8 = ret.utf8();
		size_t len = static_cast<size_t>(utf8.length()) + 1;
		// Use the engine's allocator so libgodot_free_string works
		// regardless of which CRT the host links against.
		char *buf = static_cast<char *>(memalloc(len));
		ERR_FAIL_NULL_V(buf, 0);
		memcpy(buf, utf8.get_data(), len);
		*r_ret_json = buf;
	}
	return 1;
}

void libgodot_free_string(char *p_str) {
	if (p_str != nullptr) {
		memfree(p_str);
	}
}

#endif // WINUI3_ENABLED
