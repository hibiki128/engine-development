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
#include <algorithm>
#include <icon/IconsFontAwesome5.h>

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

void SpriteManager::RemoveOwnedSprites()
{
    // 名前を先に控える（UnregisterSprite が sprites_ を書き換えるため）
    std::vector<std::string> names;
    names.reserve(sprites_.size());
    for (const auto &sprite : sprites_)
    {
        names.push_back(sprite->name);
    }
    ImGuiNotification::ScopedMute mute; // 1件ずつ「削除しました」を出さない
    for (const std::string &name : names)
    {
        UnregisterSprite(name);
    }
}

std::string SpriteManager::MakeUniqueSpriteName(const std::string &baseName)
{
    const std::string base = baseName.empty() ? std::string("sprite") : baseName;
    if (!FindSpriteByName(base))
    {
        return base;
    }
    for (int index = 1;; ++index)
    {
        const std::string candidate = base + "_" + std::to_string(index);
        if (!FindSpriteByName(candidate))
        {
            return candidate;
        }
    }
}

SpriteData *SpriteManager::DuplicateSprite(const std::string &name)
{
    SpriteData *source = FindSpriteByName(name);
    if (!source || !source->sprite)
    {
        return nullptr;
    }

    const std::string newName = MakeUniqueSpriteName(source->name);
    SpriteTransform transform;
    transform.position = source->sprite->GetPosition();
    transform.color = source->sprite->GetColor();
    transform.anchorPoint = source->sprite->GetAnchorPoint();
    transform.isFlipX = source->sprite->GetFlipX();
    transform.isFlipY = source->sprite->GetFlipY();
    transform.instanceCount = static_cast<uint32_t>(std::max<size_t>(1, source->instanceData.size()));
    RegisterSprite(newName, source->textureFilePath, transform);

    // RegisterSprite で sprites_ が伸びて source が動くことは無い（unique_ptr の指す先は不変）
    SpriteData *copy = FindSpriteByName(newName);
    copy->sprite->SetSize(source->sprite->GetSize());
    copy->sprite->SetRotation(source->sprite->GetRotation());
    copy->sprite->SetUVPosition(source->sprite->GetUVPosition());
    copy->sprite->SetUVSize(source->sprite->GetUVSize());
    copy->sprite->SetUVRotate(source->sprite->GetUVRotate());
    copy->blendMode = source->blendMode;
    copy->lockAspectRatio = source->lockAspectRatio;
    copy->isBackMost = source->isBackMost;
    copy->isVisible = source->isVisible;
    copy->drawGroup = source->drawGroup;
    copy->instanceData = source->instanceData;
    // 元と完全に重ねると掴めないので少しずらす
    for (InstanceSRT &instance : copy->instanceData)
    {
        instance.translation.x += 16.0f;
        instance.translation.y += 16.0f;
    }
    copy->syncedPosition = copy->sprite->GetPosition();
    UpdateSpriteInstances(copy);
#ifdef USE_IMGUI
    SyncGizmoTarget(copy, 0); // instanceData を差し替えたのでギズモの指す先も張り直す
#endif

    // 描画順は複製元のすぐ手前にする（末尾へ足すと、関係ない物より手前に出てしまう）
    MoveDrawOrder(newName, FindSpriteIndex(source->name) + 1);
    return copy;
}

void SpriteManager::MoveDrawOrder(const std::string &name, int toIndex)
{
    const int from = FindSpriteIndex(name);
    if (from < 0)
    {
        return;
    }
    const int to = std::clamp(toIndex, 0, static_cast<int>(sprites_.size()) - 1);
    if (from == to)
    {
        return;
    }
    std::unique_ptr<SpriteData> moving = std::move(sprites_[from]);
    sprites_.erase(sprites_.begin() + from);
    sprites_.insert(sprites_.begin() + to, std::move(moving));
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

    // 消したスプライトのファイルを片付ける。
    // 読み込みはフォルダ内の .json を全部読むので、残すと消した物が次の読み込みで復活してしまう
    {
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(folderPath, error))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            const std::u8string stemUtf8 = entry.path().stem().u8string();
            const std::string stem(stemUtf8.begin(), stemUtf8.end());
            if (stem != "DrawOrder" && !FindSpriteByName(stem))
            {
                fs::remove(entry.path(), error);
            }
        }
    }

    for (const auto &spriteData : sprites_)
    {
        if (!spriteData || !spriteData->sprite)
            continue;

        std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Sprites/" + saveFolder_, spriteData->name);
        data->Save("flipX", static_cast<int>(spriteData->sprite->GetFlipX()));
        data->Save("flipY", static_cast<int>(spriteData->sprite->GetFlipY()));
        data->Save("isBackMost", static_cast<int>(spriteData->isBackMost));
        data->Save("isVisible", static_cast<int>(spriteData->isVisible));

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
        // 名前に日本語が入っていても壊れないよう UTF-8 で取り出す
        const std::u8string stemUtf8 = entry.path().stem().u8string();
        const std::string stem(stemUtf8.begin(), stemUtf8.end());
        if (entry.path().extension() == ".json" && stem != "DrawOrder")
        {
            jsonNames.push_back(stem);
        }
    }

    // 読み込みは「置き換え」。今あるスプライトと同じ名前を二重に登録しないよう、所有分を先に片付ける
    // （ゲーム側が RegisterExternal したスプライトはそのまま）
    RemoveOwnedSprites();

    // 1件ずつ「登録しました」を出さない（最後にまとめて知らせる）
    std::optional<ImGuiNotification::ScopedMute> mute;
    mute.emplace();
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
        // 後から足した項目なので、無い古いデータでは既定値のまま
        transform.isFlipX = static_cast<bool>(data->Load<int>("flipX", 0));
        transform.isFlipY = static_cast<bool>(data->Load<int>("flipY", 0));

        RegisterSprite(spriteName, texturePath, transform);

        auto sprite = GetSprite(spriteName);
        if (sprite && sprite->sprite)
        {
            sprite->sprite->SetSize(size);
            sprite->sprite->SetRotation(rotation);
            sprite->sprite->SetUVTransform(uvTransform);
            sprite->blendMode = static_cast<BlendMode>(blendModeInt);
            sprite->lockAspectRatio = lockAspectRatio;
            sprite->isBackMost = static_cast<bool>(data->Load<int>("isBackMost", 0));
            sprite->isVisible = static_cast<bool>(data->Load<int>("isVisible", 1));
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
    mute.reset();
    ImGuiNotification::Post(std::format("スプライトを読み込みました: {}（{} 個）", saveFolder_, sprites_.size()),
                            {0.2f, 0.8f, 0.8f, 1.0f});
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

    // 復元は人が押した操作ではないので、登録・削除のトーストは止める（履歴には残る）
    ImGuiNotification::ScopedMute mute;

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
