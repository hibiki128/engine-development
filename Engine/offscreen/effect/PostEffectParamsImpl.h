#pragma once
#include "IPostEffectParams.h"
#include <type/Matrix4x4.h>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <graphics/texture/TextureManager.h>
#include <asset/AssetPath.h>
#ifdef _DEBUG
#include "imgui.h"
#endif

// ============================================================
//  ヘルパー: 定数バッファ作成
// ============================================================
namespace Hagine {
namespace PostEffectParamsHelper {
template <typename T>
static void CreateConstantBuffer(DirectXCommon *dxCommon,
                                 Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                                 T **mappedData)
{
    const UINT64 size = (sizeof(T) + 255) & ~255;
    resource = dxCommon->CreateBufferResource(size);
    resource->Map(0, nullptr, reinterpret_cast<void **>(mappedData));
}
} // namespace PostEffectParamsHelper

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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Vignette; }

    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
#ifdef _DEBUG
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Smooth; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &pData_->kernelSize, 2, 3, 15);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<int>(p + "kernelSize", pData_->kernelSize); }
    void Load(DataHandler *h, const std::string &p) override { pData_->kernelSize = h->Load<int>(p + "kernelSize", 3); }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

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
    struct Data
    {
        int kernelSize = 5;
        float sigma = 1.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Gauss; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &pData_->kernelSize, 2, 3, 15);
        ImGui::DragFloat("シグマ", &pData_->sigma, 0.01f, 0.1f, 10.0f);
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
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Outline; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Depth; }

    void SetProjectionInverse(const Matrix4x4 &mat) { pData_->projectionInverse = mat; }

    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &pData_->kernelSize, 2, 3, 9);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<int>(p + "kernelSize", pData_->kernelSize); }
    void Load(DataHandler *h, const std::string &p) override { pData_->kernelSize = h->Load<int>(p + "kernelSize", 3); }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Blur; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Cinematic; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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
    struct Data
    {
        float threshold = 0.5f;
        float edgeWidth = 0.05f;
        float pad[2] = {};
        Vector3 edgeColor = {1.0f, 0.5f, 0.0f};
        float pad2 = 0.0f;
        int invert = 0;
        float pad3[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Dissolve; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *srv, DirectXCommon *dxCommon) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        ImGui::DragFloat("閾値", &pData_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("エッジ幅", &pData_->edgeWidth, 0.001f, 0.0f, 0.5f);
        ImGui::ColorEdit3("エッジカラー", &pData_->edgeColor.x);
        bool inv = pData_->invert != 0;
        if (ImGui::Checkbox("反転", &inv))
        {
            pData_->invert = inv ? 1 : 0;
        }
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
    }
    void Load(DataHandler *h, const std::string &p) override
    {
        pData_->threshold = h->Load<float>(p + "threshold", 0.5f);
        pData_->edgeWidth = h->Load<float>(p + "edgeWidth", 0.05f);
        pData_->edgeColor.x = h->Load<float>(p + "edgeR", 1.0f);
        pData_->edgeColor.y = h->Load<float>(p + "edgeG", 0.5f);
        pData_->edgeColor.z = h->Load<float>(p + "edgeB", 0.0f);
        pData_->invert = h->Load<bool>(p + "invert", false) ? 1 : 0;
    }

    void SetNoiseTextureSrvIndex(uint32_t idx) { noiseSrvIndex_ = idx; }

    Data *GetData() { return pData_; }
    const Data *GetData() const { return pData_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
    uint32_t noiseSrvIndex_ = 0;
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Random; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::FocusLine; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Pixelate; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        // ブロックサイズはピクセル単位、中心座標はUV空間（0-1）
        ImGui::DragFloat("ブロックサイズ", &pData_->blockSize, 0.001f, 0.001f, 1.0f);
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Retro; }
    void UpdateTime(float dt) override { pData_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
        *pData_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::Bloom; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
        // 閾値は輝度の下限カット（0-1）、強度はブルーム量
        ImGui::DragFloat("閾値", &pData_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("強度", &pData_->intensity, 0.01f, 0.0f, 5.0f);
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
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *pData_ = nullptr;
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

    void Initialize(DirectXCommon *dxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &pData_);
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
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override
    {
        // 専用RootSig: [0]=srcRT(Renderer側で設定済), [1]=flareTex, [2]=cbuffer
        // GetSrvHandleGPU は内部 prepend しないのでフルパス(＝マップキー)で渡す
        const std::string fullPath = AssetPath::Image(kFlareTexPath_);
        auto flareGpu = TextureManager::GetInstance()->GetSrvHandleGPU(fullPath);
        cmd->SetGraphicsRootDescriptorTable(1, flareGpu);
        cmd->SetGraphicsRootConstantBufferView(2, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override
    {
#ifdef _DEBUG
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
} // namespace Hagine
