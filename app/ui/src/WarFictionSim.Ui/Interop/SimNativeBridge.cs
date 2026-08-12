// 文件总览：UI↔核心互操作 —— P/Invoke 桥接实现（T043）。
//
// 设计要点（contracts/sim-c-api.md；宪法第 14/17 条）：
// - DLL 名 sim_core.dll、__cdecl 调用约定与 wfs_sim_* 签名严格按 C ABI；
//   字符串按 UTF-8 封送，size_t 用 nuint，版本字符串按 IntPtr 读取
//   （返回的是静态存储指针，不能按 string 返回值自动释放）。
// - 句柄生命周期由 IDisposable 管理，destroy 幂等；同一句柄非线程安全，
//   因此所有调用串行化在同一把锁后（步进线程与渲染线程共享句柄时安全）。
// - 快照/事件查询走"先探长度、再按需分配"的两段式缓冲（NativeBufferReader），
//   首探成功也按实际写出长度解析；快照只读解析为不可变 DTO。
// - 模拟状态变更（命令注入/读档）统一经本接口，UI 不直接修改模拟状态。
// - 创建失败/DLL 缺失/ABI 错配全部抛出带可操作文案的中文异常，
//   绝不静默吞错（宪法第 17 条）。

using System.Runtime.InteropServices;

namespace WarFictionSim.Ui.Interop;

/// <summary>经 C ABI 访问模拟核心的生产桥接实现。</summary>
public sealed class SimNativeBridge : ISimClient
{
    private const int StateHashHexLength = 65;

    private readonly object _gate = new();
    private IntPtr _handle;
    private bool _disposed;

    private SimNativeBridge(IntPtr handle, string abiVersion)
    {
        _handle = handle;
        AbiVersion = abiVersion;
    }

    /// <summary>核心 ABI 版本（创建句柄时读取并校验）。</summary>
    public string AbiVersion { get; }

    /// <summary>创建句柄并校验 ABI 版本——所有启动错误在此转为可操作文案。</summary>
    /// <param name="scenarioPath">场景 JSON 路径。</param>
    /// <param name="seed">显式随机种子。</param>
    /// <param name="threads">并行度（≥1，只影响性能）。</param>
    /// <returns>就绪的桥接客户端。</returns>
    /// <exception cref="SimNativeException">DLL 缺失/错配/场景加载失败。</exception>
    public static SimNativeBridge Create(string scenarioPath, ulong seed, int threads)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(scenarioPath);
        if (threads < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(threads), "并行度必须 ≥ 1（线程数只影响性能，不影响状态哈希）。");
        }

        IntPtr handle;
        try
        {
            handle = NativeCreate(scenarioPath, seed, threads);
        }
        catch (DllNotFoundException exception)
        {
            throw new SimNativeException(
                SimResultCode.InternalError, NativeSimLibrary.BuildStartupErrorText(), exception);
        }
        catch (BadImageFormatException exception)
        {
            throw new SimNativeException(
                SimResultCode.InternalError,
                $"原生模拟核心库 {NativeSimLibrary.DefaultLibraryName} 位数与应用不匹配。请统一为 x64 后重新构建。",
                exception);
        }
        catch (EntryPointNotFoundException exception)
        {
            throw new SimNativeException(
                SimResultCode.InternalError,
                $"原生模拟核心库 {NativeSimLibrary.DefaultLibraryName} 缺少 C ABI 入口（ABI 错配）。请重新构建 native 核心与 UI，确保来自同一提交。",
                exception);
        }

        if (handle == IntPtr.Zero)
        {
            throw new SimNativeException(
                SimResultCode.InvalidData,
                $"模拟核心创建失败：场景加载或校验未通过（{scenarioPath}）。请检查场景数据是否合法、数据文件是否齐全。");
        }

        try
        {
            string version = ReadVersion();
            SimAbiVersion.Verify(version);
            return new SimNativeBridge(handle, version);
        }
        catch
        {
            NativeDestroy(handle);
            throw;
        }
    }

    /// <inheritdoc />
    public SimulationSnapshot GetSnapshot()
    {
        lock (_gate)
        {
            EnsureNotDisposed();
            string json = NativeBufferReader.Read(
                (byte[] buffer, nuint bufferSize, out nuint length) =>
                    NativeGetSnapshot(_handle, buffer, bufferSize, out length),
                "读取快照");
            return SnapshotReader.Parse(json);
        }
    }

    /// <inheritdoc />
    public string QueryEvents(string queryJson)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(queryJson);
        lock (_gate)
        {
            EnsureNotDisposed();
            return NativeBufferReader.Read(
                (byte[] buffer, nuint bufferSize, out nuint length) =>
                    NativeQueryEvents(_handle, queryJson, buffer, bufferSize, out length),
                "查询事件日志");
        }
    }

    /// <inheritdoc />
    public string GetStateHash()
    {
        lock (_gate)
        {
            EnsureNotDisposed();
            byte[] buffer = new byte[StateHashHexLength];
            NativeBufferReader.ThrowForResult(NativeGetStateHash(_handle, buffer), "读取状态哈希");

            // 核心保证写出 64 个小写十六进制字符 + NUL；按 64 字符截断防御脏缓冲。
            string hash = System.Text.Encoding.ASCII.GetString(buffer, 0, 64);
            return hash.Length == 64 ? hash : throw new SimNativeException(
                SimResultCode.InternalError, "读取状态哈希失败：核心返回长度非法。");
        }
    }

    /// <inheritdoc />
    public void Step()
    {
        lock (_gate)
        {
            EnsureNotDisposed();
            NativeBufferReader.ThrowForResult(NativeStep(_handle), "推进模拟 tick");
        }
    }

    /// <inheritdoc />
    public void InjectCommand(string commandJson)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(commandJson);
        lock (_gate)
        {
            EnsureNotDisposed();
            NativeBufferReader.ThrowForResult(NativeInjectCommand(_handle, commandJson), "注入命令");
        }
    }

    /// <inheritdoc />
    public void Save(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        lock (_gate)
        {
            EnsureNotDisposed();
            NativeBufferReader.ThrowForResult(NativeSave(_handle, path), "写入存档");
        }
    }

    /// <inheritdoc />
    public void LoadSave(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        lock (_gate)
        {
            EnsureNotDisposed();
            NativeBufferReader.ThrowForResult(NativeLoadSave(_handle, path), "读取存档");
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        // 与全部 wfs_sim_* 调用共用同一把锁：销毁句柄与在途调用互斥（F3），
        // 杜绝步进线程正在 wfs_sim_step/get_snapshot 内时被 use-after-free。
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            IntPtr handle = _handle;
            _handle = IntPtr.Zero;
            if (handle != IntPtr.Zero)
            {
                NativeDestroy(handle);
            }
        }
    }

    private void EnsureNotDisposed()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    private static string ReadVersion()
    {
        IntPtr pointer = NativeVersion();
        return pointer == IntPtr.Zero
            ? throw new SimNativeException(SimResultCode.InternalError, "读取模拟核心 ABI 版本失败（返回空指针）。")
            : Marshal.PtrToStringUTF8(pointer)
                ?? throw new SimNativeException(SimResultCode.InternalError, "读取模拟核心 ABI 版本失败（非法 UTF-8）。");
    }

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_create")]
    private static extern IntPtr NativeCreate(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string scenarioPath, ulong seed, int threads);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_destroy")]
    private static extern void NativeDestroy(IntPtr handle);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_version")]
    private static extern IntPtr NativeVersion();

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_step")]
    private static extern int NativeStep(IntPtr handle);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_inject_command")]
    private static extern int NativeInjectCommand(
        IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string commandJson);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_get_snapshot")]
    private static extern int NativeGetSnapshot(IntPtr handle, [Out] byte[] outBuffer, nuint bufferSize, out nuint outLength);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_query_events")]
    private static extern int NativeQueryEvents(
        IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string queryJson,
        [Out] byte[] outBuffer,
        nuint bufferSize,
        out nuint outLength);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_get_state_hash")]
    private static extern int NativeGetStateHash(IntPtr handle, [Out] byte[] outHex);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_save")]
    private static extern int NativeSave(IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(NativeSimLibrary.DefaultLibraryName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "wfs_sim_load_save")]
    private static extern int NativeLoadSave(IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
}
