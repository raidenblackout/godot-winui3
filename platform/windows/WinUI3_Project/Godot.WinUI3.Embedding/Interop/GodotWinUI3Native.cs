// GodotWinUI3Native.cs
// Raw P/Invoke declarations matching the C ABI exported by
// platform/windows/godot_winui3_embed.cpp. Prefer GodotWinUI3Embed instead.

namespace Godot.WinUI3.Embedding.Interop;

using System;
using System.Runtime.InteropServices;

internal static class GodotWinUI3Native
{
	// Update if the Godot shared-library output name differs in your build.
	private const string DLL_NAME = "godot";

	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotLogDelegate(IntPtr message, int level);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_log_callback(GodotLogDelegate? callback);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_embedded_parent_hwnd(IntPtr hwnd);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int godot_winui3_engine_setup(int argc, IntPtr argv);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int godot_winui3_engine_start();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int godot_winui3_engine_iteration();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_engine_shutdown();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_swap_chain_panel(
		int windowId,
		IntPtr panelNative);

	// p_ctx is engine-owned and only valid for the duration of the call.
	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotWorkDelegate(IntPtr ctx);

	// Host-supplied dispatcher: run work(ctx) on the UI thread and block until done.
	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotUiDispatchDelegate(IntPtr workFuncPtr, IntPtr ctx);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_ui_dispatcher(GodotUiDispatchDelegate? dispatch);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_notify_panel_resize(
		int windowId,
		int width,
		int height);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_composition_scale(
		int windowId,
		float scaleX,
		float scaleY);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_inject_mouse_button(
		int windowId,
		int button,
		int pressed,
		float x,
		float y);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_inject_mouse_motion(
		int windowId,
		float x,
		float y,
		float relX,
		float relY);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_inject_key(
		int windowId,
		int keycode,
		int pressed,
		int echo,
		uint character);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_inject_mouse_wheel(
		int windowId,
		float x,
		float y,
		float deltaX,
		float deltaY);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_input_mode(int mode);

	// Host <-> Engine messaging.

	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotHostMsgDelegate(IntPtr methodUtf8, IntPtr argsJsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_host_message_callback(GodotHostMsgDelegate? callback);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_set_call_return(IntPtr jsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int godot_winui3_call_engine(IntPtr methodUtf8, IntPtr argsJsonUtf8, out IntPtr retJsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void godot_winui3_free_string(IntPtr str);
}
