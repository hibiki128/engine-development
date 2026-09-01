#include "BaseObjectManager.h"
#include "SpriteManager.h"
#include <2d/ui/UIAnimator.h>
#include <metaball/MetaBallObject.h>
#include <asset/AssetPath.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "debug/imgui/ImGuizmoManager.h"
#include "ImGuizmo.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#endif // USE_IMGUI
#include "edit/motion/MotionEditor.h"
#include "object/Object3dInstancing.h"
#include <debug/log/Logger.h>
#include <browser/ShowFolder.h>
#include "render/DrawGroupManager.h"

namespace Hagine {
void BaseObjectManager::Finalize()
{
    RemoveAllObjects();

    // 外部登録（シーンが unique_ptr を持つゲームエンティティ）の参照も落とす。
    // 実体を破棄するのはシーン側なので、ここでは参照を切るだけにする。
    for (auto &[name, obj] : objects_)
    {
        DetachRegistrations(obj, name);
    }
    objects_.clear();
}

void BaseObjectManager::RemoveAllObjects()
{
    // 消すのは「このマネージャが所有しているオブジェクト」だけ。
    // RegisterExternal で登録された、シーンが所有するゲームエンティティ（Player 等）まで
    // 消してしまうと更新・描画の対象から外れてしまう。
    //
    // 以前はここで objects_ を丸ごと clear し、さらに ImGuizmoManager::DeleteTarget()
    // （＝全操作対象を消す）を呼んでいたため、「オブジェクト全削除」やシーン読み込みのたびに
    // スプライト・ライト・パーティクルのギズモ登録まで巻き添えで消えていた。
    std::vector<std::string> ownedNames;
    ownedNames.reserve(ownedObjects_.size());
    for (const auto &[name, owned] : ownedObjects_)
    {
        ownedNames.push_back(name);
    }

    // RemoveObject が親子関係の解除・各マネージャからの登録解除まで面倒を見る
    for (const std::string &name : ownedNames)
    {
        RemoveObject(name);
    }
}

void BaseObjectManager::RemoveObjectByName(const std::string &name)
{
    // 削除処理の本体は RemoveObject に一本化する
    // （以前は親子関係の解除を行わない別実装になっていて、解放済みの親を掴んだ子が残っていた）
    if (objects_.find(name) == objects_.end())
    {
        return;
    }
    RemoveObject(name);
    ImGuiNotification::Post("オブジェクトを削除しました: " + name, {0.9f, 0.7f, 0.2f, 1.0f});
}

void BaseObjectManager::RegisterExternal(BaseObject *obj)
{
    const std::string &name = obj->GetName();
#ifdef USE_IMGUI
    ImGuizmoManager::GetInstance()->AddTarget(name, obj);
#endif
    MotionEditor::GetInstance()->Register(obj);
    objects_.emplace(name, obj);
    ImGuiNotification::Post("オブジェクトを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void BaseObjectManager::UnregisterExternal(BaseObject *obj)
{
    if (!obj)
    {
        return;
    }
    DetachRegistrations(obj, obj->GetName());
    objects_.erase(obj->GetName());
}

// 破棄・登録解除の直前に、他マネージャが持つこのオブジェクトへの参照を全て落とす。
// これを忘れると解放済みポインタが編集UI側に残り、次にそれを触った時に落ちる。
void BaseObjectManager::DetachRegistrations(BaseObject *obj, const std::string &name)
{
#ifdef USE_IMGUI
    ImGuizmoManager::GetInstance()->RemoveTarget(name);
#else
    (void)name;
#endif
    if (obj)
    {
        MotionEditor::GetInstance()->Unregister(obj);
    }
}

void BaseObjectManager::AddObject(std::unique_ptr<BaseObject> baseObject)
{
    auto *ptr = baseObject.get();
    const std::string &name = baseObject->GetName();
    ownedObjects_.emplace(name, std::move(baseObject));
    RegisterExternal(ptr);
}

void BaseObjectManager::Update()
{
    for (auto &[name, obj] : objects_)
    {
        obj->UpdateHierarchy();
        obj->UpdateWorldTransformHierarchy();
    }
}

void BaseObjectManager::Draw(const ViewProjection &viewProjection)
{
    // 同じモデルを参照するオブジェクトを1回の描画にまとめる。
    // ここで囲んだ範囲の BaseObject::Draw がバッチャへ積み、Flush でまとめて描く。
    // 積めなかったもの（スキニング・半透明・ワイヤーフレーム等）はその場で従来どおり描かれる。
    Object3dInstancing *instancing = Object3dInstancing::GetInstance();
    instancing->Begin();
    for (auto &[name, obj] : objects_)
    {
        obj->Draw(viewProjection);
    }
    instancing->Flush(viewProjection);
}

void BaseObjectManager::UpdateImGui()
{
#ifdef USE_IMGUI
    // オブジェクトへの編集ジェスチャ（ImGuiウィジェット・ギズモドラッグ）をUndo履歴として追跡する
    undoTracker_.Begin([this] { return CaptureUndoState(); });

    DrawSceneSaveModel();
    DrawSceneLoadModel();
    DrawObjectCreationModel();
    DrawObjectLoadModel();

    undoTracker_.End(
        "オブジェクト編集",
        [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); },
        ImGuizmo::IsUsing());
#endif // USE_IMGUI
}

void BaseObjectManager::SaveAll()
{
    for (auto &[name, obj] : objects_)
    {
        if (obj->GetShouldSave())
        { // セーブ対象フラグをチェック
            obj->SetFolderPath("SceneData/" + sceneName_ + "/ObjectDatas");
            obj->SceneSaveToJson();
            obj->SaveParentChildRelationship();
        }
    }
    ImGuiNotification::Post("全オブジェクトを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
}

void BaseObjectManager::LoadAll(std::string sceneName)
{
    // シーンデータのフォルダパスを構築（jsons ルートからの相対パスと、走査用の実パス）
    const std::string objectDataFolder = "SceneData/" + sceneName + "/ObjectDatas";
    const std::string sceneDataPath = AssetPath::Json(objectDataFolder);

    // フォルダが存在するかチェック
    if (!std::filesystem::exists(sceneDataPath))
    {
        // フォルダが存在しない場合は何もしない
        return;
    }

    // JSONファイルを検索
    std::vector<std::string> jsonFiles;
    for (const auto &entry : std::filesystem::directory_iterator(sceneDataPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            jsonFiles.push_back(entry.path().filename().string());
        }
    }

    // 既存のオブジェクトをクリア
    RemoveAllObjects();

    // 各JSONファイルを読み込んでオブジェクトを生成
    for (const std::string &jsonFile : jsonFiles)
    {
        // JSONファイル名から拡張子を除去してオブジェクト名とする
        std::string objectName = jsonFile.substr(0, jsonFile.find_last_of('.'));

        // モデル名を先に見て、どのクラスとして作り直すかを決める
        std::unique_ptr<DataHandler> ObjectDatas = std::make_unique<DataHandler>(objectDataFolder, objectName);
        const std::string modelPath = ObjectDatas->Load<std::string>("modelName", "");
        const bool isMetaBall = (modelPath == kMetaBallModelTag);

        // 新しいオブジェクトを作成
        std::unique_ptr<BaseObject> newObject =
            isMetaBall ? std::unique_ptr<BaseObject>(std::make_unique<MetaBallObject>())
                       : std::make_unique<BaseObject>();
        // フォルダパスを設定
        newObject->SetFolderPath(objectDataFolder);
        // オブジェクト名でInit
        newObject->Init(objectName);
        newObject->SetIsScene(true);

        // モデルとテクスチャを設定
        if (isMetaBall)
        {
            // MetaBallObject::Init が動的モデルを作り済み。要素リストだけ戻す
            static_cast<MetaBallObject *>(newObject.get())->LoadMetaBallFromJson();
        }
        else if (!modelPath.empty())
        {
            newObject->CreateModel(modelPath);
        }
        else
        {
            newObject->CreatePrimitiveModel(newObject->GetPrimitiveType());
        }

        // オブジェクトマネージャーに追加
        this->AddObject(std::move(newObject));
    }

    // 全オブジェクト読み込み後に親子関係を復元
    LoadAllParentChildRelationships();
    ImGuiNotification::Post("シーンを読み込みました: " + sceneName, {0.2f, 0.8f, 0.8f, 1.0f});
}

std::string BaseObjectManager::MakeUniqueObjectName(const std::string &baseName) const
{
    if (objects_.find(baseName) == objects_.end())
    {
        return baseName;
    }
    // 同名があったら連番を振る。番号が埋まっていても空きが見つかるまで進める
    for (int index = 1;; ++index)
    {
        const std::string candidate = baseName + "_" + std::to_string(index);
        if (objects_.find(candidate) == objects_.end())
        {
            return candidate;
        }
    }
}

void BaseObjectManager::CreateObject(std::string objectName, std::string modelPath, std::string texturePath)
{
    // 同名オブジェクトを作ると unordered_map のキーが衝突して片方が行方不明になるため、必ず一意にする
    objectName = MakeUniqueObjectName(objectName);

    std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
    newObject->Init(objectName);
    newObject->CreateModel(modelPath);
    for (int i = 0; i < newObject->GetObject3d()->GetMaterialCount(); i++)
    {
        newObject->SetTexture(texturePath, i);
    }
#ifdef USE_IMGUI
    // 原点に出すと毎回カメラを原点まで戻す羽目になるので、今見ている場所の前に出す
    newObject->GetLocalPosition() = ImGuizmoManager::GetInstance()->GetSpawnPosition();
#endif // USE_IMGUI
    this->AddObject(std::move(newObject));
    ImGuiNotification::Post("オブジェクトを作成しました: " + objectName, {0.2f, 0.8f, 0.2f, 1.0f});
}

BaseObject *BaseObjectManager::CreateObjectFromModel(const std::string &modelPath, const Vector3 &position)
{
    if (modelPath.empty())
    {
        return nullptr;
    }

    // 名前はモデルのファイル名（拡張子なし）を元にする
    const std::string name = MakeUniqueObjectName(std::filesystem::path(modelPath).stem().string());

    std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
    newObject->Init(name);
    newObject->CreateModel(modelPath);
    newObject->GetLocalPosition() = position;

    this->AddObject(std::move(newObject));
    ImGuiNotification::Post("オブジェクトを配置しました: " + name, {0.2f, 0.8f, 0.2f, 1.0f});
    return GetObjectByName(name);
}

BaseObject *BaseObjectManager::CreatePrimitiveObject(PrimitiveType type, const std::string &baseName)
{
    const std::string name = MakeUniqueObjectName(baseName);

    std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
    newObject->SetPrimitive(true);
    newObject->Init(name);
    newObject->CreatePrimitiveModel(type);
#ifdef USE_IMGUI
    // 原点ではなく今見ている場所の前に出す
    newObject->GetLocalPosition() = ImGuizmoManager::GetInstance()->GetSpawnPosition();
#endif // USE_IMGUI

    this->AddObject(std::move(newObject));
    return GetObjectByName(name);
}

BaseObject *BaseObjectManager::CreateMetaBallObject(const std::string &baseName)
{
    const std::string name = MakeUniqueObjectName(baseName);

    // MetaBallObject::Init が動的モデルの生成と初期要素の配置まで済ませる
    std::unique_ptr<MetaBallObject> newObject = std::make_unique<MetaBallObject>();
    newObject->Init(name);
#ifdef USE_IMGUI
    // 原点ではなく今見ている場所の前に出す
    newObject->GetLocalPosition() = ImGuizmoManager::GetInstance()->GetSpawnPosition();
#endif // USE_IMGUI

    this->AddObject(std::move(newObject));
    return GetObjectByName(name);
}

BaseObject *BaseObjectManager::GetObjectByName(const std::string &name)
{
    auto it = objects_.find(name);
    if (it != objects_.end())
    {
        return it->second;
    }
    return nullptr;
}

// メニューからモーダルを開くメソッド
void BaseObjectManager::OpenSceneSaveModal()
{
    showSceneSaveModal_ = true;
}

void BaseObjectManager::OpenSceneLoadModal()
{
    showSceneLoadModal_ = true;
}

void BaseObjectManager::OpenObjectCreationModal()
{
    showObjectCreationModal_ = true;
}

void BaseObjectManager::OpenObjectLoadModal()
{
    showObjectLoadModal_ = true;
}

namespace {
// 階層ツリーのドラッグ＆ドロップ結果は、ツリー描画中に親子を変更すると
// イテレータが壊れるため、フレーム末にまとめて適用する
std::string g_dndReparentChild;  // ドラッグされた子オブジェクト名
std::string g_dndReparentParent; // ドロップ先の親（空文字 = ルートへ解除）
bool g_dndReparentRequested = false;
} // namespace

void BaseObjectManager::ShowParentChildHierarchy()
{
#ifdef USE_IMGUI

    if (ImGui::CollapsingHeader("階層エディター", ImGuiTreeNodeFlags_DefaultOpen))
    {

        // 親子付けは下のツリーで直感的に操作する（右クリックメニュー / ドラッグ&ドロップ）
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.80f, 0.92f, 1.0f));
        ImGui::TextWrapped("親子付けの操作:");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::BulletText("ノードを右クリック →「親を設定」「親子解除」");
        ImGui::BulletText("右クリックメニューで「継承(位置/回転/スケール)」も切替可能");
        ImGui::BulletText("ドラッグして別ノードに重ねても親子付け（余白へドロップで解除）");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("階層表示:");
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（ノードをドラッグして別ノードに重ねると親子付け / 余白へドロップで解除）");
        ImGui::PopStyleColor();

        // 階層構造を表示
        ImGui::BeginChild("HierarchyView", ImVec2(0, 300), ImGuiChildFlags_Borders);

        // objects_ は unordered_map なので、そのまま回すと並び順がハッシュ順（実質ランダム）になり、
        // オブジェクトを増減させるたびに一覧の位置が変わってしまう。名前順に並べて安定させる。
        for (const std::string &name : GetSortedObjectNames())
        {
            BaseObject *obj = GetObjectByName(name);
            if (obj && !obj->GetParent())
            { // ルートオブジェクトのみ表示
                ShowObjectHierarchy(obj, 0);
            }
        }

        // 余白へのドロップでルート（親なし）へ解除できるようにする
        ImVec2 dropAvail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(dropAvail.x, dropAvail.y > 8.0f ? dropAvail.y : 8.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("OBJ_NODE"))
            {
                g_dndReparentChild = static_cast<const char *>(p->Data);
                g_dndReparentParent.clear();
                g_dndReparentRequested = true;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndChild();

        // ドラッグ＆ドロップの結果をツリー描画後にまとめて適用する
        if (g_dndReparentRequested)
        {
            if (g_dndReparentParent.empty())
            {
                RemoveParentChild(g_dndReparentChild);
                ImGuiNotification::Post("親子付けを解除しました: " + g_dndReparentChild, {0.82f, 0.58f, 0.36f, 1.0f});
            }
            else if (g_dndReparentChild != g_dndReparentParent)
            {
                SetParentChild(g_dndReparentChild, g_dndReparentParent);
                BaseObject *c = GetObjectByName(g_dndReparentChild);
                BaseObject *p = GetObjectByName(g_dndReparentParent);
                if (c && c->GetParent() == p)
                {
                    ImGuiNotification::Post(g_dndReparentChild + " を " + g_dndReparentParent + " の子にしました", {0.45f, 0.68f, 0.52f, 1.0f});
                }
                else
                {
                    ImGuiNotification::Post("親子付けできません（循環参照など）", {0.82f, 0.58f, 0.36f, 1.0f});
                }
            }
            g_dndReparentRequested = false;
            g_dndReparentChild.clear();
            g_dndReparentParent.clear();
        }
    }
#endif // USE_IMGUI
}

void BaseObjectManager::ShowObjectHierarchy(BaseObject *obj, int depth)
{
#ifdef USE_IMGUI

    if (!obj)
        return;

    // インデントを設定
    std::string indent(depth * 2, ' ');
    std::string displayName = indent + obj->GetName();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    // 子がない場合は葉ノードフラグを追加
    if (obj->GetChildren()->empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);

    // ドラッグ元: このノード
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        const std::string &dragName = obj->GetName();
        ImGui::SetDragDropPayload("OBJ_NODE", dragName.c_str(), dragName.size() + 1);
        ImGui::Text("移動: %s", dragName.c_str());
        ImGui::EndDragDropSource();
    }
    // ドロップ先: このノードを親にする
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("OBJ_NODE"))
        {
            g_dndReparentChild = static_cast<const char *>(p->Data);
            g_dndReparentParent = obj->GetName();
            g_dndReparentRequested = true;
        }
        ImGui::EndDragDropTarget();
    }

    // 右クリックメニュー: 親を設定 / 親子解除 / 親のSRT継承設定
    if (ImGui::BeginPopupContextItem((obj->GetName() + "##ctx").c_str()))
    {
        ImGui::TextDisabled("%s", obj->GetName().c_str());
        ImGui::Separator();

        if (ImGui::BeginMenu("親を設定"))
        {
            for (const std::string &otherName : GetSortedObjectNames())
            {
                BaseObject *other = GetObjectByName(otherName);
                if (!other || other == obj)
                    continue;
                // 循環になる相手（自分の子孫）は候補から除外する
                bool isDescendant = false;
                for (BaseObject *p = other; p; p = p->GetParent())
                {
                    if (p == obj)
                    {
                        isDescendant = true;
                        break;
                    }
                }
                if (isDescendant)
                    continue;

                const bool isCurrentParent = (obj->GetParent() == other);
                if (ImGui::MenuItem(otherName.c_str(), nullptr, isCurrentParent))
                {
                    // 実際の付け替えはツリー描画後にまとめて適用する（g_dnd 経由）
                    g_dndReparentChild = obj->GetName();
                    g_dndReparentParent = otherName;
                    g_dndReparentRequested = true;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("親子解除", nullptr, false, obj->GetParent() != nullptr))
        {
            g_dndReparentChild = obj->GetName();
            g_dndReparentParent.clear();
            g_dndReparentRequested = true;
        }

        // 親がある場合のみ、親のSRTをどこまで継承するかを切り替えられる
        if (obj->GetParent() && obj->GetWorldTransform())
        {
            ImGui::Separator();
            ImGui::TextDisabled("親の継承 (外すと追従しない)");
            WorldTransform *t = obj->GetWorldTransform();
            ImGui::Checkbox("位置を継承##inh", &t->inheritTranslation_);
            ImGui::Checkbox("回転を継承##inh", &t->inheritRotation_);
            ImGui::Checkbox("スケールを継承##inh", &t->inheritScale_);
        }

        ImGui::EndPopup();
    }

    if (nodeOpen)
    {
        // 子オブジェクトを表示
        for (BaseObject *pChild : *obj->GetChildren())
        {
            ShowObjectHierarchy(pChild, depth + 1);
        }
        ImGui::TreePop();
    }
#endif // USE_IMGUI
}

void BaseObjectManager::SetParentChild(const std::string &childName, const std::string &parentName)
{
    BaseObject *pChild = GetObjectByName(childName);
    BaseObject *parent = GetObjectByName(parentName);

    if (pChild && parent && pChild != parent)
    {
        // 循環参照チェック
        BaseObject *currentParent = parent;
        while (currentParent)
        {
            if (currentParent == pChild)
            {
                // 循環参照が発生するため、親子付けを拒否
                return;
            }
            currentParent = currentParent->GetParent();
        }

        pChild->SetParent(parent);
    }
}

void BaseObjectManager::RemoveParentChild(const std::string &childName)
{
    BaseObject *pChild = GetObjectByName(childName);
    if (pChild)
    {
        pChild->DetachParent();
    }
}

std::vector<std::string> BaseObjectManager::GetObjectNames() const
{
    std::vector<std::string> names;
    names.reserve(objects_.size());
    for (const auto &[name, obj] : objects_)
    {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> BaseObjectManager::GetSortedObjectNames() const
{
    std::vector<std::string> names = GetObjectNames();
    std::sort(names.begin(), names.end());
    return names;
}

void BaseObjectManager::SaveAllParentChildRelationships()
{
    for (auto &[name, obj] : objects_)
    {
        obj->SaveParentChildRelationship();
    }
}

void BaseObjectManager::LoadAllParentChildRelationships()
{
    // まず全オブジェクトから親子関係情報を読み込む
    std::unordered_map<std::string, std::string> parentRelations;
    std::unordered_map<std::string, std::vector<std::string>> childRelations;

    for (auto &[name, obj] : objects_)
    {
        if (!obj->objectData_)
            continue;

        std::string parentName = obj->objectData_->Load<std::string>("parentName", "");
        if (!parentName.empty())
        {
            parentRelations[name] = parentName;
        }

        std::vector<std::string> childrenNames = obj->objectData_->Load<std::vector<std::string>>("childrenNames", std::vector<std::string>());
        if (!childrenNames.empty())
        {
            childRelations[name] = childrenNames;
        }
    }

    // 親子関係を復元
    for (const auto &[childName, parentName] : parentRelations)
    {
        BaseObject *pChild = GetObjectByName(childName);
        BaseObject *parent = GetObjectByName(parentName);

        if (pChild && parent)
        {
            pChild->SetParent(parent);

            // SRT成分ごとの継承設定を復元する（未保存の古いデータは全継承=trueにフォールバック）
            if (pChild->objectData_ && pChild->GetWorldTransform())
            {
                WorldTransform *ct = pChild->GetWorldTransform();
                ct->inheritTranslation_ = pChild->objectData_->Load<bool>("inheritTranslation", true);
                ct->inheritRotation_ = pChild->objectData_->Load<bool>("inheritRotation", true);
                ct->inheritScale_ = pChild->objectData_->Load<bool>("inheritScale", true);
            }
        }
    }
}

void BaseObjectManager::RemoveObject(const std::string &name)
{
    auto it = objects_.find(name);
    if (it != objects_.end())
    {
        BaseObject *targetObject = it->second;

        if (targetObject)
        {
            // 子の親子付けを解除する。
            // children_ を直接辿るので、マネージャに未登録の子も取りこぼさない
            // （取りこぼすと、破棄済みの親を指したままの WorldTransform::pParent_ が残る）。
            // DetachParent() が親の children_ から自分を外すのでループは必ず終わる。
            std::list<BaseObject *> *children = targetObject->GetChildren();
            while (children && !children->empty())
            {
                children->front()->DetachParent();
            }

            // 親からの解除
            targetObject->DetachParent();
        }

        DetachRegistrations(targetObject, name);
        objects_.erase(it);
        ownedObjects_.erase(name);
    }
}

void BaseObjectManager::ShowSaveTargetManager()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("セーブ対象管理##SaveTargetManagement", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Spacing();

        std::vector<std::string> saveTargets;
        std::vector<std::string> nonSaveTargets;

        // オブジェクトを分類（unordered_map をそのまま回すと並びが毎回変わるので名前順で見る）
        for (const std::string &name : GetSortedObjectNames())
        {
            BaseObject *obj = GetObjectByName(name);
            if (!obj)
            {
                continue;
            }
            if (obj->GetShouldSave())
            {
                saveTargets.push_back(name);
            }
            else
            {
                nonSaveTargets.push_back(name);
            }
        }

        static std::vector<int> leftSelected;
        static std::vector<int> rightSelected;

        // 選択インデックスの範囲チェック
        leftSelected.erase(std::remove_if(leftSelected.begin(), leftSelected.end(),
                                          [&](int i) { return i >= static_cast<int>(nonSaveTargets.size()); }),
                           leftSelected.end());
        rightSelected.erase(std::remove_if(rightSelected.begin(), rightSelected.end(),
                                           [&](int i) { return i >= static_cast<int>(saveTargets.size()); }),
                            rightSelected.end());

        float availableWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = 90.0f; // ボタン幅を固定
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float listWidth = (availableWidth - buttonWidth - spacing * 2) * 0.5f;

        // ヘッダーテキスト
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 1.0f, 1.0f));
        ImGui::Text("セーブしない");
        ImGui::SameLine(listWidth + spacing + buttonWidth + spacing);
        ImGui::Text("セーブする");
        ImGui::PopStyleColor();

        // 左リスト（セーブしない）
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.32f, 0.36f, 0.60f));

        ImGui::BeginChild("non_save_targets##NonSaveTargets", ImVec2(listWidth, 200), true);

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.40f, 0.28f, 0.28f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.48f, 0.34f, 0.34f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.54f, 0.40f, 0.40f, 0.85f));

        if (nonSaveTargets.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("セーブ対象外のオブジェクトがありません");
            ImGui::PopStyleColor();
        }
        else
        {
            for (int i = 0; i < nonSaveTargets.size(); ++i)
            {
                bool selected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                std::string selectableId = nonSaveTargets[i] + "##NonSave" + std::to_string(i);
                if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (!ImGui::GetIO().KeyCtrl)
                        leftSelected.clear();

                    auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                    if (it != leftSelected.end())
                        leftSelected.erase(it);
                    else
                        leftSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        AddToSaveTargets(nonSaveTargets[i]);
                        leftSelected.clear();
                    }
                }
            }
        }

        ImGui::PopStyleColor(3);
        ImGui::EndChild();

        ImGui::SameLine();

        // 中央のボタン群
        ImGui::BeginGroup();

        // 垂直中央揃えのためのスペース調整
        ImGui::Dummy(ImVec2(0, 60));

        // 選択が無いときは地味な色にして「押しても動かない」ことを見た目で伝える
        const bool canMoveRight = !leftSelected.empty();
        const ImVec2 moveButtonSize(buttonWidth, 30.0f);
        const bool addPressed = canMoveRight ? ConfirmButton("追加 >>##SaveAddButton", moveButtonSize)
                                             : NeutralButton("追加 >>##SaveAddButton", moveButtonSize);
        if (addPressed && canMoveRight)
        {
            for (int idx : leftSelected)
            {
                AddToSaveTargets(nonSaveTargets[idx]);
            }
            leftSelected.clear();
        }

        const bool canMoveLeft = !rightSelected.empty();
        const bool removePressed = canMoveLeft ? DangerButton("<< 削除##SaveRemoveButton", moveButtonSize)
                                               : NeutralButton("<< 削除##SaveRemoveButton", moveButtonSize);
        if (removePressed && canMoveLeft)
        {
            for (int idx : rightSelected)
            {
                RemoveFromSaveTargets(saveTargets[idx]);
            }
            rightSelected.clear();
        }

        ImGui::EndGroup();

        ImGui::SameLine();

        // 右リスト（セーブする）
        ImGui::BeginChild("save_targets##SaveTargets", ImVec2(listWidth, 200), true);

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.40f, 0.30f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.48f, 0.36f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.54f, 0.42f, 0.85f));

        if (saveTargets.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("セーブ対象のオブジェクトがありません");
            ImGui::PopStyleColor();
        }
        else
        {
            for (int i = 0; i < saveTargets.size(); ++i)
            {
                bool selected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                std::string selectableId = saveTargets[i] + "##Save" + std::to_string(i);
                if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (!ImGui::GetIO().KeyCtrl)
                        rightSelected.clear();

                    auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                    if (it != rightSelected.end())
                        rightSelected.erase(it);
                    else
                        rightSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        RemoveFromSaveTargets(saveTargets[i]);
                        rightSelected.clear();
                    }
                }
            }
        }

        ImGui::PopStyleColor(3);
        ImGui::EndChild();

        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // 操作説明
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped("操作: Ctrlキー + クリックで複数選択, ダブルクリックで追加/削除");
        ImGui::PopStyleColor();
    }
#endif // USE_IMGUI
}
void BaseObjectManager::AddToSaveTargets(const std::string &objectName)
{
    BaseObject *obj = GetObjectByName(objectName);
    if (obj)
    {
        obj->SetShouldSave(true);
    }
}

void BaseObjectManager::RemoveFromSaveTargets(const std::string &objectName)
{
    BaseObject *obj = GetObjectByName(objectName);
    if (obj)
    {
        obj->SetShouldSave(false);
    }
}

// シーン保存モーダルの描画
void BaseObjectManager::DrawSceneSaveModel()
{
#ifdef USE_IMGUI
    static char sceneNameBuffer[128] = "";

    // メニューから呼び出された場合のモーダル表示
    if (showSceneSaveModal_)
    {
        ImGui::OpenPopup("シーン保存");
        showSceneSaveModal_ = false;
        // 上書き保存が大半なので、編集中のシーン名を初期値として入れておく
        strncpy_s(sceneNameBuffer, sceneName_.c_str(), _TRUNCATE);
    }

    // モーダルウィンドウ（中央に表示、背景は自動で薄暗くなる）
    if (ImGui::BeginPopupModal("シーン保存", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("シーンの名前を入力してください");

        // テキスト入力欄（sceneName_ を編集）
        ImGui::InputText("シーン名", sceneNameBuffer, IM_ARRAYSIZE(sceneNameBuffer));

        // 保存する内容の選択（オブジェクト以外も一緒に保存できるようにする）
        ImGui::Separator();
        ImGui::TextDisabled("保存する内容:");
        bool objAlways = true;
        ImGui::BeginDisabled();
        ImGui::Checkbox("オブジェクト (セーブ対象のみ)", &objAlways);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("どのオブジェクトを保存するかは下の「セーブ対象管理」で仕分けできます");
        static bool saveSprites = true;
        static bool saveUI = true;
        ImGui::Checkbox("スプライト (全シーン共通)", &saveSprites);
        ImGui::Checkbox("UIアニメーション (全シーン共通)", &saveUI);
        ImGui::Separator();

        // 横並びに「保存」ボタンと「キャンセル」ボタン
        if (ImGui::Button("保存", ImVec2(120, 0)))
        {
            sceneName_ = sceneNameBuffer; // 入力内容を保存
            std::unique_ptr<DataHandler> datas_ = std::make_unique<DataHandler>("SceneData/" + sceneName_ + "/ObjectDatas", "");
            datas_->DeleteAllJsonsInFolder();
            SaveAll();                         // オブジェクトの保存
            SaveAllParentChildRelationships(); // 親子関係も保存
            if (saveSprites)
                SpriteManager::GetInstance()->SaveAllSprites(); // スプライトも保存
            if (saveUI)
                UIAnimator::GetInstance()->Save(); // UIアニメーションも保存
            ImGui::CloseCurrentPopup();           // モーダルを閉じる
            // sceneName_ は「今どのシーンを編集しているか」を保持する変数なので、
            // 保存後にクリアしてはいけない（次の SaveAll が SceneData//ObjectDatas に書いてしまう）
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup(); // キャンセル時も閉じる
        }

        ImGui::EndPopup();
    }
#endif // USE_IMGUI
}

// シーン読み込みモーダルの描画
void BaseObjectManager::DrawSceneLoadModel()
{
#ifdef USE_IMGUI
    static char sceneNameBuffer[128] = "";

    // メニューから呼び出された場合のモーダル表示
    if (showSceneLoadModal_)
    {
        ImGui::OpenPopup("シーン読み込み");
        showSceneLoadModal_ = false;
        strncpy_s(sceneNameBuffer, sceneName_.c_str(), _TRUNCATE);
    }

    if (ImGui::BeginPopupModal("シーン読み込み", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("シーンの名前を入力してください");

        // テキスト入力欄（sceneName_ を編集）
        ImGui::InputText("シーン名", sceneNameBuffer, IM_ARRAYSIZE(sceneNameBuffer));

        // 読み込む内容の選択
        ImGui::Separator();
        ImGui::TextDisabled("読み込む内容:");
        static bool loadSprites = true;
        static bool loadUI = true;
        ImGui::Checkbox("スプライトも読み込み", &loadSprites);
        ImGui::Checkbox("UIアニメーションも読み込み", &loadUI);
        ImGui::Separator();

        // 横並びに「読み込み」ボタンと「キャンセル」ボタン
        if (ImGui::Button("読み込み", ImVec2(120, 0)))
        {
            sceneName_ = sceneNameBuffer; // 入力内容を保存
            LoadAll(sceneName_);          // オブジェクトの読み込み（親子関係の復元も内部で行う）
            if (loadSprites)
                SpriteManager::GetInstance()->LoadAllSprites(); // スプライトも読み込み
            if (loadUI)
                UIAnimator::GetInstance()->Load(); // UIアニメーションも読み込み
            ImGui::CloseCurrentPopup();           // モーダルを閉じる
            // 読み込んだシーン名はそのまま保持する（次の保存先がこのシーンになる）
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup(); // キャンセル時も閉じる
        }

        ImGui::EndPopup();
    }
#endif // USE_IMGUI
}

// オブジェクト生成モーダルの描画
void BaseObjectManager::DrawObjectCreationModel()
{
#ifdef USE_IMGUI
    // メニューから呼び出された場合のモーダル表示
    if (showObjectCreationModal_)
    {
        ImGui::OpenPopup("オブジェクト生成");
        showObjectCreationModal_ = false;
    }

    // オブジェクト生成モーダルウィンドウ
    if (ImGui::BeginPopupModal("オブジェクト生成", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("新しいオブジェクトを作成します");

        static char objectNameBuffer[128] = "";

        // オブジェクト名入力欄
        ImGui::InputText("オブジェクト名", objectNameBuffer, IM_ARRAYSIZE(objectNameBuffer));

        ImGui::Separator();

        // モデルファイル選択セクション
        ImGui::Text("モデルファイル選択:");
        ImGui::BeginChild("ModelFileSelector", ImVec2(600, 300), true);
        ShowModelFile(modelPath_, "objectCreate");
        ImGui::EndChild();

        // モデルを選んだらオブジェクト名を自動で埋める。
        // 名前は後から変えられるので、毎回手打ちさせない方が置く作業が速い。
        static std::string lastAutoFilledModel;
        if (!modelPath_.empty() && modelPath_ != lastAutoFilledModel)
        {
            lastAutoFilledModel = modelPath_;
            const std::string suggested = MakeUniqueObjectName(std::filesystem::path(modelPath_).stem().string());
            strncpy_s(objectNameBuffer, suggested.c_str(), _TRUNCATE);
        }

        ImGui::Separator();

        // テクスチャファイル選択セクション
        ImGui::Text("テクスチャファイル選択 (オプション):");
        ImGui::BeginChild("TextureFileSelector", ImVec2(600, 300), true);
        ShowTextureFile(texturePath_);
        ImGui::EndChild();

        ImGui::Separator();

        // 選択状況の表示
        ImGui::Text("選択されたモデル: %s", modelPath_.empty() ? "未選択" : modelPath_.c_str());
        ImGui::Text("選択されたテクスチャ: %s", texturePath_.empty() ? "未選択" : texturePath_.c_str());

        ImGui::Separator();

        // 生成ボタンとキャンセルボタン
        bool canCreate = strlen(objectNameBuffer) > 0 && !modelPath_.empty();

        // 名前とモデルが揃うまでは確定色にしない
        const bool createPressed = canCreate ? ConfirmButton("生成", ImVec2(120, 0))
                                             : NeutralButton("生成", ImVec2(120, 0));
        if (createPressed && canCreate)
        {
            objectName_ = objectNameBuffer;
            CreateObject(objectName_, modelPath_, texturePath_);

            // 入力欄とパスをリセット
            memset(objectNameBuffer, 0, sizeof(objectNameBuffer));
            modelPath_ = "";
            texturePath_ = "";

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0)))
        {
            // 入力欄とパスをリセット
            memset(objectNameBuffer, 0, sizeof(objectNameBuffer));
            modelPath_ = "";
            texturePath_ = "";

            ImGui::CloseCurrentPopup();
        }

        // 生成できない場合の理由を表示
        if (!canCreate)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "生成するには:");
            if (strlen(objectNameBuffer) == 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・オブジェクト名を入力してください");
            }
            if (modelPath_.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・モデルファイルを選択してください");
            }
        }

        ImGui::EndPopup();
    }
#endif // USE_IMGUI
}

void BaseObjectManager::DrawObjectLoadModel()
{
#ifdef USE_IMGUI
    // メニューから呼び出された場合のモーダル表示
    if (showObjectLoadModal_)
    {
        ImGui::OpenPopup("保存済みオブジェクト呼び出し");
        showObjectLoadModal_ = false;
    }
    std::string startPath = "ObjectDatas";
    // オブジェクト呼び出しモーダルウィンドウ
    if (ImGui::BeginPopupModal("保存済みオブジェクト呼び出し", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("保存済みのオブジェクトを読み込みます");

        // JSONファイル選択セクション
        ImGui::Text("保存済みオブジェクト選択:");
        ImGui::BeginChild("JsonFileSelector", ImVec2(600, 400), true);
        ShowJsonFile(selectedJsonPath_, startPath);
        ImGui::EndChild();
        ImGui::Separator();

        // 選択状況の表示とオブジェクト名の自動設定
        ImGui::Text("選択されたファイル: %s", selectedJsonPath_.empty() ? "未選択" : selectedJsonPath_.c_str());

        // JSONファイルが選択されている場合、ファイル名からオブジェクト名を取得
        std::string autoObjectName = "";
        if (!selectedJsonPath_.empty())
        {
            std::filesystem::path jsonPath(selectedJsonPath_);
            autoObjectName = jsonPath.stem().string(); // 拡張子なしのファイル名を取得
            ImGui::Text("オブジェクト名: %s", autoObjectName.c_str());
        }

        ImGui::Separator();

        // 読み込みボタンとキャンセルボタン
        const bool canLoad = !selectedJsonPath_.empty();
        const bool loadPressed = canLoad ? ConfirmButton("読み込み", ImVec2(120, 0))
                                         : NeutralButton("読み込み", ImVec2(120, 0));
        if (loadPressed && canLoad)
        {
            LoadObjectFromJson(startPath, autoObjectName);
            // パスをリセット
            selectedJsonPath_ = "";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120, 0)))
        {
            // パスをリセット
            selectedJsonPath_ = "";
            ImGui::CloseCurrentPopup();
        }
        // 読み込みできない場合の理由を表示
        if (!canLoad)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "読み込みするには:");
            if (selectedJsonPath_.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・JSONファイルを選択してください");
            }
        }
        ImGui::EndPopup();
    }
#endif // USE_IMGUI
}

void BaseObjectManager::LoadObjectFromJson(const std::string &startPath, const std::string &objectName)
{
    // ログ表示用のフルパスを構築 (jsons/startPath/objectName.json)
    std::string fullPath = AssetPath::Json(startPath + "/" + objectName + ".json");

    // BaseObjectのLoadFromJson機能を使用してオブジェクトを作成
    auto newObject = std::make_unique<BaseObject>();
    newObject->Init(objectName);
    newObject->GetName() = objectName;
    newObject->LoadFromJson(startPath, objectName);
    if (!newObject->GetModelPath().empty())
    {
        newObject->CreateModel(newObject->GetModelPath());
    }
    else
    {
        newObject->CreatePrimitiveModel(newObject->GetPrimitiveType());
    }

    // 親子関係の復元
    RestoreParentChildRelationshipForObject(newObject.get());

    this->AddObject(std::move(newObject));
    ImGuiNotification::Post("オブジェクトを読み込みました: " + objectName, {0.2f, 0.8f, 0.8f, 1.0f});
    Logger::Info("Object loaded: " + objectName + " (" + fullPath + ")");
}

void BaseObjectManager::RestoreParentChildRelationshipForObject(BaseObject *pObject)
{
    if (!pObject)
        return;

    // 親の復元
    std::string parentName = pObject->GetParentName();
    if (!parentName.empty())
    {
        auto it = objects_.find(parentName);
        if (it != objects_.end())
        {
            pObject->SetParent(it->second);
        }
    }

    // 子の復元
    std::vector<std::string> childrenNames = pObject->GetChildrenNames();
    for (const std::string &childName : childrenNames)
    {
        auto it = objects_.find(childName);
        if (it != objects_.end())
        {
            pObject->AddChild(it->second);
        }
    }
}

void BaseObjectManager::DrawHierarchyEditor()
{
#ifdef USE_IMGUI
    // ウィンドウの Begin/End は呼び出し元（ImGuiManager::ShowHierarchyWindow）が行う。
    // ここで再度 Begin すると同名ウィンドウが入れ子になる。
    ShowParentChildHierarchy();
    ShowSaveTargetManager();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
// -------------------------------------------------------
// Undo/Redo 用の状態キャプチャ・復元
// -------------------------------------------------------

nlohmann::json BaseObjectManager::CaptureUndoState()
{
    using nlohmann::json;
    json state = json::object();

    for (auto &[name, owned] : ownedObjects_)
    {
        BaseObject *obj = owned.get();
        if (!obj)
        {
            continue;
        }

        json s;
        // 再生成に必要な情報
        s["modelPath"] = obj->GetModelPath();
        s["isPrimitive"] = obj->IsPrimitive();
        s["primitiveType"] = static_cast<int>(obj->GetPrimitiveType());

        // トランスフォーム
        const WorldTransform *transform = obj->GetWorldTransform();
        s["scale"] = transform->scale_;
        s["rotation"] = transform->quaternionRotation_;
        s["translation"] = transform->translation_;

        // 親子関係・フラグ類
        s["parent"] = obj->GetParentName();
        s["shouldSave"] = obj->GetShouldSave();
        s["isModelDraw"] = obj->GetIsModelDraw();
        s["isLighting"] = obj->GetLighting();

        // マテリアルごとのテクスチャと色
        const int materialCount = obj->GetObject3d() ? static_cast<int>(obj->GetObject3d()->GetMaterialCount()) : 0;
        const int textureCount = obj->IsPrimitive() ? (materialCount > 0 ? 1 : 0) : materialCount;
        json textures = json::array();
        json colors = json::array();
        for (int i = 0; i < textureCount; ++i)
        {
            textures.push_back(obj->GetTexturePath(i));
        }
        for (int i = 0; i < materialCount; ++i)
        {
            colors.push_back(obj->GetColor(i));
        }
        s["textures"] = textures;
        s["colors"] = colors;

        state[name] = s;
    }
    return state;
}

void BaseObjectManager::RestoreUndoState(const nlohmann::json &state)
{
    using nlohmann::json;
    if (!state.is_object())
    {
        return;
    }

    // ---- パス1: 削除・再生成・フィールド適用 ----
    for (auto it = state.begin(); it != state.end(); ++it)
    {
        const std::string &name = it.key();

        // null = このオブジェクトは存在しない状態へ戻す（削除）
        if (it.value().is_null())
        {
            RemoveObject(name);
            ImGuizmoManager::GetInstance()->RemoveTarget(name);
            continue;
        }

        const json &s = it.value();
        BaseObject *obj = GetObjectByName(name);

        // 存在しなければ所有オブジェクトとして再生成（削除のUndo）
        if (!obj)
        {
            const std::string modelPath = s.value("modelPath", std::string());
            const bool isPrimitive = s.value("isPrimitive", false);
            std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
            newObject->Init(name);
            if (!modelPath.empty())
            {
                newObject->CreateModel(modelPath);
            }
            else if (isPrimitive)
            {
                newObject->SetPrimitive(true);
                newObject->CreatePrimitiveModel(
                    static_cast<PrimitiveType>(s.value("primitiveType", static_cast<int>(PrimitiveType::Count))));
            }
            else
            {
                continue; // モデルもプリミティブも無い場合は再生成できない
            }
            AddObject(std::move(newObject));
            obj = GetObjectByName(name);
            if (!obj)
            {
                continue;
            }
        }

        // トランスフォーム適用
        WorldTransform *transform = obj->GetWorldTransform();
        if (s.contains("scale"))
        {
            transform->scale_ = s["scale"].get<Vector3>();
        }
        if (s.contains("rotation"))
        {
            transform->quaternionRotation_ = s["rotation"].get<Quaternion>();
        }
        if (s.contains("translation"))
        {
            transform->translation_ = s["translation"].get<Vector3>();
        }

        // フラグ類の適用
        if (s.contains("shouldSave"))
        {
            obj->SetShouldSave(s["shouldSave"].get<bool>());
        }
        if (s.contains("isModelDraw"))
        {
            obj->SetIsModelDraw(s["isModelDraw"].get<bool>());
        }
        if (s.contains("isLighting"))
        {
            obj->GetLighting() = s["isLighting"].get<bool>();
        }

        // マテリアルごとのテクスチャと色の適用
        const int materialCount = obj->GetObject3d() ? static_cast<int>(obj->GetObject3d()->GetMaterialCount()) : 0;
        if (s.contains("textures") && s["textures"].is_array())
        {
            const json &textures = s["textures"];
            const int textureCount = obj->IsPrimitive() ? (materialCount > 0 ? 1 : 0) : materialCount;
            for (int i = 0; i < static_cast<int>(textures.size()) && i < textureCount; ++i)
            {
                obj->SetTexture(textures[i].get<std::string>(), i);
            }
        }
        if (s.contains("colors") && s["colors"].is_array())
        {
            const json &colors = s["colors"];
            for (int i = 0; i < static_cast<int>(colors.size()) && i < materialCount; ++i)
            {
                obj->SetColor(colors[i].get<Vector4>(), i);
            }
        }
    }

    // ---- パス2: 親子関係の復元（全オブジェクトが揃ってから行う）----
    for (auto it = state.begin(); it != state.end(); ++it)
    {
        if (it.value().is_null() || !it.value().is_object() || !it.value().contains("parent"))
        {
            continue;
        }
        const std::string &name = it.key();
        if (!GetObjectByName(name))
        {
            continue;
        }
        const std::string parentName = it.value()["parent"].get<std::string>();
        if (parentName.empty())
        {
            RemoveParentChild(name);
        }
        else if (GetObjectByName(parentName))
        {
            SetParentChild(name, parentName);
        }
    }
}
#endif // USE_IMGUI
} // namespace Hagine
