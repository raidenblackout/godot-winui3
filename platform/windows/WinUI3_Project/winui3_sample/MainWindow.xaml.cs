using GodotWindowsEmbedSample.Views;
using Microsoft.UI.Xaml;

namespace GodotWindowsEmbedSample;

public sealed partial class MainWindow : Window
{
	public MainWindow()
	{
		InitializeComponent();
		RootFrame.Navigate(typeof(MapViewPage));
	}
}
