#include "ObjColor.h"

namespace Hagine {
void ObjColor::Initialize()
{

    pDxCommon_ = DirectXCommon::GetInstance();

    color_ = {1.0f, 1.0f, 1.0f, 1.0f};
    CreateConstBuffer();
    Map();
    TransferMatrix();
}

void ObjColor::TransferMatrix()
{
    if (pConstMap_)
    {
        pConstMap_->color_ = color_; // 定数バッファに色を転送
    }
}

void ObjColor::SetGraphicCommand(UINT rootParameterIndex) const
{
    pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(rootParameterIndex, constBuffer_->GetGPUVirtualAddress());
}

void ObjColor::CreateConstBuffer()
{
    const UINT bufferSize = sizeof(ConstBufferDataObjColor);
    constBuffer_ = pDxCommon_->CreateBufferResource(bufferSize);
}

void ObjColor::Map()
{
    // 定数バッファのマッピングを行う
    HRESULT hr = constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstMap_));
    if (FAILED(hr))
    {
        // エラーハンドリング
        throw std::runtime_error("Failed to map constant buffer.");
    }
}
} // namespace Hagine
