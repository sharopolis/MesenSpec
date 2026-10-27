using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using Mesen.Config;
using Mesen.Utilities;
using System;

namespace Mesen.ViewModels
{
	public partial class ZxConfigViewModel : DisposableViewModel
	{
		[ObservableProperty] public partial ZxConfig Config { get; set; }
		[ObservableProperty] public partial ZxConfig OriginalConfig { get; set; }

		public Enum[] AvailableKeyboardTypes => new Enum[] {
			ControllerType.ZxKeyboard,
		};

		public ZxConfigViewModel()
		{
			Config = ConfigManager.Config.Zx;
			OriginalConfig = Config.Clone();

			if(Design.IsDesignMode) {
				return;
			}

			AddDisposable(ReactiveHelper.RegisterRecursiveObserver(Config, (s, e) => {
				Config.ApplyConfig();
				ConfigManager.Config.Video.ApplyConfig();
			}));
		}
	}
}
