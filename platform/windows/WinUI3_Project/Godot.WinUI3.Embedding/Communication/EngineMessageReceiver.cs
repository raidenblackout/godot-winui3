// EngineMessageReceiver.cs
// Receives messages sent from GDScript via WinUI3Host.send_to_host(method,
// args) and raises them as events on the thread that constructed this
// receiver (normally the WinUI3 UI thread). The native callback itself fires
// on the engine thread, so every dispatch is marshalled across.

namespace Godot.WinUI3.Embedding.Communication;

using System;
using System.Threading;
using Godot.WinUI3.Embedding.Interop;

public sealed class EngineMessageReceiver : IDisposable
{
	private readonly SynchronizationContext? _uiContext;
	private bool _isInitialized;
	private bool _isDisposed;

	public EngineMessageReceiver()
	{
		_uiContext = SynchronizationContext.Current;
	}

	/// <summary>UI control commands (dialogs, navigation, views).</summary>
	public event EventHandler<EngineMessageEventArgs>? OnUIControlCommand;

	/// <summary><c>request_data</c> commands asking the host for JSON payloads.</summary>
	public event EventHandler<EngineMessageEventArgs>? OnDataCommand;

	/// <summary>Renderer status notifications.</summary>
	public event EventHandler<EngineMessageEventArgs>? OnRendererStatus;

	/// <summary>Anything that doesn't match a known category.</summary>
	public event EventHandler<EngineMessageEventArgs>? OnUnhandledMessage;

	/// <summary>
	/// Registers the message handler with the Godot engine. Call BEFORE
	/// <see cref="GodotEngineHost.Start"/> so messages emitted during script
	/// <c>_ready</c> are not dropped.
	/// </summary>
	public bool Initialize()
	{
		if (_isInitialized) return true;
		GodotWinUI3Embed.SetHostMessageHandler(HandleHostMessage);
		_isInitialized = true;
		return true;
	}

	public void Shutdown()
	{
		if (!_isInitialized) return;
		GodotWinUI3Embed.SetHostMessageHandler(null);
		_isInitialized = false;
	}

	// Invoked on the engine thread.
	private string? HandleHostMessage(string method, string argsJson)
	{
		var target = DetermineTarget(method);
		var args = new EngineMessageEventArgs { Method = method, ArgsJson = argsJson };

		if (_uiContext == null || SynchronizationContext.Current == _uiContext)
		{
			target?.Invoke(this, args);
		}
		else
		{
			_uiContext.Post(_ => target?.Invoke(this, args), null);
		}

		// All current routes are fire-and-forget; replies go back through
		// EngineMessageSender on a separate call_engine("response", ...) hop.
		return null;
	}

	private EventHandler<EngineMessageEventArgs>? DetermineTarget(string method)
	{
		if (method.StartsWith("request_data", StringComparison.Ordinal))
			return OnDataCommand;
		if (method.StartsWith("renderer_", StringComparison.Ordinal) ||
			method.StartsWith("notify_renderer", StringComparison.Ordinal))
			return OnRendererStatus;
		if (method.StartsWith("show_", StringComparison.Ordinal) ||
			method.StartsWith("hide_", StringComparison.Ordinal) ||
			method.StartsWith("launch_", StringComparison.Ordinal) ||
			method.StartsWith("update_", StringComparison.Ordinal) ||
			method.Contains("navigation", StringComparison.Ordinal))
			return OnUIControlCommand;
		return OnUnhandledMessage;
	}

	public void Dispose()
	{
		if (_isDisposed) return;
		Shutdown();
		_isDisposed = true;
		GC.SuppressFinalize(this);
	}
}
