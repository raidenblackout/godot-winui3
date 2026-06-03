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

#include "core/error/error_macros.h"
#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/os/memory.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "display_server_windows.h"
#include "servers/display/display_server.h"
#include "servers/display/display_server_enums.h"
#include "winui3_host_bridge.h"

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

static godot_winui3_log_func s_log_callback = nullptr;
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

void godot_winui3_set_log_callback(godot_winui3_log_func p_callback) {
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

void godot_winui3_set_embedded_parent_hwnd(void *p_hwnd) {
	DisplayServerWindows::set_embedded_parent_hwnd(p_hwnd);
}

int32_t godot_winui3_engine_setup(int32_t p_argc, char **p_argv) {
	ERR_FAIL_COND_V_MSG(g_winui3_instance != nullptr, 0, "Godot engine is already initialised.");
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
// replayed by godot_winui3_engine_start() after Main::setup2() creates it.
// (The swap chain panel uses a different mechanism: DisplayServerWindows::_pending_swap_chain_panel.)
struct DeferredResizeState {
	int32_t window_id = 0;
	int32_t width = 0;
	int32_t height = 0;
	bool pending = false;
};
static DeferredResizeState s_deferred_resize;

int32_t godot_winui3_engine_start() {
	ERR_FAIL_NULL_V_MSG(g_winui3_instance, 0, "Call godot_winui3_engine_setup() first.");
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

int32_t godot_winui3_engine_iteration() {
	ERR_FAIL_NULL_V_MSG(g_winui3_instance, 1, "Engine not initialised — caller should stop iterating.");
	// GodotInstance::iteration() returns true when the main loop wants to quit.
	return g_winui3_instance->iteration() ? 1 : 0;
}

void godot_winui3_engine_shutdown() {
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

void godot_winui3_set_swap_chain_panel(int32_t p_window_id, void *p_panel_native) {
	DisplayServerWindows *ds = Object::cast_to<DisplayServerWindows>(DisplayServer::get_singleton());
	if (ds == nullptr) {
		// Store on DisplayServerWindows so it is applied during _create_rendering_context_window,
		// avoiding a destroy+create cycle that would leave a dangling Surface pointer in the
		// RenderingDevice's SwapChain. Only the main window (id 0) is supported at pre-init time.
		DisplayServerWindows::set_pending_swap_chain_panel(
				static_cast<ISwapChainPanelNative *>(p_panel_native));
		return;
	}
	ds->window_set_swap_chain_panel(
			DisplayServerEnums::WindowID(p_window_id),
			p_panel_native);
}

void godot_winui3_set_ui_dispatcher(godot_winui3_ui_dispatch_func p_dispatch) {
	// Signatures are identical; the cast bridges the C ABI typedef and the
	// DisplayServer-side typedef without coupling the two headers.
	DisplayServerWindows::set_ui_dispatcher(
			reinterpret_cast<DisplayServerWindows::WinUI3UIDispatchFunc>(p_dispatch));
}

void godot_winui3_notify_panel_resize(int32_t p_window_id, int32_t p_width, int32_t p_height) {
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

void godot_winui3_set_composition_scale(int32_t p_window_id, float p_scale_x, float p_scale_y) {
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

void godot_winui3_inject_mouse_button(int32_t p_window_id, int32_t p_button, int32_t p_pressed, float p_x, float p_y) {
	DisplayServerWindows::_winui3_inject_mouse_button(
			DisplayServerEnums::WindowID(p_window_id),
			MouseButton(p_button),
			p_pressed != 0,
			p_x,
			p_y);
}

void godot_winui3_inject_mouse_motion(int32_t p_window_id, float p_x, float p_y, float p_rel_x, float p_rel_y) {
	DisplayServerWindows::_winui3_inject_mouse_motion(
			DisplayServerEnums::WindowID(p_window_id),
			p_x,
			p_y,
			p_rel_x,
			p_rel_y);
}

void godot_winui3_inject_key(int32_t p_window_id, int32_t p_keycode, int32_t p_pressed, int32_t p_echo, uint32_t p_char) {
	DisplayServerWindows::_winui3_inject_key(
			DisplayServerEnums::WindowID(p_window_id),
			Key(p_keycode),
			p_pressed != 0,
			p_echo != 0,
			char32_t(p_char));
}

void godot_winui3_inject_mouse_wheel(int32_t p_window_id, float p_x, float p_y, float p_delta_x, float p_delta_y) {
	DisplayServerWindows::_winui3_inject_mouse_wheel(
			DisplayServerEnums::WindowID(p_window_id),
			p_x, p_y, p_delta_x, p_delta_y);
}

void godot_winui3_set_input_mode(int32_t p_mode) {
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
// The host is allowed to call godot_winui3_set_host_message_callback() at any
// time after godot_winui3_set_log_callback() but before EngineSetup completes.
// register_winui3_host_bridge() calls godot_winui3_apply_pending_host_callback()
// immediately after constructing the bridge so the callback is never dropped.
static godot_winui3_host_msg_func s_pending_host_callback = nullptr;

// Called by register_winui3_host_bridge() (winui3_host_bridge.cpp) once the
// bridge singleton is live. Applies any callback stashed before setup finished.
void godot_winui3_apply_pending_host_callback(WinUI3HostBridge *p_bridge) {
	if (p_bridge != nullptr && s_pending_host_callback != nullptr) {
		p_bridge->set_host_callback(reinterpret_cast<WinUI3HostBridge::HostMessageFunc>(s_pending_host_callback));
		s_pending_host_callback = nullptr;
	}
}

void godot_winui3_set_host_message_callback(godot_winui3_host_msg_func p_callback) {
	WinUI3HostBridge *bridge = WinUI3HostBridge::get_singleton();
	if (bridge == nullptr) {
		// Bridge not created yet — stash and apply in register_winui3_host_bridge().
		s_pending_host_callback = p_callback;
		return;
	}
	s_pending_host_callback = nullptr;
	bridge->set_host_callback(reinterpret_cast<WinUI3HostBridge::HostMessageFunc>(p_callback));
}

void godot_winui3_set_call_return(const char *p_json) {
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

int32_t godot_winui3_call_engine(const char *p_method, const char *p_args_json, char **r_ret_json) {
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
		// Use the engine's allocator so godot_winui3_free_string works
		// regardless of which CRT the host links against.
		char *buf = static_cast<char *>(memalloc(len));
		ERR_FAIL_NULL_V(buf, 0);
		memcpy(buf, utf8.get_data(), len);
		*r_ret_json = buf;
	}
	return 1;
}

void godot_winui3_free_string(char *p_str) {
	if (p_str != nullptr) {
		memfree(p_str);
	}
}

#endif // WINUI3_ENABLED
