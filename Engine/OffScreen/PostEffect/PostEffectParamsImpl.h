#pragma once
#include "IPostEffectParams.h"
#include <type/Matrix4x4.h>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <Graphics/Texture/TextureManager.h>
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
                                 T **mappedData) {
    const UINT64 size = (sizeof(T) + 255) & ~255;
    resource = dxCommon->CreateBufferResource(size);
    resource->Map(0, nullptr, reinterpret_cast<void **>(mappedData));
}
} // namespace PostEffectParamsHelper

// ============================================================
//  None
// ============================================================
class NoneParams : public IPostEffectParams {
  public:
    void Initialize(DirectXCommon *) override {}
    ShaderMode GetMode() const override { return ShaderMode::kNone; }
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}
    void DrawUI() override {}
    void Save(DataHandler *, const std::string &) const override {}
    void Load(DataHandler *, const std::string &) override {}
};

// ============================================================
//  Vignette
// ============================================================
class VignetteParams : public IPostEffectParams {
  public:
    struct Data {
        float strength = 1.0f;
        float radius = 0.8f;
        float exponent = 2.0f;
        float padding = 0.0f;
        Vector2 center = {0.5f, 0.5f};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kVigneet; }

    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }

    void DrawUI() override {
#ifdef _DEBUG
        // ビネットの各パラメータ調整（強度・形状・位置）
        ImGui::DragFloat("強度", &data_->strength, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("半径", &data_->radius, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("指数", &data_->exponent, 0.1f, 0.1f, 10.0f);
        ImGui::DragFloat2("中心", &data_->center.x, 0.01f, 0.0f, 1.0f);
#endif
    }

    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "strength", data_->strength);
        h->Save<float>(p + "radius", data_->radius);
        h->Save<float>(p + "exponent", data_->exponent);
        h->Save<float>(p + "centerX", data_->center.x);
        h->Save<float>(p + "centerY", data_->center.y);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->strength = h->Load<float>(p + "strength", 1.0f);
        data_->radius = h->Load<float>(p + "radius", 0.8f);
        data_->exponent = h->Load<float>(p + "exponent", 2.0f);
        data_->center.x = h->Load<float>(p + "centerX", 0.5f);
        data_->center.y = h->Load<float>(p + "centerY", 0.5f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Smooth (Box Filter)
// ============================================================
class SmoothParams : public IPostEffectParams {
  public:
    struct Data {
        int kernelSize = 3;
        int pad[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kSmooth; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &data_->kernelSize, 2, 3, 15);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<int>(p + "kernelSize", data_->kernelSize); }
    void Load(DataHandler *h, const std::string &p) override { data_->kernelSize = h->Load<int>(p + "kernelSize", 3); }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Gaussian
// ============================================================
class GaussianParams : public IPostEffectParams {
  public:
    struct Data {
        int kernelSize = 5;
        float sigma = 1.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kGauss; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &data_->kernelSize, 2, 3, 15);
        ImGui::DragFloat("シグマ", &data_->sigma, 0.01f, 0.1f, 10.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<int>(p + "kernelSize", data_->kernelSize);
        h->Save<float>(p + "sigma", data_->sigma);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->kernelSize = h->Load<int>(p + "kernelSize", 5);
        data_->sigma = h->Load<float>(p + "sigma", 1.0f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Outline (Edge Detection)
// ============================================================
class OutlineEdgeParams : public IPostEffectParams {
  public:
    struct Data {
        float edgeStrength = 1.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kOutLine; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        ImGui::DragFloat("エッジ強度", &data_->edgeStrength, 0.01f, 0.0f, 5.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<float>(p + "edgeStrength", data_->edgeStrength); }
    void Load(DataHandler *h, const std::string &p) override { data_->edgeStrength = h->Load<float>(p + "edgeStrength", 1.0f); }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Outline (Depth Based)
// ============================================================
class OutlineDepthParams : public IPostEffectParams {
  public:
    struct Data {
        Matrix4x4 projectionInverse;
        int kernelSize = 3;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kDepth; }

    void SetProjectionInverse(const Matrix4x4 &mat) { data_->projectionInverse = mat; }

    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // カーネルサイズは奇数のみ有効なのでステップを2に設定
        ImGui::DragInt("カーネルサイズ", &data_->kernelSize, 2, 3, 9);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override { h->Save<int>(p + "kernelSize", data_->kernelSize); }
    void Load(DataHandler *h, const std::string &p) override { data_->kernelSize = h->Load<int>(p + "kernelSize", 3); }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Radial Blur
// ============================================================
class RadialBlurParams : public IPostEffectParams {
  public:
    struct Data {
        Vector2 center = {0.5f, 0.5f};
        float blurWidth = 0.01f;
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kBlur; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // 中心座標はUV空間（0-1）、ブラー幅は視覚的に有効な範囲に制限
        ImGui::DragFloat2("中心", &data_->center.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("ブラー幅", &data_->blurWidth, 0.001f, 0.0f, 0.2f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "centerX", data_->center.x);
        h->Save<float>(p + "centerY", data_->center.y);
        h->Save<float>(p + "blurWidth", data_->blurWidth);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->center.x = h->Load<float>(p + "centerX", 0.5f);
        data_->center.y = h->Load<float>(p + "centerY", 0.5f);
        data_->blurWidth = h->Load<float>(p + "blurWidth", 0.01f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Cinematic
// ============================================================
class CinematicParams : public IPostEffectParams {
  public:
    struct Data {
        Vector2 resolution = {1280.0f, 720.0f};
        float contrast = 1.0f;
        float saturation = 1.0f;
        float brightness = 1.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kCinematic; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        ImGui::DragFloat("コントラスト", &data_->contrast, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("彩度", &data_->saturation, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("明度", &data_->brightness, 0.01f, 0.0f, 3.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "contrast", data_->contrast);
        h->Save<float>(p + "saturation", data_->saturation);
        h->Save<float>(p + "brightness", data_->brightness);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->contrast = h->Load<float>(p + "contrast", 1.0f);
        data_->saturation = h->Load<float>(p + "saturation", 1.0f);
        data_->brightness = h->Load<float>(p + "brightness", 1.0f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Dissolve
// ============================================================
class DissolveParams : public IPostEffectParams {
  public:
    struct Data {
        float threshold = 0.5f;
        float edgeWidth = 0.05f;
        float pad[2] = {};
        Vector3 edgeColor = {1.0f, 0.5f, 0.0f};
        float pad2 = 0.0f;
        int invert = 0;
        float pad3[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kDissolve; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *srv, DirectXCommon *dxCommon) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        ImGui::DragFloat("閾値", &data_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("エッジ幅", &data_->edgeWidth, 0.001f, 0.0f, 0.5f);
        ImGui::ColorEdit3("エッジカラー", &data_->edgeColor.x);
        bool inv = data_->invert != 0;
        if (ImGui::Checkbox("反転", &inv)) {
            data_->invert = inv ? 1 : 0;
        }
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "threshold", data_->threshold);
        h->Save<float>(p + "edgeWidth", data_->edgeWidth);
        h->Save<float>(p + "edgeR", data_->edgeColor.x);
        h->Save<float>(p + "edgeG", data_->edgeColor.y);
        h->Save<float>(p + "edgeB", data_->edgeColor.z);
        h->Save<bool>(p + "invert", data_->invert != 0);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->threshold = h->Load<float>(p + "threshold", 0.5f);
        data_->edgeWidth = h->Load<float>(p + "edgeWidth", 0.05f);
        data_->edgeColor.x = h->Load<float>(p + "edgeR", 1.0f);
        data_->edgeColor.y = h->Load<float>(p + "edgeG", 0.5f);
        data_->edgeColor.z = h->Load<float>(p + "edgeB", 0.0f);
        data_->invert = h->Load<bool>(p + "invert", false) ? 1 : 0;
    }

    void SetNoiseTextureSrvIndex(uint32_t idx) { noiseSrvIndex_ = idx; }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
    uint32_t noiseSrvIndex_ = 0;
};

// ============================================================
//  Random (Noise)
// ============================================================
class RandomParams : public IPostEffectParams {
  public:
    struct Data {
        float time = 0.0f;
        float pad[3] = {};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kRandom; }
    void UpdateTime(float dt) override { data_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {}
    void Save(DataHandler *, const std::string &) const override {}
    void Load(DataHandler *, const std::string &) override {}

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Focus Line (集中線)
// ============================================================
class FocusLineParams : public IPostEffectParams {
  public:
    struct Data {
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

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kFocusLine; }
    void UpdateTime(float dt) override { data_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // 線の本数・形状パラメータ
        ImGui::DragFloat("線の数", &data_->lines, 1.0f, 10.0f, 500.0f);
        ImGui::DragFloat("線幅", &data_->width, 0.001f, 0.001f, 0.1f);
        ImGui::DragFloat("速度", &data_->speed, 0.1f, 0.0f, 10.0f);
        ImGui::DragFloat("強度", &data_->intensity, 0.01f, 0.0f, 5.0f);
        // 集中線の発生エリア設定
        ImGui::DragFloat("中心半径", &data_->centerRadius, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("最大距離", &data_->maxDistance, 0.01f, 0.0f, 2.0f);
        ImGui::ColorEdit4("線の色", &data_->lineColor.x);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "lines", data_->lines);
        h->Save<float>(p + "width", data_->width);
        h->Save<float>(p + "speed", data_->speed);
        h->Save<float>(p + "intensity", data_->intensity);
        h->Save<float>(p + "centerRadius", data_->centerRadius);
        h->Save<float>(p + "maxDistance", data_->maxDistance);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->lines = h->Load<float>(p + "lines", 200.0f);
        data_->width = h->Load<float>(p + "width", 0.01f);
        data_->speed = h->Load<float>(p + "speed", 1.0f);
        data_->intensity = h->Load<float>(p + "intensity", 1.0f);
        data_->centerRadius = h->Load<float>(p + "centerRadius", 0.1f);
        data_->maxDistance = h->Load<float>(p + "maxDistance", 0.8f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Pixelate
// ============================================================
class PixelateParams : public IPostEffectParams {
  public:
    struct Data {
        float blockSize = 8.0f;
        float centerX = 0.5f;
        float centerY = 0.5f;
        float pad = 0.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kPixelate; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // ブロックサイズはピクセル単位、中心座標はUV空間（0-1）
        ImGui::DragFloat("ブロックサイズ", &data_->blockSize, 0.001f, 0.001f, 1.0f);
        ImGui::DragFloat("中心X", &data_->centerX, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("中心Y", &data_->centerY, 0.01f, 0.0f, 1.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "blockSize", data_->blockSize);
        h->Save<float>(p + "centerX", data_->centerX);
        h->Save<float>(p + "centerY", data_->centerY);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->blockSize = h->Load<float>(p + "blockSize", 8.0f);
        data_->centerX = h->Load<float>(p + "centerX", 0.5f);
        data_->centerY = h->Load<float>(p + "centerY", 0.5f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Retro (古いゲーム風: ピクセル化+減色+スキャンライン+色収差+CRTビネット)
// ============================================================
class RetroParams : public IPostEffectParams {
  public:
    struct Data {
        float pixelSize = 4.0f;
        float colorLevels = 8.0f;
        float scanlineIntensity = 0.4f;
        float scanlineCount = 400.0f;
        float vignetteStrength = 0.6f;
        float chromaticOffset = 0.003f;
        float time = 0.0f;
        float resolutionX = 1280.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kRetro; }
    void UpdateTime(float dt) override { data_->time += dt; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        ImGui::SliderFloat("ピクセルサイズ", &data_->pixelSize, 1.0f, 32.0f);
        ImGui::SliderFloat("減色レベル", &data_->colorLevels, 2.0f, 32.0f);
        ImGui::SliderFloat("スキャンライン強度", &data_->scanlineIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("スキャンライン本数", &data_->scanlineCount, 50.0f, 800.0f);
        ImGui::SliderFloat("CRTビネット", &data_->vignetteStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("色収差", &data_->chromaticOffset, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("解像度X", &data_->resolutionX, 320.0f, 3840.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "pixelSize", data_->pixelSize);
        h->Save<float>(p + "colorLevels", data_->colorLevels);
        h->Save<float>(p + "scanlineIntensity", data_->scanlineIntensity);
        h->Save<float>(p + "scanlineCount", data_->scanlineCount);
        h->Save<float>(p + "vignetteStrength", data_->vignetteStrength);
        h->Save<float>(p + "chromaticOffset", data_->chromaticOffset);
        h->Save<float>(p + "resolutionX", data_->resolutionX);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->pixelSize = h->Load<float>(p + "pixelSize", 4.0f);
        data_->colorLevels = h->Load<float>(p + "colorLevels", 8.0f);
        data_->scanlineIntensity = h->Load<float>(p + "scanlineIntensity", 0.4f);
        data_->scanlineCount = h->Load<float>(p + "scanlineCount", 400.0f);
        data_->vignetteStrength = h->Load<float>(p + "vignetteStrength", 0.6f);
        data_->chromaticOffset = h->Load<float>(p + "chromaticOffset", 0.003f);
        data_->resolutionX = h->Load<float>(p + "resolutionX", 1280.0f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Bloom
// ============================================================
class BloomParams : public IPostEffectParams {
  public:
    struct Data {
        float threshold = 0.8f;
        float intensity = 1.0f;
        Vector2 texelSize = {1.0f / 1280.0f, 1.0f / 720.0f};
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
    }
    ShaderMode GetMode() const override { return ShaderMode::kBloom; }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        cmd->SetGraphicsRootConstantBufferView(1, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        // 閾値は輝度の下限カット（0-1）、強度はブルーム量
        ImGui::DragFloat("閾値", &data_->threshold, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("強度", &data_->intensity, 0.01f, 0.0f, 5.0f);
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "threshold", data_->threshold);
        h->Save<float>(p + "intensity", data_->intensity);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->threshold = h->Load<float>(p + "threshold", 0.8f);
        data_->intensity = h->Load<float>(p + "intensity", 1.0f);
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
};

// ============================================================
//  Shockwave (領域消去・キー破壊などのインパクト演出用衝撃波)
// ============================================================
class ShockwaveParams : public IPostEffectParams {
  public:
    struct Data {
        Vector2 center = {0.5f, 0.5f};
        float time = 0.0f;
        float duration = 0.55f;
        float amplitude = 3.0f;   // フラッシュ加算強度
        float frequency = 18.0f;  // 放射光線の本数
        float waveSpeed = 1.6f;   // 光線の伸び速度（UV/秒）
        float active = 0.0f;
    };

    void Initialize(DirectXCommon *dxCommon) override {
        PostEffectParamsHelper::CreateConstantBuffer(dxCommon, resource_, &data_);
        *data_ = Data{};
        // フレアテクスチャ読み込み（既ロードなら内部でスキップ）
        TextureManager::GetInstance()->LoadTexture(kFlareTexPath_);
    }
    ShaderMode GetMode() const override { return ShaderMode::kShockwave; }
    void UpdateTime(float dt) override {
        if (data_ && data_->active >= 0.5f) {
            data_->time += dt;
            if (data_->time > data_->duration) {
                data_->active = 0.0f;
                data_->time = 0.0f;
            }
        }
    }
    void Apply(ID3D12GraphicsCommandList *cmd, SrvManager *, DirectXCommon *) override {
        // 専用RootSig: [0]=srcRT(Renderer側で設定済), [1]=flareTex, [2]=cbuffer
        // GetSrvHandleGPU は内部 prepend しないのでフルパスで渡す
        const std::string fullPath = std::string("resources/images/") + kFlareTexPath_;
        auto flareGpu = TextureManager::GetInstance()->GetSrvHandleGPU(fullPath);
        cmd->SetGraphicsRootDescriptorTable(1, flareGpu);
        cmd->SetGraphicsRootConstantBufferView(2, resource_->GetGPUVirtualAddress());
    }
    void DrawUI() override {
#ifdef _DEBUG
        ImGui::SliderFloat2("中心(UV)", &data_->center.x, 0.0f, 1.0f);
        ImGui::SliderFloat("持続時間", &data_->duration, 0.1f, 2.0f);
        ImGui::SliderFloat("フラッシュ強度", &data_->amplitude, 0.0f, 8.0f, "%.2f");
        ImGui::SliderFloat("光線本数", &data_->frequency, 1.0f, 32.0f, "%.0f");
        ImGui::SliderFloat("光線伸び速度", &data_->waveSpeed, 0.1f, 3.0f);
        ImGui::Text("経過: %.2f / %.2f", data_->time, data_->duration);
        if (ImGui::Button("発動テスト")) {
            data_->time = 0.0f;
            data_->active = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("停止")) {
            data_->active = 0.0f;
            data_->time = 0.0f;
        }
#endif
    }
    void Save(DataHandler *h, const std::string &p) const override {
        h->Save<float>(p + "duration", data_->duration);
        h->Save<float>(p + "amplitude", data_->amplitude);
        h->Save<float>(p + "frequency", data_->frequency);
        h->Save<float>(p + "waveSpeed", data_->waveSpeed);
        h->Save<Vector2>(p + "center", data_->center);
    }
    void Load(DataHandler *h, const std::string &p) override {
        data_->duration = h->Load<float>(p + "duration", 0.55f);
        data_->amplitude = h->Load<float>(p + "amplitude", 3.0f);
        data_->frequency = h->Load<float>(p + "frequency", 18.0f);
        data_->waveSpeed = h->Load<float>(p + "waveSpeed", 1.6f);
        data_->center = h->Load<Vector2>(p + "center", Vector2(0.5f, 0.5f));
    }

    void Trigger(const Vector2 &uvCenter) {
        if (!data_) return;
        data_->center = uvCenter;
        data_->time = 0.0f;
        data_->active = 1.0f;
    }

    Data *GetData() { return data_; }
    const Data *GetData() const { return data_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Data *data_ = nullptr;
    static constexpr const char *kFlareTexPath_ = "Particle/tetrio/flare.png";
};
} // namespace Hagine
