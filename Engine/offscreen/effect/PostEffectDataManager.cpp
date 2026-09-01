#include "PostEffectDataManager.h"
#include <utility/debug/imgui/ImGuiNotification.h>

#include "../utility/debug/imgui/ImGuiNotification.h"

namespace Hagine {
void PostEffectDataManager::Initialize(PostEffectChain *chain, DirectXCommon *pDxCommon)
{
    pChain_ = chain;
    pDxCommon_ = pDxCommon;
}

void PostEffectDataManager::SaveData(const std::string &fileName) const
{
    // 呼び出しごとに指定ファイル名でDataHandlerを生成する
    auto dataHandler = std::make_unique<DataHandler>("OffScreen", fileName);

    const auto &slots = pChain_->GetSlots();

    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
    {
        const std::string prefix = "slot" + std::to_string(i) + "_";
        const auto &slot = slots[i];

        dataHandler->Save<bool>(prefix + "occupied", slot.occupied);

        if (!slot.occupied)
        {
            continue;
        }

        dataHandler->Save<bool>(prefix + "enabled", slot.enabled);
        dataHandler->Save<std::string>(prefix + "name", slot.name);
        dataHandler->Save<int>(prefix + "shaderMode", static_cast<int>(slot.params->GetMode()));

        // エフェクト固有パラメータをプレフィックス付きで保存
        slot.params->Save(dataHandler.get(), prefix + "param_");
    }
    dataHandler->Flush();
    ImGuiNotification::Post("ポストエフェクトデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void PostEffectDataManager::LoadData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("OffScreen", fileName);

    pChain_->Clear();

    // 1件以上のスロットが正常に読み込まれたかのフラグ
    bool anyLoaded = false;

    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
    {
        const std::string prefix = "slot" + std::to_string(i) + "_";

        const bool occupied = dataHandler->Load<bool>(prefix + "occupied", false);
        if (!occupied)
        {
            continue; // 未使用スロットはスキップ
        }

        const bool enabled = dataHandler->Load<bool>(prefix + "enabled", true);
        const std::string name = dataHandler->Load<std::string>(prefix + "name", "");
        const ShaderMode mode = static_cast<ShaderMode>(
            dataHandler->Load<int>(prefix + "shaderMode", 0));

        const int resultSlot = pChain_->AddEffect(mode, name, pDxCommon_, i);

        if (resultSlot == -1)
        {
            continue;
        }

        pChain_->SetEnabled(resultSlot, enabled);

        IPostEffectParams *params = pChain_->GetParams(resultSlot);
        if (params)
        {
            params->Load(dataHandler.get(), prefix + "param_");
        }

        anyLoaded = true;
    }

    // 読み込み結果をループ後に1度だけ通知する
    if (anyLoaded)
    {
        ImGuiNotification::Post("ポストエフェクトデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
    }
    else
    {
        ImGuiNotification::Post("パスが見つかりません: " + fileName, {0.8f, 0.2f, 0.2f, 1.0f});
    }
}
} // namespace Hagine
