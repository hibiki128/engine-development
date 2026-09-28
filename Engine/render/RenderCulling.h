#pragma once
#include "camera/projection/ViewProjection.h"
#include <Frustum.h>
#include <MyMath.h>
#include <type/Matrix4x4.h>

namespace Hagine {

/// <summary>
/// 錐台カリング。「このオブジェクトは画面に入るか」を描画の直前に判定する
///
/// 視錐台はビュー射影行列から作るが、同じ行列で何百回も呼ばれるので
/// 行列が変わったときだけ組み直してキャッシュしている。
///
/// シャドウパス中は必ず「見える」を返す。画面外の物でも影は画面内へ落ちるため、
/// カメラの視錐台で切ると影だけが消えてしまう
/// </summary>
class RenderCulling
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// カリングの有効/無効を切り替える
    /// </summary>
    /// <param name="enabled">有効にするなら true</param>
    static void SetEnabled(bool enabled);

    /// <summary>
    /// カリングが有効か
    /// </summary>
    /// <returns>bool: 有効なら true</returns>
    static bool IsEnabled();

    /// <summary>
    /// フレーム先頭で統計をリセットする
    /// </summary>
    static void BeginFrame();

    /// <summary>
    /// このオブジェクトを描く必要があるかを判定する
    /// </summary>
    /// <param name="viewProjection">描画に使うカメラ</param>
    /// <param name="localBounds">モデルのローカル空間AABB</param>
    /// <param name="worldMatrix">オブジェクトのワールド行列</param>
    /// <returns>bool: 描くなら true。画面外と判定できたときだけ false</returns>
    static bool IsVisible(const ViewProjection &viewProjection,
                          const AABB &localBounds,
                          const Matrix4x4 &worldMatrix);

    /// <summary>
    /// このフレームに判定した数
    /// </summary>
    /// <returns>int: 判定数</returns>
    static int GetTestedCount();

    /// <summary>
    /// このフレームに画面外と判定して省いた数
    /// </summary>
    /// <returns>int: 省いた数</returns>
    static int GetCulledCount();

    /// <summary>
    /// ImGuiでの設定UIと統計表示
    /// </summary>
    static void DrawImGui();

    /// ===================================================
    /// デバッグカメラから確かめる
    /// ===================================================

    /// <summary>
    /// デバッグカメラ使用中に、判定を「デバッグカメラの前のカメラ（メイン）」で行うための行列を渡す。
    /// 毎フレーム呼ぶ。デバッグカメラを使っていなければ nullptr を渡す（描画中のカメラで判定する）
    /// </summary>
    /// <param name="pMainViewProjection">メインカメラのビュー射影行列（無ければ nullptr）</param>
    static void SetInspectionCamera(const Matrix4x4 *pMainViewProjection);

    /// <summary>
    /// メインカメラで判定しているか（デバッグカメラ使用中かつ設定が有効）
    /// </summary>
    static bool IsInspecting();

    /// <summary>
    /// 「デバッグカメラ中はメインカメラで判定する」の切り替え
    /// </summary>
    static void SetInspectWithMainCamera(bool enabled);
    static bool IsInspectWithMainCamera();

    /// <summary>
    /// メインカメラの視錐台と、前のフレームで省いた/描いた物の箱を線で描く。
    /// LineRenderer::BeginFrame の後に呼ぶ
    /// </summary>
    static void SubmitDebugLines();
};
} // namespace Hagine
