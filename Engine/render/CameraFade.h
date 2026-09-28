#pragma once
#include "camera/projection/ViewProjection.h"
#include <MyMath.h>
#include <type/Matrix4x4.h>

namespace Hagine {

/// <summary>
/// カメラに近い物を「だんだん透けて、最後は見えなくなる」ようにする。
///
/// 物ごとに、カメラからその物の箱（向きを持ったローカルAABB）までの最短距離を測り、
/// 「消え始める距離」から「完全に消える距離」までを 1 → 0 にした値をマテリアルへ渡す。
/// ピクセルシェーダーはその値で画面の画素を網目状に間引く（半透明ではないので、
/// ディファードでも描画順を気にしなくてよく、影もそのまま残る）。
/// 地形のように大きい物は、カメラが常に近くにいるので対象から外す（大きさの上限）。
/// </summary>
class CameraFade
{
  public:
    struct Settings
    {
        bool enabled = true;
        float fadeStartDistance = 2.5f; // この距離より近づくと透け始める
        float fadeEndDistance = 0.4f;   // この距離まで近づくと完全に見えなくなる
        float maxObjectSize = 20.0f;    // これより大きい物（箱の対角線）は透けさせない（地形・床など）
    };

    /// <summary>設定（初回に保存ファイルから読み込む）</summary>
    static Settings &GetSettings();

    /// <summary>
    /// 物の見え具合を求める（1=普通 / 0=完全に消える）。影のパスでは常に 1
    /// </summary>
    /// <param name="viewProjection">描画に使うカメラ</param>
    /// <param name="localBounds">モデルのローカル空間AABB</param>
    /// <param name="worldMatrix">オブジェクトのワールド行列</param>
    static float Compute(const ViewProjection &viewProjection, const AABB &localBounds, const Matrix4x4 &worldMatrix);

    /// <summary>設定の UI（描画の設定窓に出す）</summary>
    static void DrawImGui();

    static void Load();
    static void Save();
};

} // namespace Hagine
