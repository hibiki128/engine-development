#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "type/Matrix4x4.h"
#include "type/Vector2.h"
#include "type/Vector3.h"

#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "type/Matrix4x4.h"
#include "type/Vector2.h"
#include "type/Vector3.h"

/// <summary>
/// デバッグカメラクラス
/// 開発時の視点操作と各種デバッグ用視点機能を提供する
/// </summary>
namespace Hagine {
class DebugCamera {
public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="viewProjection">対象のビュープロジェクション</param>
    void Initialize(ViewProjection *viewProjection);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void imgui();

    /// <summary>
    /// カメラのアクティブ状態を取得
    /// </summary>
    bool GetActive() { return isActive_; }

public:
    // ===================================================
    // 公開メンバ変数
    // ===================================================

    Vector3 rotation_ = {0.0f, 0.0f, 0.0f};      // 各軸(XYZ)の回転角
    Vector3 translation_ = {0.0f, 0.0f, -50.0f}; // ワールド座標
    Matrix4x4 matRot_;                           // 回転行列

private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    /// <summary>
    /// マウス入力によるカメラ移動処理
    /// </summary>
    /// <param name="cameraRotate">回転角(出力用)</param>
    /// <param name="cameraTranslate">座標(出力用)</param>
    /// <param name="clickPosition">クリック開始位置</param>
    void CameraMove(Vector3 &cameraRotate, Vector3 &cameraTranslate, Vector2 &clickPosition);

private:
    // ===================================================
    // メンバ変数
    // ===================================================

    ViewProjection *viewProjection_{};                            // 対象のビュープロジェクション
    Vector2 mouse_{};                                              // 現在のマウス座標
    Vector3 eulerRotation_ = {0.0f, 0.0f, 0.0f};                  // オイラー角による回転
    Quaternion quateRotation_ = Quaternion::IdentityQuaternion(); // クォータニオンによる回転
    Matrix4x4 rotateXYZMatrix_{};                                  // XYZ回転行列
    Matrix4x4 matRotDelta_{};                                      // 回転差分行列
    float mouseSensitivity_ = 0.003f;                              // マウスの感度
    float moveZspeed_ = 0.005f;                                    // Z軸方向の移動速度
    bool lockCamera_ = true;                                      // カメラ操作のロック状態
    bool useKey_ = true;                                          // キー操作の有効状態
    bool useMouse_ = false;                                       // マウス操作の有効状態
    bool isActive_ = false;                                       // カメラ自体のアクティブ状態
    bool isUseQuaternion_ = false;                                // クォータニオン計算の利用フラグ
};
} // namespace Hagine
