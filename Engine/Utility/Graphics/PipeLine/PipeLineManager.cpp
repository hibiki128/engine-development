#include "PipeLineManager.h"
#include "ComputePipeLineManager.h"
#include <Debug/Log/Logger.h>
#include <d3dx12.h>

namespace Hagine {
void PipeLineManager::Finalize() {
    pipelines_.clear();
    rootSignatures_.clear();
}

void PipeLineManager::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;

    CreateAllPipelines();
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::GetPipeline(PipelineType type, BlendMode blendMode, ShaderMode shaderMode) {
    // キーを生成して対応するパイプラインを取得
    std::string key = MakePipelineKey(type, blendMode, shaderMode);

    // 対応するパイプラインが存在するか確認
    if (pipelines_.find(key) == pipelines_.end()) {
        // パイプラインが見つからない場合は警告を出して、デフォルトを返す
        assert(false && "指定されたパイプラインが存在しません");

        // デフォルトのパイプラインを返す (ここではStandard/Normal/Noneを想定)
        return pipelines_[MakePipelineKey(PipelineType::kStandard, BlendMode::kNormal, ShaderMode::kNone)];
    }

    return pipelines_[key];
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::GetRootSignature(PipelineType type, ShaderMode shaderMode) {
    // キーを生成して対応するルートシグネチャを取得
    std::string key = MakeRootSignatureKey(type, shaderMode);

    // 対応するルートシグネチャが存在するか確認
    if (rootSignatures_.find(key) == rootSignatures_.end()) {
        // ルートシグネチャが見つからない場合は警告を出して、デフォルトを返す
        assert(false && "指定されたルートシグネチャが存在しません");

        // デフォルトのルートシグネチャを返す
        return rootSignatures_[MakeRootSignatureKey(PipelineType::kStandard, ShaderMode::kNone)];
    }

    return rootSignatures_[key];
}

void PipeLineManager::DrawCommonSetting(PipelineType type, BlendMode blendMode, ShaderMode shaderMode) {
    // 指定されたタイプのパイプラインとルートシグネチャを取得
    auto pipeline = GetPipeline(type, blendMode, shaderMode);
    auto rootSignature = GetRootSignature(type, shaderMode);

    // グラフィックスコマンドリストにパイプラインとルートシグネチャを設定
    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList().Get();
    commandList->SetPipelineState(pipeline.Get());
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    if (type == PipelineType::kLine3d) {
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    } else {
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
}

// スキニングパイプラインの作成
void PipeLineManager::CreateSkinningPipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateSkinningRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kSkinning, ShaderMode::kNone)] = rootSignature;

    // パイプラインを作成し、マップに格納
    auto pipeline = CreateSkinningGraphicsPipeLine(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::kSkinning, BlendMode::kNormal, ShaderMode::kNone)] = pipeline;
}

// 3Dラインパイプラインの作成
void PipeLineManager::CreateLine3dPipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateLine3dRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kLine3d, ShaderMode::kNone)] = rootSignature;

    // パイプラインを作成し、マップに格納
    auto pipeline = CreateLine3dGraphicsPipeLine(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::kLine3d, BlendMode::kNormal, ShaderMode::kNone)] = pipeline;
}

// キー文字列を生成するヘルパー関数
std::string PipeLineManager::MakePipelineKey(PipelineType type, BlendMode blendMode, ShaderMode shaderMode) {
    return std::format("Pipeline_{}_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(blendMode),
                       static_cast<int>(shaderMode));
}

std::string PipeLineManager::MakeRootSignatureKey(PipelineType type, ShaderMode shaderMode) {
    return std::format("RootSignature_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(shaderMode));
}

D3D12_STATIC_SAMPLER_DESC PipeLineManager::CreateCommonSamplerDesc() {
    D3D12_STATIC_SAMPLER_DESC desc{};
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    desc.ShaderRegister = 0;
    desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return desc;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateCommonRootSignature(bool hasCBV) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.BaseShaderRegister = 0;
    range.NumDescriptors = 1;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].DescriptorTable.NumDescriptorRanges = 1;

    UINT paramCount = 1;
    if (hasCBV) {
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].Descriptor.ShaderRegister = 0;
        paramCount = 2;
    }

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = paramCount;
    desc.pParameters = params;

    auto sampler = CreateCommonSamplerDesc();
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errBlob->GetBufferPointer()));
        assert(false);
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig));
    assert(SUCCEEDED(hr));
    return rootSig;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateFullScreenPostEffectPipeline(const std::wstring &psPath, Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    IDxcBlob *vs = dxCommon_->CompileShader(L"./resources/shaders/OffScreen/FullScreen.VS.hlsl", L"vs_6_0");
    IDxcBlob *ps = dxCommon_->CompileShader(psPath.c_str(), L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = {nullptr, 0};
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));
    return pipelineState;
}

D3D12_DEPTH_STENCIL_DESC PipeLineManager::SettingDepthStencilDesc(bool depth) {
    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = depth;
    depthStencilDesc.StencilEnable = depth;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    return depthStencilDesc;
}

void PipeLineManager::CreateAllPipelines() {
    // 各種パイプラインの作成
    CreateStandardPipelines();
    CreateParticlePipelines();
    CreateSpritePipelines();
    CreateRenderPipelines();
    CreateSkinningPipelines();
    CreateLine3dPipelines();
    CreateSkyboxPipelines();
    CreateGPUParticlePipelines();
    CreateShadowMapPipelines();
}

// 標準パイプラインの作成
void PipeLineManager::CreateStandardPipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kStandard, ShaderMode::kNone)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::kScreen); i++) {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateGraphicsPipeLine(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::kStandard, blendMode, ShaderMode::kNone)] = pipeline;
    }
}

// スプライトパイプラインの作成
void PipeLineManager::CreateSpritePipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateSpriteRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kSprite, ShaderMode::kNone)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::kScreen); i++) {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateSpriteGraphicsPipeLine(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::kSprite, blendMode, ShaderMode::kNone)] = pipeline;
    }
}

// レンダーパイプラインの作成
void PipeLineManager::CreateRenderPipelines() {
    // 各シェーダーモード用のルートシグネチャとパイプラインを作成
    for (int i = 0; i <= static_cast<int>(ShaderMode::kCount) - 1; i++) {
        ShaderMode shaderMode = static_cast<ShaderMode>(i);

        // ルートシグネチャを作成し、マップに格納
        auto rootSignature = CreateRenderRootSignature(shaderMode);
        rootSignatures_[MakeRootSignatureKey(PipelineType::kRender, shaderMode)] = rootSignature;

        // パイプラインを作成し、マップに格納
        auto pipeline = CreateRenderGraphicsPipeLine(rootSignature, shaderMode);
        pipelines_[MakePipelineKey(PipelineType::kRender, BlendMode::kNormal, shaderMode)] = pipeline;
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;
    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};

    // 通常の2Dテクスチャ t0
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].BaseShaderRegister = 0; // t0
    descriptorRange[0].RegisterSpace = 0;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE skyBoxDescriptorRange[1] = {};

    // 通常の2Dテクスチャ t0
    skyBoxDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    skyBoxDescriptorRange[0].NumDescriptors = 1;
    skyBoxDescriptorRange[0].BaseShaderRegister = 1; // t0
    skyBoxDescriptorRange[0].RegisterSpace = 0;
    skyBoxDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // シャドウマップ用DescriptorRange (t2)
    D3D12_DESCRIPTOR_RANGE shadowDescriptorRange[1] = {};
    shadowDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowDescriptorRange[0].NumDescriptors = 1;
    shadowDescriptorRange[0].BaseShaderRegister = 2; // t2
    shadowDescriptorRange[0].RegisterSpace = 0;
    shadowDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter作成。複数設定できるので配列。
    D3D12_ROOT_PARAMETER rootParameters[10] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVを使う
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // VertexShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                                   // レジスタ番号0とバインド
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVをつかう
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;               // PixelShaderで使う
    rootParameters[1].Descriptor.ShaderRegister = 0;                                   // レジスタ番号0とバインド
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;      // DescriptorTableを使う
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;             // Tableの中身の配列を指定
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVを使う
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[3].Descriptor.ShaderRegister = 1;                                   // レジスタ番号1とバインド
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVを使う
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[4].Descriptor.ShaderRegister = 2;                                   // レジスタ番号1とバインド
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVを使う
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[5].Descriptor.ShaderRegister = 3;                                   // レジスタ番号1とバインド
    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;                   // CBVを使う
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[6].Descriptor.ShaderRegister = 4;                                   // レジスタ番号1とバインド
    rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[7].DescriptorTable.pDescriptorRanges = skyBoxDescriptorRange;
    rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(skyBoxDescriptorRange);
    // シャドウマップ SRV (t2) - param 8
    rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[8].DescriptorTable.pDescriptorRanges = shadowDescriptorRange;
    rootParameters[8].DescriptorTable.NumDescriptorRanges = _countof(shadowDescriptorRange);
    // ShadowData CBV (b5) - param 9
    rootParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[9].Descriptor.ShaderRegister = 5;

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Samplerの設定（通常 + シャドウ比較サンプラー）
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    // シャドウ比較サンプラー (s1)
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1; // s1
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてパイナリする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateGraphicsPipeLine(
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;

    switch (blendMode) {
    case BlendMode::kNone:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::kNormal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::kAdd:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kSubtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kMultiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::kScreen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

void PipeLineManager::CreateParticlePipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateParticleRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kParticle, ShaderMode::kNone)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::kScreen); i++) {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateParticleGraphicsPipeLine(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::kParticle, blendMode, ShaderMode::kNone)] = pipeline;
    }
}

void PipeLineManager::CreateGPUParticlePipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateGPUParticleRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kGPUParticle, ShaderMode::kNone)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::kScreen); i++) {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateGPUParticleGraphicsPipeLine(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::kGPUParticle, blendMode, ShaderMode::kNone)] = pipeline;
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateGPUParticleRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;                                                   // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // descriptorRangeForInstancing
    D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
    descriptorRangeForInstancing[0].BaseShaderRegister = 0;                                                   // 0から始まる
    descriptorRangeForInstancing[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // ★ t2: 生存コンパクションの aliveList (VertexShaderで使用)
    D3D12_DESCRIPTOR_RANGE descriptorRangeAliveList[1] = {};
    descriptorRangeAliveList[0].BaseShaderRegister = 2; // register(t2)
    descriptorRangeAliveList[0].NumDescriptors = 1;
    descriptorRangeAliveList[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeAliveList[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ★ t3: 生存数カウンタ (VertexShaderで使用、instanceId カリング用)
    D3D12_DESCRIPTOR_RANGE descriptorRangeAliveCount[1] = {};
    descriptorRangeAliveCount[0].BaseShaderRegister = 3; // register(t3)
    descriptorRangeAliveCount[0].NumDescriptors = 1;
    descriptorRangeAliveCount[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeAliveCount[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter作成。複数設定できるので配列。
    D3D12_ROOT_PARAMETER rootParameters[6] = {}; // b0,t0,t1(tex),b1,t2,t3

    // b0: PerView用のConstantBufferView (VertexShaderで使用)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderから参照
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // t0: パーティクル用のStructuredBuffer (VertexShaderで使用)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

    // t1: テクスチャ用のSRV (PixelShaderで使用)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    // b1: Material用のConstantBufferView (PixelShaderで使用)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1; // register(b1)

    // ★ t2: 生存コンパクションの aliveList (VertexShaderで使用)
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[4].DescriptorTable.pDescriptorRanges = descriptorRangeAliveList;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeAliveList);

    // ★ t3: 生存数カウンタ (VertexShaderで使用)
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[5].DescriptorTable.pDescriptorRanges = descriptorRangeAliveCount;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeAliveCount);

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Smplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてパイナリする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    // パイナリを元に生成
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateGPUParticleGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    // BlendMode = Add
    switch (blendMode) {
    case BlendMode::kNone:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::kNormal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::kAdd:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kSubtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kMultiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::kScreen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./Resources/shaders/Particle/CSParticle/ParticleCS.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./Resources/shaders/Particle/CSParticle/ParticleCS.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateParticleRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;                                                   // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // descriptorRangeForInstancing
    D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
    descriptorRangeForInstancing[0].BaseShaderRegister = 0;                                                   // 0から始まる
    descriptorRangeForInstancing[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // RootParameter作成。複数設定できるので配列。
    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                    // レジスタ番号0とバインド

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;                   // CBVをつかう
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;                            // VertexShaderで使う
    rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;             // Tableの中身の配列を指定
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する数

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;      // DescriptorTableを使う
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;             // Tableの中身の配列を指定
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

    descriptionRootSignature.pParameters = rootParameters;             // ルートパラメータ配列へのポインタ
    descriptionRootSignature.NumParameters = _countof(rootParameters); // 配列の長さ

    // Smplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてパイナリする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    // パイナリを元に生成
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateParticleGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    // BlendMode = Add
    switch (blendMode) {
    case BlendMode::kNone:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::kNormal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::kAdd:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kSubtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kMultiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::kScreen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./Resources/shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./Resources/shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}


Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateSpriteRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[2] = {};
    // StructuredBuffer用（頂点シェーダーのt0）
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Texture用（ピクセルシェーダーのt1）
    descriptorRange[1].BaseShaderRegister = 1;
    descriptorRange[1].NumDescriptors = 1;
    descriptorRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // Material CBV (b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // StructuredBuffer SRV (t0) - 頂点シェーダー用
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRange[0];
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    // Texture SRV (t1) - ピクセルシェーダー用
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRange[1];
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Smplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてパイナリする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateSpriteGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;

    switch (blendMode) {
    case BlendMode::kNone:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::kNormal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::kAdd:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kSubtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kMultiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::kScreen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Sprite/Sprite.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Sprite/Sprite.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateRenderRootSignature(ShaderMode shaderMode) {
    // シェーダーモードに応じて適切なルートシグネチャ作成メソッドを呼び出す
    switch (shaderMode) {
    case ShaderMode::kNone:
        return CreateBaseRootSignature();
    case ShaderMode::kGray:
        return CreateGrayRootSignature();
    case ShaderMode::kVigneet:
        return CreateVignetteRootSignature();
    case ShaderMode::kSmooth:
        return CreateSmoothRootSignature();
    case ShaderMode::kGauss:
        return CreateGaussRootSignature();
    case ShaderMode::kOutLine:
        return CreateOutLineRootSignature();
    case ShaderMode::kDepth:
        return CreateDepthRootSignature();
    case ShaderMode::kBlur:
        return CreateBlurRootSignature();
    case ShaderMode::kCinematic:
        return CreateCinematicRootSignature();
    case ShaderMode::kDissolve:
        return CreateDissolveRootSignature();
    case ShaderMode::kRandom:
        return CreateRandomRootSignature();
    case ShaderMode::kFocusLine:
        return CreateFocusLineRootSignature();
    case ShaderMode::kPixelate:
        return CreatePixelateRootSignature();
    case ShaderMode::kBloom:
        return CreateBloomRootSignature();
    case ShaderMode::kRetro:
        return CreateRetroRootSignature();
    case ShaderMode::kShockwave:
        return CreateShockwaveRootSignature();
    default:
        return CreateBaseRootSignature();
    }
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateRenderGraphicsPipeLine(
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, ShaderMode shaderMode) {

    // シェーダーモードに応じて適切なパイプライン作成メソッドを呼び出す
    switch (shaderMode) {
    case ShaderMode::kNone:
        return CreateNoneGraphicsPipeLine(rootSignature);
    case ShaderMode::kGray:
        return CreateGrayGraphicsPipeLine(rootSignature);
    case ShaderMode::kVigneet:
        return CreateVigneetGraphicsPipeLine(rootSignature);
    case ShaderMode::kSmooth:
        return CreateSmoothGraphicsPipeLine(rootSignature);
    case ShaderMode::kGauss:
        return CreateGaussGraphicsPipeLine(rootSignature);
    case ShaderMode::kOutLine:
        return CreateOutLineGraphicsPipeLine(rootSignature);
    case ShaderMode::kDepth:
        return CreateDepthGraphicsPipeLine(rootSignature);
    case ShaderMode::kBlur:
        return CreateBlurGraphicsPipeLine(rootSignature);
    case ShaderMode::kCinematic:
        return CreateCinematicGraphicsPipeLine(rootSignature);
    case ShaderMode::kDissolve:
        return CreateDissolveGraphicsPipeLine(rootSignature);
    case ShaderMode::kRandom:
        return CreateRandomGraphicsPipeLine(rootSignature);
    case ShaderMode::kFocusLine:
        return CreateFocusLineGraphicsPipeLine(rootSignature);
    case ShaderMode::kPixelate:
        return CreatePixelateGraphicsPipeLine(rootSignature);
    case ShaderMode::kBloom:
        return CreateBloomGraphicsPipeLine(rootSignature);
    case ShaderMode::kRetro:
        return CreateRetroGraphicsPipeLine(rootSignature);
    case ShaderMode::kShockwave:
        return CreateShockwaveGraphicsPipeLine(rootSignature);
    default:
        return CreateNoneGraphicsPipeLine(rootSignature);
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateSkinningRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;
    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange - テクスチャ用（t0）
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};

    // 通常の2Dテクスチャ t0
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].BaseShaderRegister = 0; // t0
    descriptorRange[0].RegisterSpace = 0;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE skyBoxDescriptorRange[1] = {};

    // SkyBox用のテクスチャt1
    skyBoxDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    skyBoxDescriptorRange[0].NumDescriptors = 1;
    skyBoxDescriptorRange[0].BaseShaderRegister = 1; // t1
    skyBoxDescriptorRange[0].RegisterSpace = 0;
    skyBoxDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // descriptorRangeForInstancing（スキニング用）
    D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
    descriptorRangeForInstancing[0].BaseShaderRegister = 0;                                                   // t0から始まる（VertexShaderのStructuredBuffer用）
    descriptorRangeForInstancing[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // RootParameter作成。複数設定できるので配列。
    D3D12_ROOT_PARAMETER rootParameters[9] = {};

    // rootParameters[0]: Material (b0) - PixelShader
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // rootParameters[1]: TransformationMatrix (b0) - VertexShader
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // rootParameters[2]: テクスチャ用DescriptorTable (t0, t1) - PixelShader
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // 2つのテクスチャ

    // rootParameters[3]: DirectionalLight (b1) - PixelShader
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    // rootParameters[4]: Camera (b2) - PixelShader
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].Descriptor.ShaderRegister = 2;

    // rootParameters[5]: PointLight (b3) - PixelShader
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[5].Descriptor.ShaderRegister = 3;

    // rootParameters[6]: SpotLight (b4) - PixelShader
    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[6].Descriptor.ShaderRegister = 4;

    rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[7].DescriptorTable.pDescriptorRanges = skyBoxDescriptorRange;
    rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(skyBoxDescriptorRange);

    // rootParameters[8]: スキニング用StructuredBuffer (t0) - VertexShader
    rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[8].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
    rootParameters[8].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

    // シャドウマップ用 DescriptorRange (t2 PIXEL)
    D3D12_DESCRIPTOR_RANGE skinShadowDescriptorRange[1] = {};
    skinShadowDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    skinShadowDescriptorRange[0].NumDescriptors = 1;
    skinShadowDescriptorRange[0].BaseShaderRegister = 2; // t2
    skinShadowDescriptorRange[0].RegisterSpace = 0;
    skinShadowDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // スキン+シャドウ用に11要素のrootParametersを構築する
    D3D12_ROOT_PARAMETER rootParameters11[11] = {};
    for (int _i = 0; _i < 9; ++_i) rootParameters11[_i] = rootParameters[_i];
    // rootParameters[9]: シャドウマップ SRV (t2) - PIXEL
    rootParameters11[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters11[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters11[9].DescriptorTable.pDescriptorRanges = skinShadowDescriptorRange;
    rootParameters11[9].DescriptorTable.NumDescriptorRanges = _countof(skinShadowDescriptorRange);
    // rootParameters[10]: ShadowData CBV (b5) - PIXEL
    rootParameters11[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters11[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters11[10].Descriptor.ShaderRegister = 5;

    descriptionRootSignature.pParameters = rootParameters11;
    descriptionRootSignature.NumParameters = _countof(rootParameters11);

    // Samplerの設定（通常 + シャドウ比較）
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1; // s1
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリにする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateSkinningGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[3].SemanticName = "WEIGHT";
    inputElementDescs[3].SemanticIndex = 0;
    inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[3].InputSlot = 1; // 一番目のslotのVBVのことだと伝える
    inputElementDescs[4].SemanticName = "INDEX";
    inputElementDescs[4].SemanticIndex = 0;
    inputElementDescs[4].Format = DXGI_FORMAT_R32G32B32A32_SINT;
    inputElementDescs[4].InputSlot = 1;
    inputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Skinning/Skinning.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));

    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateLine3dRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;
    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;                                                   // 0から始まる
    descriptorRange[0].NumDescriptors = 1;                                                       // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // RootParameter作成。複数設定できるので配列。
    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0とバインド
    descriptionRootSignature.pParameters = rootParameters;               // ルートパラメータ配列へのポインタ
    descriptionRootSignature.NumParameters = _countof(rootParameters);   // 配列の長さ

    // Smplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
    staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてパイナリする
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateLine3dGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "COLOR";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // ResiterzerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Line/Line3d.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Line/Line3d.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                                    vertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

void PipeLineManager::CreateSkyboxPipelines() {
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateSkyboxRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kSkybox, ShaderMode::kNone)] = rootSignature;

    // パイプラインを作成し、マップに格納
    auto pipeline = CreateSkyboxGraphicsPipeLine(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::kSkybox, BlendMode::kNormal, ShaderMode::kNone)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateSkyboxRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // RootSignature作成
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter作成
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // 0番目：VertexShader用 CBV b0
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // 1番目：PixelShader用 CBV b0
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 1;

    // 2番目：PixelShader用 SRV テーブル t0
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    // 【修正】NumParametersを設定
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Samplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリ化
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));

    // リソースを解放
    if (signatureBlob)
        signatureBlob->Release();
    if (errorBlob)
        errorBlob->Release();

    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateSkyboxGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].InputSlot = 0;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDescs[0].InstanceDataStepRate = 0;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStateの設定
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // RasterizerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // Shaderをコンパイル
    IDxcBlob *vertexShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Skybox/Skybox.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = dxCommon_->CompileShader(L"./resources/shaders/Skybox/Skybox.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // DepthStencilStateの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // スカイボックスは深度書き込みしない
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // パイプラインステート作成
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
    graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
    graphicsPipelineStateDesc.BlendState = blendDesc;
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // 【修正】スカイボックスは三角形で描画
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // パイプラインステート作成
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateBaseRootSignature() {
    return CreateCommonRootSignature(false);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateGrayRootSignature() {
    return CreateBaseRootSignature();
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateVignetteRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateSmoothRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateGaussRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateOutLineRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateDepthRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // RootSignatureの設定
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRangeの設定（SRV用: gTexture, gDepthTexture）
    D3D12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
    descriptorRanges[0].BaseShaderRegister = 0; // gTexture用 (t0)
    descriptorRanges[0].NumDescriptors = 1;     // 1つのSRV
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    descriptorRanges[1].BaseShaderRegister = 1; // gDepthTexture用 (t1)
    descriptorRanges[1].NumDescriptors = 1;     // 1つのSRV
    descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameterの設定
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // DescriptorTable (SRV用)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[0].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    // ConstantBuffer用 (gMaterial)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 0; // b0

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRanges[1];
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Samplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};

    // サンプラー: gSampler (s0)
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // サンプラー: gSamplerPoint (s1)
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1; // s1
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // RootSignatureのシリアライズと作成
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));

    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateBlurRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateCinematicRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateDissolveRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // RootSignatureの設定
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // DescriptorRangeの設定（SRV用: gTexture, gDepthTexture）
    D3D12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
    descriptorRanges[0].BaseShaderRegister = 0; // gTexture用 (t0)
    descriptorRanges[0].NumDescriptors = 1;     // 1つのSRV
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    descriptorRanges[1].BaseShaderRegister = 1; // gDepthTexture用 (t1)
    descriptorRanges[1].NumDescriptors = 1;     // 1つのSRV
    descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameterの設定
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // DescriptorTable (SRV用)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[0].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    // ConstantBuffer用 (gMaterial)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 0; // b0

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
    rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRanges[1];
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // Samplerの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};

    // サンプラー: gSampler (s0)
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // RootSignatureのシリアライズと作成
    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));

    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateRandomRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateFocusLineRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreatePixelateRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateBloomRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateRetroRootSignature() {
    return CreateCommonRootSignature(true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateShockwaveRootSignature() {
    // 専用RootSig: t0(srcRT), t1(flareTex), b0(cbuffer)
    D3D12_DESCRIPTOR_RANGE rangeSrc{};
    rangeSrc.BaseShaderRegister = 0;
    rangeSrc.NumDescriptors = 1;
    rangeSrc.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeSrc.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeFlare{};
    rangeFlare.BaseShaderRegister = 1;
    rangeFlare.NumDescriptors = 1;
    rangeFlare.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeFlare.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].DescriptorTable.pDescriptorRanges = &rangeSrc;
    params[0].DescriptorTable.NumDescriptorRanges = 1;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.pDescriptorRanges = &rangeFlare;
    params[1].DescriptorTable.NumDescriptorRanges = 1;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = 3;
    desc.pParameters = params;

    auto sampler = CreateCommonSamplerDesc();
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errBlob->GetBufferPointer()));
        assert(false);
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig));
    assert(SUCCEEDED(hr));
    return rootSig;
}

// ========== シャドウマップパイプライン ==========

void PipeLineManager::CreateShadowMapPipelines() {
    auto rootSignature = CreateShadowMapRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::kShadowMap, ShaderMode::kNone)] = rootSignature;

    auto pipeline = CreateShadowMapGraphicsPipeLine(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::kShadowMap, BlendMode::kNormal, ShaderMode::kNone)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipeLineManager::CreateShadowMapRootSignature() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility           = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister  = 0; // b0

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags          = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters  = _countof(rootParameters);
    desc.pParameters    = rootParameters;

    ID3DBlob *sigBlob = nullptr, *errBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        Logger::Log(reinterpret_cast<char *>(errBlob->GetBufferPointer()));
        assert(false);
    }
    hr = dxCommon_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateShadowMapGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName         = "POSITION";
    inputElementDescs[0].SemanticIndex        = 0;
    inputElementDescs[0].Format               = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName         = "TEXCOORD";
    inputElementDescs[1].SemanticIndex        = 0;
    inputElementDescs[1].Format               = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName         = "NORMAL";
    inputElementDescs[2].SemanticIndex        = 0;
    inputElementDescs[2].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements        = _countof(inputElementDescs);

    IDxcBlob *vs = dxCommon_->CompileShader(L"./Resources/shaders/Shadow/ShadowMap.VS.hlsl", L"vs_6_0");
    assert(vs != nullptr);

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode              = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode              = D3D12_CULL_MODE_BACK;
    rasterizerDesc.DepthClipEnable       = TRUE;
    rasterizerDesc.DepthBias             = 100;
    rasterizerDesc.DepthBiasClamp        = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias  = 1.0f;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable    = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature        = rootSignature.Get();
    desc.InputLayout           = inputLayoutDesc;
    desc.VS                    = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0; // カラー書き込みなし
    desc.RasterizerState       = rasterizerDesc;
    desc.DepthStencilState     = depthStencilDesc;
    desc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    desc.NumRenderTargets      = 0; // RTVなし（深度のみ）
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count      = 1;
    desc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));
    return pipelineState;
}

// ========== ポストエフェクト ==========

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateNoneGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/CopyImage.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateGrayGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/GrayScale.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateVigneetGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Vignette.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateSmoothGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/BoxFilter.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateGaussGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/GaussianFilter.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateOutLineGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/LuminanceBasedOutline.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateDepthGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(true);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/DepthBasedOutline.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateBlurGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/RadialBlur.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateCinematicGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Cinematic.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateDissolveGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Dissolve.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateRandomGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Random.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateFocusLineGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/FocusLine.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreatePixelateGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Pixelate.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateBloomGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Bloom.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateRetroGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Retro.PS.hlsl", rootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipeLineManager::CreateShockwaveGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature) {
    SettingDepthStencilDesc(false);
    return CreateFullScreenPostEffectPipeline(L"./resources/shaders/OffScreen/Shockwave.PS.hlsl", rootSignature);
}

} // namespace Hagine
