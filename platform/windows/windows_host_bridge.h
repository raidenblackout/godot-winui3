/**************************************************************************/
/*  windows_embed_host_bridge.h                                                  */
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

#pragma once

#ifdef WINDOWS_EMBED_ENABLED

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/callable.h"

// Bidirectional message bus between an embedded Godot engine and a WindowsEmbed host
// process. Mirrors the conventions of Android's GodotPlugin / Web's
// JavaScriptBridge — a singleton Object exposed as Engine::Singleton("WindowsEmbedHost"),
// callable from GDScript, with a C ABI for the host on the other side.
//
// Wire format on the C boundary is JSON (UTF-8). Args are encoded as a JSON
// array; the return value is any JSON value. The singleton converts to/from
// Variant internally so GDScript handlers see typed Dictionary/Array/etc.
class WindowsEmbedHostBridge : public Object {
	GDCLASS(WindowsEmbedHostBridge, Object);

public:
	// Engine -> Host callback. Fired when GDScript invokes
	// WindowsEmbedHost.send_to_host(method, args). The host populates the return
	// value (if any) by calling libgodot_set_call_return() from inside this
	// callback.
	typedef void (*HostMessageFunc)(const char *p_method, const char *p_args_json);

private:
	static WindowsEmbedHostBridge *singleton;

	HostMessageFunc host_callback = nullptr;

	// Buffer used by libgodot_set_call_return(). Filled during a host
	// callback invocation; consumed (and cleared) by send_to_host() after the
	// callback returns. Single-threaded — host runs on the engine thread.
	String pending_return_json;
	bool pending_return_set = false;

	HashMap<StringName, Callable> handlers;

protected:
	static void _bind_methods();

public:
	static WindowsEmbedHostBridge *get_singleton() { return singleton; }

	// GDScript-facing API.
	Variant send_to_host(const StringName &p_method, const Array &p_args);
	void register_handler(const StringName &p_method, const Callable &p_handler);
	void unregister_handler(const StringName &p_method);
	bool has_handler(const StringName &p_method) const;

	// C ABI helpers.
	void set_host_callback(HostMessageFunc p_callback);
	void set_pending_return(const String &p_json);
	// Dispatches a host->engine call. Decodes args_json, invokes the registered
	// Callable for p_method (and emits the host_message_received signal), and
	// returns the JSON-stringified Variant return value.
	String dispatch_host_call(const String &p_method, const String &p_args_json);

	WindowsEmbedHostBridge();
	~WindowsEmbedHostBridge();
};

void register_windows_embed_host_bridge();
void unregister_windows_embed_host_bridge();

#endif // WINDOWS_EMBED_ENABLED
