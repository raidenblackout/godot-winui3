using GodotWinUI3Sample.Views;
using Microsoft.UI.Xaml;

namespace GodotWinUI3Sample;

public sealed partial class MainWindow : Window
{
	public MainWindow()
	{
		InitializeComponent();
		RootFrame.Navigate(typeof(MapViewPage));
	}
}
