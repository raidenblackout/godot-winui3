using System;
using System.IO;
using System.Text;
using Microsoft.UI.Xaml;

namespace GodotWindowsEmbedSample;

public partial class App : Application
{
	// Exposed so pages can pass the host HWND to Godot via WindowNative.
	public static Window? MainWindow { get; private set; }

	public App()
	{
		try
		{
			var dir = Path.Combine(AppContext.BaseDirectory, "Logs");
			Directory.CreateDirectory(dir);
			var path = Path.Combine(dir, $"host_{DateTime.Now:yyyyMMdd_HHmmss}.log");
			var sw = new StreamWriter(new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read), new UTF8Encoding(false))
			{
				AutoFlush = true
			};
			Console.SetOut(TextWriter.Synchronized(sw));
			Console.WriteLine($"=== Host log opened {DateTime.Now:O} ===");
		}
		catch { }
		InitializeComponent();
	}

	protected override void OnLaunched(LaunchActivatedEventArgs args)
	{
		MainWindow = new MainWindow();
		MainWindow.Activate();
	}
}
