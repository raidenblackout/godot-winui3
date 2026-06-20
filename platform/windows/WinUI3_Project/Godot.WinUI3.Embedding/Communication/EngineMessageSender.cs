// EngineMessageSender.cs
// Sends messages from the WinUI3 host into the embedded Godot engine.
// Calls are queued onto the engine thread via GodotEngineHost.Post so the
// underlying libgodot_call_engine() invocation always happens on the
// engine iteration thread, matching the C ABI's threading contract.

namespace Godot.WinUI3.Embedding.Communication;

using System;
using System.Diagnostics;
using System.Text.Json;
using Godot.WinUI3.Embedding.Interop;

public sealed class EngineMessageSender
{
	private readonly GodotEngineHost _host;

	public EngineMessageSender(GodotEngineHost host)
	{
		_host = host;
	}

	/// <summary>Posts a data command (mainCmd = "st_data") to the engine.</summary>
	public void PostDataCommand(string subCmd, string data) => Post("st_data", subCmd, data);

	/// <summary>Posts a UI control command (mainCmd = "ui") to the engine.</summary>
	public void PostUICommand(string subCmd, string data) => Post("ui", subCmd, data);

	/// <summary>Posts a command with an arbitrary main command category.</summary>
	public void PostRawCommand(string mainCmd, string subCmd, string data) => Post(mainCmd, subCmd, data);

	private void Post(string mainCmd, string subCmd, string data)
	{
		string argsJson = JsonSerializer.Serialize(new[] { mainCmd, subCmd, data });
		_host.Post(() =>
		{
			try
			{
				GodotWinUI3Embed.CallEngine("response", argsJson);
			}
			catch (Exception ex)
			{
				Debug.WriteLine($"[EngineMessageSender] CallEngine failed: {ex.Message}");
			}
		});
	}
}
