#include "ColliderBase.h"
#include "collider/CollisionManager.h"
#include <algorithm>
#include <vector>
#ifdef USE_IMGUI
#include "utility/debug/imgui/DebugUIHelper.h"
#endif // USE_IMGUI

namespace Hagine {
ColliderBase::~ColliderBase()
{
    CollisionManager::GetInstance()->Unregister(this);
}

void ColliderBase::SetTag(const std::string &tag)
{
    if (!ColliderTagManager::GetInstance()->HasTag(tag))
    {
        return; // タグが存在しない場合は何もしない
    }

    std::string oldTag = tag_;
    tag_ = tag;

    // タグが変更され、かつ既に登録されている場合は再登録
    if (oldTag != tag && isRegistered_)
    {
        CollisionManager::GetInstance()->UpdateColliderTag(this, oldTag, tag);
    }
}

void ColliderBase::ApplyLoadedTag(const std::string &tag)
{
    if (tag_ == tag)
    {
        return;
    }

    const std::string oldTag = tag_;
    tag_ = tag;

    // 登録済みなら CollisionManager 側のタグ別リストも張り替える。
    // これをしないと、読み込み直したタグと登録先が食い違って判定されなくなる
    if (isRegistered_)
    {
        CollisionManager::GetInstance()->UpdateColliderTag(this, oldTag, tag);
    }
}

void ColliderBase::SaveToJson()
{
    if (!dataHandler_)
    {
        dataHandler_ = std::make_unique<DataHandler>("Collider", name_);
    }

    // 「誰のどの形状か」をファイル名だけに頼らず中身にも持たせる。
    // BaseObject はこれを見て保存済みコライダーを組み立て直す
    dataHandler_->Save("name", name_);
    dataHandler_->Save("objectName", ownerName_);
    dataHandler_->Save("type", static_cast<int>(GetType()));

    dataHandler_->Save("isVisible", isVisible_);
    dataHandler_->Save("isEnabled", isEnabled_);
    dataHandler_->Save("collideWithAll", collideWithAll_);
    dataHandler_->Save("tag", tag_);
    dataHandler_->Save("color", color_);

    // 衝突マスクを配列として保存
    std::vector<std::string> maskList(collisionMask_.begin(), collisionMask_.end());
    dataHandler_->Save("collisionMask", maskList);

    // 形状ごとの値
    SaveShapeToJson(*dataHandler_);

    // 保存した時点でファイルに残す（デストラクタ任せにしない）
    dataHandler_->Flush();
}

void ColliderBase::LoadFromJson()
{
    if (!dataHandler_)
    {
        dataHandler_ = std::make_unique<DataHandler>("Collider", name_);
    }

    ownerName_ = dataHandler_->Load<std::string>("objectName", ownerName_);
    isVisible_ = dataHandler_->Load<bool>("isVisible", isVisible_);
    isEnabled_ = dataHandler_->Load<bool>("isEnabled", isEnabled_);
    collideWithAll_ = dataHandler_->Load<bool>("collideWithAll", collideWithAll_);
    ApplyLoadedTag(dataHandler_->Load<std::string>("tag", tag_));
    color_ = dataHandler_->Load<Vector4>("color", color_);

    // 衝突マスクを配列から読み込み。
    // 保存されていない場合は、呼び出し側がコードで入れたマスクを消さずに残す
    if (dataHandler_->Contains("collisionMask"))
    {
        auto maskList = dataHandler_->Load<std::vector<std::string>>("collisionMask", std::vector<std::string>());
        collisionMask_.clear();
        for (const auto &mask : maskList)
        {
            AddCollisionMask(mask);
        }
    }

    // 形状ごとの値
    LoadShapeFromJson(*dataHandler_);
}

nlohmann::json ColliderBase::CaptureState() const
{
    nlohmann::json state = nlohmann::json::object();
    state["type"] = static_cast<int>(GetType());
    state["tag"] = tag_;
    state["isEnabled"] = isEnabled_;
    state["isVisible"] = isVisible_;
    state["collideWithAll"] = collideWithAll_;
    state["color"] = {color_.x, color_.y, color_.z, color_.w};

    // collisionMask_ は unordered_set なので並びが実行ごとに変わる。
    // Undo トラッカーは毎フレーム JSON 同士を比べているので、
    // 並べてから積まないと「何も変えていないのに差分あり」になってしまう
    std::vector<std::string> maskList(collisionMask_.begin(), collisionMask_.end());
    std::sort(maskList.begin(), maskList.end());
    state["collisionMask"] = maskList;

    CaptureShape(state);
    return state;
}

void ColliderBase::RestoreState(const nlohmann::json &state)
{
    if (!state.is_object())
    {
        return;
    }

    // SetTag と違い未登録のタグでも捨てない（タグ登録より先に戻される場合があるため）
    ApplyLoadedTag(state.value("tag", tag_));
    isEnabled_ = state.value("isEnabled", isEnabled_);
    isVisible_ = state.value("isVisible", isVisible_);
    collideWithAll_ = state.value("collideWithAll", collideWithAll_);
    if (state.contains("color") && state["color"].is_array() && state["color"].size() == 4)
    {
        const nlohmann::json &color = state["color"];
        color_ = {color[0].get<float>(), color[1].get<float>(), color[2].get<float>(), color[3].get<float>()};
    }

    if (state.contains("collisionMask") && state["collisionMask"].is_array())
    {
        collisionMask_.clear();
        for (const nlohmann::json &mask : state["collisionMask"])
        {
            if (mask.is_string())
            {
                AddCollisionMask(mask.get<std::string>());
            }
        }
    }

    ApplyShape(state);
}

#ifdef USE_IMGUI
void ColliderBase::ImGuiTagSettings()
{
    // タグ・マスクを無視して全コライダーと判定する（押し戻し検証用）
    ImGui::Checkbox("全コライダーと判定（タグ無視）", &collideWithAll_);
    ImGui::SetItemTooltip("タグ/マスク設定に関係なく、全てのコライダーと衝突判定する。タグ設定ミスの切り分け用");
    ImGui::Spacing();

    // タグ選択
    // ラベルは枠付きウィジェット（コンボ）と同じ高さに揃えないと、文字だけ上へずれる
    const float tagLabelX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("タグ:");
    ImGui::SameLine(tagLabelX + LabelColumnWidth());

    auto &allTags = ColliderTagManager::GetInstance()->GetAllTags();
    std::vector<std::string> tagList(allTags.begin(), allTags.end());
    std::sort(tagList.begin(), tagList.end());

    if (ImGui::BeginCombo("##Tag", tag_.c_str()))
    {
        for (const auto &tag : tagList)
        {
            bool isSelected = (tag_ == tag);
            if (ImGui::Selectable(tag.c_str(), isSelected))
            {
                SetTag(tag);
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    // 衝突マスク設定
    ImGui::Text("衝突判定対象:");
    ImGui::Separator();

    for (const auto &tag : tagList)
    {
        if (tag == "None")
            continue;

        bool isInMask = collisionMask_.find(tag) != collisionMask_.end();
        if (ImGui::Checkbox(tag.c_str(), &isInMask))
        {
            if (isInMask)
            {
                AddCollisionMask(tag);
            }
            else
            {
                RemoveCollisionMask(tag);
            }
        }
    }

    if (!collisionMask_.empty())
    {
        ImGui::Spacing();
        if (ImGui::Button("マスクをクリア", ImVec2(150, 0)))
        {
            ClearCollisionMask();
        }
    }
}
#endif // USE_IMGUI
} // namespace Hagine
