// GodotWindowsEmbedNative.cs
// Raw P/Invoke declarations matching the C ABI exported by
// platform/windows/godot_windows_embed_embed.cpp. Prefer GodotWindowsEmbedEmbed instead.

namespace Godot.WindowsEmbed.Embedding.Interop;

using System;
using System.Runtime.InteropServices;

internal static class GodotWindowsEmbedNative
{
	// Update if the Godot shared-library output name differs in your build.
	private const string DLL_NAME = "godot";

	internal enum LibGodotInputEventType : int
	{
		MouseButton = 1,
		MouseMotion = 2,
		MouseWheel = 3,
		Key = 4,
		ScreenTouch = 5,
		ScreenDrag = 6,
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotMouseButtonEvent
	{
		public int Button;
		public int Pressed;
		public float X;
		public float Y;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotMouseMotionEvent
	{
		public float X;
		public float Y;
		public float RelativeX;
		public float RelativeY;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotMouseWheelEvent
	{
		public float X;
		public float Y;
		public float DeltaX;
		public float DeltaY;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotKeyEvent
	{
		public int Keycode;
		public int Pressed;
		public int Echo;
		public uint Unicode;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotScreenTouchEvent
	{
		public int Index;
		public int Pressed;
		public int Canceled;
		public int DoubleTap;
		public float X;
		public float Y;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotScreenDragEvent
	{
		public int Index;
		public float X;
		public float Y;
		public float RelativeX;
		public float RelativeY;
		public float VelocityX;
		public float VelocityY;
		public float Pressure;
	}

	[StructLayout(LayoutKind.Explicit)]
	internal struct LibGodotInputEventData
	{
		[FieldOffset(0)] public LibGodotMouseButtonEvent MouseButton;
		[FieldOffset(0)] public LibGodotMouseMotionEvent MouseMotion;
		[FieldOffset(0)] public LibGodotMouseWheelEvent MouseWheel;
		[FieldOffset(0)] public LibGodotKeyEvent Key;
		[FieldOffset(0)] public LibGodotScreenTouchEvent ScreenTouch;
		[FieldOffset(0)] public LibGodotScreenDragEvent ScreenDrag;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct LibGodotInputEvent
	{
		public uint Size;
		public int Type;
		public int WindowId;
		public uint Modifiers;
		public LibGodotInputEventData Data;

		public static LibGodotInputEvent Create(LibGodotInputEventType type, int windowId)
		{
			return new LibGodotInputEvent
			{
				Size = (uint)Marshal.SizeOf<LibGodotInputEvent>(),
				Type = (int)type,
				WindowId = windowId,
			};
		}
	}

	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotLogDelegate(IntPtr message, int level);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_log_callback(GodotLogDelegate? callback);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_embedded_parent_window(IntPtr window);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int libgodot_engine_setup(int argc, IntPtr argv);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int libgodot_engine_start();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int libgodot_engine_iteration();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_engine_shutdown();

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_native_window(IntPtr nativeWindow);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_attach_surface(int windowId, IntPtr nativeSurface);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_detach_surface(int windowId);

	// p_ctx is engine-owned and only valid for the duration of the call.
	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotWorkDelegate(IntPtr ctx);

	// Host-supplied dispatcher: run work(ctx) on the UI thread and block until done.
	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotUiDispatchDelegate(IntPtr workFuncPtr, IntPtr ctx);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_ui_dispatcher(GodotUiDispatchDelegate? dispatch);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_surface_set_size(
		int windowId,
		int width,
		int height);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_surface_set_scale(
		int windowId,
		float scaleX,
		float scaleY);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int libgodot_inject_input_event(ref LibGodotInputEvent inputEvent);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_input_mode(int mode);

	// Host <-> Engine messaging.

	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	internal delegate void GodotHostMsgDelegate(IntPtr methodUtf8, IntPtr argsJsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_host_message_callback(GodotHostMsgDelegate? callback);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_set_call_return(IntPtr jsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern int libgodot_call_engine(IntPtr methodUtf8, IntPtr argsJsonUtf8, out IntPtr retJsonUtf8);

	[DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void libgodot_free_string(IntPtr str);
}
