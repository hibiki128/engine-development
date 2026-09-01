#include "ComputeEffectPipeline.h"
#include "DirectXCommon.h"
#include <asset/AssetPath.h>
#include <cassert>
#include <debug/log/Logger.h>
#include <string/StringUtility.h>

namespace Hagine {

void ComputeEffectPipeline::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;
}

void ComputeEffectPipeline::Finalize()
{
    programs_.clear();
    pDxCommon_ = nullptr;
}

const ComputeEffectProgram *ComputeEffectPipeline::Get(
    const std::string &shaderFileName,
    const std::vector<ShaderRootSignature::SamplerPreset> &samplerPresets)
{
    // 生成済みならそれを返す（毎フレーム作り直さないためのキャッシュ）
    auto it = programs_.find(shaderFileName);
    if (it != programs_.end())
    {
        return it->second.IsValid() ? &it->second : nullptr;
    }

    ComputeEffectProgram program;
    if (!Build(shaderFileName, samplerPresets, program))
    {
        // 失敗したことも記録しておき、毎フレーム再試行してログが溢れるのを防ぐ
        programs_.emplace(shaderFileName, ComputeEffectProgram{});
        return nullptr;
    }

    auto [inserted, ok] = programs_.emplace(shaderFileName, std::move(program));
    return &inserted->second;
}

bool ComputeEffectPipeline::Build(const std::string &shaderFileName,
                                  const std::vector<ShaderRootSignature::SamplerPreset> &samplerPresets,
                                  ComputeEffectProgram &outProgram)
{
    if (!pDxCommon_)
    {
        Logger::Error("ComputeEffectPipeline: Initialize が呼ばれていません");
        return false;
    }

    const std::wstring fullPath = StringUtility::ConvertString(AssetPath::Shader(shaderFileName));

    // コンパイルとリフレクション取得を同時に行う
    ID3D12ShaderReflection *pReflection = nullptr;
    IDxcBlob *pShaderBlob = pDxCommon_->CompileShaderWithReflection(fullPath, L"cs_6_0", &pReflection);
    if (!pShaderBlob || !pReflection)
    {
        Logger::Error("ComputeEffectPipeline: コンパイルに失敗: " + shaderFileName);
        if (pShaderBlob)
        {
            pShaderBlob->Release();
        }
        if (pReflection)
        {
            pReflection->Release();
        }
        return false;
    }

    // ルートシグネチャはシェーダーの宣言から自動生成する
    if (!outProgram.rootSignature.BuildFromReflection(pDxCommon_, pReflection, samplerPresets, shaderFileName))
    {
        pShaderBlob->Release();
        pReflection->Release();
        return false;
    }

    // numthreads をリフレクションから取得しておく。
    // ディスパッチ数の計算にこの値を使えば、シェーダー側を変えてもC++側の修正が要らない。
    pReflection->GetThreadGroupSize(&outProgram.threadGroupSizeX,
                                    &outProgram.threadGroupSizeY,
                                    &outProgram.threadGroupSizeZ);

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = outProgram.rootSignature.Get();
    desc.CS.pShaderBytecode = pShaderBlob->GetBufferPointer();
    desc.CS.BytecodeLength = pShaderBlob->GetBufferSize();

    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&outProgram.pipelineState));

    pShaderBlob->Release();
    pReflection->Release();

    if (FAILED(hr))
    {
        Logger::Error("ComputeEffectPipeline: PSO生成に失敗: " + shaderFileName);
        return false;
    }

    Logger::Info("ComputeEffectPipeline: 生成しました: " + shaderFileName +
                 " (SRV=" + std::to_string(outProgram.rootSignature.GetSrvCount()) +
                 ", UAV=" + std::to_string(outProgram.rootSignature.GetUavCount()) +
                 ", threads=" + std::to_string(outProgram.threadGroupSizeX) + "x" +
                 std::to_string(outProgram.threadGroupSizeY) + ")");
    return true;
}
} // namespace Hagine
