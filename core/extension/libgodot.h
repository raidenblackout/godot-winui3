/**************************************************************************/
/*  libgodot.h                                                            */
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

#include "core/extension/gdextension_interface.gen.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Export macros for DLL visibility
#if defined(_MSC_VER) || defined(__MINGW32__)
#define LIBGODOT_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define LIBGODOT_API __attribute__((visibility("default")))
#else
#define LIBGODOT_API
#endif

/**
 * @name libgodot_create_godot_instance
 * @since 4.6
 *
 * Creates a new Godot instance.
 *
 * @param p_argc The number of command line arguments.
 * @param p_argv The C-style array of command line arguments.
 * @param p_init_func GDExtension initialization function of the host application.
 *
 * @return A pointer to created \ref GodotInstance GDExtension object or nullptr if there was an error.
 */
LIBGODOT_API GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func);

/**
 * @name libgodot_destroy_godot_instance
 * @since 4.6
 *
 * Destroys an existing Godot instance.
 *
 * @param p_godot_instance The reference to the GodotInstance object to destroy.
 *
 */
LIBGODOT_API void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance);

/**
 * Input routing mode used by libgodot_set_input_mode().
 */
#define LIBGODOT_INPUT_MODE_NATIVE 0
#define LIBGODOT_INPUT_MODE_EMBEDDER 1

/**
 * Types accepted by LibGodotInputEvent::type.
 */
typedef enum {
	LIBGODOT_INPUT_EVENT_NONE = 0,
	LIBGODOT_INPUT_EVENT_MOUSE_BUTTON = 1,
	LIBGODOT_INPUT_EVENT_MOUSE_MOTION = 2,
	LIBGODOT_INPUT_EVENT_MOUSE_WHEEL = 3,
	LIBGODOT_INPUT_EVENT_KEY = 4,
	LIBGODOT_INPUT_EVENT_SCREEN_TOUCH = 5,
	LIBGODOT_INPUT_EVENT_SCREEN_DRAG = 6,
} LibGodotInputEventType;

/**
 * Modifier bits accepted by LibGodotInputEvent::modifiers.
 */
typedef enum {
	LIBGODOT_INPUT_MODIFIER_SHIFT = 1 << 0,
	LIBGODOT_INPUT_MODIFIER_CTRL = 1 << 1,
	LIBGODOT_INPUT_MODIFIER_ALT = 1 << 2,
	LIBGODOT_INPUT_MODIFIER_META = 1 << 3,
} LibGodotInputModifier;

typedef struct {
	int32_t button;
	int32_t pressed;
	float x;
	float y;
} LibGodotMouseButtonEvent;

typedef struct {
	float x;
	float y;
	float relative_x;
	float relative_y;
} LibGodotMouseMotionEvent;

typedef struct {
	float x;
	float y;
	float delta_x;
	float delta_y;
} LibGodotMouseWheelEvent;

typedef struct {
	int32_t keycode;
	int32_t pressed;
	int32_t echo;
	uint32_t unicode;
} LibGodotKeyEvent;

typedef struct {
	int32_t index;
	int32_t pressed;
	int32_t canceled;
	int32_t double_tap;
	float x;
	float y;
} LibGodotScreenTouchEvent;

typedef struct {
	int32_t index;
	float x;
	float y;
	float relative_x;
	float relative_y;
	float velocity_x;
	float velocity_y;
	float pressure;
} LibGodotScreenDragEvent;

typedef struct {
	uint32_t size;
	int32_t type;
	int32_t window_id;
	uint32_t modifiers;
	union {
		LibGodotMouseButtonEvent mouse_button;
		LibGodotMouseMotionEvent mouse_motion;
		LibGodotMouseWheelEvent mouse_wheel;
		LibGodotKeyEvent key;
		LibGodotScreenTouchEvent screen_touch;
		LibGodotScreenDragEvent screen_drag;
	} data;
} LibGodotInputEvent;

typedef void (*libgodot_log_func)(const char *p_message, int32_t p_level);
typedef void (*libgodot_work_func)(void *p_ctx);
typedef void (*libgodot_ui_dispatch_func)(libgodot_work_func p_work, void *p_ctx);
typedef void (*libgodot_host_msg_func)(const char *p_method, const char *p_args_json);

LIBGODOT_API void libgodot_set_log_callback(libgodot_log_func p_callback);
LIBGODOT_API void libgodot_set_embedded_parent_window(void *p_native_parent_window);
LIBGODOT_API int32_t libgodot_engine_setup(int32_t p_argc, char **p_argv);
LIBGODOT_API int32_t libgodot_engine_start();
LIBGODOT_API int32_t libgodot_engine_iteration();
LIBGODOT_API void libgodot_engine_shutdown();

/**
 * Sets the main native rendering surface before the DisplayServer exists.
 *
 * The pointer type is selected by the platform build. For example, Windows
 * composition hosts pass ISwapChainPanelNative*, Apple embedded hosts pass a
 * CAMetalLayer*, and Android hosts pass an ANativeWindow*.
 */
LIBGODOT_API void libgodot_set_native_window(void *p_native_window);
LIBGODOT_API void libgodot_attach_surface(int32_t p_window_id, void *p_native_surface);
LIBGODOT_API void libgodot_detach_surface(int32_t p_window_id);
LIBGODOT_API void libgodot_surface_set_size(int32_t p_window_id, int32_t p_width, int32_t p_height);
LIBGODOT_API void libgodot_surface_set_scale(int32_t p_window_id, float p_scale_x, float p_scale_y);
LIBGODOT_API void libgodot_set_ui_dispatcher(libgodot_ui_dispatch_func p_dispatch);
LIBGODOT_API int32_t libgodot_inject_input_event(const LibGodotInputEvent *p_event);
LIBGODOT_API void libgodot_set_input_mode(int32_t p_mode);

LIBGODOT_API void libgodot_set_host_message_callback(libgodot_host_msg_func p_callback);
LIBGODOT_API void libgodot_set_call_return(const char *p_json);
LIBGODOT_API int32_t libgodot_call_engine(const char *p_method, const char *p_args_json, char **r_ret_json);
LIBGODOT_API void libgodot_free_string(char *p_str);

#ifdef __cplusplus
}
#endif // __cplusplus
