#include "ObjColor.h"

namespace Hagine {
void ObjColor::Initialize()
{

	dxCommon_ = DirectXCommon::GetInstance();

	color_ = { 1.0f,1.0f,1.0f,1.0f };
	CreateConstBuffer();
	Map();
	TransferMatrix();
}

void ObjColor::TransferMatrix()
{
	if (constMap_) {
		constMap_->color_ = color_; // 定数バッファに色を転送
	}
}

void ObjColor::SetGraphicCommand(UINT rootParameterIndex) const
{
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(rootParameterIndex, constBuffer_->GetGPUVirtualAddress());
}

void ObjColor::CreateConstBuffer()
{
	const UINT bufferSize = sizeof(ConstBufferDataObjColor);
	constBuffer_ = dxCommon_->CreateBufferResource(bufferSize);
}

void ObjColor::Map()
{
	// 定数バッファのマッピングを行う
	HRESULT hr = constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constMap_));
	if (FAILED(hr)) {
		// エラーハンドリング
		throw std::runtime_error("Failed to map constant buffer.");
	}
}
} // namespace Hagine
