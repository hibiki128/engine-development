#include "ShaderRootSignature.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <cassert>
#include <debug/log/Logger.h>

namespace Hagine {
namespace {

/// プリセットから D3D12_STATIC_SAMPLER_DESC を作る
D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(ShaderRootSignature::SamplerPreset preset, UINT shaderRegister)
{
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.ShaderRegister = shaderRegister;
    sampler.RegisterSpace = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    // どのステージから使われるか分からないので、可視性は絞らず全ステージから見えるようにしておく
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    switch (preset)
    {
    case ShaderRootSignature::SamplerPreset::PointClamp:
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        break;
    case ShaderRootSignature::SamplerPreset::LinearWrap:
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        break;
    case ShaderRootSignature::SamplerPreset::ShadowComparison:
        // SamplerComparisonState 用。シャドウマップの外側は「影なし」として扱いたいので
        // 境界色を白にしておく
        sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        break;
    case ShaderRootSignature::SamplerPreset::LinearClamp:
    default:
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        break;
    }
    return sampler;
}

/// 使用しているコンポーネント数と型から入力要素のフォーマットを求める
DXGI_FORMAT MakeInputFormat(BYTE mask, D3D_REGISTER_COMPONENT_TYPE componentType)
{
    // mask は使用している成分のビット（x=1, y=2, z=4, w=8）。上位の未使用分は落ちるので、
    // 立っているビットの数がそのまま成分数になる
    int componentCount = 0;
    for (int bit = 0; bit < 4; ++bit)
    {
        if (mask & (1 << bit))
        {
            ++componentCount;
        }
    }

    switch (componentType)
    {
    case D3D_REGISTER_COMPONENT_UINT32:
        switch (componentCount)
        {
        case 1: return DXGI_FORMAT_R32_UINT;
        case 2: return DXGI_FORMAT_R32G32_UINT;
        case 3: return DXGI_FORMAT_R32G32B32_UINT;
        default: return DXGI_FORMAT_R32G32B32A32_UINT;
        }
    case D3D_REGISTER_COMPONENT_SINT32:
        switch (componentCount)
        {
        case 1: return DXGI_FORMAT_R32_SINT;
        case 2: return DXGI_FORMAT_R32G32_SINT;
        case 3: return DXGI_FORMAT_R32G32B32_SINT;
        default: return DXGI_FORMAT_R32G32B32A32_SINT;
        }
    case D3D_REGISTER_COMPONENT_FLOAT32:
    default:
        switch (componentCount)
        {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }
}

} // namespace

bool ShaderRootSignature::Build(DirectXCommon *pDxCommon,
                                const std::vector<StageReflection> &stages,
                                const BuildOptions &options,
                                const std::string &debugName)
{
    if (!pDxCommon || stages.empty())
    {
        Logger::Error("ShaderRootSignature: 引数が不正です (" + debugName + ")");
        return false;
    }

    // ---- 各ステージのリフレクションを走査して、使われているリソースを集める ----
    // separateResourcesByStage が false なら (種別, レジスタ) で1件にまとめ、可視性を広げる。
    // true なら (種別, レジスタ, 可視性) ごとに別のリソースとして扱う。
    struct Collected
    {
        D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        UINT shaderRegister = 0;
        UINT rangeLength = 1; // 配列宣言（BindCount > 1）のぶん
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
    };
    std::vector<Collected> collected;
    UINT samplerCount = 0;

    auto addResource = [&](D3D12_DESCRIPTOR_RANGE_TYPE type, UINT reg, UINT count,
                           D3D12_SHADER_VISIBILITY visibility) {
        auto it = std::find_if(collected.begin(), collected.end(), [&](const Collected &c) {
            if (c.type != type || c.shaderRegister != reg)
            {
                return false;
            }
            return options.separateResourcesByStage ? (c.visibility == visibility) : true;
        });
        if (it != collected.end())
        {
            it->rangeLength = (std::max)(it->rangeLength, count);
            if (!options.separateResourcesByStage && it->visibility != visibility)
            {
                // 複数ステージから同じものを見ているので全ステージ可視にする
                it->visibility = D3D12_SHADER_VISIBILITY_ALL;
            }
            return;
        }
        collected.push_back({type, reg, count, visibility});
    };

    for (const StageReflection &stage : stages)
    {
        if (!stage.pReflection)
        {
            continue;
        }
        D3D12_SHADER_DESC shaderDesc{};
        if (FAILED(stage.pReflection->GetDesc(&shaderDesc)))
        {
            Logger::Error("ShaderRootSignature: シェーダー情報の取得に失敗 (" + debugName + ")");
            return false;
        }

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
            if (FAILED(stage.pReflection->GetResourceBindingDesc(i, &bindDesc)))
            {
                continue;
            }
            const UINT reg = bindDesc.BindPoint;
            const UINT count = (std::max)(bindDesc.BindCount, 1u);

            switch (bindDesc.Type)
            {
            case D3D_SIT_CBUFFER:
                addResource(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, reg, 1, stage.visibility);
                break;

            case D3D_SIT_TEXTURE:
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
            case D3D_SIT_TBUFFER:
                addResource(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, reg, count, stage.visibility);
                break;

            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                addResource(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, reg, count, stage.visibility);
                break;

            case D3D_SIT_SAMPLER:
                samplerCount = (std::max)(samplerCount, reg + 1);
                break;

            default:
                break;
            }
        }
    }

    // 並びを安定させる（種別 → レジスタ → 可視性の順）。
    // リフレクションの列挙順はコンパイラ任せなので、ここで決め打ちしておかないと
    // シェーダーを触るたびにルートパラメータ番号が動く
    std::stable_sort(collected.begin(), collected.end(), [](const Collected &a, const Collected &b) {
        if (a.type != b.type)
        {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        }
        if (a.shaderRegister != b.shaderRegister)
        {
            return a.shaderRegister < b.shaderRegister;
        }
        return static_cast<int>(a.visibility) < static_cast<int>(b.visibility);
    });

    srvCount_ = 0;
    uavCount_ = 0;
    for (const Collected &c : collected)
    {
        if (c.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
        {
            srvCount_ = (std::max)(srvCount_, c.shaderRegister + c.rangeLength);
        }
        else if (c.type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
        {
            uavCount_ = (std::max)(uavCount_, c.shaderRegister + c.rangeLength);
        }
    }

    // ---- ルートパラメータを組み立てる ----
    bindings_.clear();
    srvTableIndex_ = UINT_MAX;
    uavTableIndex_ = UINT_MAX;

    std::vector<D3D12_ROOT_PARAMETER> rootParameters;
    // レンジは D3D12_ROOT_PARAMETER がポインタで持つため、CreateRootSignature を呼ぶまで
    // 生存させておく必要がある。要素追加で再確保されるとポインタが無効になるので先に確保する
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    ranges.reserve(collected.size() + 2);

    auto isRootDescriptorSrv = [&](const Collected &c) {
        return c.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
               std::find(options.rootDescriptorSrvRegisters.begin(),
                         options.rootDescriptorSrvRegisters.end(),
                         c.shaderRegister) != options.rootDescriptorSrvRegisters.end();
    };

    // 1) b# … ルートCBV（options.rootConstants にあるものは32bit定数）
    for (const Collected &c : collected)
    {
        if (c.type != D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
        {
            continue;
        }
        const auto constantIt = std::find_if(options.rootConstants.begin(), options.rootConstants.end(),
                                             [&](const RootConstantDesc &d) { return d.shaderRegister == c.shaderRegister; });

        D3D12_ROOT_PARAMETER param{};
        param.ShaderVisibility = c.visibility;
        if (constantIt != options.rootConstants.end())
        {
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            param.Constants.ShaderRegister = c.shaderRegister;
            param.Constants.RegisterSpace = 0;
            param.Constants.Num32BitValues = constantIt->num32BitValues;
        }
        else
        {
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.ShaderRegister = c.shaderRegister;
            param.Descriptor.RegisterSpace = 0;
        }
        bindings_.push_back({c.type, c.shaderRegister, c.visibility, static_cast<UINT>(rootParameters.size())});
        rootParameters.push_back(param);
    }

    // 2) ルートSRV指定のある t#（GPUアドレスで直接差すもの）
    for (const Collected &c : collected)
    {
        if (!isRootDescriptorSrv(c))
        {
            continue;
        }
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        param.ShaderVisibility = c.visibility;
        param.Descriptor.ShaderRegister = c.shaderRegister;
        param.Descriptor.RegisterSpace = 0;
        bindings_.push_back({c.type, c.shaderRegister, c.visibility, static_cast<UINT>(rootParameters.size())});
        rootParameters.push_back(param);
    }

    // 3) t# / u# のデスクリプタテーブル
    auto addTables = [&](D3D12_DESCRIPTOR_RANGE_TYPE rangeType, TableMode mode, UINT &outTableIndex) {
        std::vector<const Collected *> targets;
        for (const Collected &c : collected)
        {
            if (c.type == rangeType && !isRootDescriptorSrv(c))
            {
                targets.push_back(&c);
            }
        }
        if (targets.empty())
        {
            return;
        }

        if (mode == TableMode::Merged)
        {
            // t0..tN を1本のレンジで張る。デスクリプタがヒープ上で連続している前提
            UINT descriptorCount = 0;
            for (const Collected *c : targets)
            {
                descriptorCount = (std::max)(descriptorCount, c->shaderRegister + c->rangeLength);
            }
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = rangeType;
            range.NumDescriptors = descriptorCount;
            range.BaseShaderRegister = 0;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            ranges.push_back(range);

            D3D12_ROOT_PARAMETER param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &ranges.back();
            outTableIndex = static_cast<UINT>(rootParameters.size());
            for (const Collected *c : targets)
            {
                bindings_.push_back({c->type, c->shaderRegister, c->visibility, outTableIndex});
            }
            rootParameters.push_back(param);
            return;
        }

        // レジスタ（と可視性）ごとに1本ずつテーブルを作る
        for (const Collected *c : targets)
        {
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = rangeType;
            range.NumDescriptors = c->rangeLength;
            range.BaseShaderRegister = c->shaderRegister;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            ranges.push_back(range);

            D3D12_ROOT_PARAMETER param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = c->visibility;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &ranges.back();
            bindings_.push_back({c->type, c->shaderRegister, c->visibility, static_cast<UINT>(rootParameters.size())});
            rootParameters.push_back(param);
        }
    };

    addTables(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, options.srvTableMode, srvTableIndex_);
    addTables(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, options.uavTableMode, uavTableIndex_);

    // ---- スタティックサンプラー ----
    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
    staticSamplers.reserve(samplerCount);
    for (UINT i = 0; i < samplerCount; ++i)
    {
        // 直接指定があればそれを優先する（プリセットで表せない組み合わせ用）
        if (i < options.explicitSamplers.size())
        {
            D3D12_STATIC_SAMPLER_DESC sampler = options.explicitSamplers[i];
            sampler.ShaderRegister = i;
            staticSamplers.push_back(sampler);
            continue;
        }
        const SamplerPreset preset =
            (i < options.samplerPresets.size()) ? options.samplerPresets[i] : SamplerPreset::LinearClamp;
        staticSamplers.push_back(MakeStaticSampler(preset, i));
    }

    // ---- 生成 ----
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = static_cast<UINT>(rootParameters.size());
    desc.pParameters = rootParameters.empty() ? nullptr : rootParameters.data();
    desc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    desc.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
    desc.Flags = options.allowInputAssembler ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                             : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.Flags |= options.extraFlags;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Logger::Error("ShaderRootSignature: シリアライズ失敗 (" + debugName + "): " +
                          std::string(static_cast<const char *>(errorBlob->GetBufferPointer())));
        }
        return false;
    }

    hr = pDxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(),
                                                     IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr))
    {
        Logger::Error("ShaderRootSignature: 生成失敗 (" + debugName + ")");
        return false;
    }

    return true;
}

bool ShaderRootSignature::BuildFromReflection(DirectXCommon *pDxCommon,
                                              ID3D12ShaderReflection *pReflection,
                                              const std::vector<SamplerPreset> &samplerPresets,
                                              const std::string &debugName)
{
    BuildOptions options;
    options.allowInputAssembler = false; // コンピュートシェーダーでは入力アセンブラを使わない
    options.srvTableMode = TableMode::Merged;
    options.uavTableMode = TableMode::Merged;
    options.samplerPresets = samplerPresets;

    const std::vector<StageReflection> stages = {{pReflection, D3D12_SHADER_VISIBILITY_ALL}};
    return Build(pDxCommon, stages, options, debugName);
}

UINT ShaderRootSignature::GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT shaderRegister,
                                                D3D12_SHADER_VISIBILITY visibility) const
{
    // まず可視性まで一致するものを探す
    for (const Binding &binding : bindings_)
    {
        if (binding.type == type && binding.shaderRegister == shaderRegister && binding.visibility == visibility)
        {
            return binding.rootIndex;
        }
    }

    // 無ければレジスタだけで探す。ただし候補が複数あるなら、どれを指しているか決められないので諦める
    UINT found = UINT_MAX;
    for (const Binding &binding : bindings_)
    {
        if (binding.type != type || binding.shaderRegister != shaderRegister)
        {
            continue;
        }
        if (found != UINT_MAX && found != binding.rootIndex)
        {
            return UINT_MAX; // 複数ステージに分かれている。可視性を指定して引くこと
        }
        found = binding.rootIndex;
    }
    return found;
}

std::string ShaderRootSignature::DescribeLayout() const
{
    auto typeChar = [](D3D12_DESCRIPTOR_RANGE_TYPE type) -> const char * {
        switch (type)
        {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return "b";
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return "t";
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return "u";
        default: return "?";
        }
    };
    auto visName = [](D3D12_SHADER_VISIBILITY visibility) -> const char * {
        switch (visibility)
        {
        case D3D12_SHADER_VISIBILITY_VERTEX: return "VS";
        case D3D12_SHADER_VISIBILITY_PIXEL: return "PS";
        case D3D12_SHADER_VISIBILITY_ALL: return "ALL";
        default: return "OTHER";
        }
    };

    // ルートパラメータ番号の昇順で並べ直してから文字列にする
    std::vector<const Binding *> sorted;
    sorted.reserve(bindings_.size());
    for (const Binding &binding : bindings_)
    {
        sorted.push_back(&binding);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Binding *a, const Binding *b) { return a->rootIndex < b->rootIndex; });

    std::string result;
    for (const Binding *binding : sorted)
    {
        if (!result.empty())
        {
            result += ' ';
        }
        result += std::to_string(binding->rootIndex);
        result += ':';
        result += typeChar(binding->type);
        result += std::to_string(binding->shaderRegister);
        result += '(';
        result += visName(binding->visibility);
        result += ')';
    }
    return result;
}

bool ShaderInputLayout::BuildFromReflection(ID3D12ShaderReflection *pVsReflection, const std::string &debugName)
{
    elements_.clear();
    semanticNames_.clear();

    if (!pVsReflection)
    {
        Logger::Error("ShaderInputLayout: リフレクションが不正です (" + debugName + ")");
        return false;
    }

    D3D12_SHADER_DESC shaderDesc{};
    if (FAILED(pVsReflection->GetDesc(&shaderDesc)))
    {
        Logger::Error("ShaderInputLayout: シェーダー情報の取得に失敗 (" + debugName + ")");
        return false;
    }

    // 文字列の実体が動かないよう、要素数ぶん先に確保しておく
    semanticNames_.reserve(shaderDesc.InputParameters);
    elements_.reserve(shaderDesc.InputParameters);

    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
        if (FAILED(pVsReflection->GetInputParameterDesc(i, &paramDesc)))
        {
            continue;
        }
        // SV_VertexID / SV_InstanceID などのシステム値は入力アセンブラを通らない
        if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED)
        {
            continue;
        }
        semanticNames_.emplace_back(paramDesc.SemanticName);
    }

    // semanticNames_ の再確保が終わってからポインタを取る
    size_t nameIndex = 0;
    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
        if (FAILED(pVsReflection->GetInputParameterDesc(i, &paramDesc)))
        {
            continue;
        }
        if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED)
        {
            continue;
        }

        D3D12_INPUT_ELEMENT_DESC element{};
        element.SemanticName = semanticNames_[nameIndex++].c_str();
        element.SemanticIndex = paramDesc.SemanticIndex;
        element.Format = MakeInputFormat(paramDesc.Mask, paramDesc.ComponentType);
        element.InputSlot = 0;
        element.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        elements_.push_back(element);
    }

    return true;
}

D3D12_INPUT_LAYOUT_DESC ShaderInputLayout::Get() const
{
    D3D12_INPUT_LAYOUT_DESC desc{};
    desc.pInputElementDescs = elements_.empty() ? nullptr : elements_.data();
    desc.NumElements = static_cast<UINT>(elements_.size());
    return desc;
}
} // namespace Hagine
