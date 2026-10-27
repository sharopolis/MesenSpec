using Avalonia.Media;
using Mesen.Interop;
using Mesen.ViewModels;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Mesen.Config
{
	public partial class ZxEventViewerConfig : ViewModelBase
	{
		[ObservableProperty] public partial EventViewerCategoryCfg UlaPortWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xC9, 0x29, 0x29));
		[ObservableProperty] public partial EventViewerCategoryCfg UlaPortRead { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x53, 0xD7, 0x44));
		[ObservableProperty] public partial EventViewerCategoryCfg OtherPortWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x18, 0x98, 0xE4));
		[ObservableProperty] public partial EventViewerCategoryCfg OtherPortRead { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x9F, 0x93, 0xC6));

		//Off by default - games redraw the screen constantly, which would bury every other event
		[ObservableProperty] public partial EventViewerCategoryCfg ScreenWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xB4, 0x7A, 0xDA)) { Visible = false };

		[ObservableProperty] public partial EventViewerCategoryCfg Irq { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xC4, 0xF4, 0x7A));
		[ObservableProperty] public partial EventViewerCategoryCfg Nmi { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xF4, 0xF4, 0x7A));

		[ObservableProperty] public partial EventViewerCategoryCfg MarkedBreakpoints { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x18, 0x98, 0xE4));

		[ObservableProperty] public partial bool ShowPreviousFrameEvents { get; set; } = true;

		public InteropZxEventViewerConfig ToInterop()
		{
			return new InteropZxEventViewerConfig() {
				Irq = this.Irq,
				Nmi = this.Nmi,
				MarkedBreakpoints = this.MarkedBreakpoints,

				UlaPortWrite = this.UlaPortWrite,
				UlaPortRead = this.UlaPortRead,
				OtherPortWrite = this.OtherPortWrite,
				OtherPortRead = this.OtherPortRead,

				ScreenWrite = this.ScreenWrite,

				ShowPreviousFrameEvents = this.ShowPreviousFrameEvents
			};
		}
	}
}
