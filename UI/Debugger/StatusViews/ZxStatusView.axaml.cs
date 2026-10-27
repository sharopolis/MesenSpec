using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace Mesen.Debugger.StatusViews
{
	public class ZxStatusView : UserControl
	{
		public ZxStatusView()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}
	}
}
