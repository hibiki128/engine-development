#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include "Input.h"
#include "Sprite.h"
#include <line/LineRenderer.h>
#include <object/base/BaseObjectManager.h>
#include <transform/WorldTransform.h>
#include <edit/undo/UndoRedoManager.h>
#include "WinApp.h"
#include <format>
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// ImGuizmoManager: 一括編集（整列・等間隔・接地・複製・貼り付け・削除）
// =======================================================================

namespace Hagine {
// ---- 整列・等間隔配置・地面スナップ ------------------------------------

namespace {
// ボタン1発で完了する一括操作を Undo 履歴へ積むためのヘルパー。
// ImGuiUndoTracker は「ウィジェット編集ジェスチャ」を追う仕組みなので、
// こうした即時実行のコマンドは Copy/Paste と同様に明示的に Push する。
template <typename Operation>
void RunAsUndoableCommand(const std::string &label, Operation &&operation)
{
    nlohmann::json before = BaseObjectManager::GetInstance()->CaptureUndoState();
    operation();
    nlohmann::json after = BaseObjectManager::GetInstance()->CaptureUndoState();
    if (before == after)
    {
        return; // 何も変わらなかったら履歴を汚さない
    }
    auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(before, after);
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        label, std::move(diffBefore), std::move(diffAfter),
        [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); }));
}
} // namespace

std::vector<GizmoTarget *> ImGuizmoManager::CollectMovableSelection()
{
    std::vector<GizmoTarget *> targets;
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end() || it->second.isScreenSpace)
        {
            continue; // スプライトは3D整列の対象外
        }
        targets.push_back(&it->second);
    }
    // 並びが実行のたびに変わらないよう名前順に固定する（等間隔配置の結果を安定させるため）
    std::sort(targets.begin(), targets.end(),
              [](const GizmoTarget *lhs, const GizmoTarget *rhs) { return lhs->name < rhs->name; });
    return targets;
}

void ImGuizmoManager::SetTargetWorldPosition(GizmoTarget &target, const Vector3 &worldPosition)
{
    // ワールド行列の平行移動成分だけ差し替えて適用する。
    // ApplyWorldMatrix が親のぶんを打ち消してくれるので、親子付けされていても正しく動く。
    Matrix4x4 worldMatrix = target.GetWorldMatrix();
    worldMatrix.m[3][0] = worldPosition.x;
    worldMatrix.m[3][1] = worldPosition.y;
    worldMatrix.m[3][2] = worldPosition.z;
    target.ApplyWorldMatrix(worldMatrix);
}

void ImGuizmoManager::AlignSelected(int axis, AlignMode mode)
{
    if (axis < 0 || axis > 2)
    {
        return;
    }
    std::vector<GizmoTarget *> targets = CollectMovableSelection();
    if (targets.size() < 2)
    {
        ImGuiNotification::Post("整列するには2つ以上選択してください", {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }

    auto axisOf = [axis](const Vector3 &v) { return (&v.x)[axis]; };

    float minValue = axisOf(targets.front()->GetWorldPosition());
    float maxValue = minValue;
    float sum = 0.0f;
    for (const GizmoTarget *target : targets)
    {
        const float value = axisOf(target->GetWorldPosition());
        minValue = (std::min)(minValue, value);
        maxValue = (std::max)(maxValue, value);
        sum += value;
    }

    float alignedValue = minValue;
    switch (mode)
    {
    case AlignMode::Min:
        alignedValue = minValue;
        break;
    case AlignMode::Center:
        alignedValue = sum / static_cast<float>(targets.size());
        break;
    case AlignMode::Max:
        alignedValue = maxValue;
        break;
    }

    RunAsUndoableCommand("整列", [&] {
        for (GizmoTarget *target : targets)
        {
            Vector3 position = target->GetWorldPosition();
            (&position.x)[axis] = alignedValue;
            SetTargetWorldPosition(*target, position);
        }
    });

    static const char *kAxisNames[3] = {"X", "Y", "Z"};
    ImGuiNotification::Post(std::string("整列しました (") + kAxisNames[axis] + ")", {0.45f, 0.68f, 0.52f, 1.0f});
}

void ImGuizmoManager::DistributeSelected(int axis)
{
    if (axis < 0 || axis > 2)
    {
        return;
    }
    std::vector<GizmoTarget *> targets = CollectMovableSelection();
    if (targets.size() < 3)
    {
        ImGuiNotification::Post("等間隔にするには3つ以上選択してください", {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }

    // 対象軸の座標順に並べ替えてから、両端の間を均等割りする
    std::sort(targets.begin(), targets.end(), [axis](const GizmoTarget *lhs, const GizmoTarget *rhs) {
        const Vector3 lhsPos = lhs->GetWorldPosition();
        const Vector3 rhsPos = rhs->GetWorldPosition();
        return (&lhsPos.x)[axis] < (&rhsPos.x)[axis];
    });

    const Vector3 firstPos = targets.front()->GetWorldPosition();
    const Vector3 lastPos = targets.back()->GetWorldPosition();
    const float begin = (&firstPos.x)[axis];
    const float end = (&lastPos.x)[axis];
    const float step = (end - begin) / static_cast<float>(targets.size() - 1);

    RunAsUndoableCommand("等間隔配置", [&] {
        // 両端は動かさない
        for (size_t i = 1; i + 1 < targets.size(); ++i)
        {
            Vector3 position = targets[i]->GetWorldPosition();
            (&position.x)[axis] = begin + step * static_cast<float>(i);
            SetTargetWorldPosition(*targets[i], position);
        }
    });

    static const char *kAxisNames[3] = {"X", "Y", "Z"};
    ImGuiNotification::Post(std::string("等間隔に並べました (") + kAxisNames[axis] + ")", {0.45f, 0.68f, 0.52f, 1.0f});
}

void ImGuizmoManager::SnapSelectedToGround()
{
    std::vector<GizmoTarget *> targets = CollectMovableSelection();
    if (targets.empty())
    {
        ImGuiNotification::Post("接地させる対象が選択されていません", {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }

    // 選択されているものどうしでぶつからないよう、選択外のオブジェクトだけを床候補にする
    std::vector<const GizmoTarget *> groundCandidates;
    for (const auto &[name, target] : transformMap_)
    {
        if (target.isScreenSpace || target.type != GizmoTarget::Type::BaseObject || !target.baseObject)
        {
            continue;
        }
        if (selectedNames_.find(name) != selectedNames_.end())
        {
            continue;
        }
        groundCandidates.push_back(&target);
    }

    // 対象1つを真下の地面へ落とす
    auto snapOne = [&](GizmoTarget &target) {
        const Vector3 position = target.GetWorldPosition();

        // オブジェクトの底面がどれだけ下にあるかを求める（原点が足元でないモデルへの対応）
        const AABB localBounds = target.GetLocalBounds();
        const Matrix4x4 worldMatrix = target.GetWorldMatrix();
        const float scaleY = Vector3{worldMatrix.m[1][0], worldMatrix.m[1][1], worldMatrix.m[1][2]}.Length();
        const float bottomOffset = localBounds.min.y * scaleY;

        // 十分上から真下へレイを飛ばす（すでにめり込んでいる場合も拾えるように）
        Ray downRay;
        downRay.origin = {position.x, position.y + 1000.0f, position.z};
        downRay.direction = {0.0f, -1.0f, 0.0f};
        downRay.length = 100000.0f;

        bool found = false;
        float highestHitY = 0.0f;
        for (const GizmoTarget *candidate : groundCandidates)
        {
            RayHitInfo hit;
            if (!Input::RayIntersectOBBByMatrix(downRay, candidate->GetWorldMatrix(), hit, candidate->GetLocalBounds()))
            {
                continue;
            }
            // 上から撃っているので、一番高い交点がその地点の地面になる
            if (!found || hit.hitPoint.y > highestHitY)
            {
                highestHitY = hit.hitPoint.y;
                found = true;
            }
        }

        // 何も無ければ Y=0 の地面へ落とす
        const float groundY = found ? highestHitY : 0.0f;
        SetTargetWorldPosition(target, {position.x, groundY - bottomOffset, position.z});
    };

    RunAsUndoableCommand("地面へ接地", [&] {
        for (GizmoTarget *target : targets)
        {
            snapOne(*target);
        }
    });

    ImGuiNotification::Post("地面に接地させました: " + std::to_string(targets.size()) + "個", {0.45f, 0.68f, 0.52f, 1.0f});
}

// マウスカーソルの指す先（地面との交点）を返す
Vector3 ImGuizmoManager::GetSpawnPositionUnderCursor(float fallbackDistance) const
{
    const Ray ray = Input::GetInstance()->GetCurrentRay();

    // ray.length == 0 はシーン外を指している（Input::CreateRayFromMouse の無効レイ）
    if (ray.length > 0.0f)
    {
        // 地面（Y=0平面）との交点を求める。真横を向いている場合は交わらない扱いにする。
        constexpr float kEpsilon = 1e-4f;
        if (std::abs(ray.direction.y) > kEpsilon)
        {
            const float distance = -ray.origin.y / ray.direction.y;
            // カメラの後ろ側や遠すぎる交点は使わない（地平線の彼方に飛ばさないため）
            if (distance > 0.0f && distance <= ray.length)
            {
                Vector3 hit = ray.origin + ray.direction * distance;
                if (useSnap_ && snapTranslate_ > 0.0f)
                {
                    hit.x = std::round(hit.x / snapTranslate_) * snapTranslate_;
                    hit.y = std::round(hit.y / snapTranslate_) * snapTranslate_;
                    hit.z = std::round(hit.z / snapTranslate_) * snapTranslate_;
                }
                return hit;
            }
        }
    }

    // 地面が拾えないときはカメラ前方へ置く
    return GetSpawnPosition(fallbackDistance);
}

// ---- GenerateUniqueName -----------------------------------------------

// 同名エントリが存在しないユニークな名前を生成する
std::string ImGuizmoManager::GenerateUniqueName(const std::string &baseName)
{
    std::string newName;
    int counter = 1;

    std::string cleanBaseName = baseName;
    size_t underscorePos = baseName.find_last_of('_');
    if (underscorePos != std::string::npos)
    {
        std::string suffix = baseName.substr(underscorePos + 1);
        bool isNumber = true;
        for (char c : suffix)
        {
            if (!std::isdigit(c))
            {
                isNumber = false;
                break;
            }
        }
        if (isNumber)
            cleanBaseName = baseName.substr(0, underscorePos);
    }

    do
    {
        newName = cleanBaseName + "_" + std::to_string(counter++);
    } while (transformMap_.find(newName) != transformMap_.end());

    return newName;
}

// ---- CopySelectedObjects / PasteObjects / DeleteSelectedObjects --------

// 選択中の BaseObject をコピーバッファに保存する（非 BaseObject はスキップ）
void ImGuizmoManager::CopySelectedObjects()
{
    copiedNames_.clear();
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject)
        {
            copiedNames_.push_back(name);
        }
    }
    if (!copiedNames_.empty())
    {
        ImGuiNotification::Post("コピーしました: " + std::to_string(copiedNames_.size()) + "個",
                                {0.4f, 0.8f, 1.0f, 1.0f});
    }
}

// BaseObject を複製して BaseObjectManager へ追加する（貼り付け・複製の共通処理）
std::string ImGuizmoManager::CloneObject(BaseObject *pSource, const Vector3 &offset)
{
    if (!pSource)
        return {};

    // 名前は先に決める。Init 前に確定させないと DataHandler が元の名前で作られてしまう
    const std::string uniqueName = GenerateUniqueName(pSource->GetName());

    std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
    newObject->SetPrimitive(pSource->IsPrimitive());
    newObject->Init(uniqueName);

    if (!pSource->GetModelPath().empty())
    {
        newObject->CreateModel(pSource->GetModelPath());
    }
    else if (pSource->GetPrimitiveType() != PrimitiveType::Count)
    {
        newObject->CreatePrimitiveModel(pSource->GetPrimitiveType());
    }
    else
    {
        return {}; // モデルもプリミティブも無いものは複製できない
    }

    // マテリアルごとのテクスチャ・色を全て引き継ぐ
    // （0番だけコピーしていた頃は、複数マテリアルのモデルで見た目が変わってしまっていた）
    const int materialCount = newObject->GetObject3d() ? static_cast<int>(newObject->GetObject3d()->GetMaterialCount()) : 0;
    const int textureCount = pSource->IsPrimitive() ? (materialCount > 0 ? 1 : 0) : materialCount;
    for (int i = 0; i < textureCount; ++i)
    {
        newObject->SetTexture(pSource->GetTexturePath(i), i);
    }
    for (int i = 0; i < materialCount; ++i)
    {
        newObject->SetColor(pSource->GetColor(i), i);
    }

    newObject->GetLocalPosition() = pSource->GetLocalPosition() + offset;
    newObject->GetLocalRotation() = pSource->GetLocalRotation();
    newObject->GetLocalScale() = pSource->GetLocalScale();
    newObject->GetLighting() = pSource->GetLighting();
    newObject->SetShouldSave(pSource->GetShouldSave());

    // AddObject → RegisterExternal 内でギズモへの登録も行われる
    BaseObjectManager::GetInstance()->AddObject(std::move(newObject));
    return uniqueName;
}

// コピー済み BaseObject を複製して BaseObjectManager に追加する
void ImGuizmoManager::PasteObjects()
{
    if (copiedNames_.empty())
        return;

    // ショートカット起点の操作はImGuiの編集ジェスチャに乗らないため、明示的にUndo履歴へ積む
    nlohmann::json undoBefore = BaseObjectManager::GetInstance()->CaptureUndoState();

    selectedNames_.clear();

    for (const std::string &copiedName : copiedNames_)
    {
        // コピー後に元が消えている可能性があるので、貼り付け時に引き直す
        BaseObject *copiedObj = BaseObjectManager::GetInstance()->GetObjectByName(copiedName);
        const std::string uniqueName = CloneObject(copiedObj, {1.0f, 0.0f, 0.0f});
        if (!uniqueName.empty())
        {
            selectedNames_.insert(uniqueName);
        }
    }

    if (selectedNames_.empty())
    {
        ImGuiNotification::Post("貼り付け元のオブジェクトが見つかりませんでした", {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }

    // 連続で貼り付けられるようコピーバッファは保持する（従来はここで消えていた）

    // 貼り付け操作をUndo履歴へ積む
    {
        nlohmann::json undoAfter = BaseObjectManager::GetInstance()->CaptureUndoState();
        auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(undoBefore, undoAfter);
        UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
            "オブジェクト貼り付け", std::move(diffBefore), std::move(diffAfter),
            [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); }));
    }

    ImGuiNotification::Post("オブジェクトを貼り付けました", {0.4f, 0.8f, 1.0f, 1.0f});
}

// 選択中の BaseObject をその場で複製し、複製後を選択状態にする
void ImGuizmoManager::DuplicateSelectedObjects()
{
    // 複製元を先に確定させる。複製すると transformMap_ が増えるため、走査中に追加すると壊れる
    std::vector<BaseObject *> sources = GetSelectedTargets();
    if (sources.empty())
        return;

    nlohmann::json undoBefore = BaseObjectManager::GetInstance()->CaptureUndoState();

    selectedNames_.clear();
    for (BaseObject *source : sources)
    {
        const std::string uniqueName = CloneObject(source, {0.0f, 0.0f, 0.0f});
        if (!uniqueName.empty())
        {
            selectedNames_.insert(uniqueName);
        }
    }

    {
        nlohmann::json undoAfter = BaseObjectManager::GetInstance()->CaptureUndoState();
        auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(undoBefore, undoAfter);
        UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
            "オブジェクト複製", std::move(diffBefore), std::move(diffAfter),
            [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); }));
    }

    ImGuiNotification::Post("オブジェクトを複製しました: " + std::to_string(sources.size()) + "個",
                            {0.4f, 0.8f, 1.0f, 1.0f});
}

// 選択中の全エントリを削除する
// BaseObject の場合は BaseObjectManager からも削除する
void ImGuizmoManager::DeleteSelectedObjects()
{
    if (selectedNames_.empty())
        return;

    // ショートカット起点の操作はImGuiの編集ジェスチャに乗らないため、明示的にUndo履歴へ積む
    nlohmann::json undoBefore = BaseObjectManager::GetInstance()->CaptureUndoState();

    // selectedNames_ を走査しながら消すと、削除経路が選択集合に触った時点で壊れる。
    // 先に対象名を確定させてから消す。
    const std::vector<std::string> targets(selectedNames_.begin(), selectedNames_.end());
    const size_t count = targets.size();
    for (const std::string &name : targets)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject)
        {
            // RemoveObject 側でギズモ・モーションエディタからの登録解除も行われる
            BaseObjectManager::GetInstance()->RemoveObject(name);
        }
        transformMap_.erase(name);

        // 消えたオブジェクトをコピーバッファに残さない
        copiedNames_.erase(std::remove(copiedNames_.begin(), copiedNames_.end(), name), copiedNames_.end());
    }

    // 削除操作をUndo履歴へ積む
    {
        nlohmann::json undoAfter = BaseObjectManager::GetInstance()->CaptureUndoState();
        auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(undoBefore, undoAfter);
        UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
            "オブジェクト削除", std::move(diffBefore), std::move(diffAfter),
            [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); }));
    }

    UpdateFilteredNames();
    // 削除後に別のオブジェクトを勝手に選ぶと、続けて Delete を押したときに
    // 意図しないものを消してしまうので、選択は空のままにする
    selectedNames_.clear();
    overlapCandidates_.clear();
    overlapCycleIndex_ = 0;

    ImGuiNotification::Post("選択オブジェクトを削除しました: " + std::to_string(count) + "個", {0.9f, 0.7f, 0.2f, 1.0f});
}

} // namespace Hagine
#endif // USE_IMGUI
