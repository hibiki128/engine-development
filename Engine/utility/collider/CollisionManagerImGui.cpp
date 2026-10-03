#define NOMINMAX
#include "collider/CollisionManager.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/ImGuiNotification.h"
#include "utility/debug/imgui/ImGuizmoManager.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <line/LineRenderer.h>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <string>
#include <vector>
// DebugUIHelper.h / EditWidgets.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/EditWidgets.h"
#include "utility/debug/imgui/SceneProjector.h"

// =======================================================================
// CollisionManager: コライダー窓（一覧・追加・削除・詳細の編集）
//
// 以前はサイズや表示はこの窓、タグ・衝突相手・追加・削除はオブジェクトのインスペクタ、と
// 2か所を行き来しないと設定が終わらなかった。詳細の編集を DrawColliderEditor の1つにまとめ、
// この窓だけで全部できるようにした（インスペクタも同じ関数を呼ぶので中身は常に同じ）。
// =======================================================================

namespace Hagine {
namespace {
/// <summary>コライダー種別を表示用の日本語名に変換する（保存名に使う ColliderTypeName とは別物）</summary>
const char *ColliderTypeDisplayName(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere:
        return "球 (Sphere)";
    case ColliderType::AABB:
        return "AABB";
    case ColliderType::OBB:
        return "OBB";
    case ColliderType::Cylinder:
        return "円柱 (Cylinder)";
    case ColliderType::Mesh:
        return "メッシュ (Mesh)";
    default:
        return "不明";
    }
}

/// <summary>種類ごとのアイコン（一覧で形が一目で分かるように）</summary>
const char *ColliderTypeIcon(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere:
        return ICON_FA_CIRCLE;
    case ColliderType::AABB:
        return ICON_FA_SQUARE;
    case ColliderType::OBB:
        return ICON_FA_CUBE;
    case ColliderType::Cylinder:
        return ICON_FA_DATABASE;
    case ColliderType::Mesh:
        return ICON_FA_DRAW_POLYGON;
    default:
        return ICON_FA_QUESTION;
    }
}

const char *const kTypeFilterNames[] = {"すべての種類", "球", "AABB", "OBB", "円柱", "メッシュ"}; // ColliderType と同じ並び（先頭だけ「すべて」）

/// <summary>追加メニューに出す形（ColliderType と同じ並び）</summary>
constexpr ColliderType kAddableTypes[] = {ColliderType::Sphere, ColliderType::AABB, ColliderType::OBB, ColliderType::Cylinder, ColliderType::Mesh};

std::string Lower(std::string text)
{
    for (char &ch : text)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

/// <summary>
/// コライダーを持っているオブジェクトを探す（エディタで置いた物もゲームが作る物も対象）。
/// 名前だけでなく、実際にそのオブジェクトの持ち物かまで確かめる
/// </summary>
BaseObject *FindOwner(const ColliderBase *pCollider)
{
    if (!pCollider || pCollider->GetOwnerName().empty())
    {
        return nullptr;
    }
    BaseObject *pOwner = BaseObjectManager::GetInstance()->GetObjectByName(pCollider->GetOwnerName());
    if (!pOwner)
    {
        return nullptr;
    }
    for (const auto &owned : pOwner->GetColliders())
    {
        if (owned.get() == pCollider)
        {
            return pOwner;
        }
    }
    return nullptr;
}

/// <summary>シーン（ギズモ）で選んでいるオブジェクトの名前（無ければ空）</summary>
std::string SelectedObjectName()
{
    BaseObjectManager *pManager = BaseObjectManager::GetInstance();
    for (const std::string &name : ImGuizmoManager::GetInstance()->GetSelectedNames())
    {
        if (pManager->GetObjectByName(name))
        {
            return name;
        }
    }
    return std::string();
}

/// <summary>右クリックで既定値へ戻せる3要素の欄（ラベルは左に置く）</summary>
bool Vector3Row(const char *label, const char *id, Vector3 &value, float speed, float min, float max, const ImVec4 &frameBg)
{
    float values[3] = {value.x, value.y, value.z};
    const bool changed = LabeledDrag3(label, id, values, speed, min, max, "%.2f", frameBg);
    const bool reset = FloatNContextMenu((std::string(id) + "Ctx").c_str(), values, 3);
    if (changed || reset)
    {
        value = {values[0], values[1], values[2]};
    }
    return changed || reset;
}
} // namespace

bool CollisionManager::IsRegistered(const ColliderBase *pCollider) const
{
    for (const auto &[tag, colliders] : collidersByTag_)
    {
        for (const ColliderBase *c : colliders)
        {
            if (c == pCollider)
                return true;
        }
    }
    return false;
}

void CollisionManager::MarkEditorShown()
{
    editorShownFrame_ = ImGui::GetFrameCount();
}

ColliderBase *CollisionManager::GetHighlightedCollider() const
{
    // 選んだコライダーの強調は、コライダー窓かインスペクタのコライダー欄を見ている間だけ出す。
    // シーンで物を選ぶたびに（窓を閉じていても）点滅すると、コライダーを触っていないときに邪魔になる。
    // 窓とシーンのどちらを先に描くかでフレームが1つずれるので、前のフレームまで見る
    if (!highlightSelected_ || !pInspectorSelected_ || ImGui::GetFrameCount() - editorShownFrame_ > 1)
    {
        return nullptr;
    }
    return IsRegistered(pInspectorSelected_) ? pInspectorSelected_ : nullptr;
}

void CollisionManager::FollowSceneSelection()
{
    // 破棄・登録解除されたコライダーを選んだままにしない
    if (pInspectorSelected_ && !IsRegistered(pInspectorSelected_))
    {
        pInspectorSelected_ = nullptr;
    }

    // シーンで選んだオブジェクトが変わったら、そのコライダーを選ぶ（窓が閉じていても追従させておくと、
    // 開いたときやインスペクタで見たときに、今選んでいる物のコライダーが選ばれている）
    const std::string sceneSelection = SelectedObjectName();
    if (inspectorFollowSelection_ && sceneSelection != lastSceneSelection_ && !sceneSelection.empty())
    {
        if (BaseObject *pObject = BaseObjectManager::GetInstance()->GetObjectByName(sceneSelection))
        {
            const bool alreadyOwned = pInspectorSelected_ && pInspectorSelected_->GetOwnerName() == sceneSelection;
            if (!alreadyOwned && !pObject->GetColliders().empty())
                pInspectorSelected_ = pObject->GetColliders().front().get();
        }
    }
    lastSceneSelection_ = sceneSelection;
}

void CollisionManager::DrawSelectedOverlay(ImDrawList *pDrawList, const ViewProjection &viewProjection, const ImVec2 &sceneMin,
                                           const ImVec2 &sceneSize)
{
    ColliderBase *pHighlight = GetHighlightedCollider();
    if (!pHighlight || !highlightXRay_ || !pDrawList ||
        !LineRenderer::GetInstance()->IsCategoryShown(LineCategory::Selection))
    {
        return;
    }

    // 形の作り方はコライダー自身の DebugDraw を使い、線だけ取り出して重ねて描く
    static std::vector<LineRenderer::CapturedLine> lines;
    lines.clear();
    LineRenderer *pLine = LineRenderer::GetInstance();
    const bool visible = pHighlight->IsVisible();
    const bool enabled = pHighlight->IsEnabled();
    pHighlight->SetVisible(true);
    pHighlight->SetEnabled(true);
    pHighlight->SetDrawColor(highlightColor_);
    pLine->BeginCapture(&lines);
    pHighlight->DebugDraw(viewProjection);
    pLine->EndCapture();
    pHighlight->SetEnabled(enabled);
    pHighlight->SetVisible(visible);
    if (lines.empty())
    {
        return;
    }

    // 物に隠れていても見えるよう上に重ねる。3Dの線（点滅）と区別できるよう、ゆっくり明滅させて少し透かす
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f);
    const ImVec4 base = ImVec4(highlightColor_.x, highlightColor_.y, highlightColor_.z, 1.0f);
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.35f + 0.25f * pulse));
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, 0.45f + 0.45f * pulse));

    const SceneProjector projector(viewProjection, sceneMin, sceneSize);
    pDrawList->PushClipRect(sceneMin, ImVec2(sceneMin.x + sceneSize.x, sceneMin.y + sceneSize.y), true);
    for (const LineRenderer::CapturedLine &line : lines)
    {
        ImVec2 a, b;
        if (projector.Segment(line.start, line.end, a, b))
        {
            pDrawList->AddLine(a, b, outline, 3.5f);
        }
    }
    for (const LineRenderer::CapturedLine &line : lines)
    {
        ImVec2 a, b;
        if (projector.Segment(line.start, line.end, a, b))
        {
            pDrawList->AddLine(a, b, color, 1.6f);
        }
    }
    pDrawList->PopClipRect();
    pLine->AddExternalLineCount(LineCategory::Selection, static_cast<uint32_t>(lines.size()));
}

void CollisionManager::OpenInEditor(ColliderBase *pCollider)
{
    pInspectorSelected_ = pCollider;
    openEditorRequested_ = true;
}

bool CollisionManager::ConsumeEditorOpenRequest()
{
    const bool requested = openEditorRequested_;
    openEditorRequested_ = false;
    return requested;
}

void CollisionManager::ProcessEditorRequests()
{
    FollowSceneSelection();

    // 削除は一覧やインスペクタを描いている最中に行うと、走査中の配列を壊すので次のフレームの頭で行う
    ColliderBase *pTarget = pPendingDelete_;
    pPendingDelete_ = nullptr;
    if (!pTarget || !IsRegistered(pTarget))
    {
        return;
    }
    BaseObject *pOwner = FindOwner(pTarget);
    if (!pOwner)
    {
        return;
    }
    const std::string name = pTarget->GetName();
    if (pOwner->RemoveCollider(pTarget))
    {
        ImGuiNotification::Post("コライダーを削除しました: " + name, {0.80f, 0.46f, 0.46f, 1.0f});
    }
}

std::vector<ColliderBase *> CollisionManager::GetCollidingPartners(const ColliderBase *pCollider) const
{
    std::vector<ColliderBase *> partners;
    for (const auto &[pair, colliding] : collisionStates_)
    {
        if (!colliding)
        {
            continue;
        }
        if (pair.a == pCollider)
        {
            partners.push_back(pair.b);
        }
        else if (pair.b == pCollider)
        {
            partners.push_back(pair.a);
        }
    }
    return partners;
}

// ---- 詳細（この窓とインスペクタの両方から呼ばれる） ------------------------

void CollisionManager::DrawColliderEditor(ColliderBase *pCollider)
{
    if (!pCollider)
    {
        return;
    }
    ColliderBase *c = pCollider;
    BaseObject *pOwner = FindOwner(c);
    const std::string &name = c->GetName();
    ImGui::PushID(c);

    // ---- 見出し: 形・名前・状態 ----
    const bool colliding = c->IsCollidingInCurrentFrame();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(DebugTheme::kAccentCyan, "%s", ColliderTypeIcon(c->GetType()));
    ImGui::SameLine();
    ImGui::TextUnformatted(name.empty() ? "(名前なし)" : name.c_str());
    ImGui::SameLine();
    StatusBadge(colliding ? "衝突中" : "待機", colliding ? DebugTheme::kAccentRed : DebugTheme::kAccentGreen);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ColliderTypeDisplayName(c->GetType()));

    // 持ち主（押すとシーンでその物を選ぶ）
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("持ち主:");
    ImGui::SameLine();
    if (pOwner)
    {
        ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
        if (ImGui::Button((std::string(ICON_FA_CROSSHAIRS " ") + pOwner->GetName()).c_str()))
        {
            ImGuizmoManager::GetInstance()->SelectOnly(pOwner->GetName());
        }
        ImGui::SetItemTooltip("シーンでこのオブジェクトを選ぶ");
    }
    else
    {
        ImGui::TextUnformatted(c->GetOwnerName().empty() ? "(なし)" : c->GetOwnerName().c_str());
    }

    // 今当たっている相手
    const std::vector<ColliderBase *> partners = GetCollidingPartners(c);
    if (!partners.empty())
    {
        std::string text;
        for (const ColliderBase *partner : partners)
        {
            if (!text.empty())
                text += ", ";
            text += partner->GetName().empty() ? std::string("(名前なし)") : partner->GetName();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
        ImGui::TextWrapped("当たっている相手: %s", text.c_str());
        ImGui::PopStyleColor();
    }

    // ---- 有効・表示・色 ----
    ImGui::Spacing();
    {
        bool enabled = c->IsEnabled();
        bool visible = c->IsVisible();
        // 窓が狭くてもラベルが潰れないよう、入切の2つを1行、色を次の行に置く
        InlineColumns columns(2);
        if (ThemedToggle("当たり判定##cenabled", &enabled, DebugTheme::kAccentGreen))
            c->SetEnabled(enabled);
        columns.Next(1);
        if (ThemedToggle("線で表示##cvisible", &visible, DebugTheme::kAccentBlue))
            c->SetVisible(visible);
        Vector4 color = c->GetColor();
        if (ImGui::ColorEdit4("線の色##ccolor", &color.x, ImGuiColorEditFlags_NoInputs))
            c->SetColor(color);
        if (ImGui::BeginPopupContextItem("##ccolorCtx"))
        {
            if (ImGui::MenuItem("白に戻す"))
                c->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
            ImGui::EndPopup();
        }
        ImGui::SetItemTooltip("保存すると色も残る。右クリックで白に戻す\n当たっている間は「当たっている間」の色、選んでいる間は点滅の色が優先される");
        if (colorByTag_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("（タグの色で塗り分け中）");
        }
    }

    // ---- 形 ----
    ImGui::Spacing();
    SectionHeader("[ 形 ]", DebugTheme::kAccentOrange);
    const ImVec4 shapeBg = DebugTheme::FrameBg(DebugTheme::kAccentOrange);
    switch (c->GetType())
    {
    case ColliderType::OBB: {
        auto *obb = static_cast<OBBCollider *>(c);
        Vector3 size = obb->GetSize();
        if (Vector3Row("サイズ", "##obbSize", size, 0.05f, 0.0f, 1000.0f, shapeBg))
            obb->SetSize(size);
        Vector3 offset = obb->GetPositionOffset();
        if (Vector3Row("位置のずれ", "##obbOffset", offset, 0.05f, 0.0f, 0.0f, shapeBg))
            obb->SetPositionOffSet(offset);
        Vector3 rotation = obb->GetRotationOffset();
        if (Vector3Row("回転のずれ", "##obbRotation", rotation, 0.01f, 0.0f, 0.0f, shapeBg))
            obb->SetRotationOffset(rotation);
        break;
    }
    case ColliderType::AABB: {
        auto *aabb = static_cast<AABBCollider *>(c);
        Vector3 size = aabb->GetSize();
        if (Vector3Row("サイズ", "##aabbSize", size, 0.05f, 0.0f, 1000.0f, shapeBg))
            aabb->SetSize(size);
        Vector3 offset = aabb->GetOffset();
        if (Vector3Row("位置のずれ", "##aabbOffset", offset, 0.05f, 0.0f, 0.0f, shapeBg))
            aabb->SetOffset(offset);
        break;
    }
    case ColliderType::Sphere: {
        auto *sphere = static_cast<SphereCollider *>(c);
        float radius = sphere->GetRadius();
        ImGui::SetNextItemWidth(-1);
        if (EditUI::DragFloat("##sphereRadius", &radius, 0.05f, 0.0f, 1000.0f, "半径 %.2f"))
            sphere->SetRadius(radius);
        Vector3 offset = sphere->GetOffset();
        if (Vector3Row("位置のずれ", "##sphereOffset", offset, 0.05f, 0.0f, 0.0f, shapeBg))
            sphere->SetOffset(offset);
        break;
    }
    case ColliderType::Cylinder: {
        auto *cylinder = static_cast<CylinderCollider *>(c);
        float radius = cylinder->GetRadius();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 2.0f);
        if (EditUI::DragFloat("##cylinderRadius", &radius, 0.05f, 0.0f, 1000.0f, "半径 %.2f"))
            cylinder->SetRadius(radius);
        ImGui::SameLine();
        float height = cylinder->GetHeight();
        ImGui::SetNextItemWidth(-1);
        if (EditUI::DragFloat("##cylinderHeight", &height, 0.05f, 0.0f, 1000.0f, "高さ %.2f"))
            cylinder->SetHeight(height);
        bool inward = cylinder->IsInward();
        if (ImGui::Checkbox("内側に閉じ込める（フィールドの壁）##cylinderInward", &inward))
            cylinder->SetInward(inward);
        break;
    }
    case ColliderType::Mesh: {
        auto *pMesh = static_cast<MeshCollider *>(c);
        ReadOnlyRow("三角形数", "%d", static_cast<int>(pMesh->GetTriangleCount()));
        if (!pMesh->GetSourceModelPath().empty())
            ReadOnlyRow("元のモデル", "%s", pMesh->GetSourceModelPath().c_str());
        bool wire = pMesh->IsWireframeVisible();
        if (ImGui::Checkbox("ワイヤーフレーム表示##meshWire", &wire))
            pMesh->SetWireframeVisible(wire);
        break;
    }
    }
    {
        const Vector3 center = c->GetCenterPosition();
        ImGui::TextDisabled("中心（ワールド）: %.2f, %.2f, %.2f", center.x, center.y, center.z);
    }

    // ---- タグと衝突相手 ----
    ImGui::Spacing();
    SectionHeader("[ タグと当たる相手 ]", DebugTheme::kAccentBlue);
    c->ImGuiTagSettings();

    // ---- 当たったときの動き（持ち主の設定） ----
    if (pOwner)
    {
        ImGui::Spacing();
        SectionHeader("[ 当たったとき ]", DebugTheme::kAccentPurple);
        bool resolve = pOwner->IsResolveCollision();
        if (ThemedToggle("めり込んだら押し出す##cresolve", &resolve, DebugTheme::kAccentPurple))
            pOwner->SetResolveCollision(resolve);
        ImGui::SetItemTooltip("持ち主「%s」の設定（このオブジェクトの全コライダーに効く）。\n"
                              "独自の当たり処理を持つキャラ（ゲーム側でコールバックを付けている物）では使わない",
                              pOwner->GetName().c_str());
    }

    // ---- 保存・読込・削除 ----
    ImGui::Spacing();
    SectionHeader("[ 保存 / 読込 / 削除 ]", DebugTheme::kAccentGreen);
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (ConfirmButton(ICON_FA_SAVE " 保存##csave", ImVec2(buttonWidth, 0.0f)))
    {
        c->SaveToJson();
        ImGuiNotification::Post("コライダーを保存しました: " + (name.empty() ? std::string("(名前なし)") : name), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SetItemTooltip("jsons/Collider/<名前>.json へ保存（シーンに置いた物はシーンの保存でも残る）");
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_FOLDER_OPEN " 読込##cload", ImVec2(buttonWidth, 0.0f)))
    {
        c->LoadFromJson();
        ImGuiNotification::Post("コライダーを読み込みました: " + (name.empty() ? std::string("(名前なし)") : name), {0.42f, 0.66f, 0.68f, 1.0f});
    }
    ImGui::SetItemTooltip("保存済みの設定を読み込み直す");
    ImGui::SameLine();
    ImGui::BeginDisabled(!pOwner);
    if (DangerButton(ICON_FA_TRASH_ALT " 削除##cdelete", ImVec2(buttonWidth, 0.0f)))
    {
        ImGui::OpenPopup("##confirmColliderDelete");
    }
    ImGui::EndDisabled();
    if (!pOwner)
    {
        ImGui::SetItemTooltip("持ち主のオブジェクトが分からないので、ここからは消せません（コードで作ったコライダー）");
    }
    if (ImGui::BeginPopup("##confirmColliderDelete"))
    {
        ImGui::Text("「%s」を削除しますか？", name.empty() ? "(名前なし)" : name.c_str());
        ImGui::TextDisabled("保存ファイル（jsons/Collider）は残ります");
        if (DangerButton("削除する", ImVec2(120.0f, 0.0f)))
        {
            pPendingDelete_ = c;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("やめる", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

// ---- 追加 ---------------------------------------------------------------

void CollisionManager::DrawAddColliderPopup()
{
    if (!ImGui::BeginPopup("##addColliderPopup"))
    {
        return;
    }
    BaseObjectManager *pManager = BaseObjectManager::GetInstance();

    // 付ける先（既定はシーンで選んでいる物）
    ImGui::TextDisabled("付けるオブジェクト");
    std::vector<std::string> names;
    for (const auto &[objectName, pObject] : pManager->GetObjects())
    {
        if (pObject)
            names.push_back(objectName);
    }
    std::sort(names.begin(), names.end());
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##addOwner", addOwnerName_.empty() ? "(選んでください)" : addOwnerName_.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::IsWindowAppearing())
        {
            addOwnerFilter_.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##addOwnerFilter", ICON_FA_SEARCH " 名前で絞る", &addOwnerFilter_);
        const std::string query = Lower(addOwnerFilter_);
        for (const std::string &objectName : names)
        {
            if (!query.empty() && Lower(objectName).find(query) == std::string::npos)
                continue;
            if (ImGui::Selectable(objectName.c_str(), objectName == addOwnerName_))
                addOwnerName_ = objectName;
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    BaseObject *pOwner = addOwnerName_.empty() ? nullptr : pManager->GetObjectByName(addOwnerName_);
    ImGui::BeginDisabled(!pOwner);
    for (ColliderType type : kAddableTypes)
    {
        const std::string label = std::format("{} {}", ColliderTypeIcon(type), ColliderTypeDisplayName(type));
        if (ImGui::MenuItem(label.c_str()))
        {
            if (ColliderBase *pAdded = pOwner->AddColliderForEditor(type))
            {
                pInspectorSelected_ = pAdded;
                ImGuiNotification::Post(std::format("コライダーを追加しました: {}（{}）", pAdded->GetName(), addOwnerName_),
                                        {0.45f, 0.68f, 0.52f, 1.0f});
            }
        }
    }
    ImGui::EndDisabled();
    if (!pOwner)
    {
        ImGui::TextDisabled("先に付けるオブジェクトを選んでください");
    }
    ImGui::EndPopup();
}

// ---- 窓の本体 -----------------------------------------------------------

void CollisionManager::ImGuiColliderInspector()
{
    // ── 全体操作 ──
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    ImGui::Checkbox("コライダーを表示", &isVisible_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("全コライダーのデバッグ描画のオン/オフ（個別はリストで切替）");

    ImGui::SameLine();
    if (ImGui::SmallButton("全部表示"))
    {
        for (auto &[tag, colliders] : collidersByTag_)
            for (auto *c : colliders)
                c->SetVisible(true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("全部非表示"))
    {
        for (auto &[tag, colliders] : collidersByTag_)
            for (auto *c : colliders)
                c->SetVisible(false);
    }
    ImGui::SameLine();
    if (ConfirmButton("全部保存"))
    {
        int saved = 0;
        for (auto &[tag, colliders] : collidersByTag_)
            for (auto *c : colliders)
            {
                c->SaveToJson();
                ++saved;
            }
        ImGuiNotification::Post(std::to_string(saved) + " 個のコライダーを保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SetItemTooltip("全コライダー設定を jsons/Collider/ 以下へ保存");

    // この窓を見ている間は、選んだコライダーをシーンで強調する
    MarkEditorShown();

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
    ImGui::Checkbox("選んだ物をシーンで点滅", &highlightSelected_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("一覧で選んだコライダーを、全体の表示を切っていても点滅させて場所を見せる\n"
                          "（この窓かインスペクタのコライダー欄を開いている間だけ）");
    ImGui::SameLine();
    ImGui::BeginDisabled(!highlightSelected_);
    ImGui::Checkbox("物に隠れても見せる", &highlightXRay_);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("点滅に加えて、シーン窓の上に重ねて描く（モデルの中に埋まったコライダーも見える）");
    ImGui::SameLine();
    ImGui::Checkbox("シーンの選択に合わせる", &inspectorFollowSelection_);
    ImGui::SetItemTooltip("シーンでオブジェクトを選んだら、そのオブジェクトのコライダーを一覧で選ぶ");

    bool colorByTag = colorByTag_;
    if (ImGui::Checkbox("タグの色で塗り分け", &colorByTag))
        SetColorByTag(colorByTag);
    ImGui::SetItemTooltip("タグごとに決まった色で描く（外すと各コライダーの「線の色」に戻る。設定の色は書き換えない）");
    ImGui::SameLine();
    ImGui::ColorEdit4("当たっている間##hitColor", &hitColor_.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
    ImGui::SetItemTooltip("何かと当たっている間に使う色");
    ImGui::SameLine();
    ImGui::ColorEdit4("点滅##highlightColor", &highlightColor_.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
    ImGui::SetItemTooltip("選んだコライダーの点滅の色");

    const std::string sceneSelection = SelectedObjectName();

    // ---- 絞り込み・並べ方 ----
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::InputTextWithHint("##colliderSearch", ICON_FA_SEARCH " 名前・タグ・持ち主", &inspectorSearch_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    int typeIndex = inspectorTypeFilter_ + 1;
    if (ImGui::Combo("##colliderType", &typeIndex, kTypeFilterNames, IM_ARRAYSIZE(kTypeFilterNames)))
        inspectorTypeFilter_ = typeIndex - 1;
    ImGui::SameLine();
    ImGui::Checkbox("有効だけ", &inspectorEnabledOnly_);
    ImGui::SameLine();
    ImGui::Checkbox("選択中の物だけ", &inspectorSelectedOnly_);
    ImGui::SetItemTooltip("シーンで選んでいるオブジェクトのコライダーだけを出す");
    ImGui::SameLine();
    {
        int grouping = inspectorGroupByOwner_ ? 1 : 0;
        ImGui::SetNextItemWidth(110.0f);
        const char *kGroupingNames[] = {"タグごと", "持ち主ごと"};
        if (ImGui::Combo("##colliderGrouping", &grouping, kGroupingNames, IM_ARRAYSIZE(kGroupingNames)))
            inspectorGroupByOwner_ = (grouping == 1);
    }

    const std::string query = Lower(inspectorSearch_);
    auto passes = [&](ColliderBase *c, const std::string &tag) {
        if (inspectorEnabledOnly_ && !c->IsEnabled())
            return false;
        if (inspectorTypeFilter_ >= 0 && static_cast<int>(c->GetType()) != inspectorTypeFilter_)
            return false;
        if (inspectorSelectedOnly_ && c->GetOwnerName() != sceneSelection)
            return false;
        if (query.empty())
            return true;
        return Lower(c->GetName()).find(query) != std::string::npos || Lower(tag).find(query) != std::string::npos ||
               Lower(c->GetOwnerName()).find(query) != std::string::npos;
    };

    // 見出し（タグ or 持ち主）ごとにまとめる
    struct Row
    {
        ColliderBase *pCollider;
        std::string tag;
    };
    std::map<std::string, std::vector<Row>> groups;
    int total = 0;
    for (auto &[tag, colliders] : collidersByTag_)
    {
        for (auto *c : colliders)
        {
            ++total;
            if (!passes(c, tag))
                continue;
            const std::string key = inspectorGroupByOwner_ ? (c->GetOwnerName().empty() ? std::string("(持ち主なし)") : c->GetOwnerName()) : tag;
            groups[key].push_back({c, tag});
        }
    }

    const bool filtering = !query.empty() || inspectorTypeFilter_ >= 0 || inspectorEnabledOnly_ || inspectorSelectedOnly_;
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("登録: %d コライダー / %d タグ", total, static_cast<int>(collidersByTag_.size()));
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
    if (PrimaryButton(ICON_FA_PLUS " 追加"))
    {
        addOwnerName_ = !sceneSelection.empty() ? sceneSelection
                                                : (pInspectorSelected_ ? pInspectorSelected_->GetOwnerName() : std::string());
        ImGui::OpenPopup("##addColliderPopup");
    }
    ImGui::SetItemTooltip("オブジェクトを選んで、形を選ぶとコライダーを付ける（既定はシーンで選んでいる物）");
    DrawAddColliderPopup();

    ImGui::Separator();

    // ── 左: 一覧 ──
    const float paneHeight = (std::max)(ImGui::GetContentRegionAvail().y, 240.0f);
    // 一覧は窓の4割まで（狭い窓で詳細が押しつぶされないように）。境目はドラッグで変えられる
    const float listWidth = (std::min)(260.0f, ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::BeginChild("##ColliderList", ImVec2(listWidth, paneHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (auto &[key, rows] : groups)
    {
        if (!inspectorGroupByOwner_)
        {
            // タグの色の印（塗り分けに使う色）
            const Vector4 tagColor = TagColor(key);
            ImGui::ColorButton(("##tagColor" + key).c_str(), ImVec4(tagColor.x, tagColor.y, tagColor.z, 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(10.0f, 10.0f));
            ImGui::SameLine();
        }
        const std::string header = std::format("{}  ({})###group{}", key, rows.size(), key);
        if (filtering)
            ImGui::SetNextItemOpen(true);
        if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const Row &row : rows)
        {
            ColliderBase *c = row.pCollider;
            ImGui::PushID(c);

            bool visible = c->IsVisible();
            if (ImGui::Checkbox("##vis", &visible))
                c->SetVisible(visible);
            ImGui::SetItemTooltip("線で表示");
            ImGui::SameLine();

            // 当たっている間は赤い印
            const ImVec4 iconColor = c->IsCollidingInCurrentFrame() ? DebugTheme::kAccentRed
                                     : c->IsEnabled()                ? DebugTheme::kAccentCyan
                                                                     : DebugTheme::kTextDim;
            ImGui::TextColored(iconColor, "%s", ColliderTypeIcon(c->GetType()));
            ImGui::SameLine();

            std::string label = c->GetName().empty() ? "(名前なし)" : c->GetName();
            if (!c->IsEnabled())
                label += "  (無効)";
            if (ImGui::Selectable(label.c_str(), pInspectorSelected_ == c, ImGuiSelectableFlags_AllowDoubleClick))
            {
                pInspectorSelected_ = c;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !c->GetOwnerName().empty())
                    ImGuizmoManager::GetInstance()->SelectOnly(c->GetOwnerName());
            }
            ImGui::SetItemTooltip("%s / タグ %s / 持ち主 %s\nダブルクリックで持ち主をシーンで選ぶ", ColliderTypeDisplayName(c->GetType()),
                                  row.tag.c_str(), c->GetOwnerName().empty() ? "(なし)" : c->GetOwnerName().c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (groups.empty())
    {
        ImGui::TextDisabled(total == 0 ? "コライダーがありません" : "一致するコライダーがありません");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── 右: 選んだコライダーの詳細 ──
    ImGui::BeginChild("##ColliderDetail", ImVec2(0.0f, paneHeight), ImGuiChildFlags_Borders);
    if (pInspectorSelected_)
    {
        DrawColliderEditor(pInspectorSelected_);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("左の一覧からコライダーを選んでください。\n「＋追加」でオブジェクトにコライダーを付けられます");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

} // namespace Hagine
#endif // USE_IMGUI
