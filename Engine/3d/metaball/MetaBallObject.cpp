#define NOMINMAX
#include "MetaBallObject.h"
#include "MetaBallGroupManager.h"
#include "MyMath.h"
#include "browser/ShowFolder.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#include <cmath>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {

MetaBallObject::~MetaBallObject()
{
    MetaBallGroupManager::GetInstance()->Unregister(this);
}

void MetaBallObject::Init(const std::string objectName)
{
    BaseObject::Init(objectName);

    // このオブジェクト自身はメッシュを描かない。融合した表面はグループが描く。
    // ただし選択やギズモのために Object3d は要るので、空の動的モデルを持たせておく
    obj3d_->CreateDynamicModel("debug/uvChecker.png");
    isModelDraw_ = false;
    modelPath_ = kMetaBallModelTag;

    // 弾として 1 個ずつ飛ばす使い方を想定して、既定は球 1 個だけ
    elements_.clear();
    MetaBallElement ball{};
    ball.position = {0.0f, 0.0f, 0.0f};
    ball.radius = 1.0f;
    ball.stiffness = 1.0f;
    elements_.push_back(ball);
    selectedElement_ = 0;

    MetaBallGroupManager::GetInstance()->Register(this);
}

void MetaBallObject::AppendWorldElements(std::vector<MetaBallElement> &out) const
{
    const Matrix4x4 world = transform_->matWorld_;

    // ワールド行列に入っているスケールを取り出す。半径は一様スケールとして扱う
    const Vector3 basisX{world.m[0][0], world.m[0][1], world.m[0][2]};
    const Vector3 basisY{world.m[1][0], world.m[1][1], world.m[1][2]};
    const Vector3 basisZ{world.m[2][0], world.m[2][1], world.m[2][2]};
    const float scale = std::max({basisX.Length(), basisY.Length(), basisZ.Length(), 1e-6f});

    for (const MetaBallElement &local : elements_)
    {
        if (!local.enabled || local.radius <= 0.0f)
        {
            continue;
        }
        MetaBallElement world_{};
        world_.position = Transformation(local.position, world);
        world_.shape = local.shape;
        world_.radius = local.radius * scale;
        world_.stiffness = local.stiffness;
        world_.negative = local.negative;
        // 軸は向きなので平行移動を掛けない
        world_.axis = TransformNormal(local.axis, world);
        world_.enabled = true;
        out.push_back(world_);
    }
}

size_t MetaBallObject::AddElement(const MetaBallElement &element)
{
    elements_.push_back(element);
    return elements_.size() - 1;
}

size_t MetaBallObject::AddBall(const Vector3 &localPosition, float radius)
{
    MetaBallElement element{};
    element.position = localPosition;
    element.radius = radius;
    element.stiffness = 1.0f;
    return AddElement(element);
}

void MetaBallObject::RemoveElement(size_t index)
{
    if (index >= elements_.size())
    {
        return;
    }
    elements_.erase(elements_.begin() + static_cast<ptrdiff_t>(index));
    if (selectedElement_ >= static_cast<int>(elements_.size()))
    {
        selectedElement_ = static_cast<int>(elements_.size()) - 1;
    }
}

void MetaBallObject::ClearElements()
{
    elements_.clear();
    selectedElement_ = 0;
}

void MetaBallObject::SetGroupName(const std::string &groupName)
{
    if (groupName_ == groupName)
    {
        return;
    }
    const std::string oldName = groupName_;
    groupName_ = groupName.empty() ? kMetaBallDefaultGroup : groupName;
    MetaBallGroupManager::GetInstance()->Rebind(this, oldName);
}

void MetaBallObject::SetRadius(float radius)
{
    for (MetaBallElement &e : elements_)
    {
        e.radius = radius;
    }
}

void MetaBallObject::CopyPropertiesFrom(const BaseObject &source)
{
    BaseObject::CopyPropertiesFrom(source);

    const MetaBallObject *other = dynamic_cast<const MetaBallObject *>(&source);
    if (!other)
    {
        return;
    }
    elements_ = other->elements_;
    selectedElement_ = other->selectedElement_;
    // グループを写す（同じグループなら複製した瞬間から元とくっつく）
    SetGroupName(other->groupName_);
}

void MetaBallObject::SetStiffness(float stiffness)
{
    for (MetaBallElement &e : elements_)
    {
        e.stiffness = stiffness;
    }
}

// ============================================================
//  シリアライズ（メッシュではなく要素リストを保存する）
// ============================================================

void MetaBallObject::SaveToJson()
{
    BaseObject::SaveToJson();
    // 基底は modelName を Object3d から取り直す。動的モデルにはファイルパスが無く
    // 空になってしまうので、読み込み側が種類を判別できるよう目印で上書きする
    modelPath_ = kMetaBallModelTag;
    objectData_->Save<std::string>("modelName", modelPath_);
    SaveMetaBallToJson();
}

void MetaBallObject::SceneSaveToJson()
{
    BaseObject::SceneSaveToJson();
    modelPath_ = kMetaBallModelTag;
    objectData_->Save<std::string>("modelName", modelPath_);
    SaveMetaBallToJson();
}

void MetaBallObject::LoadFromJson()
{
    BaseObject::LoadFromJson();
    LoadMetaBallFromJson();
}

void MetaBallObject::SaveMetaBallToJson()
{
    DataHandler data("MetaBallDatas", objectName_);
    data.Save<std::string>("groupName", groupName_);
    data.Save<int>("elementCount", static_cast<int>(elements_.size()));

    for (size_t i = 0; i < elements_.size(); ++i)
    {
        const std::string prefix = "element" + std::to_string(i) + "_";
        const MetaBallElement &e = elements_[i];
        data.Save<Vector3>(prefix + "position", e.position);
        data.Save<int>(prefix + "shape", static_cast<int>(e.shape));
        data.Save<float>(prefix + "radius", e.radius);
        data.Save<float>(prefix + "stiffness", e.stiffness);
        data.Save<bool>(prefix + "negative", e.negative);
        data.Save<Vector3>(prefix + "axis", e.axis);
        data.Save<bool>(prefix + "enabled", e.enabled);
    }

    // グループ設定はグループ名をキーに別ファイルへ（メンバー全員で共有するため）
    const MetaBallGroupSettings &settings = MetaBallGroupManager::GetInstance()->GetSettings(groupName_);
    DataHandler groupData("MetaBallGroups", groupName_);
    groupData.Save<float>("voxelSize", settings.voxelSize);
    groupData.Save<float>("threshold", settings.threshold);
    groupData.Save<float>("uvScale", settings.uvScale);
    groupData.Save<std::string>("texturePath", settings.texturePath);
}

void MetaBallObject::LoadMetaBallFromJson()
{
    DataHandler data("MetaBallDatas", objectName_);
    const int count = data.Load<int>("elementCount", 0);
    SetGroupName(data.Load<std::string>("groupName", kMetaBallDefaultGroup));

    // グループ設定を戻す
    {
        MetaBallGroupSettings &settings = MetaBallGroupManager::GetInstance()->GetSettings(groupName_);
        DataHandler groupData("MetaBallGroups", groupName_);
        settings.voxelSize = groupData.Load<float>("voxelSize", settings.voxelSize);
        settings.threshold = groupData.Load<float>("threshold", settings.threshold);
        settings.uvScale = groupData.Load<float>("uvScale", settings.uvScale);
        settings.texturePath = groupData.Load<std::string>("texturePath", settings.texturePath);
        MetaBallGroupManager::GetInstance()->ApplyTexture(groupName_);
        MetaBallGroupManager::GetInstance()->MarkDirty(groupName_);
    }

    if (count <= 0)
    {
        return;
    }

    elements_.clear();
    elements_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const std::string prefix = "element" + std::to_string(i) + "_";
        MetaBallElement e{};
        e.position = data.Load<Vector3>(prefix + "position", {0.0f, 0.0f, 0.0f});
        e.shape = static_cast<MetaBallShape>(data.Load<int>(prefix + "shape", 0));
        e.radius = data.Load<float>(prefix + "radius", 1.0f);
        e.stiffness = data.Load<float>(prefix + "stiffness", 1.0f);
        e.negative = data.Load<bool>(prefix + "negative", false);
        e.axis = data.Load<Vector3>(prefix + "axis", {0.0f, 0.0f, 0.0f});
        e.enabled = data.Load<bool>(prefix + "enabled", true);
        elements_.push_back(e);
    }
    selectedElement_ = 0;
}

// ============================================================
//  インスペクタ UI
// ============================================================

void MetaBallObject::DrawImGuiExtension()
{
#ifdef USE_IMGUI
    if (!ThemedHeader("メタボール##hdr", DebugTheme::kAccentPurple, true))
    {
        return;
    }
    ImGui::Indent(6.0f);

    MetaBallGroupManager *manager = MetaBallGroupManager::GetInstance();
    MetaBallGroupSettings &settings = manager->GetSettings(groupName_);

    // ---- 大きさ（一番よく触るので最初に置く）-----------------------------
    SectionHeader("[ 大きさ ]", DebugTheme::kAccentPurple);

    float radius = elements_.empty() ? 1.0f : elements_[0].radius;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##mbradius", &radius, 0.01f, 0.01f, 100.0f, "半径 %.2f"))
    {
        SetRadius(radius);
    }
    ImGui::SetItemTooltip("全要素の影響半径をまとめて変える。\n"
                          "この距離で密度が 0 になるので、大きいほど遠くの相手とくっつく。\n"
                          "トランスフォームのスケールでも変えられる（掛け算される）");

    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("スケールを掛けた実効半径: %.2f", radius * (std::max)({transform_->scale_.x, transform_->scale_.y, transform_->scale_.z}));
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ---- グループ -------------------------------------------------------
    SectionHeader("[ グループ ]", DebugTheme::kAccentPurple);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("同じグループ名のメタボール同士が融合します。");
    ImGui::PopStyleColor();

    char groupBuffer[128];
    strncpy_s(groupBuffer, groupName_.c_str(), _TRUNCATE);
    if (ImGui::InputText("グループ名", groupBuffer, IM_ARRAYSIZE(groupBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        SetGroupName(groupBuffer);
    }
    ImGui::SetItemTooltip("Enter で確定。名前を分けると別々の塊として扱われます");
    ImGui::Text("このグループのオブジェクト数: %d",
                static_cast<int>(manager->GetMemberCount(groupName_)));

    ImGui::Spacing();

    // ---- 見た目（グループ共通）-------------------------------------------
    SectionHeader("[ 見た目（グループ共通）]", DebugTheme::kAccentPurple);

    bool settingsChanged = false;

    // テクスチャ。融合した表面はグループが 1 枚のメッシュとして描くので、
    // マテリアルもグループ単位になる（BaseObject のマテリアル欄はこのオブジェクトの
    // 空モデルに対するものなので効かない）
    ImGui::Text("テクスチャ: %s", settings.texturePath.empty() ? "未設定" : settings.texturePath.c_str());
    if (ImGui::TreeNode("テクスチャを選ぶ##metaballTex"))
    {
        std::string picked = settings.texturePath;
        ImGui::BeginChild("MetaBallTexSelector", ImVec2(0, 180), true);
        ShowTextureFile(picked, "metaballTex");
        ImGui::EndChild();
        if (!picked.empty() && picked != settings.texturePath)
        {
            settings.texturePath = picked;
            manager->ApplyTexture(groupName_);
        }
        ImGui::TreePop();
    }

    if (ImGui::DragFloat("セルの大きさ", &settings.voxelSize, 0.005f, 0.01f, 5.0f, "%.3f"))
    {
        settingsChanged = true;
    }
    ImGui::SetItemTooltip("小さいほど滑らかで重い。半径の 1/6 〜 1/10 くらいが目安");

    if (ImGui::DragFloat("しきい値", &settings.threshold, 0.01f, 0.01f, 5.0f))
    {
        settingsChanged = true;
    }
    ImGui::SetItemTooltip("小さくすると太って遠くでもくっつく。大きくすると痩せる");

    if (ImGui::DragFloat("UV スケール", &settings.uvScale, 0.01f, 0.01f, 10.0f))
    {
        settingsChanged = true;
    }
    if (ImGui::Checkbox("描画する", &settings.enabled))
    {
        settingsChanged = true;
    }
    if (settingsChanged)
    {
        manager->MarkDirty(groupName_);
    }

    ImGui::Spacing();

    // ---- 統計 -----------------------------------------------------------
    SectionHeader("[ 生成結果（グループ全体）]", DebugTheme::kAccentPurple);
    const MetaBallBuildStats &stats = manager->GetStats(groupName_);
    ImGui::Text("要素 %u / かたまり %u", stats.elementCount, stats.clusterCount);
    ImGui::Text("頂点 %u / 三角形 %u", stats.vertexCount, stats.triangleCount);
    const bool slow = stats.buildMilliseconds > 8.0f;
    ImGui::TextColored(slow ? DebugTheme::kAccentOrange : DebugTheme::kAccentGreen,
                       "生成時間 %.2f ms%s", stats.buildMilliseconds,
                       slow ? "  (セルを大きくするか数を減らしてください)" : "");

    ImGui::Spacing();

    // ---- 要素（複数を組み合わせたいときだけ）-----------------------------
    if (ImGui::TreeNode("要素を細かく編集##metaballElements"))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("弾として使うなら球 1 個のままで構いません。");
        ImGui::PopStyleColor();

        if (ImGui::Button("球を追加"))
        {
            Vector3 position{};
            if (!elements_.empty())
            {
                position = elements_[elements_.size() - 1].position + Vector3{1.0f, 0.0f, 0.0f};
            }
            selectedElement_ = static_cast<int>(AddBall(position, radius));
        }
        ImGui::SameLine();
        if (ImGui::Button("カプセルを追加"))
        {
            MetaBallElement element{};
            element.shape = MetaBallShape::Capsule;
            element.axis = {1.0f, 0.0f, 0.0f};
            element.radius = radius;
            element.stiffness = 1.0f;
            selectedElement_ = static_cast<int>(AddElement(element));
        }
        ImGui::SameLine();
        if (ImGui::Button("全消去") && !elements_.empty())
        {
            ClearElements();
        }

        if (ImGui::BeginListBox("##metaballElementList",
                                ImVec2(-FLT_MIN, 5.0f * ImGui::GetTextLineHeightWithSpacing())))
        {
            for (int i = 0; i < static_cast<int>(elements_.size()); ++i)
            {
                const MetaBallElement &e = elements_[static_cast<size_t>(i)];
                char label[128];
                std::snprintf(label, sizeof(label), "%d: %s%s  r=%.2f##elem%d", i,
                              (e.shape == MetaBallShape::Capsule) ? "カプセル" : "球",
                              e.negative ? " (負)" : "", e.radius, i);
                if (ImGui::Selectable(label, selectedElement_ == i))
                {
                    selectedElement_ = i;
                }
            }
            ImGui::EndListBox();
        }

        if (selectedElement_ >= 0 && selectedElement_ < static_cast<int>(elements_.size()))
        {
            MetaBallElement &e = elements_[static_cast<size_t>(selectedElement_)];
            SectionHeader("[ 選択中の要素 ]", DebugTheme::kAccentPurple);

            ImGui::DragFloat3("位置（ローカル）", &e.position.x, 0.01f);
            ImGui::DragFloat("影響半径", &e.radius, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("強さ", &e.stiffness, 0.01f, 0.01f, 10.0f);

            int shape = static_cast<int>(e.shape);
            if (ImGui::Combo("形状", &shape, "球\0カプセル\0"))
            {
                e.shape = static_cast<MetaBallShape>(shape);
            }
            if (e.shape == MetaBallShape::Capsule)
            {
                ImGui::DragFloat3("軸（中心から端まで）", &e.axis.x, 0.01f);
            }

            ImGui::Checkbox("負の要素", &e.negative);
            ImGui::SetItemTooltip("他の要素をへこませる");
            ImGui::SameLine();
            ImGui::Checkbox("有効", &e.enabled);

            if (ImGui::Button("この要素を削除"))
            {
                RemoveElement(static_cast<size_t>(selectedElement_));
            }
        }
        ImGui::TreePop();
    }

    ImGui::Unindent(6.0f);
    ImGui::Spacing();
#endif // USE_IMGUI
}

} // namespace Hagine
