namespace Godot.WinUI3.Embedding.Interop;

using System;
using System.Runtime.InteropServices;

[ComImport, Guid("63aad0b8-7c24-40ff-85a8-640d944cc325"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface ISwapChainPanelNative
{
	[PreserveSig] int SetSwapChain(IntPtr swapChain);
}
