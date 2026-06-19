namespace Godot.WinUI3.Embedding.Communication;

using System;
using System.Text.Json;

public sealed class EngineMessageEventArgs : EventArgs
{
	/// <summary>The method name (command) sent from GDScript via <c>WinUI3Host.send_to_host</c>.</summary>
	public required string Method { get; init; }

	/// <summary>JSON-encoded array of arguments.</summary>
	public required string ArgsJson { get; init; }

	/// <summary>Deserializes <see cref="ArgsJson"/> to the specified type.</summary>
	public T? GetArgsAs<T>()
	{
		try
		{
			return JsonSerializer.Deserialize<T>(ArgsJson);
		}
		catch (JsonException)
		{
			return default;
		}
	}
}
