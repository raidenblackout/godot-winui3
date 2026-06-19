namespace Godot.WinUI3.Embedding.Interop;

/// <summary>Severity level passed to the log callback.</summary>
public enum GodotLogLevel : int
{
	Print = 0,
	Warning = 1,
	Error = 2,
}

/// <summary>
/// Godot mouse-button indices, matching the <c>MouseButton</c> enum in
/// <c>core/input/input_enums.h</c>.
/// </summary>
public enum GodotMouseButton : int
{
	None = 0,
	Left = 1,
	Right = 2,
	Middle = 3,
	WheelUp = 4,
	WheelDown = 5,
	WheelLeft = 6,
	WheelRight = 7,
	XButton1 = 8,
	XButton2 = 9,
}
