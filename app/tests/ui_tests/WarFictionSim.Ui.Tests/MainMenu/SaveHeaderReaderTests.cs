// 测试：主菜单读档前置校验 —— WFS-SAVE 存档头解析（T039）。
//
// 读档必须"先校验再进入"：magic、格式版本、头 JSON 三关与 native
// save.cpp 一致；未来版本明确拒绝（宪法第 13 条），损坏文件明确报错
// （宪法第 17 条）。这里只构造头段字节，不触碰 state_blob。

using System.Text;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.MainMenu;

public class SaveHeaderReaderTests
{
    private const string HeaderJson =
        """{"abi_version":"0.1.0","scenario_id":"scn-tutorial-platoon","scenario_name":"Tutorial: Platoon Attack","tick":1200,"seed":42,"threads":4,"schema_version":1,"state_hash_alg":"SHA-256","state_size_bytes":1024}""";

    [Fact]
    public void Read_ValidHeader_ReturnsAllFields()
    {
        string path = WriteSaveFile(HeaderJson, formatVersion: 1);

        SaveHeader header = SaveHeaderReader.Read(path);

        Assert.Equal(1u, header.FormatVersion);
        Assert.Equal("0.1.0", header.AbiVersion);
        Assert.Equal("scn-tutorial-platoon", header.ScenarioId);
        Assert.Equal((ulong)1200, header.Tick);
        Assert.Equal((ulong)42, header.Seed);
        Assert.Equal(4, header.Threads);
        Assert.Equal(1L, header.SchemaVersion);
        Assert.Equal("SHA-256", header.StateHashAlgorithm);
        Assert.Equal((ulong)1024, header.StateSizeBytes);
    }

    [Fact]
    public void Read_WithWrongMagic_Throws()
    {
        string path = WriteSaveFile(HeaderJson, formatVersion: 1, magic: "NOT-SAVE");

        SaveFileFormatException exception = Assert.Throws<SaveFileFormatException>(() => SaveHeaderReader.Read(path));

        Assert.Contains("magic", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Read_WithFutureFormatVersion_ThrowsUpgradeHint()
    {
        string path = WriteSaveFile(HeaderJson, formatVersion: 2);

        SaveFileFormatException exception = Assert.Throws<SaveFileFormatException>(() => SaveHeaderReader.Read(path));

        Assert.Contains("格式版本", exception.Message, StringComparison.Ordinal);
        Assert.Contains("升级", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void TryRead_MissingFile_ReturnsFalseWithError()
    {
        bool ok = SaveHeaderReader.TryRead("definitely-missing-save.wfs", out SaveHeader? header, out string? error);

        Assert.False(ok);
        Assert.Null(header);
        Assert.NotNull(error);
        Assert.Contains("无法读取存档", error, StringComparison.Ordinal);
    }

    [Fact]
    public void Read_WithCorruptHeaderJson_Throws()
    {
        string path = WriteSaveFile("{ not json", formatVersion: 1);

        Assert.Throws<SaveFileFormatException>(() => SaveHeaderReader.Read(path));
    }

    private static string WriteSaveFile(string headerJson, uint formatVersion, string magic = "WFS-SAVE")
    {
        string path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"wfs-save-{Guid.NewGuid():N}.wfs");
        byte[] header = Encoding.UTF8.GetBytes(headerJson);
        using var stream = new MemoryStream();
        stream.Write(Encoding.ASCII.GetBytes(magic));
        WriteUInt32LittleEndian(stream, formatVersion);
        WriteUInt32LittleEndian(stream, (uint)header.Length);
        stream.Write(header);
        byte[] blobLength = new byte[8];
        BitConverter.GetBytes((ulong)1024).CopyTo(blobLength, 0);
        stream.Write(blobLength);
        stream.Write(new byte[32]); // state_hash 占位，头解析不校验。
        File.WriteAllBytes(path, stream.ToArray());
        return path;
    }

    private static void WriteUInt32LittleEndian(Stream stream, uint value)
    {
        stream.WriteByte((byte)(value & 0xFF));
        stream.WriteByte((byte)((value >> 8) & 0xFF));
        stream.WriteByte((byte)((value >> 16) & 0xFF));
        stream.WriteByte((byte)((value >> 24) & 0xFF));
    }
}
