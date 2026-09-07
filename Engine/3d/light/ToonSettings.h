#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// トゥーンシェーディングの設定（GPU側）。
/// ※ EngineAssets/shaders/Object/Toon.hlsli の ToonSettings と必ず同じ並びにすること
/// </summary>
struct ToonSettingsGPU
{
    Vector4 shadeColor = {0.55f, 0.56f, 0.68f, 1.0f};    // 影側の色（アルベドに乗算）
    Vector4 specularColor = {1.0f, 1.0f, 1.0f, 0.35f};   // rgb=ハイライト色 a=強さ
    Vector4 rimColor = {1.0f, 1.0f, 1.0f, 0.35f};        // rgb=リム色 a=強さ

    float enabled = 0.0f;   // 0=OFF, 1=ON
    float steps = 2.0f;     // 段数（1で明暗の2階調）
    float threshold = 0.5f; // 明暗の境目
    float softness = 0.02f; // 境目のなめらかさ

    float specularThreshold = 0.5f; // ハイライトが出はじめる強さ
    float specularSoftness = 0.05f; // ハイライトの境目
    float rimPower = 4.0f;          // リムの絞り
    float rimThreshold = 0.35f;     // リムが出はじめる位置

    float rimSoftness = 0.1f;     // リムの境目
    float rimLightMask = 1.0f;    // 1でリムを光の当たる側だけに出す
    float shadowSharpness = 1.0f; // 1で落ち影も量子化する
    float padding = 0.0f;
};

/// <summary>
/// トゥーン（セル）シェーディングの設定を持ち、GPUへ渡すクラス。
///
/// 陰影の数式そのものは EngineAssets/shaders/Object/Toon.hlsli にあり、
/// 前方描画（Object3d.PS）とディファード（DeferredLighting.PS）の両方が同じものを使う。
/// このクラスは「絵作りのつまみ」を1か所に集めて、両方の経路へ同じ値を配る役。
///
/// 有効／無効はここのグローバルスイッチと、マテリアルごとの enableToon の
/// 両方が立っているときだけON（地面だけ従来の陰影にする、といった除外ができる）。
/// </summary>
class ToonSettings
{
  private:
    ToonSettings() = default;
    ~ToonSettings() = default;
    ToonSettings(const ToonSettings &) = delete;
    ToonSettings &operator=(const ToonSettings &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static ToonSettings *GetInstance()
    {
        static ToonSettings instance;
        return &instance;
    }

    /// <summary>
    /// 初期化（定数バッファの作成）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// CPU側の設定をGPUバッファへ書き込む（毎フレーム描画前に呼ぶ）
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiによる設定UI
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// 設定をJSONへ保存する
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void SaveData(const std::string &fileName);

    /// <summary>
    /// 設定をJSONから読み込む
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void LoadData(const std::string &fileName);

    /// <summary>
    /// 定数バッファのGPUアドレスを取得する。
    /// 前方描画は b6、ディファードのライティングパスは b4 に差す。
    /// </summary>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: 未初期化なら 0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const
    {
        return resource_ ? resource_->GetGPUVirtualAddress() : 0;
    }

    /// <summary>
    /// トゥーンシェーディングが有効か
    /// </summary>
    bool IsEnabled() const { return settings_.enabled >= 0.5f; }

    /// <summary>
    /// トゥーンシェーディングの有効／無効を切り替える
    /// </summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetEnabled(bool enabled) { settings_.enabled = enabled ? 1.0f : 0.0f; }

    /// <summary>
    /// 設定への参照を取得する（演出でつまみを動かしたいとき用）
    /// </summary>
    ToonSettingsGPU &GetSettings() { return settings_; }
    const ToonSettingsGPU &GetSettings() const { return settings_; }

  private:
    /// <summary>
    /// 見た目のプリセットを適用する
    /// </summary>
    /// <param name="index">0=標準セル 1=アニメ調(3階調) 2=硬いコミック調</param>
    void ApplyPreset(int index);

    DirectXCommon *pDxCommon_ = nullptr;                  // DirectX共通処理
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;     // 定数バッファ
    ToonSettingsGPU *pMapped_ = nullptr;                  // 書き込み先
    ToonSettingsGPU settings_;                            // CPU側の設定
};
} // namespace Hagine
