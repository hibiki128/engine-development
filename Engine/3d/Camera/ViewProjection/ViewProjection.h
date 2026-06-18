#pragma once
#include "DirectXCommon.h"
#include "Easing.h"
#include "d3d12.h"
#include "numbers"
#include "type/Matrix4x4.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include "wrl.h"

/// <summary>
/// ビュープロジェクション用定数バッファ
/// </summary>
namespace Hagine {
struct ConstBufferDataViewProjection {
    Matrix4x4 view;       // ビュー行列
    Matrix4x4 projection; // 射影行列
    Vector3 cameraPos;    // カメラのワールド座標
};

/// <summary>
/// ビュープロジェクションクラス
/// カメラの行列計算、イージング処理、各種投影設定を一元管理する
/// </summary>
class ViewProjection {
public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    ViewProjection() = default;
    ~ViewProjection() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="jsonFile">初期設定用JSONファイルパス(省略可)</param>
    void Initialize(std::string jsonFile = "");

    /// <summary>
    /// 定数バッファの生成
    /// </summary>
    void CreateConstBuffer();

    /// <summary>
    /// 定数バッファのメモリマッピング
    /// </summary>
    void Map();

    /// <summary>
    /// 行列の再計算
    /// </summary>
    void UpdateMatrix();

    /// <summary>
    /// 定数バッファへのデータ転送
    /// </summary>
    void TransferMatrix();

    /// <summary>
    /// ビュー行列の更新
    /// </summary>
    void UpdateViewMatrix();

    /// <summary>
    /// 射影行列の更新
    /// </summary>
    void UpdateProjectionMatrix();

    /// <summary>
    /// カメラのイージング移動
    /// </summary>
    /// <param name="easeType">イージングのアルゴリズム</param>
    /// <param name="jsonName">目標値設定が記述されたJSON名</param>
    /// <param name="duration">移動にかける時間</param>
    void EaseCameraMove(EasingType easeType, const std::string &jsonName, float duration = 2.0f);

    /// <summary>
    /// デバッグ情報表示
    /// </summary>
    void ShowDebugInfo();

    /// <summary>
    /// 定数バッファを取得
    /// </summary>
    const Microsoft::WRL::ComPtr<ID3D12Resource> &GetConstBuffer() const { return constBuffer_; }
    
    /// <summary>
    /// 移動処理中か判定
    /// </summary>
    bool GetIsCameraMove() { return isEasing_; }

public:
    // ===================================================
    // 公開メンバ変数
    // ===================================================

    bool isUseQuaternion_ = false;                                                  // 回転モード(true:クォータニオン, false:オイラー角)
    Quaternion quateRotation_ = Quaternion::IdentityQuaternion();                   // クォータニオンによる回転
    Vector3 eulerRotation_ = {0.0f, 0.0f, 0.0f};                                    // オイラー角による回転(ラジアン)
    Vector3 translation_ = {0.0f, 0.0f, -10.0f};                                    // カメラ座標
    float fovAngleY_ = 45.0f * std::numbers::pi_v<float> / 180.0f;                   // 垂直方向視野角(ラジアン)
    float aspectRatio = float(WinApp::kClientWidth) / float(WinApp::kClientHeight); // アスペクト比
    float nearZ_ = 0.1f;                                                             // 近距離クリッピング面
    float farZ_ = 1000.0f;                                                           // 遠距離クリッピング面
    Matrix4x4 matView_{};                                                           // ビュー行列
    Matrix4x4 matProjection_{};                                                     // 射影行列
    Matrix4x4 matWorld_{};                                                          // ワールド行列

private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    /// <summary>
    /// 設定の保存
    /// </summary>
    /// <param name="jsonFile">保存先パス</param>
    void Save(std::string jsonFile);

    /// <summary>
    /// 設定の読み込み
    /// </summary>
    /// <param name="jsonFile">読み込み元パス</param>
    void Load(std::string jsonFile);

    // コピー不可
    ViewProjection(const ViewProjection &) = delete;
    ViewProjection &operator=(const ViewProjection &) = delete;

private:
    // ===================================================
    // メンバ変数
    // ===================================================

    DirectXCommon *dxCommon_ = nullptr; // DirectX基盤へのポインタ

    // イージング状態管理
    bool isEasing_ = false;                              // 実行中フラグ
    float easingTime_ = 0.0f;                            // 経過時間
    float easingDuration_ = 2.0f;                        // 全体時間
    EasingType currentEasingType_ = EasingType::OutQuad; // アルゴリズム種別

    // イージング始点
    Vector3 startTranslation_{};           
    Vector3 startEulerRotation_{};         
    Quaternion startQuaternionRotation_{}; 

    // イージング目標点
    Vector3 targetTranslation_{};         
    Vector3 targetEulerRotation_{};       
    Quaternion targetQuaternionRotation_{}; 

    // 定数バッファ関連
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_{}; 
    ConstBufferDataViewProjection *constMap_ = nullptr;   
};

static_assert(!std::is_copy_assignable_v<ViewProjection>);
} // namespace Hagine
