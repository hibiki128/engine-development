#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <string>
#include <type/Vector3.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// トーンマップの設定（GPU側）。
/// ※ EngineAssets/shaders/OffScreen/ToneMap.PS.hlsl の ToneMapParameters と
///   必ず同じ並び・同じ大きさにすること
/// </summary>
struct ToneMapSettingsGPU
{
    float exposure = 1.0f;   // 露出。2倍で1段明るい
    int mode = 2;            // 0=なし 1=Reinhard 2=ACES 3=Uncharted2
    float contrast = 1.0f;   // コントラスト (1.0 で素通し)
    float saturation = 1.0f; // 彩度 (1.0 で素通し)

    float whitePoint = 4.0f;                    // これ以上を白とみなす明るさ
    Vector3 colorFilter = {1.0f, 1.0f, 1.0f};   // 画面全体に掛ける色
};

/// <summary>
/// HDR で描いたシーンを、画面に出せる 0〜1 の範囲へ収める設定を持つクラス。
///
/// シーンはリニアFP16（1.0 を超える明るさをそのまま持つ）で描かれるので、
/// どこかで「見える範囲」へ落とし込む必要がある。その最後の1回がここ。
/// ポストエフェクトのチェーンを抜けた直後、UI を重ねる前に掛かる。
///
/// 露出を上げ下げすると写真の露出と同じように画面全体の明るさが変わり、
/// 明るい部分はカーブによってなだらかに白へ寄る（いきなり白飛びしない）。
/// </summary>
class ToneMapSettings
{
  private:
    ToneMapSettings() = default;
    ~ToneMapSettings() = default;
    ToneMapSettings(const ToneMapSettings &) = delete;
    ToneMapSettings &operator=(const ToneMapSettings &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスを取得</summary>
    static ToneMapSettings *GetInstance()
    {
        static ToneMapSettings instance;
        return &instance;
    }

    /// <summary>初期化（定数バッファの作成と、保存済み設定の読み込み）</summary>
    void Initialize();

    /// <summary>終了処理</summary>
    void Finalize();

    /// <summary>CPU側の設定をGPUバッファへ書き込む（描画前に呼ぶ）</summary>
    void Update();

    /// <summary>ImGuiによる設定UI</summary>
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
    /// 定数バッファのGPUアドレスを取得する（ToneMap.PS の b0 に差す）
    /// </summary>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: 未初期化なら 0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const
    {
        return resource_ ? resource_->GetGPUVirtualAddress() : 0;
    }

    /// <summary>設定への参照（演出でつまみを動かしたいとき用）</summary>
    ToneMapSettingsGPU &GetSettings() { return settings_; }
    const ToneMapSettingsGPU &GetSettings() const { return settings_; }

    /// <summary>露出</summary>
    void SetExposure(float exposure) { settings_.exposure = exposure; }
    float GetExposure() const { return settings_.exposure; }

    /// <summary>トーンマップのカーブ種別の表示名</summary>
    /// <param name="mode">0〜3</param>
    /// <returns>const char*: 表示名</returns>
    static const char *GetModeName(int mode);

  private:
    /// ====================================
    /// private variables
    /// ====================================

    DirectXCommon *pDxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    ToneMapSettingsGPU *pMapped_ = nullptr;
    ToneMapSettingsGPU settings_;
};

} // namespace Hagine
