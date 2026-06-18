#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Transform/WorldTransform.h"

#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Transform/WorldTransform.h"

/// <summary>
/// 基本追従カメラクラス
/// ターゲットを追従するカメラの基底機能を提供する
/// </summary>
namespace Hagine {
class BaseFollowCamera {
public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Init();

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void imgui();

    /// <summary>
    /// ヨー角を取得
    /// </summary>
    float GetYaw() { return yaw_; }

    /// <summary>
    /// ビュープロジェクションを取得
    /// </summary>
    ViewProjection &GetViewProjection() { return viewProjection_; }

    /// <summary>
    /// 追従対象を設定
    /// </summary>
    void SetTarget(const WorldTransform *target) { target_ = target; }

private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    /// <summary>
    /// カメラの移動計算
    /// </summary>
    void Move();

private:
    // ===================================================
    // メンバ変数
    // ===================================================

    ViewProjection viewProjection_;          // ビュープロジェクション
    WorldTransform worldTransform_;          // ワールド変換
    const WorldTransform *target_ = nullptr; // 追従対象のワールド変換
    float yaw_ = 0.0f;                       // ヨー角(左右回転)
    float distanceFromTarget_ = 10.0f;       // ターゲットからの距離
    float heightOffset_ = 2.0f;              // 高さのオフセット
};
} // namespace Hagine
