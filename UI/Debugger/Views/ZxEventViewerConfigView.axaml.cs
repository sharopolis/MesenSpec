using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace Mesen.Debugger.Views
{
	public class ZxEventViewerConfigView : UserControl
	{
		public ZxEventViewerConfigView()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}
	}
}
