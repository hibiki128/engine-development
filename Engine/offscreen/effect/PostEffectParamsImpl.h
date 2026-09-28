#pragma once
#include "IPostEffectParams.h"
#include <type/Matrix4x4.h>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <graphics/texture/TextureManager.h>
#include <asset/AssetPath.h>
#include <algorithm>
#include <cmath>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/AssetDragDrop.h"  // テクスチャのD&D設定（ドロップ先）
#include "utility/debug/imgui/DebugUIHelper.h" // 色は役割で選ぶ（DebugTheme）
#endif

// ============================================================
//  ヘルパー: 定数バッファ作成
// ============================================================
namespace Hagine {
namespace PostEffectParamsHelper {
template <typename T>
static void CreateConstantBuffer(DirectXCommon *pDxCommon,
                                 Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                                 T **mappedData)
{
    const UINT64 size = (sizeof(T) + 255) & ~255;
    resource = pDxCommon->CreateBufferResource(size);
    resource->Map(0, nullptr, reinterpret_cast<void **>(mappedData));
}

// ------------------------------------------------------------
//  分離フィルタ（横→縦の2パス）で共有する定数バッファ
// ------------------------------------------------------------
// GaussianBlur.CS.hlsl の GaussianParams と同じ並びにすること。
// ガウスとボックスは「重みが違うだけ」で構造が同じなので、シェーダーごと共有している。
namespace SeparableBlur {
// カーネル半径の上限。GaussianBlur.CS.hlsl の kMaxRadius と一致させること
constexpr int kMaxRadius = 7;
// 横方向→縦方向の2パス
constexpr int kPassCount = 2;

struct ComputeData
{
    int radius = 2;                              // カーネル半径
    int direction = 0;                           // 0=横, 1=縦
    int textureSize[2] = {0, 0};                 // 解像度
    float weights[(kMaxRadius + 1 + 3) / 4 * 4] = {}; // 正規化済みの重み（float4詰め）
};
} // namespace SeparableBlur

// ------------------------------------------------------------
//  ルートパラメータ番号の解決
// ------------------------------------------------------------
// ポストエフェクトのルートシグネチャはシェーダーのリフレクションから組まれており、
// 並びは「b# → t#」になっている（b0=0番, t0=1番, t1=2番 …）。
// 以前の「t0=0番, b0=1番」という並びとは入れ替わっているので、番号を直書きすると
// b0 のつもりで t0 のテーブルへ差してしまい、そのエフェクトだけ描画されなくなる。
// 番号は必ずここを通して引くこと。
namespace RootParam {

/// <summary>
/// そのシェーダーモードのポストエフェクト用ルートシグネチャを取得する
/// </summary>
/// <param name="mode">シェーダーモード</param>
/// <returns>const ShaderRootSignature*: 未生成なら nullptr</returns>
inline const ShaderRootSignature *Get(ShaderMode mode)
{
    return PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::Render, mode);
}

/// <summary>
/// b0（エフェクトのパラメータ）をバインドする。
/// シェーダーが b0 を使っていなければ何もしない（未使用のレジスタはリフレクションから落ちるため）。
/// </summary>
/// <param name="pCommandList">コマンドリスト</param>
/// <param name="mode">シェーダーモード</param>
/// <param name="resource">定数バッファ</param>
inline void BindCBV(ID3D12GraphicsCommandList *pCommandList, ShaderMode mode,
                    const Microsoft::WRL::ComPtr<ID3D12Resource> &resource)
{
    const ShaderRootSignature *rs = Get(mode);
    if (!rs || !resource)
    {
        return;
    }
    const UINT rootIndex = rs->GetCbvIndex(0);
    if (rootIndex == UINT_MAX)
    {
        return;
    }
    pCommandList->SetGraphicsRootConstantBufferView(rootIndex, resource->GetGPUVirtualAddress());
}

/// <summary>
/// t# のデスクリプタテーブルをバインドする。
/// t0（入力画像）は描画側が差すので、ここで扱うのは t1 以降の追加テクスチャ。
/// </summary>
/// <param name="pCommandList">コマンドリスト</param>
/// <param name="mode">シェーダーモード</param>
/// <param name="shaderRegister">t# の番号</param>
/// <param name="handle">差すデスクリプタのGPUハンドル</param>
inline void BindSRV(ID3D12GraphicsCommandList *pCommandList, ShaderMode mode,
                    UINT shaderRegister, D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    const ShaderRootSignature *rs = Get(mode);
    if (!rs || handle.ptr == 0)
    {
        return;
    }
    const UINT rootIndex = rs->GetSrvIndex(shaderRegister);
    if (rootIndex == UINT_MAX)
    {
        return;
    }
    pCommandList->SetGraphicsRootDescriptorTable(rootIndex, handle);
}

} // namespace RootParam

} // namespace PostEffectParamsHelper

#ifdef USE_IMGUI
namespace PostEffectParamsHelper {
/// <summary>
/// テクスチャ選択UI（サムネ表示＋アセットブラウザからのドラッグ&ドロップ受け）。
/// ノイズ画像等を使うポストエフェクトのDrawUIから共通で呼ぶ。
/// </summary>
/// <param name="label">見出しラベル</param>
/// <param name="outRelPath">現在のテクスチャ相対パス（images ルート基準）。変更時に書き換わる</param>
/// <returns>ドロップで差し替えられたら true</returns>
inline bool DrawTextureSelector(const char *label, std::string &outRelPath)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::SeparatorText(label);

    auto *tm = TextureManager::GetInstance();

    // 現在のテクスチャのサムネイル（未設定ならドロップ用プレースホルダ）
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!outRelPath.empty())
    {
        tm->LoadTexture(outRelPath); // 未ロードなら読む（ロード済みは即return）
        handle = tm->GetSrvHandleGPU(AssetPath::Image(outRelPath));
    }
    if (handle.ptr != 0)
        ImGui::Image(static_cast<ImTextureID>(handle.ptr), ImVec2(56.0f, 56.0f));
    else
        ImGui::Button("ここへ\nドロップ", ImVec2(56.0f, 56.0f));

    // サムネ（またはプレースホルダ）をアセットブラウザからのドロップ先にする
    {
        std::string dropped;
        if (AssetDragDrop::TextureTarget(dropped))
        {
            outRelPath = dropped;
            tm->LoadTexture(outRelPath);
            changed = true;
        }
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(outRelPath.empty() ? "(未設定)" : outRelPath.c_str());
    ImGui::TextDisabled("アセットブラウザからD&Dで変更");
    ImGui::EndGroup();

    ImGui::PopID();
    return changed;
}
} // namespace PostEffectParamsHelper
#endif

// ============================================================
//  None
// ============================================================
class NoneParams : public IPostEffectParams
{
  public:
    void Initialize(DirectXCommon *) override {}
    ShaderMode GetMode() const override { return ShaderMode::None; }
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}
    void DrawUI() override {}
    void Save(DataHandler *, const std::string &) const override {}
    void Load(DataHandler *, const std::string &) override {}
};

// ============================================================
//  Gray（グレイスケール・調整パラメータなし）
// ============================================================
// None と同じくパラメータ（CBV）を持たないが、GetMode() で Gray を返すことで
// レンダラが Gray シェーダの PSO を選択できるようにする。
// （NoneParams を流用すると GetMode()==None となり「エフェクト無し」扱いになってしまう）
class GrayParams : public IPostEffectParams
{
  public:
    void Initialize(DirectXCommon *) override {}
    ShaderMode GetMode() const override { return ShaderMode::Gray; }
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}
    void DrawUI() override {}
    void Save(DataHandler *, const std::string &) const override {}
    void Load(DataHandler *, const std::string &) override {}
};

// ============================================================
//  Monochrome（完全な白黒・明度で白or黒に二値化）
// ============================================================
// グレイスケール（灰色化）と違い、明度(輝度)が閾値以上なら白、未満なら黒に振り分ける。
class MonochromeParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float threshold = 0.5f; // この明度を境に白/黒へ二値化する
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Monochrome; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("白黒の閾値", &pData_->threshold, 0.01f, 0.0f, 1.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "threshold", pData_->threshold);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->threshold = h->Load<float>(p + "threshold", 0.5f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Vignette
// ============================================================
class VignetteParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float strength = 1.0f;
        float radius = 0.8f;
        float exponent = 2.0f;
        float padding = 0.0f;
        Vector2 center = {0.5f, 0.5f};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Vignette; }

    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        // ビネットの各パラメータ調整（強度・形状・位置）
        ImGui::DragFloat("強度", &pData_->strength, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("半径", &pData_->radius, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("指数", &pData_->exponent, 0.1f, 0.1f, 10.0f);
        ImGui::DragFloat2("中心", &pData_->center.x, 0.01f, 0.0f, 1.0f);
#endif
    }

    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "strength", pData_->strength);
        h->Save<float>(p + "radius", pData_->radius);
        h->Save<float>(p + "exponent", pData_->exponent);
        h->Save<float>(p + "centerX", pData_->center.x);
        h->Save<float>(p + "centerY", pData_->center.y);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->strength = h->Load<float>(p + "strength", 1.0f);
        pData_->radius = h->Load<float>(p + "radius", 0.8f);
        pData_->exponent = h->Load<float>(p + "exponent", 2.0f);
        pData_->center.x = h->Load<float>(p + "centerX", 0.5f);
        pData_->center.y = h->Load<float>(p + "centerY", 0.5f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Smooth (Box Filter)
// ============================================================
class SmoothParams : public IPostEffectParams
{
  public:
    struct Data
    {
        int kernelSize = 3;
        int pad[3] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        // 定数バッファはパスごとに別で持つ（使い回すと両パスが同じ向きで実行される）
        for (int pass = 0; pass < kComputePassCount; ++pass)
        {
            PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResources_[pass], &pComputeData_[pass]);
            *pComputeData_[pass] = PostEffectParamsHelper::SeparableBlur::ComputeData{};
        }
    }
    ShaderMode GetMode() const override { return ShaderMode::Smooth; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }

    // ---- コンピュートシェーダー版 ----
    // ボックスフィルタは「重みが一様なだけ」でガウスと構造が同じなので、
    // 分離2パスのシェーダーをそのまま使い回し、重みを 1/(2r+1) で埋める。
    // 旧PS版は kIndex3x3[3][3] を x<=3 で回しており配列外まで読んでいた（このバグも解消される）。
    std::string GetComputeShaderFile() const override { return "OffScreen/GaussianBlur.CS.hlsl"; }
    int GetComputePassCount() const override { return kComputePassCount; }

    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int passIndex,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX || passIndex < 0 || passIndex >= kComputePassCount)
        {
            return;
        }
        UpdateComputeWeights();

        PostEffectParamsHelper::SeparableBlur::ComputeData *data = pComputeData_[passIndex];
        data->direction = passIndex;
        data->textureSize[0] = static_cast<int>(textureWidth);
        data->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex,
                                                       computeResources_[passIndex]->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &pData_->kernelSize, 2, 3, PostEffectParamsHelper::SeparableBlur::kMaxRadius * 2 + 1);
        ImGui::TextDisabled("コンピュートシェーダーで横→縦の2パス実行");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<int>(p + "kernelSize", pData_->kernelSize); }
    void Load(DataHandler *h, const std::string &p) override { pData_->kernelSize = h->Load<int>(p + "kernelSize", 3); }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    static constexpr int kComputePassCount = 2;

    /// 一様な重み（ボックスフィルタ）を定数バッファへ書き込む
    void UpdateComputeWeights()
    {
        int kernelSize = (std::max)(3, pData_->kernelSize);
        if (kernelSize % 2 == 0)
        {
            ++kernelSize;
        }
        const int radius = (std::min)(PostEffectParamsHelper::SeparableBlur::kMaxRadius, (kernelSize - 1) / 2);
        if (radius == cachedRadius_)
        {
            return;
        }
        cachedRadius_ = radius;

        // 1次元の一様重み。中心1個＋左右それぞれ radius 個で合計 2*radius+1 個。
        const float weight = 1.0f / static_cast<float>(radius * 2 + 1);
        for (int pass = 0; pass < kComputePassCount; ++pass)
        {
            pComputeData_[pass]->radius = radius;
            for (int i = 0; i <= radius; ++i)
            {
                pComputeData_[pass]->weights[i] = weight;
            }
            for (int i = radius + 1; i <= PostEffectParamsHelper::SeparableBlur::kMaxRadius; ++i)
            {
                pComputeData_[pass]->weights[i] = 0.0f;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> computeResources_[kComputePassCount];
    PostEffectParamsHelper::SeparableBlur::ComputeData *pComputeData_[kComputePassCount] = {};
    int cachedRadius_ = -1;

  public:

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Gaussian
// ============================================================
class GaussianParams : public IPostEffectParams
{
  public:
    // 定数バッファの形と上限はボックスフィルタ（SmoothParams）と共有している
    using ComputeData = PostEffectParamsHelper::SeparableBlur::ComputeData;
    static constexpr int kMaxRadius = PostEffectParamsHelper::SeparableBlur::kMaxRadius;
    static constexpr int kComputePassCount = PostEffectParamsHelper::SeparableBlur::kPassCount;

    struct Data
    {
        int kernelSize = 5;
        float sigma = 1.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        // 定数バッファはパスごとに別で持つ。
        // ディスパッチはコマンドリストへ積まれるだけで実行はあとなので、
        // 1つのバッファを使い回すと、両方のパスが「最後に書いた値」を読んでしまい、
        // 横方向のぼかしが縦方向として2回実行されてしまう。
        for (int pass = 0; pass < kComputePassCount; ++pass)
        {
            PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResources_[pass], &pComputeData_[pass]);
            *pComputeData_[pass] = ComputeData{};
        }
    }
    ShaderMode GetMode() const override { return ShaderMode::Gauss; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }

    // ---- コンピュートシェーダー版 ----
    // 横方向→縦方向の2パスに分けることで、7x7なら 49タップ → 14タップになる。
    std::string GetComputeShaderFile() const override { return "OffScreen/GaussianBlur.CS.hlsl"; }
    int GetComputePassCount() const override { return kComputePassCount; }

    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int passIndex,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX || passIndex < 0 || passIndex >= kComputePassCount)
        {
            return;
        }
        // 重みはCPUで1回だけ計算する。シェーダー側で exp() を回さないのが狙い。
        UpdateComputeWeights();

        ComputeData *data = pComputeData_[passIndex];
        data->direction = passIndex; // 0パス目=横, 1パス目=縦
        data->textureSize[0] = static_cast<int>(textureWidth);
        data->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex,
                                                       computeResources_[passIndex]->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &pData_->kernelSize, 2, 3, kMaxRadius * 2 + 1);
        ImGui::DragFloat("シグマ", &pData_->sigma, 0.01f, 0.1f, 10.0f);
        ImGui::TextDisabled("コンピュートシェーダーで横→縦の2パス実行");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<int>(p + "kernelSize", pData_->kernelSize);
        h->Save<float>(p + "sigma", pData_->sigma);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->kernelSize = h->Load<int>(p + "kernelSize", 5);
        pData_->sigma = h->Load<float>(p + "sigma", 1.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    /// <summary>
    /// ガウス係数を計算して定数バッファへ書き込む。
    /// カーネルサイズとシグマが変わったときだけ再計算する。
    /// </summary>
    void UpdateComputeWeights()
    {
        // カーネルサイズは奇数に丸め、半径の上限でクランプする
        int kernelSize = (std::max)(3, pData_->kernelSize);
        if (kernelSize % 2 == 0)
        {
            ++kernelSize;
        }
        const int radius = (std::min)(kMaxRadius, (kernelSize - 1) / 2);
        const float sigma = (std::max)(0.01f, pData_->sigma);

        if (radius == cachedRadius_ && sigma == cachedSigma_)
        {
            return; // 変化していなければ計算しない
        }
        cachedRadius_ = radius;
        cachedSigma_ = sigma;
        // 以降、全パスぶんの定数バッファへ同じ係数を書き込む

        // 1次元のガウス関数。2次元ガウスは1次元の積に分解できるので、
        // 横方向・縦方向で同じ重みを使い回せる。
        float sum = 0.0f;
        float weights[kMaxRadius + 1] = {};
        const float denominator = 2.0f * sigma * sigma;
        for (int i = 0; i <= radius; ++i)
        {
            weights[i] = std::exp(-static_cast<float>(i * i) / denominator);
            // 中心以外は左右（上下）の2箇所で使うので、正規化の合計には2回ぶん数える
            sum += (i == 0) ? weights[i] : weights[i] * 2.0f;
        }

        for (int pass = 0; pass < kComputePassCount; ++pass)
        {
            pComputeData_[pass]->radius = radius;
            for (int i = 0; i <= radius; ++i)
            {
                pComputeData_[pass]->weights[i] = weights[i] / sum;
            }
            for (int i = radius + 1; i <= kMaxRadius; ++i)
            {
                pComputeData_[pass]->weights[i] = 0.0f;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;

    // パスごとに別の定数バッファを持つ（使い回すと両パスが同じ値を読んでしまう）
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResources_[kComputePassCount];
    ComputeData *pComputeData_[kComputePassCount] = {};
    int cachedRadius_ = -1;    // 係数を計算したときの半径
    float cachedSigma_ = -1.0f; // 係数を計算したときのシグマ
};

// ============================================================
//  Outline (Edge Detection)
// ============================================================
class OutlineEdgeParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float edgeStrength = 1.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Outline; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("エッジ強度", &pData_->edgeStrength, 0.01f, 0.0f, 5.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<float>(p + "edgeStrength", pData_->edgeStrength); }
    void Load(DataHandler *h, const std::string &p) override { pData_->edgeStrength = h->Load<float>(p + "edgeStrength", 1.0f); }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Outline (Depth Based)
// ============================================================
class OutlineDepthParams : public IPostEffectParams
{
  public:
    struct Data
    {
        Matrix4x4 projectionInverse;
        int kernelSize = 3;
        float pad[3] = {};
    };

    /// CS版に渡す定数バッファ。DepthOutline.CS.hlsl の OutlineParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 projectionInverse;
        Vector4 outlineColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 輪郭の色と濃さ
        float threshold = 0.02f;                        // 輪郭とみなす相対深度差
        float thickness = 1.0f;                         // 輪郭の太さ（ピクセル）
        int textureSize[2] = {0, 0};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Depth; }

    void SetProjectionInverse(const Matrix4x4 &mat)
    {
        pData_->projectionInverse = mat;
        pComputeData_->projectionInverse = mat;
    }

    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
        // t1: シーンの深度。CS版が使えないときのPS版フォールバックで必要になる
        // （差さずに描くと t1 が未設定のまま描画され、輪郭がまったく出ない）
        if (pDxCommon)
        {
            PostEffectParamsHelper::RootParam::BindSRV(pCommandList, GetMode(), 1,
                                                       pDxCommon->GetDepthGPUHandle());
        }
    }

    // ---- コンピュートシェーダー版 ----
    // 旧PS版は生のビュー空間ZにPrewittを掛けていたため、距離によって輪郭の出方が変わっていた。
    // CS版は深度差を中心深度で割った相対値で判定する。
    std::string GetComputeShaderFile() const override { return "OffScreen/DepthOutline.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth};
    }
    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::ColorEdit3("輪郭の色", &pComputeData_->outlineColor.x);
        ImGui::DragFloat("輪郭の濃さ", &pComputeData_->outlineColor.w, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("しきい値", &pComputeData_->threshold, 0.001f, 0.001f, 0.5f, "%.3f");
        ImGui::SetItemTooltip("小さくすると輪郭が増えます。深度差を中心の深度で割った相対値なので、\n"
                              "カメラからの距離が変わっても同じ設定で使えます");
        ImGui::DragFloat("太さ(px)", &pComputeData_->thickness, 0.1f, 1.0f, 8.0f, "%.0f");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<int>(p + "kernelSize", pData_->kernelSize);
        h->Save<Vector4>(p + "outlineColor", pComputeData_->outlineColor);
        h->Save<float>(p + "threshold", pComputeData_->threshold);
        h->Save<float>(p + "thickness", pComputeData_->thickness);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->kernelSize = h->Load<int>(p + "kernelSize", 3);
        pComputeData_->outlineColor = h->Load<Vector4>(p + "outlineColor", Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        pComputeData_->threshold = h->Load<float>(p + "threshold", 0.02f);
        pComputeData_->thickness = h->Load<float>(p + "thickness", 1.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;
};

// ============================================================
//  Depth of Field（被写界深度）
// ============================================================
// ピント面から外れた場所ほどボカす。コンピュートシェーダー専用。
class DepthOfFieldParams : public IPostEffectParams
{
  public:
    /// DepthOfField.CS.hlsl の DofParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 projectionInverse;
        float focusDistance = 20.0f;  // ピントの合う距離
        float focusRange = 8.0f;      // ピントが合っているとみなす前後の幅
        float maxBlurRadius = 12.0f;  // 最大のボケ半径（ピクセル）
        float falloff = 30.0f;        // ピント面から外れたときのボケの立ち上がり方
        int textureSize[2] = {0, 0};
        float padding[2] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::DepthOfField; }

    /// 深度をビュー空間へ戻すために射影行列の逆行列が要る。
    /// カメラが変わるたびに設定すること（OutlineDepthParams と同じ扱い）。
    void SetProjectionInverse(const Matrix4x4 &mat) { pComputeData_->projectionInverse = mat; }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    std::string GetComputeShaderFile() const override { return "OffScreen/DepthOfField.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth};
    }
    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("ピント距離", &pComputeData_->focusDistance, 0.5f, 0.1f, 500.0f);
        ImGui::SetItemTooltip("カメラからこの距離にある物にピントが合います");
        ImGui::DragFloat("ピントの幅", &pComputeData_->focusRange, 0.5f, 0.0f, 200.0f);
        ImGui::SetItemTooltip("この幅のあいだは完全にピントが合ったままになります");
        ImGui::DragFloat("ボケの立ち上がり", &pComputeData_->falloff, 0.5f, 1.0f, 500.0f);
        ImGui::SetItemTooltip("小さいほどピント面を外れた途端に強くボケます");
        ImGui::DragFloat("最大ボケ半径(px)", &pComputeData_->maxBlurRadius, 0.5f, 1.0f, 40.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "focusDistance", pComputeData_->focusDistance);
        h->Save<float>(p + "focusRange", pComputeData_->focusRange);
        h->Save<float>(p + "maxBlurRadius", pComputeData_->maxBlurRadius);
        h->Save<float>(p + "falloff", pComputeData_->falloff);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pComputeData_->focusDistance = h->Load<float>(p + "focusDistance", 20.0f);
        pComputeData_->focusRange = h->Load<float>(p + "focusRange", 8.0f);
        pComputeData_->maxBlurRadius = h->Load<float>(p + "maxBlurRadius", 12.0f);
        pComputeData_->falloff = h->Load<float>(p + "falloff", 30.0f);
    }

    ComputeData *GetData() { return pComputeData_; }
    const ComputeData *GetData() const { return pComputeData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;
};

// ============================================================
//  Radial Blur
// ============================================================
class RadialBlurParams : public IPostEffectParams
{
  public:
    struct Data
    {
        Vector2 center = {0.5f, 0.5f};
        float blurWidth = 0.01f;
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Blur; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        // 中心座標はUV空間（0-1）、ブラー幅は視覚的に有効な範囲に制限
        ImGui::DragFloat2("中心", &pData_->center.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("ブラー幅", &pData_->blurWidth, 0.001f, 0.0f, 0.2f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "centerX", pData_->center.x);
        h->Save<float>(p + "centerY", pData_->center.y);
        h->Save<float>(p + "blurWidth", pData_->blurWidth);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->center.x = h->Load<float>(p + "centerX", 0.5f);
        pData_->center.y = h->Load<float>(p + "centerY", 0.5f);
        pData_->blurWidth = h->Load<float>(p + "blurWidth", 0.01f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Cinematic
// ============================================================
class CinematicParams : public IPostEffectParams
{
  public:
    struct Data
    {
        Vector2 resolution = {1280.0f, 720.0f};
        float contrast = 1.0f;
        float saturation = 1.0f;
        float brightness = 1.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Cinematic; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        // 解像度はグラデーションのアスペクト補正に使う。
        // 固定値のままだと解像度を変えたときにグラデーションが歪むので毎回入れ直す。
        pData_->resolution = {static_cast<float>(WinApp::GetVirtualWidth()),
                              static_cast<float>(WinApp::GetVirtualHeight())};
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("コントラスト", &pData_->contrast, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("彩度", &pData_->saturation, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("明度", &pData_->brightness, 0.01f, 0.0f, 3.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "contrast", pData_->contrast);
        h->Save<float>(p + "saturation", pData_->saturation);
        h->Save<float>(p + "brightness", pData_->brightness);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->contrast = h->Load<float>(p + "contrast", 1.0f);
        pData_->saturation = h->Load<float>(p + "saturation", 1.0f);
        pData_->brightness = h->Load<float>(p + "brightness", 1.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Dissolve
// ============================================================
class DissolveParams : public IPostEffectParams
{
  public:
    // HLSL(Dissolve.PS.hlsl の DissolveParams)のパッキングに合わせること。
    // float3 は 16 バイト境界をまたげないので gEdgeColor は 16 から始まり、
    // 続く gInvert は 28 に来る。ここへ余分なパディングを挟むと反転フラグが読まれなくなる。
    struct Data
    {
        float threshold = 0.5f;  // 0
        float edgeWidth = 0.05f; // 4
        float pad[2] = {};       // 8  （float3 を 16 へ送るための詰め物）
        Vector3 edgeColor = {1.0f, 0.5f, 0.0f}; // 16
        int invert = 0;                         // 28
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        // ノイズ(マスク)テクスチャを読み込んでおく（未指定なら既定のノイズ画像）
        TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
    }
    ShaderMode GetMode() const override { return ShaderMode::Dissolve; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *srv, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
        // t1: ノイズ(マスク)テクスチャをバインドする。
        // t0（画面）は描画側が差すので、ここで面倒を見るのは t1 だけ。
        if (srv)
        {
            auto *tm = TextureManager::GetInstance();
            tm->LoadTexture(maskTexturePath_); // 未ロードなら読む（ロード済みは即return）
            PostEffectParamsHelper::RootParam::BindSRV(
                pCommandList, GetMode(), 1,
                srv->GetGPUDescriptorHandle(tm->GetTextureIndexByFilePath(maskTexturePath_)));
        }
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("閾値", &pData_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("エッジ幅", &pData_->edgeWidth, 0.001f, 0.0f, 0.5f);
        ImGui::ColorEdit3("エッジカラー", &pData_->edgeColor.x);
        bool inv = pData_->invert != 0;
        if (ImGui::Checkbox("反転", &inv))
        {
            pData_->invert = inv ? 1 : 0;
        }
        PostEffectParamsHelper::DrawTextureSelector("ノイズ画像", maskTexturePath_);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "threshold", pData_->threshold);
        h->Save<float>(p + "edgeWidth", pData_->edgeWidth);
        h->Save<float>(p + "edgeR", pData_->edgeColor.x);
        h->Save<float>(p + "edgeG", pData_->edgeColor.y);
        h->Save<float>(p + "edgeB", pData_->edgeColor.z);
        h->Save<bool>(p + "invert", pData_->invert != 0);
        h->Save<std::string>(p + "maskTex", maskTexturePath_);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->threshold = h->Load<float>(p + "threshold", 0.5f);
        pData_->edgeWidth = h->Load<float>(p + "edgeWidth", 0.05f);
        pData_->edgeColor.x = h->Load<float>(p + "edgeR", 1.0f);
        pData_->edgeColor.y = h->Load<float>(p + "edgeG", 0.5f);
        pData_->edgeColor.z = h->Load<float>(p + "edgeB", 0.0f);
        pData_->invert = h->Load<bool>(p + "invert", false) ? 1 : 0;
        maskTexturePath_ = h->Load<std::string>(p + "maskTex", "debug/noise0.png");
        TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
    }

    /// <summary>マスクに使うテクスチャを差し替える（images ルート基準の相対パス）</summary>
    void SetMaskTexture(const std::string &relPath)
    {
        maskTexturePath_ = relPath;
        TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
    std::string maskTexturePath_ = "debug/noise0.png"; ///< ノイズ(マスク)画像（images ルート基準の相対パス）
};

// ============================================================
//  Random (Noise)
// ============================================================
class RandomParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float time = 0.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Random; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override {}
    void Save(DataHandler *, const std::string &) const override {}
    void Load(DataHandler *, const std::string &) override {}

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Focus Line (集中線)
// ============================================================
class FocusLineParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float time = 0.0f;
        float lines = 200.0f;
        float width = 0.01f;
        float speed = 1.0f;
        float intensity = 1.0f;
        float centerRadius = 0.1f;
        float maxDistance = 0.8f;
        float pad = 0.0f;
        Vector4 lineColor = {0.0f, 0.0f, 0.0f, 1.0f};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::FocusLine; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        // 線の本数・形状パラメータ
        ImGui::DragFloat("線の数", &pData_->lines, 1.0f, 10.0f, 500.0f);
        ImGui::DragFloat("線幅", &pData_->width, 0.001f, 0.001f, 0.1f);
        ImGui::DragFloat("速度", &pData_->speed, 0.1f, 0.0f, 10.0f);
        ImGui::DragFloat("強度", &pData_->intensity, 0.01f, 0.0f, 5.0f);
        // 集中線の発生エリア設定
        ImGui::DragFloat("中心半径", &pData_->centerRadius, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("最大距離", &pData_->maxDistance, 0.01f, 0.0f, 2.0f);
        ImGui::ColorEdit4("線の色", &pData_->lineColor.x);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "lines", pData_->lines);
        h->Save<float>(p + "width", pData_->width);
        h->Save<float>(p + "speed", pData_->speed);
        h->Save<float>(p + "intensity", pData_->intensity);
        h->Save<float>(p + "centerRadius", pData_->centerRadius);
        h->Save<float>(p + "maxDistance", pData_->maxDistance);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->lines = h->Load<float>(p + "lines", 200.0f);
        pData_->width = h->Load<float>(p + "width", 0.01f);
        pData_->speed = h->Load<float>(p + "speed", 1.0f);
        pData_->intensity = h->Load<float>(p + "intensity", 1.0f);
        pData_->centerRadius = h->Load<float>(p + "centerRadius", 0.1f);
        pData_->maxDistance = h->Load<float>(p + "maxDistance", 0.8f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Pixelate
// ============================================================
class PixelateParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float blockSize = 8.0f;
        float centerX = 0.5f;
        float centerY = 0.5f;
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Pixelate; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        // ブロックサイズはピクセル単位、中心座標はUV空間（0-1）
        ImGui::DragFloat("ブロックサイズ(px)", &pData_->blockSize, 0.5f, 1.0f, 64.0f, "%.0f");
        ImGui::DragFloat("中心X", &pData_->centerX, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("中心Y", &pData_->centerY, 0.01f, 0.0f, 1.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "blockSize", pData_->blockSize);
        h->Save<float>(p + "centerX", pData_->centerX);
        h->Save<float>(p + "centerY", pData_->centerY);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->blockSize = h->Load<float>(p + "blockSize", 8.0f);
        pData_->centerX = h->Load<float>(p + "centerX", 0.5f);
        pData_->centerY = h->Load<float>(p + "centerY", 0.5f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Retro (古いゲーム風: ピクセル化+減色+スキャンライン+色収差+CRTビネット)
// ============================================================
class RetroParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float pixelSize = 4.0f;
        float colorLevels = 8.0f;
        float scanlineIntensity = 0.4f;
        float scanlineCount = 400.0f;
        float vignetteStrength = 0.6f;
        float chromaticOffset = 0.003f;
        float time = 0.0f;
        float resolutionX = 1280.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Retro; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::SliderFloat("ピクセルサイズ", &pData_->pixelSize, 1.0f, 32.0f);
        ImGui::SliderFloat("減色レベル", &pData_->colorLevels, 2.0f, 32.0f);
        ImGui::SliderFloat("スキャンライン強度", &pData_->scanlineIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("スキャンライン本数", &pData_->scanlineCount, 50.0f, 800.0f);
        ImGui::SliderFloat("CRTビネット", &pData_->vignetteStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("色収差", &pData_->chromaticOffset, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("解像度X", &pData_->resolutionX, 320.0f, 3840.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "pixelSize", pData_->pixelSize);
        h->Save<float>(p + "colorLevels", pData_->colorLevels);
        h->Save<float>(p + "scanlineIntensity", pData_->scanlineIntensity);
        h->Save<float>(p + "scanlineCount", pData_->scanlineCount);
        h->Save<float>(p + "vignetteStrength", pData_->vignetteStrength);
        h->Save<float>(p + "chromaticOffset", pData_->chromaticOffset);
        h->Save<float>(p + "resolutionX", pData_->resolutionX);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->pixelSize = h->Load<float>(p + "pixelSize", 4.0f);
        pData_->colorLevels = h->Load<float>(p + "colorLevels", 8.0f);
        pData_->scanlineIntensity = h->Load<float>(p + "scanlineIntensity", 0.4f);
        pData_->scanlineCount = h->Load<float>(p + "scanlineCount", 400.0f);
        pData_->vignetteStrength = h->Load<float>(p + "vignetteStrength", 0.6f);
        pData_->chromaticOffset = h->Load<float>(p + "chromaticOffset", 0.003f);
        pData_->resolutionX = h->Load<float>(p + "resolutionX", 1280.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Bloom
// ============================================================
class BloomParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float threshold = 0.8f;
        float intensity = 1.0f;
        Vector2 texelSize = {1.0f / 1280.0f, 1.0f / 720.0f};
    };

    /// CS版に渡す定数バッファ。Bloom.CS.hlsl の BloomParams と同じ並びにすること
    struct ComputeData
    {
        float threshold = 0.8f;
        float intensity = 1.0f;
        int direction = 0; // 0=横（明るい部分の抽出も行う）, 1=縦（元画像へ加算）
        int padding = 0;
        int textureSize[2] = {0, 0};
        int padding2[2] = {};
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        // 定数バッファはパスごとに別で持つ（使い回すと両パスが同じ向きで実行される）
        for (int pass = 0; pass < kComputePassCount; ++pass)
        {
            PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResources_[pass], &pComputeData_[pass]);
            *pComputeData_[pass] = ComputeData{};
        }
    }
    ShaderMode GetMode() const override { return ShaderMode::Bloom; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }

    // ---- コンピュートシェーダー版 ----
    // 横方向（明るい部分の抽出込み）→ 縦方向（元画像へ加算）の2パス。25タップ → 10タップ。
    std::string GetComputeShaderFile() const override { return "OffScreen/Bloom.CS.hlsl"; }
    int GetComputePassCount() const override { return kComputePassCount; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        // t0 = そのパスの入力、t1 = 元画像（2パス目で加算するため中間結果に差し替えない）
        return {ComputeInput::SourceColor, ComputeInput::EffectInput};
    }

    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int passIndex,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX || passIndex < 0 || passIndex >= kComputePassCount)
        {
            return;
        }
        ComputeData *data = pComputeData_[passIndex];
        data->threshold = pData_->threshold;
        data->intensity = pData_->intensity;
        data->direction = passIndex;
        data->textureSize[0] = static_cast<int>(textureWidth);
        data->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex,
                                                       computeResources_[passIndex]->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        // 閾値は輝度の下限カット（0-1）、強度はブルーム量
        ImGui::DragFloat("閾値", &pData_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("強度", &pData_->intensity, 0.01f, 0.0f, 5.0f);
        ImGui::TextDisabled("コンピュートシェーダーで横→縦の2パス実行");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "threshold", pData_->threshold);
        h->Save<float>(p + "intensity", pData_->intensity);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->threshold = h->Load<float>(p + "threshold", 0.8f);
        pData_->intensity = h->Load<float>(p + "intensity", 1.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    static constexpr int kComputePassCount = 2;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;

    // パスごとに別の定数バッファを持つ（使い回すと両パスが同じ値を読んでしまう）
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResources_[kComputePassCount];
    ComputeData *pComputeData_[kComputePassCount] = {};
};

// ============================================================
//  Shockwave (領域消去・キー破壊などのインパクト演出用衝撃波)
// ============================================================
class ShockwaveParams : public IPostEffectParams
{
  public:
    struct Data
    {
        Vector2 center = {0.5f, 0.5f};
        float time = 0.0f;
        float duration = 0.55f;
        float amplitude = 3.0f;  // フラッシュ加算強度
        float frequency = 18.0f; // 放射光線の本数
        float waveSpeed = 1.6f;  // 光線の伸び速度（UV/秒）
        float active = 0.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
        // フレアテクスチャ読み込み（既ロードなら内部でスキップ）
        TextureManager::GetInstance()->LoadTexture(kFlareTexPath_);
    }
    ShaderMode GetMode() const override { return ShaderMode::Shockwave; }
    void UpdateTime(float dt) override
    {
        if (pData_ && pData_->active >= 0.5f)
        {
            pData_->time += dt;
            if (pData_->time > pData_->duration)
            {
                pData_->active = 0.0f;
                pData_->time = 0.0f;
            }
        }
    }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        // t0=画面（描画側が設定）, t1=フレア, b0=パラメータ。
        // ルートパラメータ番号はリフレクション由来なので必ず引いてから差す。
        // GetSrvHandleGPU は内部 prepend しないのでフルパス(＝マップキー)で渡す
        const std::string fullPath = AssetPath::Image(kFlareTexPath_);
        PostEffectParamsHelper::RootParam::BindSRV(
            pCommandList, GetMode(), 1,
            TextureManager::GetInstance()->GetSrvHandleGPU(fullPath));
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::SliderFloat2("中心(UV)", &pData_->center.x, 0.0f, 1.0f);
        ImGui::SliderFloat("持続時間", &pData_->duration, 0.1f, 2.0f);
        ImGui::SliderFloat("フラッシュ強度", &pData_->amplitude, 0.0f, 8.0f, "%.2f");
        ImGui::SliderFloat("光線本数", &pData_->frequency, 1.0f, 32.0f, "%.0f");
        ImGui::SliderFloat("光線伸び速度", &pData_->waveSpeed, 0.1f, 3.0f);
        ImGui::Text("経過: %.2f / %.2f", pData_->time, pData_->duration);
        if (ImGui::Button("発動テスト"))
        {
            pData_->time = 0.0f;
            pData_->active = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("停止"))
        {
            pData_->active = 0.0f;
            pData_->time = 0.0f;
        }
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "duration", pData_->duration);
        h->Save<float>(p + "amplitude", pData_->amplitude);
        h->Save<float>(p + "frequency", pData_->frequency);
        h->Save<float>(p + "waveSpeed", pData_->waveSpeed);
        h->Save<Vector2>(p + "center", pData_->center);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->duration = h->Load<float>(p + "duration", 0.55f);
        pData_->amplitude = h->Load<float>(p + "amplitude", 3.0f);
        pData_->frequency = h->Load<float>(p + "frequency", 18.0f);
        pData_->waveSpeed = h->Load<float>(p + "waveSpeed", 1.6f);
        pData_->center = h->Load<Vector2>(p + "center", Vector2(0.5f, 0.5f));
    }

    void Trigger(const Vector2 &uvCenter)
    {
        if (!pData_)
            return;
        pData_->center = uvCenter;
        pData_->time = 0.0f;
        pData_->active = 1.0f;
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
    static constexpr const char *kFlareTexPath_ = "Particle/tetrio/flare.png";
};

// ============================================================
//  Fxaa（アンチエイリアス）
// ============================================================
// 描き終えた絵から輪郭を見つけて、その向きに沿って馴染ませる。
// MSAA と違い描画コストが増えないかわりに、細い線は少し甘くなる。
class FxaaParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float edgeThreshold = 0.125f;    // これ以上の明るさの差を輪郭とみなす
        float edgeThresholdMin = 0.035f; // 暗い場所での下限（ノイズを拾わないため）
        float subpixelBlend = 0.75f;     // 細かい部分をどれだけ馴染ませるか
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Fxaa; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("輪郭とみなす差", &pData_->edgeThreshold, 0.005f, 0.02f, 0.5f);
        ImGui::DragFloat("暗部での下限", &pData_->edgeThresholdMin, 0.005f, 0.0f, 0.2f);
        ImGui::DragFloat("馴染ませ具合", &pData_->subpixelBlend, 0.01f, 0.0f, 1.0f);
        ImGui::TextDisabled("チェーンの最後のほうに置くと効果的です");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "edgeThreshold", pData_->edgeThreshold);
        h->Save<float>(p + "edgeThresholdMin", pData_->edgeThresholdMin);
        h->Save<float>(p + "subpixelBlend", pData_->subpixelBlend);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->edgeThreshold = h->Load<float>(p + "edgeThreshold", 0.125f);
        pData_->edgeThresholdMin = h->Load<float>(p + "edgeThresholdMin", 0.035f);
        pData_->subpixelBlend = h->Load<float>(p + "subpixelBlend", 0.75f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  ColorGrading（カラーグレーディング）
// ============================================================
// 暗部・中間・明部を別々に動かして、画面全体の空気感を作る。
class ColorGradingParams : public IPostEffectParams
{
  public:
    struct Data
    {
        Vector4 lift = {0.0f, 0.0f, 0.0f, 0.0f};  // rgb=暗部へ足す色 / a=全体の持ち上げ
        Vector4 gamma = {1.0f, 1.0f, 1.0f, 1.0f}; // rgb=中間の色 / a=全体のガンマ
        Vector4 gain = {1.0f, 1.0f, 1.0f, 1.0f};  // rgb=明部へ掛ける色 / a=全体のゲイン

        float temperature = 0.0f; // -1=寒色, +1=暖色
        float tint = 0.0f;        // -1=緑, +1=マゼンタ
        float saturation = 1.0f;
        float contrast = 1.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::ColorGrading; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("色温度 (寒色 - 暖色)", &pData_->temperature, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("色合い (緑 - マゼンタ)", &pData_->tint, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("彩度", &pData_->saturation, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("コントラスト", &pData_->contrast, 0.01f, 0.0f, 2.0f);
        ImGui::Separator();
        ImGui::TextDisabled("暗い所 / 中間 / 明るい所を別々に動かす");
        ImGui::ColorEdit3("暗部の色", &pData_->lift.x, ImGuiColorEditFlags_Float);
        ImGui::DragFloat("暗部の持ち上げ", &pData_->lift.w, 0.005f, -0.5f, 0.5f);
        ImGui::ColorEdit3("中間の色", &pData_->gamma.x, ImGuiColorEditFlags_Float);
        ImGui::DragFloat("中間のガンマ", &pData_->gamma.w, 0.01f, 0.1f, 3.0f);
        ImGui::ColorEdit3("明部の色", &pData_->gain.x, ImGuiColorEditFlags_Float);
        ImGui::DragFloat("明部のゲイン", &pData_->gain.w, 0.01f, 0.0f, 3.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<Vector4>(p + "lift", pData_->lift);
        h->Save<Vector4>(p + "gamma", pData_->gamma);
        h->Save<Vector4>(p + "gain", pData_->gain);
        h->Save<float>(p + "temperature", pData_->temperature);
        h->Save<float>(p + "tint", pData_->tint);
        h->Save<float>(p + "saturation", pData_->saturation);
        h->Save<float>(p + "contrast", pData_->contrast);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->lift = h->Load<Vector4>(p + "lift", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
        pData_->gamma = h->Load<Vector4>(p + "gamma", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        pData_->gain = h->Load<Vector4>(p + "gain", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        pData_->temperature = h->Load<float>(p + "temperature", 0.0f);
        pData_->tint = h->Load<float>(p + "tint", 0.0f);
        pData_->saturation = h->Load<float>(p + "saturation", 1.0f);
        pData_->contrast = h->Load<float>(p + "contrast", 1.0f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  ChromaticAberration（色収差）
// ============================================================
// 画面の端ほど赤と青がずれる、レンズ越しの生々しさを足す。
class ChromaticAberrationParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float strength = 0.006f; // ずれの大きさ
        float falloff = 2.0f;    // 端だけに出す度合い
        int sampleCount = 5;     // 分割数
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::ChromaticAberration; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("ずれの大きさ", &pData_->strength, 0.0005f, 0.0f, 0.05f, "%.4f");
        ImGui::DragFloat("端だけに出す", &pData_->falloff, 0.05f, 0.1f, 6.0f);
        ImGui::SliderInt("なめらかさ", &pData_->sampleCount, 1, 8);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "strength", pData_->strength);
        h->Save<float>(p + "falloff", pData_->falloff);
        h->Save<int>(p + "sampleCount", pData_->sampleCount);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->strength = h->Load<float>(p + "strength", 0.006f);
        pData_->falloff = h->Load<float>(p + "falloff", 2.0f);
        pData_->sampleCount = h->Load<int>(p + "sampleCount", 5);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  FilmGrain（フィルムグレイン）
// ============================================================
// 細かいざらつきを乗せて、CG特有のつるっとした均一さを崩す。
class FilmGrainParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float intensity = 0.06f;     // ざらつきの強さ
        float grainSize = 1.5f;      // 粒の大きさ
        float time = 0.0f;           // 時間（粒を動かす）
        float luminanceKeep = 0.6f;  // 明るい所で弱める度合い
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::FilmGrain; }
    void UpdateTime(float deltaTime) override { pData_->time += deltaTime; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("ざらつきの強さ", &pData_->intensity, 0.002f, 0.0f, 0.4f, "%.3f");
        ImGui::DragFloat("粒の大きさ", &pData_->grainSize, 0.1f, 0.5f, 8.0f);
        ImGui::DragFloat("明部で弱める", &pData_->luminanceKeep, 0.01f, 0.0f, 1.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "intensity", pData_->intensity);
        h->Save<float>(p + "grainSize", pData_->grainSize);
        h->Save<float>(p + "luminanceKeep", pData_->luminanceKeep);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->intensity = h->Load<float>(p + "intensity", 0.06f);
        pData_->grainSize = h->Load<float>(p + "grainSize", 1.5f);
        pData_->luminanceKeep = h->Load<float>(p + "luminanceKeep", 0.6f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  LensDistortion（レンズ歪み）
// ============================================================
// 広角レンズのように画面を曲げる。周辺減光も一緒に掛けられる。
class LensDistortionParams : public IPostEffectParams
{
  public:
    struct Data
    {
        float distortion = 0.1f; // +で樽型 / -で糸巻き型
        float cubic = 0.0f;      // 端だけを強く曲げる
        float scale = 1.05f;     // 歪みでできる外周の隙間を詰める
        float vignette = 0.2f;   // 周辺減光
    };

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::LensDistortion; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::DragFloat("歪み (+樽型 / -糸巻き)", &pData_->distortion, 0.005f, -0.5f, 0.5f);
        ImGui::DragFloat("端の強さ", &pData_->cubic, 0.005f, -0.5f, 0.5f);
        ImGui::DragFloat("拡大で隙間を詰める", &pData_->scale, 0.005f, 0.8f, 1.5f);
        ImGui::DragFloat("周辺減光", &pData_->vignette, 0.01f, 0.0f, 1.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "distortion", pData_->distortion);
        h->Save<float>(p + "cubic", pData_->cubic);
        h->Save<float>(p + "scale", pData_->scale);
        h->Save<float>(p + "vignette", pData_->vignette);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->distortion = h->Load<float>(p + "distortion", 0.1f);
        pData_->cubic = h->Load<float>(p + "cubic", 0.0f);
        pData_->scale = h->Load<float>(p + "scale", 1.05f);
        pData_->vignette = h->Load<float>(p + "vignette", 0.2f);
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  Impact（打撃インパクト）
// ============================================================
// 当たった瞬間の画面効果をまとめたもの。衝撃波リングの歪み（最大4つ）・集中ブラー・
// 色収差・白フラッシュ・明るさで2色に塗り分けるインパクトフレームを1パスで掛ける。
// 値は演出側が毎フレーム書き換える前提なので、保存するのは塗り分けの見た目だけ。
class ImpactParams : public IPostEffectParams
{
  public:
    static constexpr int kMaxWaves = 4; // Impact.PS.hlsl の kMaxWaves と一致させること

    struct Data
    {
        Vector4 waves[kMaxWaves] = {};     // xy=中心UV / z=半径（画面の高さ=1） / w=歪みの強さ
        Vector4 waveShape[kMaxWaves] = {}; // x=輪の太さ / y=輪の上の色収差 / z=輪の光
        Vector2 focus = {0.5f, 0.5f};      // 集中ブラー・色収差の中心UV
        float radialBlur = 0.0f;           // 集中ブラーの強さ
        float chroma = 0.0f;               // 色収差の強さ
        float flash = 0.0f;                // 白フラッシュ量
        float impactFrame = 0.0f;          // インパクトフレームの混ぜ具合 0〜1
        float impactThreshold = 0.35f;     // 塗り分けの境目（トーンマップ後の明るさ）
        float aspect = 16.0f / 9.0f;       // 画面の横/縦
        Vector4 impactDark = {0.0f, 0.0f, 0.0f, 1.0f};    // 暗い側の色
        Vector4 impactLight = {12.0f, 12.0f, 12.0f, 1.0f}; // 明るい側の色（HDRなので大きめ）
        int waveCount = 0;                 // 使っている輪の数
        float pad[3] = {};
    };
    static_assert(sizeof(Data) == 208, "Impact.PS.hlsl の ImpactParameters と並びを一致させること");

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Impact; }
    void Apply(ID3D12GraphicsCommandList *pCommandList, SrvManager *, DirectXCommon *) override
    {
        PostEffectParamsHelper::RootParam::BindCBV(pCommandList, GetMode(), resource_);
    }
    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::TextDisabled("歪み・ブラー・フラッシュは演出側が毎フレーム書き換えます");
        ImGui::DragFloat2("注目点(UV)", &pData_->focus.x, 0.005f, 0.0f, 1.0f);
        ImGui::DragFloat("集中ブラー", &pData_->radialBlur, 0.005f, 0.0f, 1.0f);
        ImGui::DragFloat("色収差", &pData_->chroma, 0.0005f, 0.0f, 0.1f, "%.4f");
        ImGui::DragFloat("白フラッシュ", &pData_->flash, 0.01f, 0.0f, 2.0f);
        ImGui::SliderFloat("インパクトフレーム", &pData_->impactFrame, 0.0f, 1.0f);
        ImGui::SliderFloat("塗り分けの境目", &pData_->impactThreshold, 0.0f, 1.0f);
        ImGui::ColorEdit3("暗い側の色", &pData_->impactDark.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        ImGui::ColorEdit3("明るい側の色", &pData_->impactLight.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "impactThreshold", pData_->impactThreshold);
        h->Save<Vector4>(p + "impactDark", pData_->impactDark);
        h->Save<Vector4>(p + "impactLight", pData_->impactLight);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->impactThreshold = h->Load<float>(p + "impactThreshold", 0.35f);
        pData_->impactDark = h->Load<Vector4>(p + "impactDark", Vector4{0.0f, 0.0f, 0.0f, 1.0f});
        pData_->impactLight = h->Load<Vector4>(p + "impactLight", Vector4{12.0f, 12.0f, 12.0f, 1.0f});
    }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
};

// ============================================================
//  HeightFog（距離＋高さフォグ）
// ============================================================
// 深度からワールド座標を復元し、高さ方向に指数で薄くなる霧を視線に沿って積分する。
// 遠景が霞んで奥行きが出るほか、低地に霧をためることもできる。
// コンピュートシェーダー専用（PS版は素通しが割り当ててある）。
class HeightFogParams : public IPostEffectParams
{
  public:
    /// HeightFog.CS.hlsl の FogParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド座標
        Vector3 cameraPosition = {};       // カメラのワールド座標
        float density = 0.02f;             // 基準高さでの霧の濃さ
        Vector3 fogColor = {0.62f, 0.70f, 0.80f}; // 霧の色
        float heightFalloff = 0.05f;       // 高さ方向の減衰。0で高さ無視の距離フォグ
        Vector3 sunDirection = {0.0f, -1.0f, 0.0f}; // 平行光源が進む向き
        float sunIntensity = 0.6f;         // 太陽側の色へ寄せる強さ
        Vector3 sunColor = {1.0f, 0.88f, 0.70f}; // 太陽を覗き込んだときの霧の色
        float sunExponent = 8.0f;          // 散乱の鋭さ
        float startDistance = 10.0f;       // ここまではフォグを掛けない
        float maxDistance = 400.0f;        // フォグを積分する最大距離
        float baseHeight = 0.0f;           // 霧が最も濃くなる高さ
        float maxOpacity = 0.9f;           // フォグの上限
        int textureSize[2] = {0, 0};       // 処理対象の解像度
        float skyStrength = 0.6f;          // 空へ掛ける割合
        float padding = 0.0f;
    };
    // HLSL側は16バイト境界をまたがないように詰められる。
    // Vector3+float の組で並べてあるので、C++側とサイズが一致していれば並びも一致する
    static_assert(sizeof(ComputeData) == 160, "HeightFog.CS.hlsl の FogParams と並びが合っていません");

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::HeightFog; }

    /// <summary>
    /// 深度からワールド座標を復元するのに要るカメラ情報を渡す。
    /// 毎フレーム、カメラが確定したあとに設定すること
    /// </summary>
    /// <param name="inverseViewProjection">ビュー射影行列の逆行列</param>
    /// <param name="cameraPosition">カメラのワールド座標</param>
    void SetCamera(const Matrix4x4 &inverseViewProjection, const Vector3 &cameraPosition)
    {
        pComputeData_->inverseViewProjection = inverseViewProjection;
        pComputeData_->cameraPosition = cameraPosition;
    }

    /// <summary>
    /// 太陽への散乱に使う平行光源の向きを渡す（光が進む向き）
    /// </summary>
    /// <param name="direction">平行光源の向き</param>
    void SetSunDirection(const Vector3 &direction)
    {
        if (!followSceneLight_)
        {
            return;
        }
        pComputeData_->sunDirection = direction;
    }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    std::string GetComputeShaderFile() const override { return "OffScreen/HeightFog.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth};
    }
    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        ImGui::SeparatorText("霧");
        ImGui::ColorEdit3("霧の色", &pComputeData_->fogColor.x);
        ImGui::DragFloat("濃さ", &pComputeData_->density, 0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::SetItemTooltip("基準の高さでの濃さ。大きくするほど近くから霞みます");
        ImGui::DragFloat("開始距離", &pComputeData_->startDistance, 0.5f, 0.0f, 500.0f);
        ImGui::SetItemTooltip("カメラからこの距離までは霧を掛けません。\n手前のキャラが白く浮くのを防げます");
        ImGui::DragFloat("最大距離", &pComputeData_->maxDistance, 1.0f, 1.0f, 5000.0f);
        ImGui::DragFloat("濃さの上限", &pComputeData_->maxOpacity, 0.01f, 0.0f, 1.0f);
        ImGui::SetItemTooltip("1未満にすると、どれだけ遠くても景色が完全には消えません");

        ImGui::SeparatorText("高さ");
        ImGui::DragFloat("高さ減衰", &pComputeData_->heightFalloff, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("大きいほど低い所だけに霧がたまります。\n0にすると高さを無視した素直な距離フォグになります");
        ImGui::DragFloat("基準の高さ", &pComputeData_->baseHeight, 0.1f, -100.0f, 100.0f);
        ImGui::SetItemTooltip("この高さで霧が最も濃くなります。地面の高さに合わせます");
        ImGui::DragFloat("空へ掛ける割合", &pComputeData_->skyStrength, 0.01f, 0.0f, 1.0f);
        ImGui::SetItemTooltip("0にするとスカイボックスがそのまま見えます。\n上げると地平線まで霞んで空と地面がつながります");

        ImGui::SeparatorText("太陽への散乱");
        ImGui::DragFloat("散乱の強さ", &pComputeData_->sunIntensity, 0.01f, 0.0f, 2.0f);
        ImGui::SetItemTooltip("太陽の方を向いたときだけ霧を明るい色へ寄せます。0で散乱なし");
        ImGui::ColorEdit3("散乱の色", &pComputeData_->sunColor.x);
        ImGui::DragFloat("散乱の鋭さ", &pComputeData_->sunExponent, 0.1f, 1.0f, 64.0f);
        ImGui::SetItemTooltip("大きいほど太陽の周りの狭い範囲だけが光ります");
        ImGui::Checkbox("シーンの平行光源に追従", &followSceneLight_);
        ImGui::SetItemTooltip("チェックを外すと、下の向きを手で指定できます");
        ImGui::BeginDisabled(followSceneLight_);
        ImGui::DragFloat3("太陽の向き", &pComputeData_->sunDirection.x, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<Vector3>(p + "fogColor", pComputeData_->fogColor);
        h->Save<float>(p + "density", pComputeData_->density);
        h->Save<float>(p + "heightFalloff", pComputeData_->heightFalloff);
        h->Save<float>(p + "baseHeight", pComputeData_->baseHeight);
        h->Save<float>(p + "startDistance", pComputeData_->startDistance);
        h->Save<float>(p + "maxDistance", pComputeData_->maxDistance);
        h->Save<float>(p + "maxOpacity", pComputeData_->maxOpacity);
        h->Save<float>(p + "skyStrength", pComputeData_->skyStrength);
        h->Save<Vector3>(p + "sunColor", pComputeData_->sunColor);
        h->Save<Vector3>(p + "sunDirection", pComputeData_->sunDirection);
        h->Save<float>(p + "sunIntensity", pComputeData_->sunIntensity);
        h->Save<float>(p + "sunExponent", pComputeData_->sunExponent);
        h->Save<bool>(p + "followSceneLight", followSceneLight_);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        const ComputeData defaults{};
        pComputeData_->fogColor = h->Load<Vector3>(p + "fogColor", defaults.fogColor);
        pComputeData_->density = h->Load<float>(p + "density", defaults.density);
        pComputeData_->heightFalloff = h->Load<float>(p + "heightFalloff", defaults.heightFalloff);
        pComputeData_->baseHeight = h->Load<float>(p + "baseHeight", defaults.baseHeight);
        pComputeData_->startDistance = h->Load<float>(p + "startDistance", defaults.startDistance);
        pComputeData_->maxDistance = h->Load<float>(p + "maxDistance", defaults.maxDistance);
        pComputeData_->maxOpacity = h->Load<float>(p + "maxOpacity", defaults.maxOpacity);
        pComputeData_->skyStrength = h->Load<float>(p + "skyStrength", defaults.skyStrength);
        pComputeData_->sunColor = h->Load<Vector3>(p + "sunColor", defaults.sunColor);
        pComputeData_->sunDirection = h->Load<Vector3>(p + "sunDirection", defaults.sunDirection);
        pComputeData_->sunIntensity = h->Load<float>(p + "sunIntensity", defaults.sunIntensity);
        pComputeData_->sunExponent = h->Load<float>(p + "sunExponent", defaults.sunExponent);
        followSceneLight_ = h->Load<bool>(p + "followSceneLight", true);
    }

    ComputeData *GetData() { return pComputeData_; }
    const ComputeData *GetData() const { return pComputeData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;

    // 太陽の向きをシーンの平行光源から取るか、手で指定するか
    bool followSceneLight_ = true;
};

// ============================================================
//  LightShaft（光の筋 / ボリュメトリックライト）
// ============================================================
// カメラからそのピクセルまで視線を刻んで進み、各点が平行光源から見えているかを
// シャドウマップで調べて、見えている分だけ空気の散乱として光を足す。
// 放射ブラー式と違い、太陽が画面外にあっても物陰から光芒が伸びる。
// コンピュートシェーダー専用（PS版は素通しが割り当ててある）。
class LightShaftParams : public IPostEffectParams
{
  public:
    /// LightShafts.CS.hlsl の LightShaftParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド座標
        Matrix4x4 lightViewProjection{};   // ワールド → ライトのクリップ空間
        Vector3 cameraPosition = {};       // カメラのワールド座標
        float intensity = 1.0f;            // 光の筋の強さ
        Vector3 lightColor = {1.0f, 0.95f, 0.82f}; // 光の筋の色
        // 濃さ×距離×位相関数がそのまま明るさになる。HDRなので上げすぎるとすぐ白飛びする
        float density = 0.004f;            // 空気中の散乱の濃さ
        Vector3 lightDirection = {0.0f, -1.0f, 0.0f}; // 平行光源が進む向き
        float maxDistance = 150.0f;        // レイマーチする最大距離
        float anisotropy = 0.75f;          // 散乱の偏り
        float shadowBias = 0.002f;         // シャドウ比較のバイアス
        float jitterStrength = 1.0f;       // 開始位置をずらす量
        float padding = 0.0f;
        int sampleCount = 48;              // レイマーチの分割数
        int shadowEnabled = 1;             // シャドウマップが有効か
        int textureSize[2] = {0, 0};       // 処理対象の解像度
    };
    static_assert(sizeof(ComputeData) == 208, "LightShafts.CS.hlsl の LightShaftParams と並びが合っていません");

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::LightShaft; }

    /// <summary>
    /// 深度からワールド座標を復元するのに要るカメラ情報を渡す
    /// </summary>
    /// <param name="inverseViewProjection">ビュー射影行列の逆行列</param>
    /// <param name="cameraPosition">カメラのワールド座標</param>
    void SetCamera(const Matrix4x4 &inverseViewProjection, const Vector3 &cameraPosition)
    {
        pComputeData_->inverseViewProjection = inverseViewProjection;
        pComputeData_->cameraPosition = cameraPosition;
    }

    /// <summary>
    /// 平行光源の向きを渡す（光が進む向き）
    /// </summary>
    /// <param name="direction">平行光源の向き</param>
    void SetLightDirection(const Vector3 &direction) { pComputeData_->lightDirection = direction; }

    /// <summary>
    /// シャドウマップの行列と有効状態を渡す
    /// </summary>
    /// <param name="lightViewProjection">ライトのビュー射影行列</param>
    /// <param name="enabled">シャドウマップが有効か</param>
    void SetShadow(const Matrix4x4 &lightViewProjection, bool enabled)
    {
        pComputeData_->lightViewProjection = lightViewProjection;
        pComputeData_->shadowEnabled = enabled ? 1 : 0;
    }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    std::string GetComputeShaderFile() const override { return "OffScreen/LightShafts.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth, ComputeInput::ShadowMap};
    }
    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        if (pComputeData_->shadowEnabled == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextWrapped("シャドウマップが無効です。遮る物が無い＝全面が一様に光るだけになります");
            ImGui::PopStyleColor();
        }
        // 筋が出ないという相談の原因はたいていこれ。
        // シャドウマップの外側は「光が届いている」扱いなので、
        // 投影範囲が見えている景色を覆っていないと遮蔽がまったく起きない
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("筋が出ないときは、シャドウマップの投影範囲(ortho)と中心が\n"
                           "見えている景色を覆っているかを先に確認してください");
        ImGui::PopStyleColor();

        ImGui::DragFloat("強さ", &pComputeData_->intensity, 0.01f, 0.0f, 10.0f);
        ImGui::ColorEdit3("光の色", &pComputeData_->lightColor.x);
        ImGui::DragFloat("空気の濃さ", &pComputeData_->density, 0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::SetItemTooltip("大きいほど空気が白く光ります。上げすぎると画面全体が霞みます");
        ImGui::DragFloat("散乱の偏り", &pComputeData_->anisotropy, 0.01f, -0.95f, 0.95f);
        ImGui::SetItemTooltip("1に近いほど太陽の方を向いたときだけ強く光ります。0で全方位に均一");

        ImGui::SeparatorText("レイマーチ");
        ImGui::DragInt("分割数", &pComputeData_->sampleCount, 1, 1, 128);
        ImGui::SetItemTooltip("多いほど筋がなめらかになりますが、そのぶん重くなります");
        ImGui::DragFloat("最大距離", &pComputeData_->maxDistance, 1.0f, 1.0f, 1000.0f);
        ImGui::SetItemTooltip("この距離までを空気として進みます。シャドウマップの範囲に合わせます");
        ImGui::DragFloat("ジッタ", &pComputeData_->jitterStrength, 0.01f, 0.0f, 1.0f);
        ImGui::SetItemTooltip("開始位置を画素ごとにずらして縞模様を散らします。\n0にすると分割の等間隔な縞が見えます");
        ImGui::DragFloat("影のバイアス", &pComputeData_->shadowBias, 0.0005f, 0.0f, 0.05f, "%.4f");
        ImGui::SetItemTooltip("筋が縞状にちらつくときに上げます");
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "intensity", pComputeData_->intensity);
        h->Save<Vector3>(p + "lightColor", pComputeData_->lightColor);
        h->Save<float>(p + "density", pComputeData_->density);
        h->Save<float>(p + "anisotropy", pComputeData_->anisotropy);
        h->Save<float>(p + "maxDistance", pComputeData_->maxDistance);
        h->Save<float>(p + "jitterStrength", pComputeData_->jitterStrength);
        h->Save<float>(p + "shadowBias", pComputeData_->shadowBias);
        h->Save<int>(p + "sampleCount", pComputeData_->sampleCount);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        const ComputeData defaults{};
        pComputeData_->intensity = h->Load<float>(p + "intensity", defaults.intensity);
        pComputeData_->lightColor = h->Load<Vector3>(p + "lightColor", defaults.lightColor);
        pComputeData_->density = h->Load<float>(p + "density", defaults.density);
        pComputeData_->anisotropy = h->Load<float>(p + "anisotropy", defaults.anisotropy);
        pComputeData_->maxDistance = h->Load<float>(p + "maxDistance", defaults.maxDistance);
        pComputeData_->jitterStrength = h->Load<float>(p + "jitterStrength", defaults.jitterStrength);
        pComputeData_->shadowBias = h->Load<float>(p + "shadowBias", defaults.shadowBias);
        pComputeData_->sampleCount = h->Load<int>(p + "sampleCount", defaults.sampleCount);
    }

    ComputeData *GetData() { return pComputeData_; }
    const ComputeData *GetData() const { return pComputeData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;
};

// ============================================================
//  SSR（画面内反射）
// ============================================================
// 各画素の法線で視線を反射させ、その向きへ画面の中を進んで交点を探す。
// 床や水面に周囲が映り込む。画面に写っていないものは映せないのが原理上の限界。
// G-Buffer の法線を使うので、**ディファードが有効なときだけ効く**。
class SsrParams : public IPostEffectParams
{
  public:
    /// Ssr.CS.hlsl の SsrParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 viewProjection{};        // ワールド → クリップ
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド
        Vector3 cameraPosition = {};       // カメラのワールド座標
        float intensity = 1.0f;            // 反射の強さ
        float maxDistance = 30.0f;         // レイを飛ばす最大距離
        float thickness = 1.0f;            // 交点とみなす深度差の許容量
        float edgeFade = 0.15f;            // 画面端で弱める幅
        float fresnelPower = 5.0f;         // 斜めから見たときに強くなる度合い
        float reflectance = 0.04f;         // 正面から見たときの反射率（F0）
        float stepJitter = 1.0f;           // レイ開始位置のばらつき
        int stepCount = 32;                // レイマーチの分割数
        int hasNormals = 0;                // G-Bufferの法線が使えるか
        int textureSize[2] = {0, 0};       // 処理対象の解像度
        float padding[2] = {};
    };
    static_assert(sizeof(ComputeData) == 192, "Ssr.CS.hlsl の SsrParams と並びが合っていません");

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Ssr; }

    /// <summary>
    /// カメラの行列を渡す。ワールド↔画面を往復するので、順行列と逆行列の両方が要る
    /// </summary>
    /// <param name="viewProjection">ビュー射影行列</param>
    /// <param name="inverseViewProjection">その逆行列</param>
    /// <param name="cameraPosition">カメラのワールド座標</param>
    void SetCamera(const Matrix4x4 &viewProjection, const Matrix4x4 &inverseViewProjection,
                   const Vector3 &cameraPosition)
    {
        pComputeData_->viewProjection = viewProjection;
        pComputeData_->inverseViewProjection = inverseViewProjection;
        pComputeData_->cameraPosition = cameraPosition;
    }

    /// <summary>
    /// G-Buffer の法線が使えるか（＝ディファードが有効か）を渡す
    /// </summary>
    /// <param name="available">使えるなら true</param>
    void SetNormalsAvailable(bool available) { pComputeData_->hasNormals = available ? 1 : 0; }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    std::string GetComputeShaderFile() const override { return "OffScreen/Ssr.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth, ComputeInput::GBufferNormal};
    }
    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        if (pComputeData_->hasNormals == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextWrapped("ディファードが無効です。G-Bufferの法線が無いのでSSRは効きません");
            ImGui::PopStyleColor();
        }

        ImGui::DragFloat("反射の強さ", &pComputeData_->intensity, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("正面の反射率", &pComputeData_->reflectance, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("真正面から見たときにどれだけ映り込むか。\n"
                              "水や磨いた床なら小さめ(0.02〜0.05)、金属的にしたいなら上げます");
        ImGui::DragFloat("斜めの強まり方", &pComputeData_->fresnelPower, 0.1f, 0.1f, 10.0f);
        ImGui::SetItemTooltip("大きいほど「浅い角度のときだけ」映り込みます（水面らしくなります）");

        ImGui::SeparatorText("レイマーチ");
        ImGui::DragInt("分割数", &pComputeData_->stepCount, 1, 1, 128);
        ImGui::SetItemTooltip("多いほど遠くまで正確に映りますが重くなります");
        ImGui::DragFloat("最大距離", &pComputeData_->maxDistance, 0.5f, 1.0f, 500.0f);
        ImGui::DragFloat("厚みの許容", &pComputeData_->thickness, 0.05f, 0.01f, 20.0f);
        ImGui::SetItemTooltip("交点とみなす深度差です。小さすぎると映り込みが途切れ、\n"
                              "大きすぎると手前の物の裏側を誤って映します");
        ImGui::DragFloat("ジッタ", &pComputeData_->stepJitter, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("画面端のぼかし幅", &pComputeData_->edgeFade, 0.01f, 0.0f, 0.5f);
        ImGui::SetItemTooltip("画面の外に出たものは映せないので、端に近いほど反射を弱めて誤魔化します");

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("画面に写っていないものは映りません（原理上の限界です）");
        // 「床に映らない」という相談の原因はたいていこれ。
        // ライティングOFFの物は G-Buffer に載らない＝法線が無いので、SSRの対象外になる
        ImGui::TextWrapped("映り込むのは「ライティング有効」のマテリアルだけです。\n"
                           "床に映らないときは、床のマテリアルのライティングを確認してください");
        ImGui::PopStyleColor();
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "intensity", pComputeData_->intensity);
        h->Save<float>(p + "maxDistance", pComputeData_->maxDistance);
        h->Save<float>(p + "thickness", pComputeData_->thickness);
        h->Save<float>(p + "edgeFade", pComputeData_->edgeFade);
        h->Save<float>(p + "fresnelPower", pComputeData_->fresnelPower);
        h->Save<float>(p + "reflectance", pComputeData_->reflectance);
        h->Save<float>(p + "stepJitter", pComputeData_->stepJitter);
        h->Save<int>(p + "stepCount", pComputeData_->stepCount);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        const ComputeData defaults{};
        pComputeData_->intensity = h->Load<float>(p + "intensity", defaults.intensity);
        pComputeData_->maxDistance = h->Load<float>(p + "maxDistance", defaults.maxDistance);
        pComputeData_->thickness = h->Load<float>(p + "thickness", defaults.thickness);
        pComputeData_->edgeFade = h->Load<float>(p + "edgeFade", defaults.edgeFade);
        pComputeData_->fresnelPower = h->Load<float>(p + "fresnelPower", defaults.fresnelPower);
        pComputeData_->reflectance = h->Load<float>(p + "reflectance", defaults.reflectance);
        pComputeData_->stepJitter = h->Load<float>(p + "stepJitter", defaults.stepJitter);
        pComputeData_->stepCount = h->Load<int>(p + "stepCount", defaults.stepCount);
    }

    ComputeData *GetData() { return pComputeData_; }
    const ComputeData *GetData() const { return pComputeData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;
};

/// <summary>
/// RT反射（インラインRT）。
///
/// SSR と同じ「床や水面に周囲を映す」効果だが、交点をレイ1本で正確に求める。
/// SSR の3つの制約（画面外で打ち切られる／分割が粗いと物をまたぐ／厚みの当てずっぽう）が
/// そのまま無くなり、**画面の外を向いた反射でも環境マップの色が正しく出る**。
///
/// 当たった点の色は「画面に写っていればその色」。写っていなければ何も足さない
/// （そこに物があって空を遮っていることだけは正しく出る）。
/// 写っていない面の色まで出すには、当たった三角形の頂点とマテリアルを
/// シェーダーから引ける仕組み（バインドレス）が要る
/// </summary>
class RtReflectionParams : public IPostEffectParams
{
  public:
    /// RtReflection.CS.hlsl の RtReflectionParams と同じ並びにすること
    struct ComputeData
    {
        Matrix4x4 viewProjection{};        // ワールド → クリップ
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド
        Vector3 cameraPosition = {};       // カメラのワールド座標
        float intensity = 1.0f;            // 反射の強さ

        float maxDistance = 100.0f; // レイを飛ばす最大距離
        float fresnelPower = 5.0f;  // 斜めから見たときに強くなる度合い
        float reflectance = 0.04f;  // 正面から見たときの反射率（F0）
        float skyIntensity = 1.0f;  // 空（環境マップ）の映り込みの強さ

        float depthTolerance = 0.0005f; // 交点が画面に写っているとみなす深度差
        float roughness = 0.0f;         // 反射のばらつき（0で鏡面）
        int hasNormals = 0;             // G-Bufferの法線が使えるか
        int hasEnvironment = 1;         // 環境マップが使えるか

        int textureSize[2] = {0, 0}; // 処理対象の解像度
        float padding[2] = {};
    };
    static_assert(sizeof(ComputeData) == 192, "RtReflection.CS.hlsl の RtReflectionParams と並びが合っていません");

    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, computeResource_, &pComputeData_);
        *pComputeData_ = ComputeData{};
    }
    ShaderMode GetMode() const override { return ShaderMode::RtReflection; }

    /// <summary>
    /// カメラの行列を渡す。ワールド↔画面を往復するので、順行列と逆行列の両方が要る
    /// </summary>
    /// <param name="viewProjection">ビュー射影行列</param>
    /// <param name="inverseViewProjection">その逆行列</param>
    /// <param name="cameraPosition">カメラのワールド座標</param>
    void SetCamera(const Matrix4x4 &viewProjection, const Matrix4x4 &inverseViewProjection,
                   const Vector3 &cameraPosition)
    {
        pComputeData_->viewProjection = viewProjection;
        pComputeData_->inverseViewProjection = inverseViewProjection;
        pComputeData_->cameraPosition = cameraPosition;
    }

    /// <summary>
    /// G-Buffer の法線が使えるか（＝ディファードが有効か）を渡す
    /// </summary>
    /// <param name="available">使えるなら true</param>
    void SetNormalsAvailable(bool available) { pComputeData_->hasNormals = available ? 1 : 0; }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    std::string GetComputeShaderFile() const override { return "OffScreen/RtReflection.CS.hlsl"; }
    std::vector<ComputeInput> GetComputeInputs() const override
    {
        return {ComputeInput::SourceColor, ComputeInput::SceneDepth, ComputeInput::GBufferNormal,
                ComputeInput::Tlas, ComputeInput::EnvironmentCube};
    }
    // RayQuery は cs_6_5 以上でないとコンパイルが通らない
    const wchar_t *GetComputeShaderProfile() const override { return L"cs_6_5"; }

    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pComputeData_->textureSize[0] = static_cast<int>(textureWidth);
        pComputeData_->textureSize[1] = static_cast<int>(textureHeight);
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, computeResource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef USE_IMGUI
        if (pComputeData_->hasNormals == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextWrapped("ディファードが無効です。G-Bufferの法線が無いのでRT反射は効きません");
            ImGui::PopStyleColor();
        }

        ImGui::DragFloat("反射の強さ", &pComputeData_->intensity, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("正面の反射率", &pComputeData_->reflectance, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("真正面から見たときにどれだけ映り込むか。\n"
                              "水や磨いた床なら小さめ(0.02〜0.05)、金属的にしたいなら上げます");
        ImGui::DragFloat("斜めの強まり方", &pComputeData_->fresnelPower, 0.1f, 0.1f, 10.0f);
        ImGui::SetItemTooltip("大きいほど「浅い角度のときだけ」映り込みます（水面らしくなります）");
        ImGui::DragFloat("空の映り込み", &pComputeData_->skyIntensity, 0.01f, 0.0f, 4.0f);
        ImGui::SetItemTooltip("何にも当たらなかったレイが拾う、環境マップ（スカイボックス）の強さ。\n"
                              "**画面の外を向いた反射でも正しく出る**のがSSRとの一番の違いです");

        ImGui::SeparatorText("レイ");
        ImGui::DragFloat("最大距離", &pComputeData_->maxDistance, 0.5f, 1.0f, 2000.0f);
        ImGui::SetItemTooltip("これより遠くに当たった物は映しません。\n"
                              "SSRの「分割数」と違い、伸ばしてもレイは1本のままです");
        ImGui::DragFloat("ざらつき", &pComputeData_->roughness, 0.005f, 0.0f, 0.5f, "%.3f");
        ImGui::SetItemTooltip("0で完全な鏡面。上げるとぼやけますが、1画素1本なのでざらつきます");
        ImGui::DragFloat("写っている判定", &pComputeData_->depthTolerance, 0.00005f, 0.0f, 0.01f, "%.5f");
        ImGui::SetItemTooltip("当たった点が画面に写っているかを深度差で判定する許容量。\n"
                              "映り込みが虫食いになるときは少し上げてください");

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("当たった物が画面に写っていない場合は色が分からないので、空を遮るだけになります"
                           "（そこを正しい色にするにはバインドレスの仕組みが要ります）");
        ImGui::TextWrapped("映り込むのは「ライティング有効」のマテリアルだけです。\n"
                           "床に映らないときは、床のマテリアルのライティングを確認してください");
        ImGui::PopStyleColor();
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override
    {
        h->Save<float>(p + "intensity", pComputeData_->intensity);
        h->Save<float>(p + "maxDistance", pComputeData_->maxDistance);
        h->Save<float>(p + "fresnelPower", pComputeData_->fresnelPower);
        h->Save<float>(p + "reflectance", pComputeData_->reflectance);
        h->Save<float>(p + "skyIntensity", pComputeData_->skyIntensity);
        h->Save<float>(p + "depthTolerance", pComputeData_->depthTolerance);
        h->Save<float>(p + "roughness", pComputeData_->roughness);
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        const ComputeData defaults{};
        pComputeData_->intensity = h->Load<float>(p + "intensity", defaults.intensity);
        pComputeData_->maxDistance = h->Load<float>(p + "maxDistance", defaults.maxDistance);
        pComputeData_->fresnelPower = h->Load<float>(p + "fresnelPower", defaults.fresnelPower);
        pComputeData_->reflectance = h->Load<float>(p + "reflectance", defaults.reflectance);
        pComputeData_->skyIntensity = h->Load<float>(p + "skyIntensity", defaults.skyIntensity);
        pComputeData_->depthTolerance = h->Load<float>(p + "depthTolerance", defaults.depthTolerance);
        pComputeData_->roughness = h->Load<float>(p + "roughness", defaults.roughness);
    }

    ComputeData *GetData() { return pComputeData_; }
    const ComputeData *GetData() const { return pComputeData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> computeResource_;
    ComputeData *pComputeData_ = nullptr;
};
} // namespace Hagine
