// 文件总览：UI↔核心互操作 —— 原生 DLL 探测与启动错误文案（T043）。
//
// 为什么独立成类型：DLL 缺失/位数不匹配是安装配置问题而非代码缺陷，
// 错误文案要告诉玩家"缺什么、在哪找、怎么修"（宪法第 17 条不静默），
// 同时保持可单元测试（探测函数不依赖真实 sim_core.dll）。

using System.IO;
using System.Runtime.InteropServices;

namespace WarFictionSim.Ui.Interop;

/// <summary>原生模拟核心库（sim_core.dll）的探测与错误文案。</summary>
public static class NativeSimLibrary
{
    /// <summary>默认库名（与 native/sim/CMakeLists.txt 的 sim_core 目标一致）。</summary>
    public const string DefaultLibraryName = "sim_core.dll";

    /// <summary>探测原生库是否可用（文件存在且可加载）。</summary>
    /// <param name="libraryName">库文件名；默认 sim_core.dll。</param>
    /// <returns>可加载返回 <see langword="true"/>；缺失或不可加载返回 <see langword="false"/>。</returns>
    public static bool IsAvailable(string libraryName = DefaultLibraryName)
    {
        // 按绝对路径探测：避免只按文件名命中 PATH 中其他同名库造成误判。
        string fullPath = Path.Combine(AppContext.BaseDirectory, libraryName);
        if (!File.Exists(fullPath))
        {
            return false;
        }

        try
        {
            return NativeLibrary.TryLoad(fullPath, out _);
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (BadImageFormatException)
        {
            return false;
        }
    }

    /// <summary>生成"缺什么 + 在哪找 + 怎么修"的可操作启动错误文案。</summary>
    /// <param name="libraryName">缺失的库文件名。</param>
    /// <returns>面向玩家的多行中文提示。</returns>
    public static string BuildStartupErrorText(string libraryName = DefaultLibraryName)
    {
        string expectedPath = Path.Combine(AppContext.BaseDirectory, libraryName);
        return $"未找到原生模拟核心库 {libraryName}，无法启动战斗。\n" +
               $"期望位置：{expectedPath}\n" +
               "请先构建 native 核心（例如：cd native 后执行 cmake --preset clang-cl-release 再执行 " +
               "cmake --build --preset clang-cl-release），并确认构建产物 sim_core.dll 已被复制到应用输出目录。\n" +
               "若出现位数不匹配错误，请确认核心与应用同为 x64。";
    }
}
