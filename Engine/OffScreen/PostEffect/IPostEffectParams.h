#pragma once
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include "DirectXCommon.h"
#include "Graphics/Srv/SrvManager.h"
#include "Data/DataHandler.h"
#include "Graphics/PipeLine/PipeLineManager.h"

/// @brief ポストエフェクトパラメータの基底インターフェース
/// 各エフェクトはこのインターフェースを実装し、自身のパラメータを所有する
namespace Hagine {
class IPostEffectParams {
  public:
    virtual ~IPostEffectParams() = default;

    /// @brief GPUバッファの初期化
    virtual void Initialize(DirectXCommon *dxCommon) = 0;

    /// @brief このパラメータが対応するシェーダーモードを返す
    virtual ShaderMode GetMode() const = 0;

    /// @brief コマンドリストにパラメータをバインドする
    virtual void Apply(ID3D12GraphicsCommandList *commandList,
                       SrvManager *srvManager,
                       DirectXCommon *dxCommon) = 0;

    /// @brief ImGuiによるパラメータ編集UI
    virtual void DrawUI() = 0;

    /// @brief パラメータを保存（prefixでスロット番号を区別）
    virtual void Save(DataHandler *handler, const std::string &prefix) const = 0;

    /// @brief パラメータを読み込み
    virtual void Load(DataHandler *handler, const std::string &prefix) = 0;

    /// @brief 時間更新が必要なエフェクト向け（デフォルトは何もしない）
    virtual void UpdateTime(float /*deltaTime*/) {}
};
} // namespace Hagine
