// 测试：兵牌地图 —— 视口平移/缩放数学（T040）。
//
// 平移缩放是纯几何变换（世界坐标 km ↔ 屏幕像素），不依赖 WPF，必须可单测：
// 缩放有界、平移有界（地图不得完全移出视口）、正反变换可逆。

using WarFictionSim.Ui.BattleMap;
using Xunit;

namespace WarFictionSim.Ui.Tests.BattleMap;

public class MapViewportTests
{
    [Fact]
    public void ResetToFit_CentersMap()
    {
        var viewport = new MapViewport(mapWidthKm: 5, mapHeightKm: 5);
        viewport.SetViewportSize(1000, 500);
        viewport.ResetToFit();

        (double centerX, double centerY) = viewport.WorldToScreen(2.5, 2.5);
        Assert.Equal(500, centerX, 1);
        Assert.Equal(250, centerY, 1);
    }

    [Fact]
    public void ZoomIn_And_ZoomOut_StayWithinBounds()
    {
        var viewport = new MapViewport(mapWidthKm: 5, mapHeightKm: 5, minZoom: 40, maxZoom: 100);
        viewport.SetViewportSize(1000, 1000);
        viewport.ResetToFit();
        double initial = viewport.Zoom;

        for (int i = 0; i < 100; i++)
        {
            viewport.ZoomIn();
        }

        Assert.Equal(100, viewport.Zoom);

        for (int i = 0; i < 200; i++)
        {
            viewport.ZoomOut();
        }

        Assert.Equal(40, viewport.Zoom);
        Assert.True(initial >= 40 && initial <= 100);
    }

    [Fact]
    public void WorldToScreen_And_ScreenToWorld_AreInverse()
    {
        var viewport = new MapViewport(mapWidthKm: 5, mapHeightKm: 5);
        viewport.SetViewportSize(1000, 1000);
        viewport.ResetToFit();
        viewport.PanBy(-50, -30);

        (double screenX, double screenY) = viewport.WorldToScreen(3.75, 1.25);
        (double worldX, double worldY) = viewport.ScreenToWorld(screenX, screenY);

        Assert.Equal(3.75, worldX, 6);
        Assert.Equal(1.25, worldY, 6);
    }

    [Fact]
    public void PanBy_ClampsSoMapCannotLeaveViewport()
    {
        var viewport = new MapViewport(mapWidthKm: 5, mapHeightKm: 5);
        viewport.SetViewportSize(500, 500);
        viewport.ResetToFit();
        viewport.ZoomIn(); // 500px 视口 / 5km → 125 px/km，地图 625px 大于视口，平移才有效。

        viewport.PanBy(100000, 100000);
        (double leftEdgeX, _) = viewport.WorldToScreen(0, 0);
        Assert.InRange(leftEdgeX, -1, 501);

        viewport.PanBy(-100000, -100000);
        (double rightEdgeX, _) = viewport.WorldToScreen(5, 0);
        Assert.InRange(rightEdgeX, -1, 501);
    }
}
