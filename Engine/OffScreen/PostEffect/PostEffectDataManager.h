#pragma once
#include "Data/DataHandler.h"
#include "PostEffectChain.h"
#include <memory>
#include <string>

/// @brief スロットベースのエフェクトチェーンのセーブ/ロードを担当
namespace Hagine {
class PostEffectDataManager {
  public:
    /// @brief エフェクトチェーンとDirectXCommonへのポインタを受け取り初期化する
    void Initialize(PostEffectChain *chain, DirectXCommon *dxCommon);

    /// @brief 指定ファイル名でエフェクトチェーンの状態を保存する
    void SaveData(const std::string &fileName) const;

    /// @brief 指定ファイル名からエフェクトチェーンの状態を復元する
    void LoadData(const std::string &fileName);

  private:
    PostEffectChain *chain_ = nullptr;
    DirectXCommon *dxCommon_ = nullptr;
};
} // namespace Hagine
