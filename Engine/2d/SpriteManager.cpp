#define NOMINMAX
#include "SpriteManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuizmoManager.h"
#include "SpriteCommon.h"
#include "WinApp.h"
#include "MyMath.h"
#include <asset/AssetPath.h>
#include <data/DataHandler.h>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <browser/ShowFolder.h>
#include "render/DrawGroupManager.h"
#include <filesystem>

namespace Hagine {
namespace fs = std::filesystem;

void SpriteManager::Finalize()
{
    // ギズモがインスタンスへのポインタを握っているので、登録解除を伴う Clear() 経由で破棄する
    Clear();
}

void SpriteManager::RegisterSprite(const std::string &name, const std::string &textureFilePath, const SpriteTransform &transform)
{
    // 新しいスプライトデータを作成し、必要なコンポーネントを初期化してリストに登録する
    auto spriteData = std::make_unique<SpriteData>(name, textureFilePath, transform.instanceCount);

    spriteData->sprite = std::make_unique<Sprite>();
    spriteData->sprite->Initialize(textureFilePath, transform.position, transform.color,
                                   transform.anchorPoint, transform.isFlipX, transform.isFlipY);
    spriteData->sprite->SetInstanceCount(transform.instanceCount);
    // 行列は UpdateSpriteInstances が instanceData から毎フレーム構築する
    spriteData->sprite->SetUseExternalTransforms(true);
    spriteData->sprite->SetUVPosition({0.0f, 0.0f});
    spriteData->sprite->SetUVSize({1.0f, 1.0f});
    spriteData->sprite->SetUVRotate(0.0f);

    // インスタンスデータを基に初期変換データを設定する
    for (uint32_t i = 0; i < transform.instanceCount; ++i)
    {
        spriteData->instanceData[i].translation = {transform.position.x, transform.position.y, 0.0f};
        spriteData->instanceData[i].scale = {1.0f, 1.0f, 1.0f};
        spriteData->instanceData[i].rotation = {0.0f, 0.0f, 0.0f};
        spriteData->instanceData[i].isActive = true;
    }
    spriteData->syncedPosition = transform.position;

    sprites_.push_back(std::move(spriteData));
    UpdateSpriteInstances(sprites_.back().get());
    DrawGroupManager::GetInstance()->RegisterGroup(sprites_.back()->drawGroup); // 所属グループを登録
#ifdef USE_IMGUI
    // instanceData[0].translation の xy をギズモで直接編集できるよう登録
    SyncGizmoTarget(sprites_.back().get(), 0);
#endif
    ImGuiNotification::Post("スプライトを登録しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void SpriteManager::UnregisterSprite(const std::string &name)
{
    // 指定された名前のスプライトをリストから検索して削除する
    auto it = std::find_if(sprites_.begin(), sprites_.end(),
                           [&name](const std::unique_ptr<SpriteData> &sprite) {
                               return sprite->name == name;
                           });

    if (it != sprites_.end())
    {
#ifdef USE_IMGUI
        ImGuizmoManager::GetInstance()->RemoveTarget(name);
        gizmoBound_.erase(name);
#endif
        ImGuiNotification::Post("スプライトを削除しました: " + name, {0.9f, 0.7f, 0.2f, 1.0f});
        sprites_.erase(it);
    }
}

void SpriteManager::DrawAll()
{
    // シャドウパス中(D32 DSV)はスプライト(D24 PSO)を描かない（深度フォーマット不一致を防ぐ）
    // G-Bufferパス中も同様に描かない（不透明のObject3d専用のパスなので、
    // スプライトは後続の前方描画フェーズで描かれる）
    if (ShadowMap::GetInstance()->IsShadowPassActive() ||
        DeferredRenderer::GetInstance()->IsGBufferPassActive())
    {
        return;
    }
    // 所有スプライトの描画
    for (auto &spriteData : sprites_)
    {
        if (spriteData->isVisible)
        {
            SpriteCommon::GetInstance()->SetBlendMode(spriteData->blendMode);
            spriteData->sprite->Draw(spriteData->isBackMost);
        }
    }
    // 外部登録スプライトの描画
    for (auto *sprite : externalSprites_)
    {
        if (sprite)
        {
            sprite->Draw();
        }
    }
}

void SpriteManager::RegisterExternal(Sprite *sprite)
{
    if (!sprite)
        return;
    auto it = std::find(externalSprites_.begin(), externalSprites_.end(), sprite);
    if (it == externalSprites_.end())
    {
        externalSprites_.push_back(sprite);
    }
}

void SpriteManager::UnregisterExternal(Sprite *sprite)
{
    auto it = std::find(externalSprites_.begin(), externalSprites_.end(), sprite);
    if (it != externalSprites_.end())
    {
        externalSprites_.erase(it);
    }
}

void SpriteManager::SetSpriteBlendMode(const std::string &name, BlendMode blendMode)
{
    // スプライトのブレンドモードを更新する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->blendMode = blendMode;
    }
}

void SpriteManager::UpdateAll(float deltaTime)
{
    // 各スプライトのカスタム更新関数を実行し、変換行列を更新する
    for (auto &spriteData : sprites_)
    {
        if (spriteData->isVisible)
        {
            if (spriteData->updateFunction)
            {
                spriteData->updateFunction(*spriteData, deltaTime);
            }
            UpdateSpriteInstances(spriteData.get());
        }
    }
}

void SpriteManager::UpdateImGui()
{
#ifdef USE_IMGUI
    DrawSpriteCreationModal();
#endif // USE_IMGUI
}

std::string SpriteManager::GetTextureFilePath(const std::string &name)
{
    // スプライトの名前からテクスチャファイルパスを取得する
    auto spriteData = GetSprite(name);
    return spriteData ? spriteData->textureFilePath : "";
}

std::vector<SpriteData *> SpriteManager::GetAllSprites()
{
    // 全てのスプライトデータのリストを取得する
    std::vector<SpriteData *> result;
    result.reserve(sprites_.size());
    for (auto &s : sprites_)
    {
        result.push_back(s.get());
    }
    return result;
}

void SpriteManager::SetTextureFilePath(const std::string &name, const std::string &textureFilePath)
{
    // スプライトのテクスチャパスを更新する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->textureFilePath = textureFilePath;
        spriteData->sprite->SetTexturePath(textureFilePath);
    }
}

SpriteData *SpriteManager::GetSprite(const std::string &name)
{
    // 名前による検索を実行する
    return FindSpriteByName(name);
}

SpriteData *SpriteManager::FindSpriteByName(const std::string &name)
{
    // 名前一致するスプライトデータを検索しポインタを返す
    auto it = std::find_if(sprites_.begin(), sprites_.end(),
                           [&name](const std::unique_ptr<SpriteData> &sprite) {
                               return sprite->name == name;
                           });
    return (it != sprites_.end()) ? it->get() : nullptr;
}

int SpriteManager::FindSpriteIndex(const std::string &name)
{
    // インデックスによる検索を実行する
    for (size_t i = 0; i < sprites_.size(); ++i)
    {
        if (sprites_[i]->name == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SpriteManager::SetInstanceSRT(const std::string &name, uint32_t index, const InstanceSRT &srt)
{
    // インスタンスのSRTデータを更新する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        spriteData->instanceData[index] = srt;
    }
}

void SpriteManager::SetInstanceScale(const std::string &name, uint32_t index, const Vector3 &scale)
{
    // 特定のインスタンスのスケールを設定する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        spriteData->instanceData[index].scale = scale;
    }
}

void SpriteManager::SetInstanceRotation(const std::string &name, uint32_t index, const Vector3 &rotation)
{
    // 特定のインスタンスの回転を設定する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        spriteData->instanceData[index].rotation = rotation;
    }
}

void SpriteManager::SetInstanceTranslation(const std::string &name, uint32_t index, const Vector3 &translation)
{
    // 特定のインスタンスの移動を設定する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        spriteData->instanceData[index].translation = translation;
    }
}

void SpriteManager::SetInstanceActive(const std::string &name, uint32_t index, bool isActive)
{
    // 特定のインスタンスの有効/無効を設定する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        spriteData->instanceData[index].isActive = isActive;
    }
}

InstanceSRT *SpriteManager::GetInstanceSRT(const std::string &name, uint32_t index)
{
    // 特定のインスタンスのSRTデータを取得する
    auto spriteData = GetSprite(name);
    if (spriteData && index < spriteData->instanceData.size())
    {
        return &spriteData->instanceData[index];
    }
    return nullptr;
}

void SpriteManager::SetSpriteVisible(const std::string &name, bool visible)
{
    // スプライトの表示可否を設定する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->isVisible = visible;
    }
}

void SpriteManager::SetSpriteBackMost(const std::string &name, bool isBackMost)
{
    // スプライトの背面配置フラグを設定する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->isBackMost = isBackMost;
    }
}

void SpriteManager::SetSpritePosition(const std::string &name, const Vector2 &position)
{
    // スプライトの基準位置を設定する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->sprite->SetPosition(position);
        // 所有スプライトの描画行列は instanceData 起点で構築されるため、先頭インスタンスにも反映する
        if (!spriteData->instanceData.empty())
        {
            spriteData->instanceData[0].translation.x = position.x;
            spriteData->instanceData[0].translation.y = position.y;
        }
        spriteData->syncedPosition = position;
    }
}

void SpriteManager::SetSpriteSize(const std::string &name, const Vector2 &size)
{
    // スプライトのサイズを設定する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->sprite->SetSize(size);
    }
}

void SpriteManager::SetSpriteColor(const std::string &name, const Vector4 &color)
{
    // スプライトの色を設定する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->sprite->SetColor({color.x, color.y, color.z});
        spriteData->sprite->SetAlpha(color.w);
    }
}

void SpriteManager::SetUpdateFunction(const std::string &name, std::function<void(SpriteData &, float)> updateFunc)
{
    // カスタム更新関数を登録する
    auto spriteData = GetSprite(name);
    if (spriteData)
    {
        spriteData->updateFunction = updateFunc;
    }
}

void SpriteManager::Clear()
{
#ifdef USE_IMGUI
    for (auto &sp : sprites_)
    {
        if (sp)
            ImGuizmoManager::GetInstance()->RemoveTarget(sp->name);
    }
    gizmoBound_.clear();
#endif
    sprites_.clear();
    externalSprites_.clear();
}

#ifdef USE_IMGUI
void SpriteManager::SyncGizmoTarget(SpriteData *spriteData, int instanceIndex)
{
    if (!spriteData)
        return;

    auto *gizmo = ImGuizmoManager::GetInstance();
    gizmo->RemoveTarget(spriteData->name);

    if (spriteData->instanceData.empty())
    {
        gizmoBound_.erase(spriteData->name);
        return;
    }

    instanceIndex = std::clamp(instanceIndex, 0, static_cast<int>(spriteData->instanceData.size()) - 1);
    Vector3 *translation = &spriteData->instanceData[instanceIndex].translation;

    gizmo->AddTarget(spriteData->name, translation, nullptr, nullptr, true);
    gizmo->SetScreenSpace(spriteData->name, true, 50.0f);
    // Vector3直接指定の既定はParticleなので、スプライトとして明示的に分類し直す
    gizmo->SetCategory(spriteData->name, GizmoCategory::Sprite);

    // 当たり判定はスプライトの実際の矩形で行う（translation は矩形の角なので円判定だと掴めない）。
    // spriteData は unique_ptr の指す先なので vector 再確保でもアドレスは不変
    gizmo->SetScreenHitTest(spriteData->name, [spriteData, instanceIndex](const Vector2 &p) -> bool {
        if (!spriteData->sprite || instanceIndex >= static_cast<int>(spriteData->instanceData.size()))
            return false;

        const InstanceSRT &inst = spriteData->instanceData[instanceIndex];
        if (!inst.isActive)
            return false;

        // UpdateSpriteInstances と同じ規則で矩形を組み立てる
        const Vector2 size = spriteData->sprite->GetSize();
        const Vector2 anchor = spriteData->sprite->GetAnchorPoint();
        const float w = size.x * inst.scale.x;
        const float h = size.y * inst.scale.y;
        const float angle = inst.rotation.z + spriteData->sprite->GetRotation();

        // マウス位置をスプライトのローカル空間へ逆変換する（回転を打ち消す）
        const float relX = p.x - inst.translation.x;
        const float relY = p.y - inst.translation.y;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float localX = relX * c + relY * s;
        const float localY = -relX * s + relY * c;

        // 頂点は [-anchor, 1-anchor] の範囲にスケールを掛けたもの
        const float left = -anchor.x * w;
        const float right = (1.0f - anchor.x) * w;
        const float top = -anchor.y * h;
        const float bottom = (1.0f - anchor.y) * h;

        return localX >= std::min(left, right) && localX <= std::max(left, right) &&
               localY >= std::min(top, bottom) && localY <= std::max(top, bottom);
    });

    gizmoBound_[spriteData->name] = translation;
}
#endif // USE_IMGUI

void SpriteManager::UpdateSpriteInstances(SpriteData *spriteData)
{
    // 各インスタンスごとのワールド行列を作成し、スプライト側の変換行列リソースに適用する
    if (!spriteData || !spriteData->sprite)
        return;

    spriteData->sprite->SetInstanceCount(static_cast<uint32_t>(spriteData->instanceData.size()));

    // 行列は instanceData 起点で作るため、Sprite::SetPosition の変化分を先頭インスタンスへ反映する
    const Vector2 basePosition = spriteData->sprite->GetPosition();
    if (basePosition.x != spriteData->syncedPosition.x || basePosition.y != spriteData->syncedPosition.y)
    {
        if (!spriteData->instanceData.empty())
        {
            spriteData->instanceData[0].translation.x = basePosition.x;
            spriteData->instanceData[0].translation.y = basePosition.y;
        }
        spriteData->syncedPosition = basePosition;
    }

    // スプライト本体のサイズと回転を取得してインスタンス行列に反映する
    Vector2 spriteSize = spriteData->sprite->GetSize();
    float spriteRotation = spriteData->sprite->GetRotation();

    for (uint32_t i = 0; i < spriteData->instanceData.size(); ++i)
    {
        const auto &instanceSRT = spriteData->instanceData[i];

        Transform transform;
        // インスタンスのスケールにスプライトサイズを掛け合わせる
        transform.scale.x = instanceSRT.scale.x * spriteSize.x;
        transform.scale.y = instanceSRT.scale.y * spriteSize.y;
        transform.scale.z = 1.0f;
        // Z回転にスプライト本体の回転を加算する
        transform.rotate.x = instanceSRT.rotation.x;
        transform.rotate.y = instanceSRT.rotation.y;
        transform.rotate.z = instanceSRT.rotation.z + spriteRotation;
        transform.translate = instanceSRT.translation;
        // isBackMost が有効な場合は奥に配置する
        transform.translate.z = spriteData->isBackMost ? 10000.0f : 0.0f;

        if (!instanceSRT.isActive)
        {
            transform.scale = {0.0f, 0.0f, 1.0f};
        }

        Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
        Matrix4x4 viewMatrix = MakeIdentity4x4();
        Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
            0.0f, 0.0f,
            float(WinApp::GetVirtualWidth()),
            float(WinApp::GetVirtualHeight()),
            0.0f, 100.0f);

        TransformationMatrix transformMatrix;
        transformMatrix.WVP = worldMatrix * viewMatrix * projectionMatrix;
        transformMatrix.World = worldMatrix;

        spriteData->sprite->SetInstanceTransform(i, transformMatrix);
    }
}

void SpriteManager::DrawSpriteCreationModal()
{
#ifdef USE_IMGUI
    if (showSpriteCreationModal_)
    {
        ImGui::OpenPopup("スプライト生成##modal");
        showSpriteCreationModal_ = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    ImGui::SetNextWindowSize(ImVec2(1080, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("スプライト生成##modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoResize))
    {
        static char nameBuf[128] = "";
        static SpriteTransform tf;
        static bool inited = false;
        if (!inited)
        {
            tf = SpriteTransform();
            inited = true;
        }

        // ---- Name ----
        SectionHeader("[ 名前 ]", DebugTheme::kAccentBlue);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
        ImGui::InputText("##spname", nameBuf, IM_ARRAYSIZE(nameBuf));
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // ---- Texture ----
        SectionHeader("[ テクスチャ ]", DebugTheme::kAccentOrange);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.10f, 0.10f, 0.12f, 1.0f});
        ImGui::BeginChild("TexSel##modal", ImVec2(-1, 360), true);
        ShowTextureFile(texturePath_);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("選択中: %s",
                    texturePath_.empty() ? "(未選択)" : texturePath_.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // ---- Settings ----
        SectionHeader("[ 設定 ]", DebugTheme::kAccentGreen);

        // 位置
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("位置 (X / Y)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
        ImGui::DragFloat2("##sppos", &tf.position.x, 1.0f);
        ImGui::PopStyleColor();

        // 色
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("カラー (R / G / B / A)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::ColorEdit4("##spcol", &tf.color.x);

        // アンカー
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("アンカーポイント (0.0 - 1.0)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat2("##spanc", &tf.anchorPoint.x, 0.0f, 1.0f, "%.2f");

        // フリップ
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("反転");
        ImGui::PopStyleColor();
        ImGui::Checkbox("水平##spfx", &tf.isFlipX);
        ImGui::SameLine();
        ImGui::Checkbox("垂直##spfy", &tf.isFlipY);

        // インスタンス数
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("インスタンス数 (1 - 1000)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputScalar("##spinst", ImGuiDataType_U32, &tf.instanceCount,
                           nullptr, nullptr, nullptr,
                           ImGuiInputTextFlags_CharsDecimal);
        tf.instanceCount = std::clamp(tf.instanceCount, 1u, 1000u);

        ImGui::Spacing();
        ImGui::Separator();

        // ---- バリデーション ----
        bool nameOk = strlen(nameBuf) > 0;
        bool texOk = !texturePath_.empty();
        bool nameUniq = (GetSprite(nameBuf) == nullptr);
        bool canCreate = nameOk && texOk && nameUniq;

        if (!canCreate)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            if (!nameOk)
                ImGui::TextUnformatted("  * スプライト名を入力してください");
            if (!texOk)
                ImGui::TextUnformatted("  * テクスチャファイルを選択してください");
            if (!nameUniq)
                ImGui::TextUnformatted("  * 同名のスプライトが既に存在します");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        // ---- ボタン ----
        auto ResetModal = [&]() {
            memset(nameBuf, 0, sizeof(nameBuf));
            texturePath_ = "";
            tf = SpriteTransform();
            inited = false;
        };

        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        // 名前とテクスチャが揃うまでは確定色にせず、押しても何も起きないことを見た目で示す
        const bool createPressed = canCreate ? ConfirmButton("生成##spcreate", ImVec2(bw, 0))
                                             : NeutralButton("生成##spcreate", ImVec2(bw, 0));
        if (createPressed && canCreate)
        {
            // 生成操作をUndo履歴へ積む（生成前後の差分）
            nlohmann::json before = CaptureUndoState();
            RegisterSprite(nameBuf, texturePath_, tf);
            nlohmann::json after = CaptureUndoState();
            auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(before, after);
            UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
                "スプライト作成: " + std::string(nameBuf), std::move(diffBefore), std::move(diffAfter),
                [](const nlohmann::json &s) { SpriteManager::GetInstance()->RestoreUndoState(s); }));
            // マネージャウィンドウ側トラッカーとの二重登録を防ぐ
            undoTracker_.SkipCurrentGesture();
            ResetModal();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (DangerButton("キャンセル##spcancel", ImVec2(bw, 0)))
        {
            ResetModal();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
#endif // USE_IMGUI
}

void SpriteManager::DrawSpriteManager()
{
#ifdef USE_IMGUI
    // このウィンドウでの編集ジェスチャをUndo履歴として追跡する
    undoTracker_.Begin([this] { return CaptureUndoState(); });

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // ---- 新規作成ボタン ----
    if (ConfirmButton("+ スプライト新規作成##spmain", ImVec2(-1, 0)))
        ShowSpriteCreationModal();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("登録スプライト数: %zu", sprites_.size());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (sprites_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("  スプライトが登録されていません。");
        ImGui::PopStyleColor();
    }
    else
    {
        // 選択中スプライト名・スプライトごとの選択インスタンス番号（UI状態）
        static std::string selectedName;
        static std::unordered_map<std::string, int> selectedInstanceMap;

        // 選択スプライトが消えていたら先頭を選び直す
        if (!FindSpriteByName(selectedName))
            selectedName = sprites_.front()->name;

        // ====================================================
        // リストテーブル（選択・描画順・表示切替・削除）
        // ====================================================
        SectionHeader("[ スプライト一覧 (上が手前に描画 / 名前クリックで選択) ]", DebugTheme::kAccentBlue);

        float tableH = std::min(static_cast<float>(sprites_.size()) * 26.f + 36.f, 160.f);

        if (ImGui::BeginTable("SprList", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(-1, tableH)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("No.", ImGuiTableColumnFlags_WidthFixed, 22.f);
            ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("表示", ImGuiTableColumnFlags_WidthFixed, 34.f);
            ImGui::TableSetupColumn("数", ImGuiTableColumnFlags_WidthFixed, 28.f);
            ImGui::TableSetupColumn("テクスチャ", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("削除", ImGuiTableColumnFlags_WidthFixed, 34.f);
            ImGui::TableHeadersRow();

            std::vector<std::string> toDelete;
            for (size_t i = 0; i < sprites_.size(); ++i)
            {
                auto &sp = sprites_[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                // 順序 & 矢印
                ImGui::TableNextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
                if (i > 0 && ImGui::ArrowButton("U", ImGuiDir_Up))
                    std::swap(sprites_[i], sprites_[i - 1]);
                if (i < sprites_.size() - 1 && ImGui::ArrowButton("D", ImGuiDir_Down))
                    std::swap(sprites_[i], sprites_[i + 1]);
                ImGui::PopStyleVar();

                // 名前（クリックで選択。選択中の行はハイライト表示）
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const bool isSelected = (sp->name == selectedName);
                if (isSelected)
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::GetColorU32({0.62f, 0.50f, 0.74f, 0.25f}));
                }
                if (ImGui::Selectable(sp->name.c_str(), isSelected))
                {
                    selectedName = sp->name;
                }

                // 表示チェック
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_CheckMark,
                                      sp->isVisible ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);
                ImGui::Checkbox("##vis", &sp->isVisible);
                ImGui::PopStyleColor();

                // インスタンス数
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text("%zu", sp->instanceData.size());
                ImGui::PopStyleColor();

                // テクスチャ
                ImGui::TableNextColumn();
                std::string p = sp->textureFilePath;
                if (p.size() > 20)
                    p = ".." + p.substr(p.size() - 18);
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted(p.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", sp->textureFilePath.c_str());

                // 削除
                ImGui::TableNextColumn();
                {
                    ScopedButtonColors danger(DebugTheme::kButtonDanger, DebugTheme::kButtonDangerHover);
                    if (ImGui::SmallButton("削除##del"))
                        toDelete.push_back(sp->name);
                }

                ImGui::PopID();
            }
            for (auto &n : toDelete)
                UnregisterSprite(n);
            ImGui::EndTable();
        }

        ImGui::Spacing();

        // ====================================================
        // 選択中スプライトの詳細（一覧で選んだ1件だけを表示する）
        // ====================================================
        if (SpriteData *sp = FindSpriteByName(selectedName))
        {
            SectionHeader(("[ 詳細編集: " + sp->name + " ]").c_str(), DebugTheme::kAccentPurple);

            ImGui::PushID(sp->name.c_str());
            ImGui::Indent(6.0f);

            // ギズモのバインド先を選択中インスタンスへ追従させる。
            // instanceData の再確保で登録済みポインタが無効になるため、アドレス比較で張り直す
            auto syncGizmoToSelection = [&] {
                if (sp->instanceData.empty())
                    return;
                int &idx = selectedInstanceMap[sp->name];
                idx = std::clamp(idx, 0, static_cast<int>(sp->instanceData.size()) - 1);
                if (gizmoBound_[sp->name] != &sp->instanceData[idx].translation)
                    SyncGizmoTarget(sp, idx);
            };
            syncGizmoToSelection();

            // ---- インスタンス編集（最もよく使うため先頭に配置）----
            uint32_t instCount = static_cast<uint32_t>(sp->instanceData.size());
            if (instCount > 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, {0.15f, 0.35f, 0.30f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.20f, 0.55f, 0.45f, 0.30f});
                bool instOpen = ImGui::TreeNodeEx("インスタンス編集##inst",
                                                  ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor(2);

                if (instOpen)
                {
                    int &selIdx = selectedInstanceMap[sp->name];
                    selIdx = std::clamp(selIdx, 0, static_cast<int>(instCount) - 1);

                    // ---- インスタンス切り替え（◀ コンボ ▶）----
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("編集するインスタンス");
                    ImGui::PopStyleColor();

                    if (ImGui::ArrowButton("##instprev", ImGuiDir_Left))
                        selIdx = (selIdx + static_cast<int>(instCount) - 1) % static_cast<int>(instCount);
                    ImGui::SameLine();
                    const float navBtnW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                    std::string comboLabel = "インスタンス " + std::to_string(selIdx) +
                                             "  (全 " + std::to_string(instCount) + " 個)" +
                                             (sp->instanceData[selIdx].isActive ? "" : " [非表示]");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - navBtnW);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.12f, 0.28f, 0.24f, 1.0f});
                    if (ImGui::BeginCombo("##instsel", comboLabel.c_str()))
                    {
                        for (uint32_t idx = 0; idx < instCount; ++idx)
                        {
                            bool selected = (selIdx == static_cast<int>(idx));
                            std::string label = "インスタンス " + std::to_string(idx) +
                                                (sp->instanceData[idx].isActive ? "" : " [非表示]");
                            if (ImGui::Selectable(label.c_str(), selected))
                                selIdx = static_cast<int>(idx);
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##instnext", ImGuiDir_Right))
                        selIdx = (selIdx + 1) % static_cast<int>(instCount);

                    // ---- インスタンスの追加・削除 ----
                    {
                        float bwAdd = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                        ImGui::BeginDisabled(instCount >= 1000); // Sprite 側の行列バッファ上限
                        if (ConfirmButton("+ 追加##instadd", ImVec2(bwAdd, 0)))
                        {
                            // 選択中インスタンスを複製し、視認しやすいよう少しずらして直後に挿入する
                            InstanceSRT newInst = sp->instanceData[selIdx];
                            newInst.translation.x += 20.0f;
                            newInst.translation.y += 20.0f;
                            sp->instanceData.insert(sp->instanceData.begin() + selIdx + 1, newInst);
                            selIdx++;
                            UpdateSpriteInstances(sp);
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::BeginDisabled(instCount <= 1);
                        if (DangerButton("- 削除##instdel", ImVec2(bwAdd, 0)) && instCount > 1)
                        {
                            sp->instanceData.erase(sp->instanceData.begin() + selIdx);
                            selIdx = std::clamp(selIdx, 0, static_cast<int>(sp->instanceData.size()) - 1);
                            UpdateSpriteInstances(sp);
                        }
                        ImGui::EndDisabled();

                        // 追加・削除で要素数が変わっている可能性があるため取り直す
                        instCount = static_cast<uint32_t>(sp->instanceData.size());
                        selIdx = std::clamp(selIdx, 0, static_cast<int>(instCount) - 1);
                    }

                    // 追加・削除で再確保された場合はここでギズモを張り直す
                    syncGizmoToSelection();

                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("※ビューポートのギズモでも選択中インスタンスを移動できます");
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    // 選択インスタンスの編集
                    InstanceSRT &inst = sp->instanceData[selIdx];

                    // 位置
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("位置 (X / Y)");
                    ImGui::PopStyleColor();
                    float pos[2] = {inst.translation.x, inst.translation.y};
                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.10f, 0.25f, 0.22f, 1.0f});
                    if (ImGui::DragFloat2("##ipos", pos, 1.0f))
                    {
                        inst.translation.x = pos[0];
                        inst.translation.y = pos[1];
                    }
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    // スケール
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("スケール (X / Y)");
                    ImGui::PopStyleColor();
                    float sc[2] = {inst.scale.x, inst.scale.y};
                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.10f, 0.25f, 0.22f, 1.0f});
                    if (ImGui::DragFloat2("##isc", sc, 0.01f, 0.0f, 10.0f))
                    {
                        inst.scale.x = sc[0];
                        inst.scale.y = sc[1];
                    }
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    // 個別回転
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("個別回転 [rad]");
                    ImGui::PopStyleColor();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, {0.20f, 0.70f, 0.55f, 1.0f});
                    ImGui::SliderAngle("##irot", &inst.rotation.z);
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    // 表示フラグ
                    ImGui::Checkbox("このインスタンスを表示##iact", &inst.isActive);

                    // 一括操作（複数インスタンス時のみ表示する）
                    if (instCount > 1)
                    {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                        ImGui::TextUnformatted("一括操作");
                        ImGui::PopStyleColor();

                        float bwInst = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
                        if (PrimaryButton("全て表示##iactall", ImVec2(bwInst, 0)))
                        {
                            for (auto &inst2 : sp->instanceData)
                                inst2.isActive = true;
                        }
                        ImGui::SameLine();
                        if (PrimaryButton("全て非表示##ideactall", ImVec2(bwInst, 0)))
                        {
                            for (auto &inst2 : sp->instanceData)
                                inst2.isActive = false;
                        }
                        ImGui::SameLine();
                        if (PrimaryButton("スケールリセット##iscrs", ImVec2(bwInst, 0)))
                        {
                            for (auto &inst2 : sp->instanceData)
                                inst2.scale = {1.0f, 1.0f, 1.0f};
                        }
                    }

                    ImGui::TreePop();
                }
            }

            // ---- Basic (共通設定) ----
            ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgBlue);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.60f, 0.78f, 0.20f});
            if (ImGui::TreeNodeEx("共通設定##bs", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen))
            {
                // Size - 比率維持 / XY独立 モード切り替え
                {
                    Vector2 sz = sp->sprite->GetSize();

                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("サイズ (全インスタンス共通)");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    const bool locked = sp->lockAspectRatio;
                    if (locked)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kAccentBlue);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.52f, 0.66f, 0.84f, 0.9f});
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.4f, 0.6f, 0.5f});
                    }
                    if (ImGui::SmallButton(locked ? "[比率維持]##lar" : "[XY独立]##lar"))
                        sp->lockAspectRatio = !sp->lockAspectRatio;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(locked ? "クリックでXY独立モードへ切り替え" : "クリックで比率維持モードへ切り替え");
                    ImGui::PopStyleColor(2);

                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
                    if (sp->lockAspectRatio)
                    {
                        const float aspect = (sz.x > 0.0f) ? sz.y / sz.x : 1.0f;
                        float scaleW = sz.x;
                        if (ImGui::DragFloat("##bssz_w", &scaleW, 1.0f, 1.0f, 2000.0f, "W: %.1f"))
                        {
                            scaleW = std::max(1.0f, scaleW);
                            sp->sprite->SetSize({scaleW, scaleW * aspect});
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                        ImGui::Text("H: %.1f  (比率 1 : %.3f)", sz.y, aspect);
                        ImGui::PopStyleColor();
                    }
                    else
                    {
                        float v[2] = {sz.x, sz.y};
                        if (ImGui::DragFloat2("##bssz", v, 1.f, 0.f, 2000.f))
                            sp->sprite->SetSize({v[0], v[1]});
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();

                // Color
                {
                    Vector4 c = sp->sprite->GetColor();
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("カラー (R / G / B / A)");
                    ImGui::PopStyleColor();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::ColorEdit4("##bscol", &c.x, ImGuiColorEditFlags_NoInputs))
                    {
                        sp->sprite->SetColor({c.x, c.y, c.z});
                        sp->sprite->SetAlpha(c.w);
                    }
                }
                ImGui::Spacing();

                // Rotation (共通ベース)
                {
                    float rot = sp->sprite->GetRotation();
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("基準回転 [rad] (全インスタンスに加算)");
                    ImGui::PopStyleColor();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, DebugTheme::kAccentBlue);
                    if (ImGui::SliderAngle("##bsrot", &rot))
                        sp->sprite->SetRotation(rot);
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();

                // Blend mode
                {
                    static const char *bmNames[] = {
                        "なし", "通常", "加算", "減算", "乗算", "スクリーン"};
                    int bm = static_cast<int>(sp->blendMode);
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("ブレンドモード");
                    ImGui::PopStyleColor();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##bsbm", &bm, bmNames, IM_ARRAYSIZE(bmNames)))
                        sp->blendMode = static_cast<BlendMode>(bm);
                }
                ImGui::Spacing();

                // BackMost / Visible
                ImGui::Checkbox("最背面##bkm", &sp->isBackMost);
                ImGui::SameLine();
                ImGui::Checkbox("表示##vis2", &sp->isVisible);

                ImGui::TreePop();
            }
            ImGui::PopStyleColor(2);

            // ---- UV ----
            ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgOrange);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.82f, 0.58f, 0.36f, 0.20f});
            if (ImGui::TreeNodeEx("UV設定##uv", ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                Vector2 uvPos = sp->sprite->GetUVPosition();
                Vector2 uvSz = sp->sprite->GetUVSize();
                float uvRot = sp->sprite->GetUVRotate();
                bool changed = false;

                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("UVスケール (X / Y)");
                ImGui::PopStyleColor();
                float uvszV[2] = {uvSz.x, uvSz.y};
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
                if (ImGui::DragFloat2("##uvsc", uvszV, 0.01f, 0.1f, 10.f))
                {
                    uvSz = {uvszV[0], uvszV[1]};
                    changed = true;
                }
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("UV回転 [rad]");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderAngle("##uvrt", &uvRot))
                    changed = true;

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("UVオフセット (X / Y)");
                ImGui::PopStyleColor();
                float uvposV[2] = {uvPos.x, uvPos.y};
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
                if (ImGui::DragFloat2("##uvpos", uvposV, 0.01f, -2.f, 2.f))
                {
                    uvPos = {uvposV[0], uvposV[1]};
                    changed = true;
                }
                ImGui::PopStyleColor();

                if (changed)
                {
                    sp->sprite->SetUVPosition(uvPos);
                    sp->sprite->SetUVSize(uvSz);
                    sp->sprite->SetUVRotate(uvRot);
                }

                ImGui::Spacing();
                if (ImGui::SmallButton("UVリセット##uvrs"))
                {
                    sp->sprite->SetUVPosition({0, 0});
                    sp->sprite->SetUVSize({1, 1});
                    sp->sprite->SetUVRotate(0);
                }
                ImGui::TreePop();
            }
            ImGui::PopStyleColor(2);

            ImGui::Unindent(6.0f);
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Spacing();

    // ====================================================
    // ファイル操作
    // ====================================================
    SectionHeader("[ ファイル操作 ]", DebugTheme::kAccentOrange);

    // 説明テキスト
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("スプライトは %s/Sprites/<フォルダ名> にJSONとして保存されます。", AssetPath::JsonRoot().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // フォルダ名入力
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("保存/読み込みフォルダ名");
    ImGui::PopStyleColor();
    static char folderBuf[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
    ImGui::InputText("##spfolder", folderBuf, sizeof(folderBuf));
    ImGui::PopStyleColor();
    saveFolder_ = folderBuf;

    ImGui::Spacing();

    if (ConfirmButton("全スプライトを保存##spsvall", ImVec2(-1, 0)))
        SaveAllSprites();
    ImGui::Spacing();
    if (ConfirmButton("全スプライトを読み込み##spldall", ImVec2(-1, 0)))
    {
        Clear();
        LoadAllSprites();
    }

    ImGui::Spacing();

    // 全削除
    if (DangerButton("全スプライトを削除##spdelall", ImVec2(-1, 0)))
        ImGui::OpenPopup("全削除の確認##spdelconfirm");

    // 確認モーダル
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    if (ImGui::BeginPopupModal("全削除の確認##spdelconfirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
        ImGui::TextUnformatted("全スプライトを削除しますか？");
        ImGui::TextUnformatted("この操作は元に戻せません。");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (DangerButton("削除##spdelok", ImVec2(bw, 0)))
        {
            Clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル##spdelcancel", ImVec2(bw, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    ImGui::PopStyleVar(3);

    // 編集ジェスチャが確定していたら差分をUndo履歴へ積む
    undoTracker_.End(
        "スプライト編集",
        [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { SpriteManager::GetInstance()->RestoreUndoState(s); });
#endif // USE_IMGUI
}

void SpriteManager::SetSaveFolder(const std::string &folderName)
{
    saveFolder_ = folderName;
}

void SpriteManager::SaveDrawOrder()
{
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Sprites/" + saveFolder_, "DrawOrder");

    // スプライト名の順序を保存
    for (size_t i = 0; i < sprites_.size(); ++i)
    {
        data->Save("sprite_" + std::to_string(i), sprites_[i]->name);
    }
    data->Save("sprite_count", static_cast<int>(sprites_.size()));
}

void SpriteManager::LoadDrawOrder()
{
    // DrawOrder.jsonファイルが存在するかチェック
    std::string drawOrderPath = AssetPath::Json("Sprites/" + saveFolder_ + "/DrawOrder.json");
    if (!fs::exists(drawOrderPath))
    {
        return;
    }

    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Sprites/" + saveFolder_, "DrawOrder");

    int spriteCount = data->Load<int>("sprite_count", 0);
    if (spriteCount == 0)
        return;

    std::vector<std::string> loadedOrder;
    for (int i = 0; i < spriteCount; ++i)
    {
        std::string spriteName = data->Load<std::string>("sprite_" + std::to_string(i), "");
        if (!spriteName.empty())
        {
            loadedOrder.push_back(spriteName);
        }
    }

    // ロードした順序に基づいてスプライトを並び替え
    std::vector<std::unique_ptr<SpriteData>> reorderedSprites;

    for (const std::string &name : loadedOrder)
    {
        auto it = std::find_if(sprites_.begin(), sprites_.end(),
                               [&name](const std::unique_ptr<SpriteData> &sprite) {
                                   return sprite->name == name;
                               });
        if (it != sprites_.end())
        {
            reorderedSprites.push_back(std::move(*it));
            sprites_.erase(it);
        }
    }

    // 順序リストに含まれなかった残りのスプライトを末尾に追加
    for (auto &sprite : sprites_)
    {
        if (sprite)
        {
            reorderedSprites.push_back(std::move(sprite));
        }
    }

    sprites_ = std::move(reorderedSprites);
}

void SpriteManager::SaveAllSprites()
{
    SaveDrawOrder();

    std::string folderPath = AssetPath::Json("Sprites/" + saveFolder_);
    if (!fs::exists(folderPath))
    {
        fs::create_directories(folderPath);
    }

    for (const auto &spriteData : sprites_)
    {
        if (!spriteData || !spriteData->sprite)
            continue;

        std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Sprites/" + saveFolder_, spriteData->name);

        data->Save("name", spriteData->name);
        data->Save("texturePath", spriteData->textureFilePath);

        Vector2 pos = spriteData->sprite->GetPosition();
        Vector2 size = spriteData->sprite->GetSize();
        Vector4 color = spriteData->sprite->GetColor();
        float rotation = spriteData->sprite->GetRotation();
        Vector2 anchor = spriteData->sprite->GetAnchorPoint();
        Matrix4x4 uvTransform = spriteData->sprite->GetUVTransform();

        data->Save("position", pos);
        data->Save("size", size);
        data->Save("color", color);
        data->Save("rotation", rotation);
        data->Save("anchor", anchor);
        data->Save("uvTransform", uvTransform);
        data->Save("blendMode", static_cast<int>(spriteData->blendMode));

        // アスペクト比ロック状態を保存
        data->Save("lockAspectRatio", static_cast<int>(spriteData->lockAspectRatio));

        // 描画グループを保存
        data->Save("drawGroup", spriteData->drawGroup);

        // インスタンスデータを保存する
        int instCount = static_cast<int>(spriteData->instanceData.size());
        data->Save("instanceCount", instCount);
        for (int idx = 0; idx < instCount; ++idx)
        {
            const auto &inst = spriteData->instanceData[idx];
            std::string prefix = "inst_" + std::to_string(idx) + "_";
            data->Save(prefix + "tx", inst.translation.x);
            data->Save(prefix + "ty", inst.translation.y);
            data->Save(prefix + "sx", inst.scale.x);
            data->Save(prefix + "sy", inst.scale.y);
            data->Save(prefix + "rz", inst.rotation.z);
            data->Save(prefix + "active", static_cast<int>(inst.isActive));
        }
    }
    ImGuiNotification::Post("スプライトデータを保存しました: " + saveFolder_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void SpriteManager::LoadAllSprites()
{
    std::string folderPath = AssetPath::Json("Sprites/" + saveFolder_);

    if (!fs::exists(folderPath) || !fs::is_directory(folderPath))
    {
        return;
    }

    std::vector<std::string> jsonNames;
    for (const auto &entry : fs::directory_iterator(folderPath))
    {
        if (entry.path().extension() == ".json" && entry.path().stem().string() != "DrawOrder")
        {
            jsonNames.push_back(entry.path().stem().string());
        }
    }

    for (const auto &name : jsonNames)
    {
        std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Sprites/" + saveFolder_, name);

        std::string spriteName = data->Load<std::string>("name", "");
        std::string texturePath = data->Load<std::string>("texturePath", "");

        Vector2 position = data->Load<Vector2>("position", {0.0f, 0.0f});
        Vector2 size = data->Load<Vector2>("size", {300.0f, 300.0f});
        Vector4 color = data->Load<Vector4>("color", {1.0f, 1.0f, 1.0f, 1.0f});
        float rotation = data->Load<float>("rotation", 0.0f);
        Vector2 anchor = data->Load<Vector2>("anchor", {0.0f, 0.0f});
        Matrix4x4 uvTransform = data->Load<Matrix4x4>("uvTransform", MakeIdentity4x4());
        int blendModeInt = data->Load<int>("blendMode", static_cast<int>(BlendMode::Normal));

        // アスペクト比ロック状態を復元（旧データには存在しないためデフォルトfalse）
        bool lockAspectRatio = static_cast<bool>(data->Load<int>("lockAspectRatio", 0));

        // 描画グループを復元（旧データには存在しないため "UI"。"3D" 以外はUIに正規化）
        std::string drawGroup = data->Load<std::string>("drawGroup", "UI");
        if (drawGroup != "3D")
        {
            drawGroup = "UI";
        }

        // インスタンスデータを復元する（旧データは1インスタンスとして扱う）
        int savedInstCount = data->Load<int>("instanceCount", 1);

        SpriteTransform transform;
        transform.position = position;
        transform.color = color;
        transform.anchorPoint = anchor;
        transform.instanceCount = static_cast<uint32_t>(savedInstCount);

        RegisterSprite(spriteName, texturePath, transform);

        auto sprite = GetSprite(spriteName);
        if (sprite && sprite->sprite)
        {
            sprite->sprite->SetSize(size);
            sprite->sprite->SetRotation(rotation);
            sprite->sprite->SetUVTransform(uvTransform);
            sprite->blendMode = static_cast<BlendMode>(blendModeInt);
            sprite->lockAspectRatio = lockAspectRatio;
            sprite->drawGroup = drawGroup;
            DrawGroupManager::GetInstance()->RegisterGroup(drawGroup);

            // 保存されたインスタンスデータを反映する
            for (int idx = 0; idx < savedInstCount && idx < static_cast<int>(sprite->instanceData.size()); ++idx)
            {
                std::string prefix = "inst_" + std::to_string(idx) + "_";
                sprite->instanceData[idx].translation.x = data->Load<float>(prefix + "tx", position.x);
                sprite->instanceData[idx].translation.y = data->Load<float>(prefix + "ty", position.y);
                sprite->instanceData[idx].scale.x = data->Load<float>(prefix + "sx", 1.0f);
                sprite->instanceData[idx].scale.y = data->Load<float>(prefix + "sy", 1.0f);
                sprite->instanceData[idx].rotation.z = data->Load<float>(prefix + "rz", 0.0f);
                sprite->instanceData[idx].isActive = static_cast<bool>(data->Load<int>(prefix + "active", 1));
            }
        }
    }

    LoadDrawOrder();
    ImGuiNotification::Post("スプライトデータを読み込みました: " + saveFolder_, {0.2f, 0.8f, 0.8f, 1.0f});
}

#ifdef USE_IMGUI
// -------------------------------------------------------
// Undo/Redo 用の状態キャプチャ・復元
// -------------------------------------------------------

nlohmann::json SpriteManager::CaptureUndoState()
{
    using nlohmann::json;
    json state = json::object();
    json order = json::array();

    for (auto &sp : sprites_)
    {
        if (!sp || !sp->sprite)
        {
            continue;
        }
        order.push_back(sp->name);

        json s;
        s["texturePath"] = sp->textureFilePath;
        s["position"] = sp->sprite->GetPosition();
        s["size"] = sp->sprite->GetSize();
        s["color"] = sp->sprite->GetColor();
        s["rotation"] = sp->sprite->GetRotation();
        s["anchor"] = sp->sprite->GetAnchorPoint();
        s["flipX"] = sp->sprite->GetFlipX();
        s["flipY"] = sp->sprite->GetFlipY();
        s["uvTransform"] = sp->sprite->GetUVTransform();
        s["blendMode"] = static_cast<int>(sp->blendMode);
        s["lockAspectRatio"] = sp->lockAspectRatio;
        s["drawGroup"] = sp->drawGroup;
        s["isVisible"] = sp->isVisible;
        s["isBackMost"] = sp->isBackMost;

        json instances = json::array();
        for (const auto &inst : sp->instanceData)
        {
            json ij;
            ij["scale"] = inst.scale;
            ij["rotation"] = inst.rotation;
            ij["translation"] = inst.translation;
            ij["active"] = inst.isActive;
            instances.push_back(ij);
        }
        s["instances"] = instances;

        state[sp->name] = s;
    }
    state["__order"] = order;
    return state;
}

void SpriteManager::RestoreUndoState(const nlohmann::json &state)
{
    using nlohmann::json;
    if (!state.is_object())
    {
        return;
    }

    for (auto it = state.begin(); it != state.end(); ++it)
    {
        const std::string &name = it.key();
        if (name == "__order")
        {
            continue; // 描画順は最後にまとめて処理する
        }

        // null = このスプライトは存在しない状態へ戻す（削除）
        if (it.value().is_null())
        {
            UnregisterSprite(name);
            continue;
        }

        const json &s = it.value();
        SpriteData *sp = FindSpriteByName(name);

        // 存在しなければ再生成（削除のUndo）
        if (!sp)
        {
            SpriteTransform tf;
            tf.position = s.value("position", Vector2{0.0f, 0.0f});
            tf.color = s.value("color", Vector4{1.0f, 1.0f, 1.0f, 1.0f});
            tf.anchorPoint = s.value("anchor", Vector2{0.0f, 0.0f});
            const size_t instCount = s.contains("instances") ? s["instances"].size() : 1;
            tf.instanceCount = static_cast<uint32_t>(instCount > 0 ? instCount : 1);
            RegisterSprite(name, s.value("texturePath", std::string()), tf);
            sp = FindSpriteByName(name);
            if (!sp || !sp->sprite)
            {
                continue;
            }
        }

        // 各フィールドを適用する
        if (s.contains("texturePath"))
        {
            sp->textureFilePath = s["texturePath"].get<std::string>();
            sp->sprite->SetTexturePath(sp->textureFilePath);
        }
        if (s.contains("position"))
        {
            sp->sprite->SetPosition(s["position"].get<Vector2>());
        }
        if (s.contains("size"))
        {
            sp->sprite->SetSize(s["size"].get<Vector2>());
        }
        if (s.contains("color"))
        {
            Vector4 color = s["color"].get<Vector4>();
            sp->sprite->SetColor({color.x, color.y, color.z});
            sp->sprite->SetAlpha(color.w);
        }
        if (s.contains("rotation"))
        {
            sp->sprite->SetRotation(s["rotation"].get<float>());
        }
        if (s.contains("anchor"))
        {
            sp->sprite->SetAnchorPoint(s["anchor"].get<Vector2>());
        }
        if (s.contains("flipX"))
        {
            sp->sprite->SetFlipX(s["flipX"].get<bool>());
        }
        if (s.contains("flipY"))
        {
            sp->sprite->SetFlipY(s["flipY"].get<bool>());
        }
        if (s.contains("uvTransform"))
        {
            sp->sprite->SetUVTransform(s["uvTransform"].get<Matrix4x4>());
        }
        if (s.contains("blendMode"))
        {
            sp->blendMode = static_cast<BlendMode>(s["blendMode"].get<int>());
        }
        if (s.contains("lockAspectRatio"))
        {
            sp->lockAspectRatio = s["lockAspectRatio"].get<bool>();
        }
        if (s.contains("drawGroup"))
        {
            sp->drawGroup = s["drawGroup"].get<std::string>();
            DrawGroupManager::GetInstance()->RegisterGroup(sp->drawGroup);
        }
        if (s.contains("isVisible"))
        {
            sp->isVisible = s["isVisible"].get<bool>();
        }
        if (s.contains("isBackMost"))
        {
            sp->isBackMost = s["isBackMost"].get<bool>();
        }

        // インスタンスデータの復元
        if (s.contains("instances") && s["instances"].is_array())
        {
            const json &instances = s["instances"];
            const size_t newCount = instances.size();
            const bool countChanged = (newCount != sp->instanceData.size());
            sp->instanceData.resize(newCount);
            for (size_t i = 0; i < newCount; ++i)
            {
                const json &ij = instances[i];
                auto &inst = sp->instanceData[i];
                inst.scale = ij.value("scale", Vector3{1.0f, 1.0f, 1.0f});
                inst.rotation = ij.value("rotation", Vector3{0.0f, 0.0f, 0.0f});
                inst.translation = ij.value("translation", Vector3{0.0f, 0.0f, 0.0f});
                inst.isActive = ij.value("active", true);
            }
            if (countChanged)
            {
                sp->sprite->SetInstanceCount(static_cast<uint32_t>(newCount));
                // instanceData の再確保でギズモ登録済みポインタが無効になるため登録し直す
                SyncGizmoTarget(sp, 0);
            }
        }

        // 復元した instanceData が基準位置の差分反映で上書きされないよう同期を取り直す
        sp->syncedPosition = sp->sprite->GetPosition();

        UpdateSpriteInstances(sp);
    }

    // 描画順の復元
    auto orderIt = state.find("__order");
    if (orderIt != state.end() && orderIt->is_array())
    {
        std::vector<std::unique_ptr<SpriteData>> reordered;
        reordered.reserve(sprites_.size());
        for (const auto &nameJson : *orderIt)
        {
            const std::string name = nameJson.get<std::string>();
            auto found = std::find_if(sprites_.begin(), sprites_.end(),
                                      [&name](const std::unique_ptr<SpriteData> &sp) {
                                          return sp && sp->name == name;
                                      });
            if (found != sprites_.end())
            {
                reordered.push_back(std::move(*found));
                sprites_.erase(found);
            }
        }
        // 順序リストに含まれないスプライトは現在の順序のまま末尾へ
        for (auto &sp : sprites_)
        {
            if (sp)
            {
                reordered.push_back(std::move(sp));
            }
        }
        sprites_ = std::move(reordered);
    }
}
#endif // USE_IMGUI
} // namespace Hagine
