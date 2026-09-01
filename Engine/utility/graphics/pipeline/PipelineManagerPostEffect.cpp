#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// ポストエフェクト（全画面描画）のルートシグネチャとPSO。ShaderMode ごとに1本ずつ。
namespace Hagine {
// レンダーパイプラインの作成
void PipelineManager::CreateRenderPipelines()
{
    // 各シェーダーモード用のルートシグネチャとパイプラインを作成
    for (int i = 0; i <= static_cast<int>(ShaderMode::Count) - 1; i++)
    {
        ShaderMode shaderMode = static_cast<ShaderMode>(i);

        // ルートシグネチャを作成し、マップに格納
        auto rootSignature = CreateRenderRootSignature(shaderMode);
        rootSignatures_[MakeRootSignatureKey(PipelineType::Render, shaderMode)] = rootSignature;

        // パイプラインを作成し、マップに格納
        auto pipeline = CreateRenderGraphicsPipeline(rootSignature, shaderMode);
        pipelines_[MakePipelineKey(PipelineType::Render, BlendMode::Normal, shaderMode)] = pipeline;
    }

    // バックバッファへの最終合成用。
    // チェーン内(リニアFP16)とバックバッファ(sRGB)でフォーマットが違うので、
    // 同じ CopyImage シェーダーでも別PSOが必要になる。
    // ルートシグネチャは None と同じもの（テクスチャ1枚）を使い回す。
    {
        auto rootSignature = rootSignatures_[MakeRootSignatureKey(PipelineType::Render, ShaderMode::None)];
        rootSignatures_[MakeRootSignatureKey(PipelineType::PresentCopy, ShaderMode::None)] = rootSignature;
        AliasReflectedRootSignature(PipelineType::Render, PipelineType::PresentCopy);
        pipelines_[MakePipelineKey(PipelineType::PresentCopy, BlendMode::Normal, ShaderMode::None)] =
            CreateFullScreenPostEffectPipeline(shaderPath + L"shaders/OffScreen/CopyImage.PS.hlsl",
                                               rootSignature, kBackBufferFormat);
    }
}

/// <summary>
/// シェーダーモードに対応するピクセルシェーダーのパスを返す。
/// ルートシグネチャもPSOもこの1本から作るので、エフェクトを足すときに触るのはここだけになる。
/// </summary>
/// <param name="shaderMode">シェーダーモード</param>
/// <returns>std::wstring: ピクセルシェーダーの完全パス</returns>
std::wstring PipelineManager::GetPostEffectPixelShaderPath(ShaderMode shaderMode) const
{
    const std::wstring root = shaderPath + L"shaders/OffScreen/";
    switch (shaderMode)
    {
    case ShaderMode::Gray:      return root + L"GrayScale.PS.hlsl";
    case ShaderMode::Vignette:  return root + L"Vignette.PS.hlsl";
    case ShaderMode::Smooth:    return root + L"BoxFilter.PS.hlsl";
    case ShaderMode::Gauss:     return root + L"GaussianFilter.PS.hlsl";
    case ShaderMode::Outline:   return root + L"LuminanceBasedOutline.PS.hlsl";
    case ShaderMode::Depth:     return root + L"DepthBasedOutline.PS.hlsl";
    case ShaderMode::Blur:      return root + L"RadialBlur.PS.hlsl";
    case ShaderMode::Cinematic: return root + L"Cinematic.PS.hlsl";
    case ShaderMode::Dissolve:  return root + L"Dissolve.PS.hlsl";
    case ShaderMode::Random:    return root + L"Random.PS.hlsl";
    case ShaderMode::FocusLine: return root + L"FocusLine.PS.hlsl";
    case ShaderMode::Pixelate:  return root + L"Pixelate.PS.hlsl";
    case ShaderMode::Bloom:     return root + L"Bloom.PS.hlsl";
    case ShaderMode::Retro:     return root + L"Retro.PS.hlsl";
    case ShaderMode::Shockwave: return root + L"Shockwave.PS.hlsl";
    case ShaderMode::Monochrome:return root + L"Monochrome.PS.hlsl";
    case ShaderMode::None:
    case ShaderMode::DepthOfField: // コンピュート専用。PS版が無いので素通しを割り当てる
    default:                    return root + L"CopyImage.PS.hlsl";
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateRenderRootSignature(ShaderMode shaderMode)
{
    // 全画面ポストエフェクト。使うレジスタ（t0=入力画像, b0=パラメータ, t1=深度やノイズ）は
    // エフェクトごとに違うが、リフレクションが実際の宣言を読むのでここで場合分けする必要はない。
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;

    // サンプラーだけはリフレクションから中身が分からないので、エフェクトごとに指定する。
    // 既定は全画面用のクランプ（ブラー等でUVが外へずれても反対側の端を巻き込まないため）
    switch (shaderMode)
    {
    case ShaderMode::Depth:
    {
        // s0=線形/繰り返し, s1=深度用のポイント/繰り返し。プリセットに無い組み合わせ
        D3D12_STATIC_SAMPLER_DESC linearWrap{};
        linearWrap.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearWrap.AddressU = linearWrap.AddressV = linearWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrap.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        linearWrap.MaxLOD = D3D12_FLOAT32_MAX;
        linearWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC pointWrap = linearWrap;
        pointWrap.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        options.explicitSamplers = {linearWrap, pointWrap};
        break;
    }
    case ShaderMode::Dissolve:
        // ノイズテクスチャをタイリングさせるので繰り返し
        options.samplerPresets = {ShaderRootSignature::SamplerPreset::LinearWrap};
        break;
    default:
        options.samplerPresets = {ShaderRootSignature::SamplerPreset::LinearClamp};
        break;
    }

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/OffScreen/FullScreen.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {GetPostEffectPixelShaderPath(shaderMode), L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::Render, shaderMode, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateRenderGraphicsPipeline(
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, ShaderMode shaderMode)
{
    return CreateFullScreenPostEffectPipeline(GetPostEffectPixelShaderPath(shaderMode), rootSignature);
}
} // namespace Hagine
