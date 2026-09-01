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
// ImGuizmoManager: 本体（登録・更新・ギズモ表示・行列ヘルパー）
// =======================================================================

namespace Hagine {
// =======================================================================
// ImGuizmoManager メンバ関数実装
// =======================================================================

void ImGuizmoManager::Finalize()
{
    transformMap_.clear();
    selectedNames_.clear();
    copiedNames_.clear();
}

void ImGuizmoManager::BeginFrame()
{
    ImGuizmo::BeginFrame();
}

void ImGuizmoManager::SetViewProjection(ViewProjection *pViewProjection)
{
    pViewProjection_ = pViewProjection;
}

// ---- AddTarget オーバーロード群 ----------------------------------------

// BaseObject を登録する
void ImGuizmoManager::AddTarget(const std::string &name, BaseObject *pObject, bool selectable)
{
    GizmoTarget target;
    target.type = GizmoTarget::Type::BaseObject;
    target.category = GizmoCategory::Object;
    target.name = name;
    target.baseObject = pObject;
    target.selectable = selectable;
    transformMap_[name] = target;

    UpdateFilteredNames();
}

// WorldTransform のみを持つオブジェクトを登録する
void ImGuizmoManager::AddTarget(const std::string &name, WorldTransform *worldTransform,
                                bool selectable,
                                std::function<void()> imguiCallback)
{
    GizmoTarget target;
    target.type = GizmoTarget::Type::WorldTransform;
    // WorldTransform 単体は 3Dオブジェクト扱いを既定とする。
    // パーティクル等で分類を変えたい場合は AddTarget 後に SetCategory を呼ぶ。
    target.category = GizmoCategory::Object;
    target.name = name;
    target.worldTransform = worldTransform;
    target.selectable = selectable;
    target.imguiCallback = imguiCallback;
    transformMap_[name] = target;

    UpdateFilteredNames();
}

// Vector3 ポインタを直接指定して登録する（Sprite・ParticleEmitter など）
void ImGuizmoManager::AddTarget(const std::string &name,
                                Vector3 *translate,
                                Vector3 *rotate,
                                Vector3 *scale,
                                bool selectable,
                                std::function<void()> imguiCallback)
{
    GizmoTarget target;
    target.type = GizmoTarget::Type::FreeTransform;
    // Vector3 直接指定はパーティクルエミッターで多く使われるため既定はParticle。
    // スプライト等は AddTarget 後に SetCategory で上書きする。
    target.category = GizmoCategory::Particle;
    target.name = name;
    target.translate = translate;
    target.rotate = rotate;
    target.scale = scale;
    target.selectable = selectable;
    target.imguiCallback = imguiCallback;
    transformMap_[name] = target;

    UpdateFilteredNames();
}

// Sprite を登録する（スクリーン空間 XY のみ操作）
void ImGuizmoManager::AddTarget(const std::string &name, Sprite *pSprite, bool selectable)
{
    GizmoTarget target;
    target.type = GizmoTarget::Type::Sprite2D;
    target.category = GizmoCategory::Sprite;
    target.name = name;
    target.position2D = &pSprite->GetPositionRef();
    target.selectable = selectable;
    target.isScreenSpace = true;
    target.screenHitRadius = 50.0f;
    transformMap_[name] = target;

    UpdateFilteredNames();
}

// ---- 選択オブジェクト取得（BaseObject 互換用）---------------------------

// 選択中の最初のエントリが BaseObject である場合に返す
BaseObject *ImGuizmoManager::GetSelectedTarget()
{
    if (selectedNames_.empty())
        return nullptr;

    auto it = transformMap_.find(*selectedNames_.begin());
    if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject)
    {
        return it->second.baseObject;
    }
    return nullptr;
}

// 選択中のエントリのうち BaseObject のもののみを返す
std::vector<BaseObject *> ImGuizmoManager::GetSelectedTargets()
{
    std::vector<BaseObject *> selected;
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject)
        {
            selected.push_back(it->second.baseObject);
        }
    }
    return selected;
}

// ---- Update -----------------------------------------------------------

void ImGuizmoManager::Update(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered)
{
    if (!pViewProjection_)
        return;

    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);
    ImGuizmo::SetDrawlist();

    if (!ImGuizmo::IsUsing())
    {
        // 矩形選択のドラッグ判定を先に回す。
        // しきい値未満のドラッグはクリック扱いになり、下の単体選択がそのまま働く。
        HandleBoxSelection(scenePosition, sceneSize, sceneHovered);
        HandleMouseSelection(scenePosition, sceneSize, sceneHovered);
        HandleHotkeys(sceneHovered);
    }
    else
    {
        // ギズモ操作に入ったら矩形選択は取り消す（枠が出しっぱなしにならないように）
        isBoxSelecting_ = false;
    }

    DrawSelectedObjectHighlight();

    if (!selectedNames_.empty())
    {
        // スプライト用正射影 VP を使うためシーン情報を渡す
        DisplayGizmo(scenePosition, sceneSize);
    }
}

// ---- DisplayGizmo -----------------------------------------------------

// 選択中の全エントリの重心位置にギズモを表示し、操作量を各エントリに反映する
void ImGuizmoManager::DisplayGizmo(const ImVec2 &scenePosition, const ImVec2 &sceneSize)
{
    if (!pViewProjection_ || selectedNames_.empty())
        return;

    std::vector<GizmoTarget *> selectedTargets;
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            selectedTargets.push_back(&it->second);
        }
    }
    if (selectedTargets.empty())
        return;

    // 親と子を同時に選んでいる場合に、子を先に動かすと親の移動で二重にずれる。
    // 必ず親から順に適用できるよう、階層の浅い順に並べておく。
    std::stable_sort(selectedTargets.begin(), selectedTargets.end(),
                     [](const GizmoTarget *lhs, const GizmoTarget *rhs) {
                         auto depthOf = [](const GizmoTarget *target) {
                             const WorldTransform *transform = nullptr;
                             if (target->type == GizmoTarget::Type::BaseObject && target->baseObject)
                             {
                                 transform = target->baseObject->GetWorldTransform();
                             }
                             else if (target->type == GizmoTarget::Type::WorldTransform)
                             {
                                 transform = target->worldTransform;
                             }
                             int depth = 0;
                             for (const WorldTransform *node = transform; node && node->pParent_; node = node->pParent_)
                             {
                                 ++depth;
                             }
                             return depth;
                         };
                         return depthOf(lhs) < depthOf(rhs);
                     });

    // 選択中にスクリーン空間（Sprite）が含まれるか確認
    // ※ 3Dオブジェクトとスプライトを同時選択した場合は動作が未定義
    bool anyScreenSpace = std::any_of(selectedTargets.begin(), selectedTargets.end(),
                                      [](const GizmoTarget *t) { return t->isScreenSpace; });

    // ギズモを置く基準行列（ピボット）を決める。
    // ・単一選択の3D対象 … その対象のワールド行列そのもの
    //   （回転・拡縮がその場で効き、「ローカル」座標系もオブジェクトの向きを向く）
    // ・複数選択／スクリーン空間 … 重心位置の無回転行列を共通ピボットにする
    // スケール0の軸があるオブジェクトの行列をそのまま渡すと、ImGuizmo 内部の逆行列計算が
    // 破綻して座標が NaN になる。その場合は重心ピボットへ逃がす。
    auto isDegenerate = [](const Matrix4x4 &matrix) {
        constexpr float kEpsilon = 1e-5f;
        for (int row = 0; row < 3; ++row)
        {
            if (Vector3{matrix.m[row][0], matrix.m[row][1], matrix.m[row][2]}.Length() <= kEpsilon)
            {
                return true;
            }
        }
        return false;
    };

    const bool useTargetMatrix = (selectedTargets.size() == 1 && !anyScreenSpace &&
                                  !isDegenerate(selectedTargets[0]->GetWorldMatrix()));

    Matrix4x4 pivotMatrix;
    if (useTargetMatrix)
    {
        pivotMatrix = selectedTargets[0]->GetWorldMatrix();
    }
    else
    {
        Vector3 centerPos = {0.0f, 0.0f, 0.0f};
        for (GizmoTarget *target : selectedTargets)
        {
            centerPos = centerPos + target->GetWorldPosition();
        }
        centerPos = centerPos / static_cast<float>(selectedTargets.size());

        pivotMatrix = MakeIdentity4x4();
        pivotMatrix.m[3][0] = centerPos.x;
        pivotMatrix.m[3][1] = centerPos.y;
        pivotMatrix.m[3][2] = centerPos.z;
    }
    const Matrix4x4 centerMatrix = pivotMatrix;

    float matrixArray[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            matrixArray[i * 4 + j] = centerMatrix.m[i][j];

    float viewArray[16], projArray[16];

    if (anyScreenSpace)
    {
        // スプライト用：単位ビュー行列 + スプライトと同じ正射影行列
        // これにより ImGuizmo がピクセル座標系でギズモを正しい位置に描画する
        Matrix4x4 identView = MakeIdentity4x4();
        Matrix4x4 orthoProj = MakeOrthographicMatrix(
            0.0f, 0.0f,
            static_cast<float>(WinApp::GetVirtualWidth()),
            static_cast<float>(WinApp::GetVirtualHeight()),
            0.0f, 100.0f);

        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                viewArray[i * 4 + j] = identView.m[i][j];
                projArray[i * 4 + j] = orthoProj.m[i][j];
            }
        }
    }
    else
    {
        // 3Dオブジェクト用：カメラの View/Projection をそのまま使用
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                viewArray[i * 4 + j] = pViewProjection_->matView_.m[i][j];
                projArray[i * 4 + j] = pViewProjection_->matProjection_.m[i][j];
            }
        }
    }

    // スクリーン空間（Sprite 等）は XY 移動のみ許可
    ImGuizmo::OPERATION effectiveOp = currentOperation_;
    if (anyScreenSpace)
    {
        effectiveOp = ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y;
    }

    // 正射影かどうかを ImGuizmo に伝える（グローバル状態なので毎回明示的に設定する）。
    // これを怠ると、スプライト用の正射影（nearClip=0）では原点の clip z が 0 になり、
    // ImGuizmo の「カメラ後方」判定 (mIsOrthographic==false && z < 0.001) に引っかかって
    // Manipulate が描画前に return し、ギズモが一切表示されない。
    ImGuizmo::SetOrthographic(anyScreenSpace);

    // スナップ値は操作モードごとに意味が変わる（移動=距離 / 回転=度 / 拡縮=倍率）。
    // Shift 押下中は設定を一時的に反転させ、ON時の微調整・OFF時の一時吸着を両立させる。
    const bool shiftHeld = ImGui::GetIO().KeyShift;
    const bool snapActive = (useSnap_ != shiftHeld) && !anyScreenSpace;
    float snapValues[3] = {snapTranslate_, snapTranslate_, snapTranslate_};
    if (currentOperation_ == ImGuizmo::ROTATE)
    {
        snapValues[0] = snapValues[1] = snapValues[2] = snapRotateDegree_;
    }
    else if (currentOperation_ == ImGuizmo::SCALE)
    {
        snapValues[0] = snapValues[1] = snapValues[2] = snapScale_;
    }

    if (ImGuizmo::Manipulate(viewArray, projArray, effectiveOp, currentMode_, matrixArray,
                             nullptr, snapActive ? snapValues : nullptr))
    {
        Matrix4x4 newMatrix;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                newMatrix.m[i][j] = matrixArray[i * 4 + j];

        if (useTargetMatrix)
        {
            // 単一選択はギズモの結果をそのまま反映する（余計な行列演算を挟まず誤差を出さない）
            selectedTargets[0]->ApplyWorldMatrix(newMatrix);
        }
        else
        {
            // 複数選択はピボット基準の差分を全員に掛ける。
            // 行ベクトル規約なので「ピボット空間へ戻す → 新しいピボットへ乗せ直す」の順に掛ける。
            const Matrix4x4 pivotDelta = Inverse(centerMatrix) * newMatrix;

            // 親と子を同時に選んでいる場合、親を先に動かすと子のワールド行列が更新されてしまい、
            // その後に子へ差分を掛けると二重に効いてしまう。適用前に全員の行列を控えておく。
            std::vector<Matrix4x4> beforeMatrices;
            beforeMatrices.reserve(selectedTargets.size());
            for (const GizmoTarget *target : selectedTargets)
            {
                beforeMatrices.push_back(target->GetWorldMatrix());
            }

            for (size_t i = 0; i < selectedTargets.size(); ++i)
            {
                GizmoTarget *target = selectedTargets[i];
                if (target->isScreenSpace)
                {
                    // スクリーン空間はピクセル単位の XY 平行移動のみ
                    target->ApplyTranslationDelta({newMatrix.m[3][0] - centerMatrix.m[3][0],
                                                   newMatrix.m[3][1] - centerMatrix.m[3][1],
                                                   0.0f});
                    continue;
                }
                target->ApplyWorldMatrix(beforeMatrices[i] * pivotDelta);
            }
        }
    }
}

// ---- DecomposeMatrix --------------------------------------------------

// 行列からスケール・回転（クォータニオン）・位置を分解して返す
void ImGuizmoManager::DecomposeMatrix(const Matrix4x4 &matrix, Vector3 &position, Quaternion &rotation, Vector3 &scale)
{
    position = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};

    Vector3 col0 = {matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]};
    Vector3 col1 = {matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]};
    Vector3 col2 = {matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]};

    scale.x = col0.Length();
    scale.y = col1.Length();
    scale.z = col2.Length();

    Matrix4x4 rotMatrix = matrix;
    if (scale.x != 0.0f)
    {
        rotMatrix.m[0][0] /= scale.x;
        rotMatrix.m[0][1] /= scale.x;
        rotMatrix.m[0][2] /= scale.x;
    }
    if (scale.y != 0.0f)
    {
        rotMatrix.m[1][0] /= scale.y;
        rotMatrix.m[1][1] /= scale.y;
        rotMatrix.m[1][2] /= scale.y;
    }
    if (scale.z != 0.0f)
    {
        rotMatrix.m[2][0] /= scale.z;
        rotMatrix.m[2][1] /= scale.z;
        rotMatrix.m[2][2] /= scale.z;
    }

    rotation = Quaternion::FromMatrix(rotMatrix);
}

// ---- WorldToScreen ----------------------------------------------------

// ワールド座標をシーンウィンドウのスクリーン座標に変換する
bool ImGuizmoManager::WorldToScreen(const Vector3 &worldPos, Vector3 &screenPos, const ImVec2 &scenePosition, const ImVec2 &sceneSize)
{
    Vector4 clipPos;
    {
        Vector3 v = worldPos;
        float x = v.x * pViewProjection_->matView_.m[0][0] + v.y * pViewProjection_->matView_.m[1][0] + v.z * pViewProjection_->matView_.m[2][0] + pViewProjection_->matView_.m[3][0];
        float y = v.x * pViewProjection_->matView_.m[0][1] + v.y * pViewProjection_->matView_.m[1][1] + v.z * pViewProjection_->matView_.m[2][1] + pViewProjection_->matView_.m[3][1];
        float z = v.x * pViewProjection_->matView_.m[0][2] + v.y * pViewProjection_->matView_.m[1][2] + v.z * pViewProjection_->matView_.m[2][2] + pViewProjection_->matView_.m[3][2];
        float w = v.x * pViewProjection_->matView_.m[0][3] + v.y * pViewProjection_->matView_.m[1][3] + v.z * pViewProjection_->matView_.m[2][3] + pViewProjection_->matView_.m[3][3];

        clipPos.x = x * pViewProjection_->matProjection_.m[0][0] + y * pViewProjection_->matProjection_.m[1][0] + z * pViewProjection_->matProjection_.m[2][0] + w * pViewProjection_->matProjection_.m[3][0];
        clipPos.y = x * pViewProjection_->matProjection_.m[0][1] + y * pViewProjection_->matProjection_.m[1][1] + z * pViewProjection_->matProjection_.m[2][1] + w * pViewProjection_->matProjection_.m[3][1];
        clipPos.z = x * pViewProjection_->matProjection_.m[0][2] + y * pViewProjection_->matProjection_.m[1][2] + z * pViewProjection_->matProjection_.m[2][2] + w * pViewProjection_->matProjection_.m[3][2];
        clipPos.w = x * pViewProjection_->matProjection_.m[0][3] + y * pViewProjection_->matProjection_.m[1][3] + z * pViewProjection_->matProjection_.m[2][3] + w * pViewProjection_->matProjection_.m[3][3];
    }

    if (clipPos.w <= 0.0f)
        return false;

    float ndcX = clipPos.x / clipPos.w;
    float ndcY = clipPos.y / clipPos.w;

    screenPos.x = scenePosition.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
    screenPos.y = scenePosition.y + (0.5f - ndcY * 0.5f) * sceneSize.y;
    screenPos.z = clipPos.z / clipPos.w;

    return true;
}

} // namespace Hagine
#endif // USE_IMGUI
