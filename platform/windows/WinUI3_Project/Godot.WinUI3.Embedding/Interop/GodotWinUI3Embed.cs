// GodotWinUI3Embed.cs
// Typed C# wrapper around the raw P/Invoke declarations in GodotWinUI3Native.
// Callers outside this assembly should go through GodotEngineHost instead of
// calling these methods directly -- the host marshals calls onto the engine
// thread and keeps the pinned native callbacks alive.

namespace Godot.WinUI3.Embedding.Interop;

using System;
using System.Runtime.InteropServices;

public static class GodotWinUI3Embed
{
	// Pinned delegates -- must outlive their native callback registration.
	private static GodotWinUI3Native.GodotLogDelegate? _logDelegatePin;
	private static GodotWinUI3Native.GodotUiDispatchDelegate? _uiDispatchPin;
	private static GodotWinUI3Native.GodotHostMsgDelegate? _hostMsgDelegatePin;

	public delegate string? HostMessageHandler(string method, string argsJson);

	/// <summary>
	/// Installs a callback that receives all Godot print/warning/error output.
	/// Call BEFORE <see cref="EngineSetup"/> to capture setup failures. Pass
	/// <c>null</c> to uninstall.
	/// </summary>
	public static void SetLogCallback(Action<string, GodotLogLevel>? callback)
	{
		if (callback == null)
		{
			GodotWinUI3Native.godot_winui3_set_log_callback(null);
			_logDelegatePin = null;
			return;
		}

		_logDelegatePin = (msgPtr, level) =>
		{
			string msg = Marshal.PtrToStringUTF8(msgPtr) ?? string.Empty;
			callback(msg, (GodotLogLevel)level);
		};
		GodotWinUI3Native.godot_winui3_set_log_callback(_logDelegatePin);
	}

	/// <summary>
	/// Sets the host HWND the engine's main window should be re-parented into.
	/// Must be called BEFORE <see cref="EngineSetup"/>.
	/// </summary>
	public static void SetEmbeddedParentHwnd(IntPtr hostHwnd)
	{
		GodotWinUI3Native.godot_winui3_set_embedded_parent_hwnd(hostHwnd);
	}

	/// <summary>
	/// Initialises the embedded Godot engine in this process (Main::setup).
	/// </summary>
	public static bool EngineSetup(string[] args)
	{
		if (args == null || args.Length < 1)
			throw new ArgumentException("args must contain at least argv[0].", nameof(args));

		IntPtr[] utf8Ptrs = new IntPtr[args.Length];
		IntPtr argv = Marshal.AllocHGlobal(IntPtr.Size * args.Length);
		try
		{
			for (int i = 0; i < args.Length; i++)
			{
				byte[] bytes = System.Text.Encoding.UTF8.GetBytes(args[i] + '\0');
				utf8Ptrs[i] = Marshal.AllocHGlobal(bytes.Length);
				Marshal.Copy(bytes, 0, utf8Ptrs[i], bytes.Length);
				Marshal.WriteIntPtr(argv, i * IntPtr.Size, utf8Ptrs[i]);
			}
			return GodotWinUI3Native.godot_winui3_engine_setup(args.Length, argv) != 0;
		}
		finally
		{
			for (int i = 0; i < utf8Ptrs.Length; i++)
			{
				if (utf8Ptrs[i] != IntPtr.Zero)
					Marshal.FreeHGlobal(utf8Ptrs[i]);
			}
			Marshal.FreeHGlobal(argv);
		}
	}

	/// <summary>Starts the loaded project (Main::setup2 + Main::start).</summary>
	public static bool EngineStart()
	{
		return GodotWinUI3Native.godot_winui3_engine_start() != 0;
	}

	/// <summary>
	/// Runs a single frame of the engine main loop.
	/// </summary>
	/// <returns><c>true</c> when the engine wants to quit.</returns>
	public static bool EngineIteration()
	{
		return GodotWinUI3Native.godot_winui3_engine_iteration() != 0;
	}

	/// <summary>Shuts down the engine and releases all resources. Idempotent.</summary>
	public static void EngineShutdown()
	{
		GodotWinUI3Native.godot_winui3_engine_shutdown();
	}

	/// <summary>
	/// Passes the <c>ISwapChainPanelNative*</c> pointer for a Godot window to
	/// the engine so it can render into the WinUI3 panel.
	/// </summary>
	public static void SetSwapChainPanel(int windowId, IntPtr panelNative)
	{
		GodotWinUI3Native.godot_winui3_set_swap_chain_panel(windowId, panelNative);
	}

	/// <summary>
	/// Installs a dispatcher used to marshal the few rendering calls that have
	/// UI-thread affinity (ISwapChainPanelNative::SetSwapChain) onto the
	/// thread that owns the panel. Required whenever the engine runs on a
	/// dedicated thread. Call BEFORE <see cref="EngineStart"/>.
	/// </summary>
	public static void SetUiDispatcher(Action<Action> dispatch)
	{
		_uiDispatchPin = (workFuncPtr, ctx) =>
		{
			var work = Marshal.GetDelegateForFunctionPointer<GodotWinUI3Native.GodotWorkDelegate>(workFuncPtr);
			dispatch(() => work(ctx));
		};
		GodotWinUI3Native.godot_winui3_set_ui_dispatcher(_uiDispatchPin);
	}

	public static void ClearUiDispatcher()
	{
		GodotWinUI3Native.godot_winui3_set_ui_dispatcher(null);
		_uiDispatchPin = null;
	}

	/// <summary>Notifies the engine that the SwapChainPanel was resized.</summary>
	public static void NotifyPanelResize(int windowId, int width, int height)
	{
		GodotWinUI3Native.godot_winui3_notify_panel_resize(windowId, width, height);
	}

	/// <summary>Sets the panel's composition scale (physical pixels per DIP).</summary>
	public static void SetCompositionScale(int windowId, float scaleX, float scaleY)
	{
		GodotWinUI3Native.godot_winui3_set_composition_scale(windowId, scaleX, scaleY);
	}

	/// <summary>Injects a mouse button press or release event into Godot.</summary>
	public static void InjectMouseButton(int windowId, GodotMouseButton button, bool pressed, float x, float y)
	{
		GodotWinUI3Native.godot_winui3_inject_mouse_button(
			windowId, (int)button, pressed ? 1 : 0, x, y);
	}

	/// <summary>Injects a mouse motion event into Godot.</summary>
	public static void InjectMouseMotion(int windowId, float x, float y, float relX, float relY)
	{
		GodotWinUI3Native.godot_winui3_inject_mouse_motion(windowId, x, y, relX, relY);
	}

	/// <summary>Injects a key press or release event into Godot.</summary>
	public static void InjectKey(int windowId, int keycode, bool pressed, bool echo, uint character = 0)
	{
		GodotWinUI3Native.godot_winui3_inject_key(
			windowId, keycode, pressed ? 1 : 0, echo ? 1 : 0, character);
	}

	/// <summary>Injects a scroll-wheel event into Godot.</summary>
	public static void InjectMouseWheel(int windowId, float x, float y, float deltaX, float deltaY)
	{
		GodotWinUI3Native.godot_winui3_inject_mouse_wheel(windowId, x, y, deltaX, deltaY);
	}

	/// <summary>Sets the input routing mode for the embedded Godot window.</summary>
	public static void SetInputMode(int mode)
	{
		GodotWinUI3Native.godot_winui3_set_input_mode(mode);
	}

	/// <summary>
	/// Installs the engine -> host message handler. Pass <c>null</c> to clear.
	/// Call BEFORE <see cref="EngineStart"/> so messages emitted during script
	/// <c>_ready</c> are not dropped.
	/// </summary>
	public static void SetHostMessageHandler(HostMessageHandler? handler)
	{
		if (handler == null)
		{
			GodotWinUI3Native.godot_winui3_set_host_message_callback(null);
			_hostMsgDelegatePin = null;
			return;
		}

		_hostMsgDelegatePin = (methodPtr, argsPtr) =>
		{
			string method = Marshal.PtrToStringUTF8(methodPtr) ?? string.Empty;
			string args = Marshal.PtrToStringUTF8(argsPtr) ?? string.Empty;
			string? ret;
			try
			{
				ret = handler(method, args);
			}
			catch (Exception ex)
			{
				System.Diagnostics.Debug.WriteLine($"WinUI3 host handler '{method}' threw: {ex}");
				ret = null;
			}
			if (ret != null)
			{
				IntPtr retUtf8 = Utf8Alloc(ret);
				try
				{
					GodotWinUI3Native.godot_winui3_set_call_return(retUtf8);
				}
				finally
				{
					Marshal.FreeHGlobal(retUtf8);
				}
			}
		};
		GodotWinUI3Native.godot_winui3_set_host_message_callback(_hostMsgDelegatePin);
	}

	/// <summary>
	/// Invokes a GDScript handler registered via
	/// <c>WinUI3Host.register_handler(method, callable)</c>.
	/// </summary>
	public static string? CallEngine(string method, string? argsJson = null)
	{
		if (string.IsNullOrEmpty(method))
			throw new ArgumentException("method must be non-empty.", nameof(method));

		IntPtr methodUtf8 = Utf8Alloc(method);
		IntPtr argsUtf8 = argsJson != null ? Utf8Alloc(argsJson) : IntPtr.Zero;
		IntPtr retUtf8 = IntPtr.Zero;
		try
		{
			int ok = GodotWinUI3Native.godot_winui3_call_engine(methodUtf8, argsUtf8, out retUtf8);
			if (ok == 0)
			{
				throw new InvalidOperationException(
					"WinUI3Host bridge is not initialised. Call EngineSetup() first.");
			}
			if (retUtf8 == IntPtr.Zero)
			{
				return null;
			}
			return Marshal.PtrToStringUTF8(retUtf8);
		}
		finally
		{
			if (retUtf8 != IntPtr.Zero)
			{
				GodotWinUI3Native.godot_winui3_free_string(retUtf8);
			}
			if (argsUtf8 != IntPtr.Zero)
			{
				Marshal.FreeHGlobal(argsUtf8);
			}
			Marshal.FreeHGlobal(methodUtf8);
		}
	}

	private static IntPtr Utf8Alloc(string s)
	{
		byte[] bytes = System.Text.Encoding.UTF8.GetBytes(s + '\0');
		IntPtr ptr = Marshal.AllocHGlobal(bytes.Length);
		Marshal.Copy(bytes, 0, ptr, bytes.Length);
		return ptr;
	}
}
