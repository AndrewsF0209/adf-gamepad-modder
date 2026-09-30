using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using AdfConfigurator.Deploy;

namespace AdfConfigurator.ViewModels;

public sealed class DeployKindConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value switch
        {
            DeployKind.Auto => "自动（按静态导入选择）",
            DeployKind.Winmm => "winmm.dll（最常见）",
            DeployKind.Version => "version.dll",
            DeployKind.Dinput8 => "dinput8.dll",
            DeployKind.Launcher => "启动器 exe",
            _ => value?.ToString() ?? "",
        };

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

public sealed class InverseBoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is true ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

// Stick alt-mode combo: alt.mode is -1 keep / 0 keys / 1 mouse, the combo
// index is 0 保持 / 1 键盘 / 2 鼠标.
public sealed class AltModeIndexConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is int i ? i + 1 : 0;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => value is int i ? i - 1 : -1;
}
