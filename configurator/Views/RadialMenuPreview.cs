// Donut preview of the 8-sector radial ("wheel") menu, drawn to match the
// engine's sector layout exactly (U, UR, R, DR, D, DL, L, UL clockwise from
// the top, see RadialSector in mapper.cpp). Bound sectors are filled blue,
// unbound stay grey; the inner hole is the stick-centred cancel zone. The
// whole ring turns grey while the wheel is disabled (IsRadial == false), so
// a fully-bound but un-enabled wheel is visually obvious.

using System.ComponentModel;
using System.Globalization;
using System.Windows;
using System.Windows.Media;
using AdfConfigurator.ViewModels;

namespace AdfConfigurator.Views;

public sealed class RadialMenuPreview : FrameworkElement
{
    private static readonly Brush BoundFill = Freeze(new SolidColorBrush(Color.FromRgb(0x4A, 0x90, 0xD9)));
    private static readonly Brush IdleFill = Freeze(new SolidColorBrush(Color.FromRgb(0xE9, 0xE9, 0xE9)));
    private static readonly Brush DimText = Freeze(new SolidColorBrush(Color.FromRgb(0x88, 0x88, 0x88)));
    private static readonly Pen SectorPen = new(Brushes.White, 1.5);
    private static readonly Pen CancelPen = new(Freeze(new SolidColorBrush(Color.FromRgb(0xBB, 0xBB, 0xBB))), 1.0);
    private static readonly Typeface UiFont = new("Microsoft YaHei UI");
    private static readonly Typeface UiFontBold = new(new FontFamily("Microsoft YaHei UI"),
        FontStyles.Normal, FontWeights.SemiBold, FontStretches.Normal);

    static RadialMenuPreview()
    {
        SectorPen.Freeze();
        CancelPen.Freeze();
    }

    private static T Freeze<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }

    public static readonly DependencyProperty SectorsProperty =
        DependencyProperty.Register(nameof(Sectors), typeof(IReadOnlyList<BindingRow>),
            typeof(RadialMenuPreview),
            new FrameworkPropertyMetadata(null,
                FrameworkPropertyMetadataOptions.AffectsRender, OnSectorsChanged));

    public IReadOnlyList<BindingRow>? Sectors
    {
        get => (IReadOnlyList<BindingRow>?)GetValue(SectorsProperty);
        set => SetValue(SectorsProperty, value);
    }

    public static readonly DependencyProperty WheelEnabledProperty =
        DependencyProperty.Register(nameof(WheelEnabled), typeof(bool),
            typeof(RadialMenuPreview),
            new FrameworkPropertyMetadata(false,
                FrameworkPropertyMetadataOptions.AffectsRender));

    public bool WheelEnabled
    {
        get => (bool)GetValue(WheelEnabledProperty);
        set => SetValue(WheelEnabledProperty, value);
    }

    private static void OnSectorsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var self = (RadialMenuPreview)d;
        if (e.OldValue is IEnumerable<BindingRow> oldRows)
            foreach (var r in oldRows) r.PropertyChanged -= self.OnSectorChanged;
        if (e.NewValue is IEnumerable<BindingRow> newRows)
            foreach (var r in newRows) r.PropertyChanged += self.OnSectorChanged;
    }

    private void OnSectorChanged(object? sender, PropertyChangedEventArgs e) => InvalidateVisual();

    // Angles are degrees clockwise from straight up (sector 0 = U at 0°).
    private static Point Pt(double cx, double cy, double r, double deg)
    {
        double a = deg * Math.PI / 180.0;
        return new Point(cx + r * Math.Sin(a), cy - r * Math.Cos(a));
    }

    private static StreamGeometry Slice(double cx, double cy, double rOut, double rIn,
                                        double a0, double a1)
    {
        var g = new StreamGeometry();
        using (var ctx = g.Open())
        {
            ctx.BeginFigure(Pt(cx, cy, rOut, a0), true, true);
            ctx.ArcTo(Pt(cx, cy, rOut, a1), new Size(rOut, rOut), 0, false,
                      SweepDirection.Clockwise, true, false);
            ctx.LineTo(Pt(cx, cy, rIn, a1), true, false);
            ctx.ArcTo(Pt(cx, cy, rIn, a0), new Size(rIn, rIn), 0, false,
                      SweepDirection.Counterclockwise, true, false);
        }
        g.Freeze();
        return g;
    }

    private static void DrawCentered(DrawingContext dc, string text, double size,
                                     bool bold, Brush brush, double x, double yTop, double dpi)
    {
        if (string.IsNullOrEmpty(text)) return;
        var ft = new FormattedText(text, CultureInfo.CurrentUICulture,
            FlowDirection.LeftToRight, bold ? UiFontBold : UiFont, size, brush, dpi);
        dc.DrawText(ft, new Point(x - ft.Width / 2, yTop));
    }

    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        double w = RenderSize.Width, h = RenderSize.Height;
        if (w < 24 || h < 24) return;

        double cx = w / 2, cy = h / 2;
        double rOut = Math.Min(w, h) / 2 - 2;
        double rIn = rOut * 0.36;
        double rText = (rIn + rOut) / 2;
        double dpi = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        var rows = Sectors;
        for (int i = 0; i < 8; ++i)
        {
            var row = rows != null && i < rows.Count ? rows[i] : null;
            bool bound = WheelEnabled && row?.IsBound == true;
            double aMid = i * 45.0;

            dc.DrawGeometry(bound ? BoundFill : IdleFill, SectorPen,
                            Slice(cx, cy, rOut, rIn, aMid - 21.0, aMid + 21.0));

            var pos = Pt(cx, cy, rText, aMid);
            var brush = bound ? Brushes.White : DimText;
            string key = row?.IsBound == true ? row.ActionText : "";
            double dirY = pos.Y - (key.Length > 0 ? 11 : 6);
            DrawCentered(dc, row?.Label ?? "", 11, true, brush, pos.X, dirY, dpi);
            if (key.Length > 0)
                DrawCentered(dc, key, 10, false, brush, pos.X, pos.Y + 3, dpi);
        }

        dc.DrawEllipse(Brushes.White, CancelPen, new Point(cx, cy), rIn, rIn);
        DrawCentered(dc, "取消", 10, false, DimText, cx, cy - 7, dpi);
    }
}
