#pragma once
#include "data/DataHandler.h"
#include "PostEffectChain.h"
#include <memory>
#include <string>

/// @brief スロットベースのエフェクトチェーンのセーブ/ロードを担当
namespace Hagine {
class PostEffectDataManager
{
  public:
    /// @brief エフェクトチェーンとDirectXCommonへのポインタを受け取り初期化する
    void Initialize(PostEffectChain *chain, DirectXCommon *pDxCommon);

    /// @brief 指定ファイル名でエフェクトチェーンの状態を保存する
    void SaveData(const std::string &fileName) const;

    /// @brief 指定ファイル名からエフェクトチェーンの状態を復元する
    void LoadData(const std::string &fileName);

  private:
    PostEffectChain *pChain_ = nullptr;
    DirectXCommon *pDxCommon_ = nullptr;
};
} // namespace Hagine
