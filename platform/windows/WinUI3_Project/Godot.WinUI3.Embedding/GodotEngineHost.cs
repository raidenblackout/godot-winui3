// GodotEngineHost.cs
// Runs the embedded Godot engine on a dedicated background thread instead of
// the WinUI3 UI thread. UI-affine native calls (binding the swap chain to the
// SwapChainPanel) are marshalled back onto the UI thread via the captured
// SynchronizationContext; everything else (input injection, panel resize,
// host<->engine calls) is funnelled through a work queue drained at the top
// of each engine iteration, so callers on the UI thread never touch engine
// state directly.

namespace Godot.WinUI3.Embedding;

using System;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Godot.WinUI3.Embedding.Interop;

public sealed class GodotEngineHost : IDisposable
{
	public string ProjectPath { get; set; } = string.Empty;
	public string RenderingDriver { get; set; } = "d3d12";
	public EngineState State
	{
		get => (EngineState)Volatile.Read(ref _state);
		private set => Volatile.Write(ref _state, (int)value);
	}

	private int _state = (int)EngineState.Stopped;

	private readonly SynchronizationContext? _uiContext;
	private readonly ConcurrentQueue<Action> _workQueue = new();
	private Thread? _engineThread;
	private volatile bool _stopRequested;
	private bool _isDisposed;

	private static readonly object _logFileLock = new();
	private static StreamWriter? _logFileWriter;

	public GodotEngineHost()
	{
		_uiContext = SynchronizationContext.Current;
	}

	/// <summary>
	/// Spawns the engine thread and brings the engine up there. Returns
	/// immediately; ownership of <paramref name="panelNative"/> transfers to
	/// the engine thread, which releases it after binding the panel.
	/// </summary>
	public bool Start(IntPtr hostHwnd, IntPtr panelNative, int widthPx, int heightPx, float scaleX, float scaleY)
	{
		if (State != EngineState.Stopped)
			return false;
		if (string.IsNullOrWhiteSpace(ProjectPath))
			throw new InvalidOperationException("ProjectPath must be set before Start().");

		State = EngineState.Starting;
		_stopRequested = false;

		_engineThread = new Thread(() => RunEngineThread(hostHwnd, panelNative, widthPx, heightPx, scaleX, scaleY))
		{
			IsBackground = true,
			Name = "GodotEngineThread",
			// Startup boost: load as fast as possible while the host shows a
			// loading state. Call EndStartupBoost() once the project signals
			// it has finished its heavy initial load.
			Priority = ThreadPriority.Highest,
		};
		_engineThread.Start();
		return true;
	}

	/// <summary>Drops the engine thread back to normal priority after startup.</summary>
	public void EndStartupBoost()
	{
		var thread = _engineThread;
		if (thread != null && thread.IsAlive)
		{
			thread.Priority = ThreadPriority.Normal;
		}
	}

	/// <summary>
	/// Queues <paramref name="work"/> to run on the engine thread at the top
	/// of the next iteration. Safe to call from any thread; a no-op once the
	/// engine has stopped or is stopping.
	/// </summary>
	public void Post(Action work)
	{
		if (State is EngineState.Stopping or EngineState.Stopped)
			return;
		_workQueue.Enqueue(work);
	}

	public void ConfigurePanel(double widthPx, double heightPx, float scaleX, float scaleY)
	{
		Post(() =>
		{
			GodotWinUI3Embed.SetCompositionScale(0, scaleX, scaleY);
			GodotWinUI3Embed.NotifyPanelResize(0, (int)widthPx, (int)heightPx);
		});
	}

	public void InjectMouseButton(GodotMouseButton button, bool pressed, float x, float y)
		=> Post(() => GodotWinUI3Embed.InjectMouseButton(0, button, pressed, x, y));

	public void InjectMouseMotion(float x, float y, float relX, float relY)
		=> Post(() => GodotWinUI3Embed.InjectMouseMotion(0, x, y, relX, relY));

	public void InjectMouseWheel(float x, float y, float deltaX, float deltaY)
		=> Post(() => GodotWinUI3Embed.InjectMouseWheel(0, x, y, deltaX, deltaY));

	public void InjectKey(int keycode, bool pressed, bool echo, uint character = 0)
		=> Post(() => GodotWinUI3Embed.InjectKey(0, keycode, pressed, echo, character));

	private void RunEngineThread(IntPtr hostHwnd, IntPtr panelNative, int widthPx, int heightPx, float scaleX, float scaleY)
	{
		OpenLogFile();
		GodotWinUI3Embed.SetLogCallback(OnGodotLog);
		GodotWinUI3Embed.SetEmbeddedParentHwnd(hostHwnd);
		GodotWinUI3Embed.SetUiDispatcher(RunOnUiThread);

		string[] args = { "godot", "--main-pack", ProjectPath, "--rendering-driver", RenderingDriver };
		if (!GodotWinUI3Embed.EngineSetup(args))
		{
			System.Diagnostics.Debug.WriteLine("[GodotEngineHost] EngineSetup failed.");
			ReleasePanel(panelNative);
			TeardownNativeCallbacks();
			State = EngineState.Stopped;
			return;
		}

		try
		{
			GodotWinUI3Embed.SetSwapChainPanel(0, panelNative);
		}
		finally
		{
			// Engine AddRefs internally; release the reference obtained by the
			// caller via Marshal.GetComInterfaceForObject().
			ReleasePanel(panelNative);
		}

		GodotWinUI3Embed.SetCompositionScale(0, scaleX, scaleY);
		GodotWinUI3Embed.NotifyPanelResize(0, widthPx, heightPx);

		if (!GodotWinUI3Embed.EngineStart())
		{
			System.Diagnostics.Debug.WriteLine("[GodotEngineHost] EngineStart failed.");
			TeardownNativeCallbacks();
			State = EngineState.Stopped;
			return;
		}

		State = EngineState.Running;

		while (!_stopRequested)
		{
			DrainWorkQueue();
			if (GodotWinUI3Embed.EngineIteration())
				break;
		}

		State = EngineState.Stopping;
		DrainWorkQueue();
		GodotWinUI3Embed.EngineShutdown();
		TeardownNativeCallbacks();
		CloseLogFile();
		State = EngineState.Stopped;
	}

	private void DrainWorkQueue()
	{
		while (_workQueue.TryDequeue(out var work))
		{
			try
			{
				work();
			}
			catch (Exception ex)
			{
				System.Diagnostics.Debug.WriteLine($"[GodotEngineHost] Queued work threw: {ex.Message}");
			}
		}
	}

	private void RunOnUiThread(Action work)
	{
		if (_uiContext == null || SynchronizationContext.Current == _uiContext)
		{
			work();
			return;
		}

		// WinUI3's DispatcherQueueSynchronizationContext doesn't implement
		// Send() (it throws NotSupportedException), so block on a wait handle
		// around Post() instead to get the synchronous hand-off the native
		// dispatcher contract requires.
		using var done = new ManualResetEventSlim(false);
		_uiContext.Post(_ =>
		{
			try
			{
				work();
			}
			finally
			{
				done.Set();
			}
		}, null);
		done.Wait();
	}

	private static void ReleasePanel(IntPtr panelNative)
	{
		if (panelNative != IntPtr.Zero)
		{
			Marshal.Release(panelNative);
		}
	}

	private static void TeardownNativeCallbacks()
	{
		GodotWinUI3Embed.ClearUiDispatcher();
		GodotWinUI3Embed.SetLogCallback(null);
	}

	private static void OnGodotLog(string message, GodotLogLevel level)
	{
		string tag = level switch
		{
			GodotLogLevel.Error => "Error",
			GodotLogLevel.Warning => "Warn",
			_ => "Print",
		};
		System.Diagnostics.Debug.WriteLine($"[Godot/{tag}] {message}");
		WriteLogLine(tag, message);
	}

	private static void OpenLogFile()
	{
		try
		{
			string dir = Path.Combine(AppContext.BaseDirectory, "Logs");
			Directory.CreateDirectory(dir);
			string fileName = $"godot_{DateTime.Now:yyyyMMdd_HHmmss}.log";
			string path = Path.Combine(dir, fileName);

			lock (_logFileLock)
			{
				_logFileWriter?.Dispose();
				_logFileWriter = new StreamWriter(
					new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read),
					new UTF8Encoding(encoderShouldEmitUTF8Identifier: false))
				{
					AutoFlush = true,
				};
				_logFileWriter.WriteLine($"=== Godot WinUI3 sample log opened {DateTime.Now:O} ===");
			}
		}
		catch (Exception ex)
		{
			System.Diagnostics.Debug.WriteLine($"[GodotEngineHost] Failed to open log file: {ex.Message}");
		}
	}

	private static void WriteLogLine(string tag, string message)
	{
		try
		{
			lock (_logFileLock)
			{
				_logFileWriter?.WriteLine($"{DateTime.Now:HH:mm:ss.fff} [{tag,-5}] {message}");
			}
		}
		catch (Exception ex)
		{
			System.Diagnostics.Debug.WriteLine($"[GodotEngineHost] Log write failed: {ex.Message}");
		}
	}

	private static void CloseLogFile()
	{
		lock (_logFileLock)
		{
			if (_logFileWriter == null) return;
			try
			{
				_logFileWriter.WriteLine($"=== Log closed {DateTime.Now:O} ===");
				_logFileWriter.Dispose();
			}
			catch (Exception ex)
			{
				System.Diagnostics.Debug.WriteLine($"[GodotEngineHost] Log close failed: {ex.Message}");
			}
			_logFileWriter = null;
		}
	}

	public void Dispose()
	{
		if (_isDisposed) return;
		_isDisposed = true;

		if (_engineThread != null)
		{
			_stopRequested = true;
			_engineThread.Join(TimeSpan.FromSeconds(5));
			_engineThread = null;
		}

		GC.SuppressFinalize(this);
	}
}
