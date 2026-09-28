#pragma once
#include <camera/Camera.h>
#include <type/Matrix4x4.h>
#include <type/Vector2.h>
#include <type/Vector3.h>

/// <summary>
/// デバッグカメラクラス
/// 開発時の視点操作と各種デバッグ用視点機能を提供する
/// </summary>
namespace Hagine {
class DebugCamera
{
  public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// 初期化。デバッグ用のカメラを CameraManager へ登録する。
    /// 有効にするとそのカメラへ切り替わり、無効に戻すと元のカメラへ戻る。
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// カメラのアクティブ状態を取得
    /// </summary>
    bool GetActive() { return isActive_; }

    /// <summary>
    /// 有効にする直前までアクティブだったカメラ（メイン）。使っていなければ nullptr
    /// </summary>
    Camera *GetPreviousCamera() const { return isActive_ ? pPreviousCamera_ : nullptr; }

    /// <summary>
    /// カメラのアクティブ状態を設定する
    /// 有効にするとデバッグカメラへ、無効に戻すと元のカメラへ切り替わる
    /// （実際の切り替えは Update で行われる）
    /// </summary>
    /// <param name="active">有効にするか</param>
    void SetActive(bool active) { isActive_ = active; }

    /// <summary>
    /// キー移動の速さ（シーンビューのツールバーから変える）
    /// </summary>
    float GetMoveSpeed() const { return moveZspeed_; }
    void SetMoveSpeed(float speed) { moveZspeed_ = speed; }

    /// <summary>今の視点（位置とオイラー角）。カメラのブックマークに使う</summary>
    const Vector3 &GetViewPosition() const { return translation_; }
    const Vector3 &GetViewRotation() const { return eulerRotation_; }

    /// <summary>
    /// 視点を直接置く（ブックマークの呼び出し）。回転はオイラー角（ラジアン）
    /// </summary>
    void SetView(const Vector3 &position, const Vector3 &rotation);

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

    Camera *pCamera_ = nullptr;         // デバッグ操作を反映するカメラ（CameraManager が所有）
    Camera *pPreviousCamera_ = nullptr; // 有効化する直前までアクティブだったカメラ（戻る先）
    bool wasActive_ = false;            // 前フレームのアクティブ状態（切り替わりの検出用）
    Vector2 mouse_{};                                             // 現在のマウス座標
    Vector3 eulerRotation_ = {0.0f, 0.0f, 0.0f};                  // オイラー角による回転
    Quaternion quaternionRotation_ = Quaternion::IdentityQuaternion(); // クォータニオンによる回転
    Matrix4x4 rotateXYZMatrix_{};                                 // XYZ回転行列
    Matrix4x4 matRotDelta_{};                                     // 回転差分行列
    float mouseSensitivity_ = 0.003f;                             // マウスの感度
    float moveZspeed_ = 0.005f;                                   // Z軸方向の移動速度
    bool lockCamera_ = true;                                      // カメラ操作のロック状態
    bool useKey_ = true;                                          // キー操作の有効状態
    bool useMouse_ = false;                                       // マウス操作の有効状態
    bool isActive_ = false;                                       // カメラ自体のアクティブ状態
    bool hasPendingView_ = false;                                 // SetView で置く視点があるか
    Vector3 pendingViewPosition_ = {0.0f, 0.0f, 0.0f};            // SetView で置く位置
    Vector3 pendingViewRotation_ = {0.0f, 0.0f, 0.0f};            // SetView で置く回転
    bool isUseQuaternion_ = false;                                // クォータニオン計算の利用フラグ
};
} // namespace Hagine
