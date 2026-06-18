#include "ColliderBase.h"
#include"Collider/CollisionManager.h"

namespace Hagine {
ColliderBase::~ColliderBase() {
    CollisionManager::GetInstance()->Unregister(this);
}

void ColliderBase::SetTag(const std::string &tag) {
    if (!ColliderTagManager::GetInstance()->HasTag(tag)) {
        return; // タグが存在しない場合は何もしない
    }

    std::string oldTag = tag_;
    tag_ = tag;

    // タグが変更され、かつ既に登録されている場合は再登録
    if (oldTag != tag && isRegistered_) {
        CollisionManager::GetInstance()->UpdateColliderTag(this, oldTag, tag);
    }
}

void ColliderBase::SaveToJson() {
    if (!dataHandler_) {
        dataHandler_ = std::make_unique<DataHandler>("Collider", name_);
    }

    dataHandler_->Save("isVisible", isVisible_);
    dataHandler_->Save("isEnabled", isEnabled_);
    dataHandler_->Save("collideWithAll", collideWithAll_);
    dataHandler_->Save("tag", tag_);

    // 衝突マスクを配列として保存
    std::vector<std::string> maskList(collisionMask_.begin(), collisionMask_.end());
    dataHandler_->Save("collisionMask", maskList);
}

void ColliderBase::LoadFromJson() {
    if (!dataHandler_) {
        dataHandler_ = std::make_unique<DataHandler>("Collider", name_);
    }

    isVisible_ = dataHandler_->Load<bool>("isVisible", true);
    isEnabled_ = dataHandler_->Load<bool>("isEnabled", true);
    collideWithAll_ = dataHandler_->Load<bool>("collideWithAll", false);
    tag_ = dataHandler_->Load<std::string>("tag", "None");

    // 衝突マスクを配列から読み込み
    auto maskList = dataHandler_->Load<std::vector<std::string>>("collisionMask", std::vector<std::string>());
    collisionMask_.clear();
    for (const auto &mask : maskList) {
        AddCollisionMask(mask);
    }
}

#ifdef _DEBUG
void ColliderBase::ImGuiTagSettings() {
    // タグ・マスクを無視して全コライダーと判定する（押し戻し検証用）
    ImGui::Checkbox("全コライダーと判定（タグ無視）", &collideWithAll_);
    ImGui::SetItemTooltip("タグ/マスク設定に関係なく、全てのコライダーと衝突判定する。タグ設定ミスの切り分け用");
    ImGui::Spacing();

    // タグ選択
    ImGui::Text("タグ:");
    ImGui::SameLine(120);

    auto &allTags = ColliderTagManager::GetInstance()->GetAllTags();
    std::vector<std::string> tagList(allTags.begin(), allTags.end());
    std::sort(tagList.begin(), tagList.end());

    if (ImGui::BeginCombo("##Tag", tag_.c_str())) {
        for (const auto &tag : tagList) {
            bool isSelected = (tag_ == tag);
            if (ImGui::Selectable(tag.c_str(), isSelected)) {
                SetTag(tag);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    // 衝突マスク設定
    ImGui::Text("衝突判定対象:");
    ImGui::Separator();

    for (const auto &tag : tagList) {
        if (tag == "None")
            continue;

        bool isInMask = collisionMask_.find(tag) != collisionMask_.end();
        if (ImGui::Checkbox(tag.c_str(), &isInMask)) {
            if (isInMask) {
                AddCollisionMask(tag);
            } else {
                RemoveCollisionMask(tag);
            }
        }
    }

    if (!collisionMask_.empty()) {
        ImGui::Spacing();
        if (ImGui::Button("マスクをクリア", ImVec2(150, 0))) {
            ClearCollisionMask();
        }
    }
}
#endif // _DEBUG
} // namespace Hagine
