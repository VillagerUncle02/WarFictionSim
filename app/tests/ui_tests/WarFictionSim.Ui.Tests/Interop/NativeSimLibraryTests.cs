// 测试：UI↔核心互操作 —— 原生 DLL 探测与启动错误路径（T043）。
//
// 验证宪法第 17 条"禁止静默吞错"在表现层的落地：sim_core.dll 缺失/不兼容时，
// 必须给出可操作的启动错误，而不是崩溃或空窗口；这些断言不依赖真实 DLL，
// 保证 CI 上（native job 上传 DLL 前/后）都确定性可跑。

using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.Interop;

public class NativeSimLibraryTests
{
    [Fact]
    public void DefaultLibraryName_IsSimCoreDll()
    {
        Assert.Equal("sim_core.dll", NativeSimLibrary.DefaultLibraryName);
    }

    [Fact]
    public void IsAvailable_WithMissingLibrary_ReturnsFalse()
    {
        // 文件名刻意不存在：验证探测路径按"加载失败"返回 false，而非抛异常。
        Assert.False(NativeSimLibrary.IsAvailable("wfs-definitely-missing.dll"));
    }

    [Fact]
    public void BuildStartupErrorText_IsActionable()
    {
        string text = NativeSimLibrary.BuildStartupErrorText("wfs-definitely-missing.dll");

        // 可操作 = 说明"缺什么" + "在哪找" + "怎么修"，三者缺一不可。
        Assert.Contains("wfs-definitely-missing.dll", text, StringComparison.Ordinal);
        Assert.Contains("cmake", text, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("输出目录", text, StringComparison.Ordinal);
    }
}
