// 文件总览：UI↔核心互操作 —— 存档头只读解析（T039 读档前置校验）。
//
// 为什么在 C# 侧再实现一份轻量头解析：读档入口需要"先校验再进入"，
// 校验规则与 native/sim/src/save.cpp 保持一致（magic → 格式版本 → 头 JSON），
// 只读取头元数据、不触碰 state_blob；校验失败给出明确原因（宪法第 13/17 条），
// 真正的状态恢复仍交由核心 wfs_sim_load_save 完成（核心是最终权威）。

using System.IO;
using System.Text;
using System.Text.Json;

namespace WarFictionSim.Ui.Interop;

/// <summary>WFS-SAVE 存档头只读解析器。</summary>
public static class SaveHeaderReader
{
    private const string Magic = "WFS-SAVE";
    private const int MagicSize = 8;
    private const int VersionSize = 4;
    private const int HeaderLengthSize = 4;
    private const int BlobLengthSize = 8;
    private const int StateHashSize = 32;
    private const uint SupportedFormatVersion = 1U;

    /// <summary>读取并校验存档头；失败抛出带原因的中文异常。</summary>
    /// <param name="path">存档文件路径。</param>
    /// <returns>校验通过的存档头。</returns>
    /// <exception cref="SaveFileFormatException">magic/版本/头 JSON 校验失败。</exception>
    /// <exception cref="IOException">文件不可读。</exception>
    public static SaveHeader Read(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        byte[] data = File.ReadAllBytes(path);
        if (data.Length < MagicSize + VersionSize + HeaderLengthSize + BlobLengthSize + StateHashSize)
        {
            throw new SaveFileFormatException("存档文件过短，可能已损坏或不是 WarFictionSim 存档。");
        }

        if (Encoding.ASCII.GetString(data, 0, MagicSize) != Magic)
        {
            throw new SaveFileFormatException("文件头不是 WFS-SAVE magic，不是有效的 WarFictionSim 存档。");
        }

        uint version = ReadUInt32LittleEndian(data, MagicSize);
        if (version > SupportedFormatVersion)
        {
            throw new SaveFileFormatException(
                $"存档格式版本 {version} 高于当前支持的 {SupportedFormatVersion}，请升级应用后再读取。");
        }

        uint headerLength = ReadUInt32LittleEndian(data, MagicSize + VersionSize);
        const int headerOffset = MagicSize + VersionSize + HeaderLengthSize;
        if (headerLength > data.Length - headerOffset - BlobLengthSize - StateHashSize)
        {
            throw new SaveFileFormatException("存档头长度越界，文件已损坏。");
        }

        string headerText;
        try
        {
            headerText = Encoding.UTF8.GetString(data, headerOffset, checked((int)headerLength));
        }
        catch (DecoderFallbackException exception)
        {
            throw new SaveFileFormatException("存档头不是合法 UTF-8 文本，文件已损坏。", exception);
        }

        JsonDocument header;
        try
        {
            header = JsonDocument.Parse(headerText);
        }
        catch (JsonException exception)
        {
            throw new SaveFileFormatException("存档头不是合法 JSON，文件已损坏。", exception);
        }

        using (header)
        {
            return new SaveHeader
            {
                FormatVersion = version,
                AbiVersion = RequireString(header.RootElement, "abi_version"),
                ScenarioId = RequireString(header.RootElement, "scenario_id"),
                ScenarioName = RequireString(header.RootElement, "scenario_name"),
                Tick = RequireUInt64(header.RootElement, "tick"),
                Seed = RequireUInt64(header.RootElement, "seed"),
                Threads = RequireInt32(header.RootElement, "threads"),
                SchemaVersion = RequireInt64(header.RootElement, "schema_version"),
                StateHashAlgorithm = RequireString(header.RootElement, "state_hash_alg"),
                StateSizeBytes = RequireUInt64(header.RootElement, "state_size_bytes"),
            };
        }
    }

    /// <summary>读取存档头，失败不抛异常而是输出错误文本（供 UI 内联提示）。</summary>
    /// <param name="path">存档文件路径。</param>
    /// <param name="header">成功时输出存档头；失败时为 <see langword="null"/>。</param>
    /// <param name="error">失败时的中文原因；成功时为 <see langword="null"/>。</param>
    /// <returns>成功返回 <see langword="true"/>。</returns>
    public static bool TryRead(string path, out SaveHeader? header, out string? error)
    {
        try
        {
            header = Read(path);
            error = null;
            return true;
        }
        catch (Exception exception) when (exception is SaveFileFormatException or IOException or UnauthorizedAccessException)
        {
            header = null;
            error = $"无法读取存档：{exception.Message}";
            return false;
        }
    }

    private static uint ReadUInt32LittleEndian(byte[] data, int offset) =>
        (uint)data[offset] |
        ((uint)data[offset + 1] << 8) |
        ((uint)data[offset + 2] << 16) |
        ((uint)data[offset + 3] << 24);

    private static string RequireString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
        {
            throw new SaveFileFormatException($"存档头缺少字符串字段 {name}，文件已损坏。");
        }

        return value.GetString() ?? string.Empty;
    }

    private static ulong RequireUInt64(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || !value.TryGetUInt64(out ulong result))
        {
            throw new SaveFileFormatException($"存档头缺少非负整数字段 {name}，文件已损坏。");
        }

        return result;
    }

    private static long RequireInt64(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || !value.TryGetInt64(out long result))
        {
            throw new SaveFileFormatException($"存档头缺少整数字段 {name}，文件已损坏。");
        }

        return result;
    }

    private static int RequireInt32(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || !value.TryGetInt32(out int result))
        {
            throw new SaveFileFormatException($"存档头缺少整数字段 {name}，文件已损坏。");
        }

        return result;
    }
}

/// <summary>存档文件格式/损坏校验失败。</summary>
public sealed class SaveFileFormatException : Exception
{
    /// <summary>初始化异常。</summary>
    /// <param name="message">面向玩家的中文原因。</param>
    public SaveFileFormatException(string message)
        : base(message)
    {
    }

    /// <summary>初始化异常并保留内部异常链。</summary>
    /// <param name="message">面向玩家的中文原因。</param>
    /// <param name="innerException">底层解析异常。</param>
    public SaveFileFormatException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
