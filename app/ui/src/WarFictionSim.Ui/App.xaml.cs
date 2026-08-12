// 文件总览：WPF 应用入口与组合根（应用壳）。
//
// 启动顺序：先探测原生 sim_core.dll（缺失给出可操作的启动错误并退出，
// 宪法第 17 条不静默）→ 定位 data/scenarios → 构建目录与客户端工厂 →
// 组装 MainWindowViewModel。这里只装配依赖，不承载业务逻辑。

using System.IO;
using System.Windows;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.MainMenu;
using WarFictionSim.Ui.ViewModels;

namespace WarFictionSim.Ui;

/// <summary>WPF 应用入口。</summary>
public partial class App : Application
{
    /// <inheritdoc />
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        if (!NativeSimLibrary.IsAvailable())
        {
            MessageBox.Show(
                NativeSimLibrary.BuildStartupErrorText(),
                "战争幻想模拟器 - 无法启动",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        ScenarioCatalog catalog = ScenarioCatalog.Load(ResolveScenariosDirectory());
        var viewModel = new MainWindowViewModel(catalog, new SimNativeClientFactory());
        var window = new MainWindow { DataContext = viewModel };
        window.Closed += (_, _) => viewModel.Dispose();
        MainWindow = window;
        window.Show();
    }

    // 从应用输出目录向上寻找仓库 data/scenarios（bin/<config>/<tfm> → 仓库根）。
    // 找不到时回退输出目录下的相对路径：目录不存在会被 ScenarioCatalog 记为
    // 加载问题并在主菜单内联展示，而不是启动崩溃。
    private static string ResolveScenariosDirectory()
    {
        const int maxParentLevels = 8;
        string? current = AppContext.BaseDirectory;
        for (int level = 0; level < maxParentLevels && current is not null; level++)
        {
            string candidate = Path.Combine(current, "data", "scenarios");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = Path.GetDirectoryName(current);
        }

        return Path.Combine(AppContext.BaseDirectory, "data", "scenarios");
    }
}
