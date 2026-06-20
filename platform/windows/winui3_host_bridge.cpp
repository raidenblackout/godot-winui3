/**************************************************************************/
/*  winui3_host_bridge.cpp                                                */
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

#include "winui3_host_bridge.h"

#include "core/config/engine.h"
#include "core/error/error_macros.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"

WinUI3HostBridge *WinUI3HostBridge::singleton = nullptr;

static WinUI3HostBridge *bridge_instance = nullptr;

void WinUI3HostBridge::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_to_host", "method", "args"), &WinUI3HostBridge::send_to_host, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("register_handler", "method", "handler"), &WinUI3HostBridge::register_handler);
	ClassDB::bind_method(D_METHOD("unregister_handler", "method"), &WinUI3HostBridge::unregister_handler);
	ClassDB::bind_method(D_METHOD("has_handler", "method"), &WinUI3HostBridge::has_handler);

	// Catch-all signal that fires for every host->engine call, regardless of
	// whether a handler is registered. Useful for logging / debugging.
	ADD_SIGNAL(MethodInfo("host_message_received",
			PropertyInfo(Variant::STRING_NAME, "method"),
			PropertyInfo(Variant::ARRAY, "args")));
}

WinUI3HostBridge::WinUI3HostBridge() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "WinUI3HostBridge singleton already exists.");
	singleton = this;
}

WinUI3HostBridge::~WinUI3HostBridge() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

Variant WinUI3HostBridge::send_to_host(const StringName &p_method, const Array &p_args) {
	if (host_callback == nullptr) {
		WARN_PRINT_ONCE(vformat("WinUI3Host.send_to_host('%s'): no host callback registered; message dropped.", String(p_method)));
		return Variant();
	}

	String args_json = JSON::stringify(p_args);
	CharString method_utf8 = String(p_method).utf8();
	CharString args_utf8 = args_json.utf8();

	pending_return_json = String();
	pending_return_set = false;

	host_callback(method_utf8.get_data(), args_utf8.get_data());

	if (!pending_return_set) {
		return Variant();
	}

	String ret_json = pending_return_json;
	pending_return_json = String();
	pending_return_set = false;

	if (ret_json.is_empty()) {
		return Variant();
	}

	JSON json_parser;
	if (json_parser.parse(ret_json) != OK) {
		ERR_PRINT(vformat("WinUI3HostBridge: bad return JSON (line %d): %s", json_parser.get_error_line(), json_parser.get_error_message()));
		return Variant();
	}
	return json_parser.get_data();
}

void WinUI3HostBridge::register_handler(const StringName &p_method, const Callable &p_handler) {
	if (!p_handler.is_valid()) {
		handlers.erase(p_method);
		return;
	}
	handlers[p_method] = p_handler;
}

void WinUI3HostBridge::unregister_handler(const StringName &p_method) {
	handlers.erase(p_method);
}

bool WinUI3HostBridge::has_handler(const StringName &p_method) const {
	return handlers.has(p_method);
}

void WinUI3HostBridge::set_host_callback(HostMessageFunc p_callback) {
	host_callback = p_callback;
}

void WinUI3HostBridge::set_pending_return(const String &p_json) {
	pending_return_json = p_json;
	pending_return_set = true;
}

String WinUI3HostBridge::dispatch_host_call(const String &p_method, const String &p_args_json) {
	Variant args_var;
	if (!p_args_json.is_empty()) {
		JSON json_parser;
		if (json_parser.parse(p_args_json) != OK) {
			ERR_PRINT(vformat("WinUI3HostBridge: bad args JSON (line %d): %s", json_parser.get_error_line(), json_parser.get_error_message()));
			return String();
		}
		args_var = json_parser.get_data();
	}

	Array args;
	if (args_var.get_type() == Variant::ARRAY) {
		args = args_var;
	} else if (args_var.get_type() != Variant::NIL) {
		// Single-value payload — wrap in a one-element array so handlers see
		// the same shape regardless of caller convention.
		args.push_back(args_var);
	}

	StringName method_name = p_method;
	emit_signal(SNAME("host_message_received"), method_name, args);

	HashMap<StringName, Callable>::Iterator it = handlers.find(method_name);
	if (!it) {
		return String();
	}

	const Callable &handler = it->value;
	if (!handler.is_valid()) {
		handlers.remove(it);
		return String();
	}

	Variant ret = handler.callv(args);
	if (ret.get_type() == Variant::NIL) {
		return String();
	}
	return JSON::stringify(ret);
}

// Defined in godot_winui3_embed.cpp. Applies any host callback that was
// registered before this bridge was constructed.
extern void godot_winui3_apply_pending_host_callback(WinUI3HostBridge *p_bridge);

void register_winui3_host_bridge() {
	if (bridge_instance != nullptr) {
		return;
	}
	GDREGISTER_ABSTRACT_CLASS(WinUI3HostBridge);
	bridge_instance = memnew(WinUI3HostBridge);
	Engine::get_singleton()->add_singleton(Engine::Singleton("WinUI3Host", bridge_instance));
	// Apply any callback stashed by libgodot_set_host_message_callback()
	// before this singleton existed.
	godot_winui3_apply_pending_host_callback(bridge_instance);
}

void unregister_winui3_host_bridge() {
	if (bridge_instance != nullptr) {
		memdelete(bridge_instance);
		bridge_instance = nullptr;
	}
}

#endif // WINUI3_ENABLED
