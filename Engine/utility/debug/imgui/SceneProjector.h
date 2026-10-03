#pragma once
#ifdef USE_IMGUI
#include <camera/projection/ViewProjection.h>
#include <imgui.h>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>

namespace Hagine {

/// <summary>
/// ワールド座標をシーン窓の画面座標へ写す。
/// シーン窓の描画リスト（ImDrawList）へ、太さのある滑らかな線で3Dの形を重ねて描くときに使う
/// （D3D の線は1ピクセル固定で、物に隠れると見えないため）。
/// カメラの後ろへはみ出す線分は、手前で切ってから写す
/// </summary>
class SceneProjector
{
  public:
    SceneProjector(const ViewProjection &viewProjection, const ImVec2 &sceneMin, const ImVec2 &sceneSize)
        : viewProjection_(viewProjection.matView_ * viewProjection.matProjection_), sceneMin_(sceneMin), sceneSize_(sceneSize)
    {
    }

    /// <summary>点を写す（カメラの後ろなら false）</summary>
    bool Point(const Vector3 &world, ImVec2 &outScreen) const
    {
        const Clip clip = ToClip(world);
        if (clip.w <= kNearW)
        {
            return false;
        }
        outScreen = ToScreen(clip);
        return true;
    }

    /// <summary>線分を写す（全部カメラの後ろなら false。片側だけなら手前で切る）</summary>
    bool Segment(const Vector3 &a, const Vector3 &b, ImVec2 &outA, ImVec2 &outB) const
    {
        Clip clipA = ToClip(a);
        Clip clipB = ToClip(b);
        if (clipA.w <= kNearW && clipB.w <= kNearW)
        {
            return false;
        }
        if (clipA.w <= kNearW)
        {
            clipA = Cut(clipB, clipA);
        }
        else if (clipB.w <= kNearW)
        {
            clipB = Cut(clipA, clipB);
        }
        outA = ToScreen(clipA);
        outB = ToScreen(clipB);
        return true;
    }

  private:
    struct Clip
    {
        float x, y, z, w;
    };
    static constexpr float kNearW = 0.01f;

    Clip ToClip(const Vector3 &v) const
    {
        const Matrix4x4 &m = viewProjection_;
        return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
                v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
                v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2],
                v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3]};
    }

    /// <summary>inside（手前）から outside（後ろ）へ向かう線分を、w = kNearW の所で切る</summary>
    static Clip Cut(const Clip &inside, const Clip &outside)
    {
        const float t = (inside.w - kNearW) / (inside.w - outside.w);
        return {inside.x + (outside.x - inside.x) * t, inside.y + (outside.y - inside.y) * t,
                inside.z + (outside.z - inside.z) * t, kNearW};
    }

    ImVec2 ToScreen(const Clip &clip) const
    {
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        return ImVec2(sceneMin_.x + (ndcX * 0.5f + 0.5f) * sceneSize_.x, sceneMin_.y + (0.5f - ndcY * 0.5f) * sceneSize_.y);
    }

    Matrix4x4 viewProjection_;
    ImVec2 sceneMin_;
    ImVec2 sceneSize_;
};

} // namespace Hagine
#endif // USE_IMGUI
