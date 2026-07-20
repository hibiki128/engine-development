#include "ModelCommon.h"

namespace Hagine {
void ModelCommon::Finalize()
{
    pDxCommon_ = nullptr;
}

void ModelCommon::Initialize()
{
    // 引数で受け取ってメンバ変数に記録する
    pDxCommon_ = DirectXCommon::GetInstance();
}
} // namespace Hagine
