// MapViewPage.xaml.cs
// Hosts the embedded Godot engine inside a SwapChainPanel and wires up the
// host<->engine message bus. The engine itself runs on a dedicated thread owned
// by GodotEngineHost (in the Godot.WindowsEmbed.Embedding library) — this page only
// forwards input/sizing onto that thread and answers `request_data` calls from
// GDScript with indoor-map / rooms JSON from the bundled Assets folder.

namespace GodotWindowsEmbedSample.Views;

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Godot.WindowsEmbed.Embedding;
using Godot.WindowsEmbed.Embedding.Communication;
using Godot.WindowsEmbed.Embedding.Interop;
using GodotWindowsEmbedSample.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;

public sealed partial class MapViewPage : Page
{
	// Default to the MapViewProject folder already in the Godot repo. Override
	// in code or with a sibling-of-exe folder named "MapViewProject" if the
	// repo path is missing at runtime.
	private const string DefaultProjectPath = @"C:\Projects\GodotProject\GodotProject";

	private readonly MapViewModel _viewModel = new();

	// The engine runtime (own thread) plus the two halves of the message bus.
	// Constructed on the UI thread so the receiver captures the UI dispatcher's
	// SynchronizationContext for its event callbacks.
	private readonly GodotEngineHost _host = new();
	private readonly EngineMessageReceiver _receiver;
	private readonly EngineMessageSender _sender;

	private double _lastX, _lastY;

	public MapViewPage()
	{
		InitializeComponent();
		NavigationCacheMode = NavigationCacheMode.Required;

		_receiver = new EngineMessageReceiver();
		_sender = new EngineMessageSender(_host);
	}

	private void OnPanelLoaded(object sender, RoutedEventArgs e)
	{
		if (_host.State != EngineState.Stopped) return;

		var hostHwnd = WinRT.Interop.WindowNative.GetWindowHandle(App.MainWindow);
		var panelPtr = Marshal.GetComInterfaceForObject(GodotPanel, typeof(ISwapChainPanelNative));

		// Tear the engine thread down when the host window closes. Wired here
		// (not in the constructor) because App.MainWindow is only assigned after
		// the MainWindow ctor — which is what navigates to this page — returns.
		if (App.MainWindow != null)
		{
			App.MainWindow.Closed += (_, _) => _host.Dispose();
		}

		_host.ProjectPath = ResolveProjectPath();

		// Subscribe and register the host-message callback BEFORE Start() so
		// messages emitted during GDScript _ready are not dropped.
		_receiver.OnDataCommand += OnDataCommand;
		_receiver.OnUIControlCommand += OnUIControlCommand;
		_receiver.OnRendererStatus += OnRendererStatus;
		_receiver.OnUnhandledMessage += OnUnhandledMessage;
		_receiver.OnSynchronousMessage = OnSynchronousHostMessage;
		_receiver.Initialize();

		// Seed the engine with the panel's initial physical size + DPI.
		float scaleX = GodotPanel.CompositionScaleX;
		float scaleY = GodotPanel.CompositionScaleY;
		int widthPx = (int)(GodotPanel.ActualWidth * scaleX);
		int heightPx = (int)(GodotPanel.ActualHeight * scaleY);

		// Spawns the engine thread and brings the engine up there; returns
		// immediately. Ownership of panelPtr transfers to the engine thread.
		_host.Start(hostHwnd, panelPtr, widthPx, heightPx, scaleX, scaleY);
	}

	// ---------------------------------------------------------------------
	// Engine -> Host (raised on the UI thread by the receiver)
	// ---------------------------------------------------------------------

	private void OnDataCommand(object? sender, EngineMessageEventArgs e)
	{
		// args := ["st_data", "<sub_cmd>", "<payload>"]
		string[]? request;
		try
		{
			request = JsonSerializer.Deserialize<string[]>(e.ArgsJson);
		}
		catch (JsonException ex)
		{
			Debug.WriteLine($"[MapViewPage] OnDataCommand JSON parse failed: {ex.Message}");
			return;
		}

		if (request is null || request.Length < 2) return;
		var subCmd = request[1];

		switch (subCmd)
		{
			case "get_indoor_map":
				_sender.PostDataCommand("result_" + subCmd, _viewModel.GetIndoorMap());
				// The indoor map is the primary payload; once it's served the heavy
				// load is essentially done, so settle the engine into paced frames.
				// Move this to your real "fully loaded" signal for finer control.
				_host.EndStartupBoost();
				break;
			case "get_rooms":
				_sender.PostDataCommand("result_" + subCmd, _viewModel.GetRooms());
				break;
			case "get_scenes":
				_sender.PostDataCommand("result_" + subCmd, _viewModel.GetScenes());
				break;
			case "get_devices":
				_sender.PostDataCommand("result_" + subCmd, _viewModel.GetDevices());
				break;
			case "get_locations":
				_sender.PostDataCommand("result_" + subCmd, _viewModel.GetLocations());
				break;
			default:
				// Stay silent for unknown sub-commands. The GDScript
				// WindowsEmbedInteractor will fall back to SimulatedResponse
				// after a short timeout if we don't reply.
				Debug.WriteLine($"[MapViewPage] No host data for sub-command '{subCmd}' (deferring to SimulatedResponse).");
				break;
		}
	}

	private void OnUIControlCommand(object? sender, EngineMessageEventArgs e)
	{
		Debug.WriteLine($"[MapViewPage] UI: {e.Method} {e.ArgsJson}");
	}

	private void OnRendererStatus(object? sender, EngineMessageEventArgs e)
	{
		Debug.WriteLine($"[MapViewPage] Renderer: {e.Method} {e.ArgsJson}");
	}

	private void OnUnhandledMessage(object? sender, EngineMessageEventArgs e)
	{
		Debug.WriteLine($"[MapViewPage] Unhandled: {e.Method} {e.ArgsJson}");
	}

	private string? OnSynchronousHostMessage(EngineMessageEventArgs e)
	{
		switch (e.Method)
		{
			case "get_indoor_map":
				return _viewModel.GetIndoorMap();
			case "get_rooms":
				return _viewModel.GetRooms();
			case "get_scenes":
				return _viewModel.GetScenes();
			case "get_devices":
				return _viewModel.GetDevices();
			case "get_locations":
				return _viewModel.GetLocations();
			case "get_capability_status":
				return _viewModel.GetCapabilityStatus();
			case "get_host_time":
				return JsonSerializer.Serialize(DateTimeOffset.Now.ToString("O"));
			case "custom_command":
				return JsonSerializer.Serialize(new { ok = true, method = e.Method });
			default:
				return null;
		}
	}

	// ---------------------------------------------------------------------
	// Panel sizing / DPI (forwarded onto the engine thread)
	// ---------------------------------------------------------------------

	private void ConfigurePanel()
	{
		float scaleX = GodotPanel.CompositionScaleX;
		float scaleY = GodotPanel.CompositionScaleY;
		double width = GodotPanel.ActualWidth * scaleX;
		double height = GodotPanel.ActualHeight * scaleY;
		_host.ConfigurePanel(width, height, scaleX, scaleY);
	}

	private void OnPanelSizeChanged(object sender, SizeChangedEventArgs e) => ConfigurePanel();

	private void OnPanelCompositionScaleChanged(SwapChainPanel sender, object args) => ConfigurePanel();

	// ---------------------------------------------------------------------
	// Input forwarding (physical pixels = DIP * CompositionScale)
	// ---------------------------------------------------------------------

	private float DpiScale => GodotPanel.CompositionScaleX;

	private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
	{
		var pt = e.GetCurrentPoint(GodotPanel);
		float scale = DpiScale;
		float x = (float)(pt.Position.X * scale), y = (float)(pt.Position.Y * scale);
		if (pt.Properties.IsLeftButtonPressed) _host.InjectMouseButton(GodotMouseButton.Left, true, x, y);
		if (pt.Properties.IsRightButtonPressed) _host.InjectMouseButton(GodotMouseButton.Right, true, x, y);
		if (pt.Properties.IsMiddleButtonPressed) _host.InjectMouseButton(GodotMouseButton.Middle, true, x, y);
		_ = GodotPanel.Focus(FocusState.Programmatic);
		e.Handled = true;
	}

	private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
	{
		var pt = e.GetCurrentPoint(GodotPanel);
		float scale = DpiScale;
		float x = (float)(pt.Position.X * scale), y = (float)(pt.Position.Y * scale);
		if (!pt.Properties.IsLeftButtonPressed) _host.InjectMouseButton(GodotMouseButton.Left, false, x, y);
		if (!pt.Properties.IsRightButtonPressed) _host.InjectMouseButton(GodotMouseButton.Right, false, x, y);
		if (!pt.Properties.IsMiddleButtonPressed) _host.InjectMouseButton(GodotMouseButton.Middle, false, x, y);
		_ = GodotPanel.Focus(FocusState.Programmatic);
		e.Handled = true;
	}

	private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
	{
		var pt = e.GetCurrentPoint(GodotPanel);
		float scale = DpiScale;
		float px = (float)(pt.Position.X * scale);
		float py = (float)(pt.Position.Y * scale);
		_host.InjectMouseMotion(px, py,
			(float)((pt.Position.X - _lastX) * scale),
			(float)((pt.Position.Y - _lastY) * scale));
		_lastX = pt.Position.X;
		_lastY = pt.Position.Y;
		e.Handled = true;
	}

	private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs e)
	{
		var pt = e.GetCurrentPoint(GodotPanel);
		float scale = DpiScale;
		float x = (float)(pt.Position.X * scale), y = (float)(pt.Position.Y * scale);
		float notches = pt.Properties.MouseWheelDelta / 120.0f;
		if (pt.Properties.IsHorizontalMouseWheel)
			_host.InjectMouseWheel(x, y, deltaX: notches, deltaY: 0f);
		else
			_host.InjectMouseWheel(x, y, deltaX: 0f, deltaY: notches);
		e.Handled = true;
	}

	private void OnKeyDown(object sender, KeyRoutedEventArgs e)
	{
		_host.InjectKey((int)e.Key, pressed: true, echo: e.KeyStatus.WasKeyDown, character: 0);
	}

	private void OnKeyUp(object sender, KeyRoutedEventArgs e)
	{
		_host.InjectKey((int)e.Key, pressed: false, echo: false, character: 0);
	}

	// ---------------------------------------------------------------------
	// Project path resolution
	// ---------------------------------------------------------------------

	private static string ResolveProjectPath()
	{
		// Prefer the TestProject.pck copied next to the exe by the csproj
		// (production-style deployment). Fall back to the dev path.
		var sibling = Path.Combine(AppContext.BaseDirectory, "Assets", "TestProject.pck");
		if (File.Exists(sibling)) return sibling;
		var siblingFlat = Path.Combine(AppContext.BaseDirectory, "TestProject.pck");
		if (File.Exists(siblingFlat)) return siblingFlat;
		return DefaultProjectPath;
	}
}
