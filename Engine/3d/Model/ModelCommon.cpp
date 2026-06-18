#include "ModelCommon.h"

namespace Hagine {
void ModelCommon::Finalize() {
    dxCommon_ = nullptr;
}

void ModelCommon::Initialize()
{
	// 引数で受け取ってメンバ変数に記録する
	dxCommon_ = DirectXCommon::GetInstance();
}
} // namespace Hagine
