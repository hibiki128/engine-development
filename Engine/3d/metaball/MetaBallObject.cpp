#define NOMINMAX
#include "MetaBallObject.h"
#include "graphics/model/ModelManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#ifdef USE_IMGUI
#include <ImGuizmo.h>
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {

MetaBallObject::~MetaBallObject()
{
    // 動的モデルはこのオブジェクト専用なので、一緒に片付ける
    if (obj3d_ && !obj3d_->GetDynamicModelKey().empty())
    {
        ModelManager::GetInstance()->RemoveModel(obj3d_->GetDynamicModelKey());
    }
}

void MetaBallObject::Init(const std::string objectName)
{
    BaseObject::Init(objectName);

    obj3d_->CreateDynamicModel("debug/uvChecker.png");
    modelPath_ = kMetaBallModelTag;

    // 何も見えないと分かりづらいので、球を 2 つ置いた状態から始める
    elements_.clear();
    MetaBallElement a{};
    a.position = {-0.6f, 0.0f, 0.0f};
    a.radius = 1.5f;
    a.stiffness = 1.0f;
    elements_.push_back(a);
    MetaBallElement b{};
    b.position = {0.6f, 0.0f, 0.0f};
    b.radius = 1.5f;
    b.stiffness = 1.0f;
    elements_.push_back(b);

    selectedElement_ = 0;
    isDirty_ = true;
}

void MetaBallObject::Update()
{
    BaseObject::Update();

    // isInteracting_ は ImGui を出しているときだけ毎フレーム更新される。
    // ゲーム側から MarkDirty(true) で明示された場合は、そのフレームだけ粗く作る
    const bool interacting = isInteracting_ || interactiveHint_;

    if (isDirty_)
    {
        // ドラッグ中は粗く作って 60fps を守り、手を離してから本解像度で作り直す
        const uint32_t resolution = interacting ? editResolution_ : params_.resolution;
        RebuildMesh(resolution);
        builtAtEditResolution_ = interacting;
        isDirty_ = false;
    }
    else if (builtAtEditResolution_ && !interacting)
    {
        // ドラッグが終わったので本解像度で作り直す
        RebuildMesh(params_.resolution);
        builtAtEditResolution_ = false;
    }

    // ヒントは 1 フレーム限り。立てっぱなしで粗いまま固定されるのを防ぐ
    interactiveHint_ = false;
}

void MetaBallObject::RebuildMesh(uint32_t resolution)
{
    MetaBallBuildParams params = params_;
    params.resolution = resolution;
    MeshData data = MetaBallBuilder::Build(elements_, params, &stats_);
    obj3d_->RebuildDynamicMesh(std::move(data));
}

size_t MetaBallObject::AddElement(const MetaBallElement &element)
{
    elements_.push_back(element);
    MarkDirty();
    return elements_.size() - 1;
}

size_t MetaBallObject::AddBall(const Vector3 &position, float radius)
{
    MetaBallElement element{};
    element.position = position;
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
    MarkDirty();
}

void MetaBallObject::ClearElements()
{
    elements_.clear();
    selectedElement_ = 0;
    MarkDirty();
}

void MetaBallObject::MarkDirty(bool interactive)
{
    isDirty_ = true;
    // 次の Update で 1 回だけ効くヒント。isInteracting_ を直接触ると、
    // ImGui を出していないビルドで立ったまま戻らなくなる
    interactiveHint_ = interactive;
}

void MetaBallObject::SetResolution(uint32_t resolution)
{
    params_.resolution = std::clamp(resolution, 4u, 192u);
    MarkDirty();
}

void MetaBallObject::SetEditResolution(uint32_t resolution)
{
    editResolution_ = std::clamp(resolution, 4u, 192u);
}

void MetaBallObject::SetThreshold(float threshold)
{
    params_.threshold = threshold;
    MarkDirty();
}

void MetaBallObject::SetUvScale(float scale)
{
    params_.uvScale = scale;
    MarkDirty();
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
    data.Save<int>("elementCount", static_cast<int>(elements_.size()));
    data.Save<int>("resolution", static_cast<int>(params_.resolution));
    data.Save<int>("editResolution", static_cast<int>(editResolution_));
    data.Save<float>("threshold", params_.threshold);
    data.Save<float>("uvScale", params_.uvScale);

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
}

void MetaBallObject::LoadMetaBallFromJson()
{
    DataHandler data("MetaBallDatas", objectName_);
    const int count = data.Load<int>("elementCount", 0);
    if (count <= 0)
    {
        return;
    }

    params_.resolution = static_cast<uint32_t>(data.Load<int>("resolution", 32));
    editResolution_ = static_cast<uint32_t>(data.Load<int>("editResolution", 20));
    params_.threshold = data.Load<float>("threshold", 0.5f);
    params_.uvScale = data.Load<float>("uvScale", 1.0f);

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
    MarkDirty();
}

// ============================================================
//  インスペクタ UI
// ============================================================

void MetaBallObject::DrawImGui()
{
#ifdef USE_IMGUI
    BaseObject::DrawImGui();

    if (!ImGui::CollapsingHeader("メタボール", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 畳んでいる間もドラッグ状態は落としておく
        isInteracting_ = false;
        return;
    }

    bool changed = false;

    // ---- 生成パラメータ ----------------------------------------------
    ImGui::SeparatorText("生成");

    int resolution = static_cast<int>(params_.resolution);
    if (ImGui::SliderInt("解像度", &resolution, 8, 128))
    {
        params_.resolution = static_cast<uint32_t>(resolution);
        changed = true;
    }
    ImGui::SetItemTooltip("一番長い辺の分割数。手を離したときに使う本解像度");

    int editResolution = static_cast<int>(editResolution_);
    if (ImGui::SliderInt("ドラッグ中の解像度", &editResolution, 4, 64))
    {
        editResolution_ = static_cast<uint32_t>(editResolution);
    }
    ImGui::SetItemTooltip("ドラッグしている間だけ使う粗い解像度。60fps を守るためのもの");

    if (ImGui::DragFloat("しきい値", &params_.threshold, 0.01f, 0.01f, 5.0f))
    {
        changed = true;
    }
    ImGui::SetItemTooltip("等値面の高さ。大きくすると痩せ、小さくすると太る");

    if (ImGui::DragFloat("UV スケール", &params_.uvScale, 0.01f, 0.01f, 10.0f))
    {
        changed = true;
    }

    // ---- 統計 ---------------------------------------------------------
    ImGui::SeparatorText("生成結果");
    ImGui::Text("頂点 %u / 三角形 %u", stats_.vertexCount, stats_.triangleCount);
    ImGui::Text("格子 %u x %u x %u  (セル %.4f)", stats_.gridX, stats_.gridY, stats_.gridZ,
                stats_.cellSize);
    const bool slow = stats_.buildMilliseconds > 8.0f;
    ImGui::TextColored(slow ? ImVec4(1.0f, 0.5f, 0.3f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                       "生成時間 %.2f ms%s", stats_.buildMilliseconds,
                       slow ? "  (解像度を下げるか要素を減らしてください)" : "");

    // ---- 要素リスト ---------------------------------------------------
    ImGui::SeparatorText("要素");

    if (ImGui::Button("球を追加"))
    {
        Vector3 position{};
        if (!elements_.empty())
        {
            position = elements_[elements_.size() - 1].position + Vector3{1.0f, 0.0f, 0.0f};
        }
        selectedElement_ = static_cast<int>(AddBall(position, 1.5f));
    }
    ImGui::SameLine();
    if (ImGui::Button("カプセルを追加"))
    {
        MetaBallElement element{};
        element.shape = MetaBallShape::Capsule;
        element.axis = {1.0f, 0.0f, 0.0f};
        element.radius = 1.0f;
        element.stiffness = 1.0f;
        if (!elements_.empty())
        {
            element.position = elements_[elements_.size() - 1].position + Vector3{0.0f, 1.5f, 0.0f};
        }
        selectedElement_ = static_cast<int>(AddElement(element));
    }
    ImGui::SameLine();
    if (ImGui::Button("全消去") && !elements_.empty())
    {
        ClearElements();
    }

    if (ImGui::BeginListBox("##metaballElements", ImVec2(-FLT_MIN, 6.0f * ImGui::GetTextLineHeightWithSpacing())))
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

    // ---- 選択中の要素 -------------------------------------------------
    if (selectedElement_ >= 0 && selectedElement_ < static_cast<int>(elements_.size()))
    {
        MetaBallElement &e = elements_[static_cast<size_t>(selectedElement_)];
        ImGui::SeparatorText("選択中の要素");

        if (ImGui::DragFloat3("位置", &e.position.x, 0.01f))
        {
            changed = true;
        }
        if (ImGui::DragFloat("影響半径", &e.radius, 0.01f, 0.01f, 100.0f))
        {
            changed = true;
        }
        ImGui::SetItemTooltip("この距離で密度がちょうど 0 になる");

        if (ImGui::DragFloat("強さ", &e.stiffness, 0.01f, 0.01f, 10.0f))
        {
            changed = true;
        }

        int shape = static_cast<int>(e.shape);
        if (ImGui::Combo("形状", &shape, "球\0カプセル\0"))
        {
            e.shape = static_cast<MetaBallShape>(shape);
            changed = true;
        }
        if (e.shape == MetaBallShape::Capsule)
        {
            if (ImGui::DragFloat3("軸（中心から端まで）", &e.axis.x, 0.01f))
            {
                changed = true;
            }
        }

        if (ImGui::Checkbox("負の要素", &e.negative))
        {
            changed = true;
        }
        ImGui::SetItemTooltip("他の要素をへこませる");
        ImGui::SameLine();
        if (ImGui::Checkbox("有効", &e.enabled))
        {
            changed = true;
        }

        if (ImGui::Button("この要素を削除"))
        {
            RemoveElement(static_cast<size_t>(selectedElement_));
        }
    }

    // ---- 保存・読み込み -----------------------------------------------
    ImGui::SeparatorText("保存");
    if (ImGui::Button("メタボールを保存"))
    {
        SaveMetaBallToJson();
    }
    ImGui::SameLine();
    if (ImGui::Button("メタボールを読み込み"))
    {
        LoadMetaBallFromJson();
    }

    // ドラッグ中かどうかを毎フレーム更新する。
    // これが立っている間は低解像度で作り直し、離れた時に本解像度へ戻す
    isInteracting_ = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();

    if (changed)
    {
        isDirty_ = true;
    }
#endif // USE_IMGUI
}

} // namespace Hagine
