#pragma once
#include <camera/Camera.h>
#include <string>
#include <transform/WorldTransform.h>

/// <summary>
/// 基本追従カメラクラス
/// ターゲットを追従するカメラの基底機能を提供する
/// カメラ本体（位置・向き・行列）は Camera が持ち、このクラスは追従のしかただけを決める
/// </summary>
namespace Hagine {
class BaseFollowCamera
{
  public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// 初期化（カメラを CameraManager へ登録する）
    /// </summary>
    /// <param name="cameraName">登録するカメラ名</param>
    void Init(const std::string &cameraName = "追従カメラ");

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// ヨー角を取得
    /// </summary>
    float GetYaw() { return yaw_; }

    /// <summary>
    /// カメラを取得（所有は CameraManager）
    /// </summary>
    Camera *GetCamera() const { return pCamera_; }

    /// <summary>
    /// 描画へ渡すビュープロジェクションを取得
    /// </summary>
    ViewProjection &GetViewProjection() { return pCamera_->GetViewProjection(); }

    /// <summary>
    /// 追従対象を設定
    /// </summary>
    void SetTarget(const WorldTransform *pTarget) { pTarget_ = pTarget; }

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

    Camera *pCamera_ = nullptr;               // カメラ本体（所有は CameraManager）
    const WorldTransform *pTarget_ = nullptr; // 追従対象のワールド変換
    float yaw_ = 0.0f;                        // ヨー角(左右回転)
    float distanceFromTarget_ = 10.0f;        // ターゲットからの距離
    float heightOffset_ = 2.0f;               // 高さのオフセット
};
} // namespace Hagine
