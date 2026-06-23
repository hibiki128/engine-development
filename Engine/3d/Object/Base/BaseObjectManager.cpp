#include "BaseObjectManager.h"
#include <Utility/Debug/ImGui/ImGuiNotification.h>
#ifdef _DEBUG
#include "Debug/ImGui/ImGuizmoManager.h"
#endif // _DEBUG
#include <Debug/Log/Logger.h>
#include <ShowFolder/ShowFolder.h>
#include"Render/DrawSystem.h"

namespace Hagine {
void BaseObjectManager::Finalize() {
    RemoveAllObjects();
}

void BaseObjectManager::RemoveAllObjects() {
    // 親子関係をすべてクリア
    for (auto &[name, obj] : objects_) {
        if (obj) {
            obj->SetParent(nullptr);
        }
    }

    objects_.clear();
    ownedObjects_.clear();

#ifdef _DEBUG
    ImGuizmoManager::GetInstance()->DeleteTarget();
#endif
}

void BaseObjectManager::RemoveObjectByName(const std::string &name) {
    auto it = objects_.find(name);
    if (it != objects_.end()) {
        ImGuiNotification::Post("オブジェクトを削除しました: " + name, {0.9f, 0.7f, 0.2f, 1.0f});
        objects_.erase(it);
        ownedObjects_.erase(name);
    }
}

void BaseObjectManager::RegisterExternal(BaseObject* obj) {
    const std::string &name = obj->GetName();
#ifdef _DEBUG
    ImGuizmoManager::GetInstance()->AddTarget(name, obj);
#endif
    // アプリ側が差し込んだ登録フックを通知（MotionEditor 等）
    for (auto &cb : registerObservers_) {
        cb(obj);
    }
    objects_.emplace(name, obj);
    ImGuiNotification::Post("オブジェクトを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void BaseObjectManager::UnregisterExternal(BaseObject* obj) {
    objects_.erase(obj->GetName());
}

void BaseObjectManager::AddObject(std::unique_ptr<BaseObject> baseObject) {
    auto* ptr = baseObject.get();
    const std::string &name = baseObject->GetName();
    ownedObjects_.emplace(name, std::move(baseObject));
    RegisterExternal(ptr);
}

void BaseObjectManager::Update() {
    for (auto &[name, obj] : objects_) {
        obj->UpdateHierarchy();
        obj->UpdateWorldTransformHierarchy();
    }
}

void BaseObjectManager::Draw(const ViewProjection &viewProjection) {
    for (auto &[name, obj] : objects_) {
        obj->Draw(viewProjection);
    }
}

void BaseObjectManager::UpdateImGui() {
#ifdef _DEBUG
    DrawSceneSaveModel();
    DrawSceneLoadModel();
    DrawObjectCreationModel();
    DrawObjectLoadModel();
#endif // _DEBUG
}

void BaseObjectManager::SaveAll() {
    for (auto &[name, obj] : objects_) {
        if (obj->GetShouldSave()) { // セーブ対象フラグをチェック
            obj->SetFolderPath("SceneData/" + sceneName_ + "/ObjectDatas");
            obj->SceneSaveToJson();
            obj->SaveParentChildRelationship();
        }
    }
    ImGuiNotification::Post("全オブジェクトを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
}

void BaseObjectManager::LoadAll(std::string sceneName) {
    // シーンデータのフォルダパスを構築
    std::string sceneDataPath = "Resources/jsons/SceneData/" + sceneName + "/ObjectDatas";

    // フォルダが存在するかチェック
    if (!std::filesystem::exists(sceneDataPath)) {
        // フォルダが存在しない場合は何もしない
        return;
    }

    // JSONファイルを検索
    std::vector<std::string> jsonFiles;
    for (const auto &entry : std::filesystem::directory_iterator(sceneDataPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            jsonFiles.push_back(entry.path().filename().string());
        }
    }

    // 既存のオブジェクトをクリア
    RemoveAllObjects();

    // 各JSONファイルを読み込んでオブジェクトを生成
    for (const std::string &jsonFile : jsonFiles) {
        // JSONファイル名から拡張子を除去してオブジェクト名とする
        std::string objectName = jsonFile.substr(0, jsonFile.find_last_of('.'));

        // 新しいオブジェクトを作成
        std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
        std::string foldarPath = "SceneData/" + sceneName + "/ObjectDatas";
        // フォルダパスを設定
        newObject->SetFolderPath("SceneData/" + sceneName + "/ObjectDatas");
        std::unique_ptr<DataHandler> ObjectDatas = std::make_unique<DataHandler>(foldarPath, objectName);
        // オブジェクト名でInit
        newObject->Init(objectName);
        newObject->SetIsScene(true);

        // 読み込んだデータからモデルとテクスチャのパスを取得
        std::string modelPath = ObjectDatas->Load<std::string>("modelName", "");

        // モデルとテクスチャを設定
        if (!modelPath.empty()) {
            newObject->CreateModel(modelPath);
        } else {
            newObject->CreatePrimitiveModel(newObject->GetPrimitiveType());
        }

        // オブジェクトマネージャーに追加
        this->AddObject(std::move(newObject));
    }

    // 全オブジェクト読み込み後に親子関係を復元
    LoadAllParentChildRelationships();
    ImGuiNotification::Post("シーンを読み込みました: " + sceneName, {0.2f, 0.8f, 0.8f, 1.0f});
}

void BaseObjectManager::CreateObject(std::string objectName, std::string modelPath, std::string texturePath) {
    std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
    newObject->Init(objectName);
    newObject->CreateModel(modelPath);
    for (int i = 0; i < newObject->GetObject3d()->GetMaterialCount(); i++) {
        newObject->SetTexture(texturePath, i);
    }
    this->AddObject(std::move(newObject));
    ImGuiNotification::Post("オブジェクトを作成しました: " + objectName, {0.2f, 0.8f, 0.2f, 1.0f});
}

BaseObject *BaseObjectManager::GetObjectByName(const std::string &name) {
    auto it = objects_.find(name);
    if (it != objects_.end()) {
        return it->second;
    }
    return nullptr;
}

// メニューからモーダルを開くメソッド
void BaseObjectManager::OpenSceneSaveModal() {
    showSceneSaveModal_ = true;
}

void BaseObjectManager::OpenSceneLoadModal() {
    showSceneLoadModal_ = true;
}

void BaseObjectManager::OpenObjectCreationModal() {
    showObjectCreationModal_ = true;
}

void BaseObjectManager::OpenObjectLoadModal() {
    showObjectLoadModal_ = true;
}

namespace {
// 階層ツリーのドラッグ＆ドロップ結果は、ツリー描画中に親子を変更すると
// イテレータが壊れるため、フレーム末にまとめて適用する
std::string g_dndReparentChild;  // ドラッグされた子オブジェクト名
std::string g_dndReparentParent; // ドロップ先の親（空文字 = ルートへ解除）
bool g_dndReparentRequested = false;
} // namespace

void BaseObjectManager::ShowParentChildHierarchy() {
#ifdef _DEBUG

    if (ImGui::CollapsingHeader("階層エディター", ImGuiTreeNodeFlags_DefaultOpen)) {

        // 親子付けセクション
        ImGui::Separator();
        ImGui::Text("親子付け:");

        static int selectedChild = 0;
        static int selectedParent = 0;

        std::vector<std::string> objectNames = GetObjectNames();

        if (!objectNames.empty()) {
            std::vector<const char *> objectNamesCStr;
            for (const auto &name : objectNames) {
                objectNamesCStr.push_back(name.c_str());
            }

            ImGui::Text("子オブジェクト:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150);
            ImGui::Combo("##ChildObject", &selectedChild, objectNamesCStr.data(), static_cast<int>(objectNamesCStr.size()));
            ImGui::PopItemWidth();

            ImGui::Text("親オブジェクト:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150);
            ImGui::Combo("##ParentObject", &selectedParent, objectNamesCStr.data(), static_cast<int>(objectNamesCStr.size()));
            ImGui::PopItemWidth();

            selectedChild = std::clamp(selectedChild, 0, static_cast<int>(objectNames.size()) - 1);
            selectedParent = std::clamp(selectedParent, 0, static_cast<int>(objectNames.size()) - 1);

            if (ImGui::Button("親子付け")) {
                if (selectedChild != selectedParent) {
                    SetParentChild(objectNames[selectedChild], objectNames[selectedParent]);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("親子解除")) {
                RemoveParentChild(objectNames[selectedChild]);
            }
        }

        ImGui::Separator();
        ImGui::Text("階層表示:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
        ImGui::TextUnformatted("（ノードをドラッグして別ノードに重ねると親子付け / 余白へドロップで解除）");
        ImGui::PopStyleColor();

        // 階層構造を表示
        ImGui::BeginChild("HierarchyView", ImVec2(0, 300), ImGuiChildFlags_Borders);

        for (auto &[name, obj] : objects_) {
            if (!obj->GetParent()) { // ルートオブジェクトのみ表示
                ShowObjectHierarchy(obj, 0);
            }
        }

        // 余白へのドロップでルート（親なし）へ解除できるようにする
        ImVec2 dropAvail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(dropAvail.x, dropAvail.y > 8.0f ? dropAvail.y : 8.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("OBJ_NODE")) {
                g_dndReparentChild = static_cast<const char *>(p->Data);
                g_dndReparentParent.clear();
                g_dndReparentRequested = true;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndChild();

        // ドラッグ＆ドロップの結果をツリー描画後にまとめて適用する
        if (g_dndReparentRequested) {
            if (g_dndReparentParent.empty()) {
                RemoveParentChild(g_dndReparentChild);
                ImGuiNotification::Post("親子付けを解除しました: " + g_dndReparentChild, {0.82f, 0.58f, 0.36f, 1.0f});
            } else if (g_dndReparentChild != g_dndReparentParent) {
                SetParentChild(g_dndReparentChild, g_dndReparentParent);
                BaseObject *c = GetObjectByName(g_dndReparentChild);
                BaseObject *p = GetObjectByName(g_dndReparentParent);
                if (c && c->GetParent() == p) {
                    ImGuiNotification::Post(g_dndReparentChild + " を " + g_dndReparentParent + " の子にしました", {0.45f, 0.68f, 0.52f, 1.0f});
                } else {
                    ImGuiNotification::Post("親子付けできません（循環参照など）", {0.82f, 0.58f, 0.36f, 1.0f});
                }
            }
            g_dndReparentRequested = false;
            g_dndReparentChild.clear();
            g_dndReparentParent.clear();
        }
    }
#endif // _DEBUG
}

void BaseObjectManager::ShowObjectHierarchy(BaseObject *obj, int depth) {
#ifdef _DEBUG

    if (!obj)
        return;

    // インデントを設定
    std::string indent(depth * 2, ' ');
    std::string displayName = indent + obj->GetName();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    // 子がない場合は葉ノードフラグを追加
    if (obj->GetChildren()->empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);

    // ドラッグ元: このノード
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const std::string &dragName = obj->GetName();
        ImGui::SetDragDropPayload("OBJ_NODE", dragName.c_str(), dragName.size() + 1);
        ImGui::Text("移動: %s", dragName.c_str());
        ImGui::EndDragDropSource();
    }
    // ドロップ先: このノードを親にする
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("OBJ_NODE")) {
            g_dndReparentChild = static_cast<const char *>(p->Data);
            g_dndReparentParent = obj->GetName();
            g_dndReparentRequested = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (nodeOpen) {
        // 子オブジェクトを表示
        for (BaseObject *child : *obj->GetChildren()) {
            ShowObjectHierarchy(child, depth + 1);
        }
        ImGui::TreePop();
    }
#endif // _DEBUG
}

void BaseObjectManager::SetParentChild(const std::string &childName, const std::string &parentName) {
    BaseObject *child = GetObjectByName(childName);
    BaseObject *parent = GetObjectByName(parentName);

    if (child && parent && child != parent) {
        // 循環参照チェック
        BaseObject *currentParent = parent;
        while (currentParent) {
            if (currentParent == child) {
                // 循環参照が発生するため、親子付けを拒否
                return;
            }
            currentParent = currentParent->GetParent();
        }

        child->SetParent(parent);
    }
}

void BaseObjectManager::RemoveParentChild(const std::string &childName) {
    BaseObject *child = GetObjectByName(childName);
    if (child) {
        child->DetachParent();
    }
}

std::vector<std::string> BaseObjectManager::GetObjectNames() const {
    std::vector<std::string> names;
    for (const auto &[name, obj] : objects_) {
        names.push_back(name);
    }
    return names;
}

void BaseObjectManager::SaveAllParentChildRelationships() {
    for (auto &[name, obj] : objects_) {
        obj->SaveParentChildRelationship();
    }
}

void BaseObjectManager::LoadAllParentChildRelationships() {
    // まず全オブジェクトから親子関係情報を読み込む
    std::unordered_map<std::string, std::string> parentRelations;
    std::unordered_map<std::string, std::vector<std::string>> childRelations;

    for (auto &[name, obj] : objects_) {
        if (!obj->ObjectDatas_)
            continue;

        std::string parentName = obj->ObjectDatas_->Load<std::string>("parentName", "");
        if (!parentName.empty()) {
            parentRelations[name] = parentName;
        }

        std::vector<std::string> childrenNames = obj->ObjectDatas_->Load<std::vector<std::string>>("childrenNames", std::vector<std::string>());
        if (!childrenNames.empty()) {
            childRelations[name] = childrenNames;
        }
    }

    // 親子関係を復元
    for (const auto &[childName, parentName] : parentRelations) {
        BaseObject *child = GetObjectByName(childName);
        BaseObject *parent = GetObjectByName(parentName);

        if (child && parent) {
            child->SetParent(parent);
        }
    }
}

void BaseObjectManager::RemoveObject(const std::string &name) {
    auto it = objects_.find(name);
    if (it != objects_.end()) {
        BaseObject *targetObject = it->second;

        if (targetObject) {
            // 子供の親解除
            for (auto &pair : objects_) {
                BaseObject *obj = pair.second;
                if (obj && obj->GetParent() == targetObject) {
                    obj->DetachParent();
                }
            }

            // 親からの解除
            targetObject->DetachParent();
        }

        objects_.erase(it);
        ownedObjects_.erase(name);
    }
}

void BaseObjectManager::ShowSaveTargetManager() {
#ifdef _DEBUG
    if (ImGui::CollapsingHeader("セーブ対象管理##SaveTargetManagement", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        std::vector<std::string> saveTargets;
        std::vector<std::string> nonSaveTargets;

        // オブジェクトを分類
        for (const auto &[name, obj] : objects_) {
            if (obj->GetShouldSave()) {
                saveTargets.push_back(name);
            } else {
                nonSaveTargets.push_back(name);
            }
        }

        static std::vector<int> leftSelected;
        static std::vector<int> rightSelected;

        // 選択インデックスの範囲チェック
        leftSelected.erase(std::remove_if(leftSelected.begin(), leftSelected.end(),
                                          [&](int i) { return i >= (int)nonSaveTargets.size(); }),
                           leftSelected.end());
        rightSelected.erase(std::remove_if(rightSelected.begin(), rightSelected.end(),
                                           [&](int i) { return i >= (int)saveTargets.size(); }),
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

        if (nonSaveTargets.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("セーブ対象外のオブジェクトがありません");
            ImGui::PopStyleColor();
        } else {
            for (int i = 0; i < nonSaveTargets.size(); ++i) {
                bool selected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                std::string selectableId = nonSaveTargets[i] + "##NonSave" + std::to_string(i);
                if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!ImGui::GetIO().KeyCtrl)
                        leftSelected.clear();

                    auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                    if (it != leftSelected.end())
                        leftSelected.erase(it);
                    else
                        leftSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
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

        bool canMoveRight = !leftSelected.empty();
        if (!canMoveRight) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.30f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.38f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.58f, 0.44f, 1.0f));
        }

        if (ImGui::Button("追加 >>##SaveAddButton", ImVec2(buttonWidth, 30)) && canMoveRight) {
            for (int idx : leftSelected) {
                AddToSaveTargets(nonSaveTargets[idx]);
            }
            leftSelected.clear();
        }

        ImGui::PopStyleColor(3);

        bool canMoveLeft = !rightSelected.empty();
        if (!canMoveLeft) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.46f, 0.24f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.30f, 0.30f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.66f, 0.36f, 0.36f, 1.0f));
        }

        if (ImGui::Button("<< 削除##SaveRemoveButton", ImVec2(buttonWidth, 30)) && canMoveLeft) {
            for (int idx : rightSelected) {
                RemoveFromSaveTargets(saveTargets[idx]);
            }
            rightSelected.clear();
        }

        ImGui::PopStyleColor(3);
        ImGui::EndGroup();

        ImGui::SameLine();

        // 右リスト（セーブする）
        ImGui::BeginChild("save_targets##SaveTargets", ImVec2(listWidth, 200), true);

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.40f, 0.30f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.48f, 0.36f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.54f, 0.42f, 0.85f));

        if (saveTargets.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("セーブ対象のオブジェクトがありません");
            ImGui::PopStyleColor();
        } else {
            for (int i = 0; i < saveTargets.size(); ++i) {
                bool selected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                std::string selectableId = saveTargets[i] + "##Save" + std::to_string(i);
                if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!ImGui::GetIO().KeyCtrl)
                        rightSelected.clear();

                    auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                    if (it != rightSelected.end())
                        rightSelected.erase(it);
                    else
                        rightSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
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
#endif // _DEBUG
}
void BaseObjectManager::AddToSaveTargets(const std::string &objectName) {
    BaseObject *obj = GetObjectByName(objectName);
    if (obj) {
        obj->SetShouldSave(true);
    }
}

void BaseObjectManager::RemoveFromSaveTargets(const std::string &objectName) {
    BaseObject *obj = GetObjectByName(objectName);
    if (obj) {
        obj->SetShouldSave(false);
    }
}

// シーン保存モーダルの描画
void BaseObjectManager::DrawSceneSaveModel() {
#ifdef _DEBUG
    // メニューから呼び出された場合のモーダル表示
    if (showSceneSaveModal_) {
        ImGui::OpenPopup("シーン保存");
        showSceneSaveModal_ = false;
    }

    // モーダルウィンドウ（中央に表示、背景は自動で薄暗くなる）
    if (ImGui::BeginPopupModal("シーン保存", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("シーンの名前を入力してください");

        static char sceneNameBuffer[128] = "";

        // テキスト入力欄（sceneName_ を編集）
        ImGui::InputText("シーン名", sceneNameBuffer, IM_ARRAYSIZE(sceneNameBuffer));

        // 横並びに「保存」ボタンと「キャンセル」ボタン
        if (ImGui::Button("保存", ImVec2(120, 0))) {
            sceneName_ = sceneNameBuffer; // 入力内容を保存
            std::unique_ptr<DataHandler> datas_ = std::make_unique<DataHandler>("SceneData/" + sceneName_ + "/ObjectDatas", "");
            datas_->DeleteAllJsonsInFolder();
            SaveAll();                         // 実際の保存処理
            SaveAllParentChildRelationships(); // 親子関係も保存
            ImGui::CloseCurrentPopup();        // モーダルを閉じる
            sceneName_.clear();                // 入力欄をクリア
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup(); // キャンセル時も閉じる
        }

        ImGui::EndPopup();
    }
#endif // _DEBUG
}

// シーン読み込みモーダルの描画
void BaseObjectManager::DrawSceneLoadModel() {
#ifdef _DEBUG
    // メニューから呼び出された場合のモーダル表示
    if (showSceneLoadModal_) {
        ImGui::OpenPopup("シーン読み込み");
        showSceneLoadModal_ = false;
    }

    if (ImGui::BeginPopupModal("シーン読み込み", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("シーンの名前を入力してください");

        static char sceneNameBuffer[128] = "";

        // テキスト入力欄（sceneName_ を編集）
        ImGui::InputText("シーン名", sceneNameBuffer, IM_ARRAYSIZE(sceneNameBuffer));

        // 横並びに「読み込み」ボタンと「キャンセル」ボタン
        if (ImGui::Button("読み込み", ImVec2(120, 0))) {
            sceneName_ = sceneNameBuffer;      // 入力内容を保存
            LoadAll(sceneName_);               // 実際の読み込み処理
            LoadAllParentChildRelationships(); // 親子関係も読み込み
            ImGui::CloseCurrentPopup();        // モーダルを閉じる
            sceneName_.clear();                // 読み込み後はシーン名をクリア
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup(); // キャンセル時も閉じる
        }

        ImGui::EndPopup();
    }
#endif // _DEBUG
}

// オブジェクト生成モーダルの描画
void BaseObjectManager::DrawObjectCreationModel() {
#ifdef _DEBUG
    // メニューから呼び出された場合のモーダル表示
    if (showObjectCreationModal_) {
        ImGui::OpenPopup("オブジェクト生成");
        showObjectCreationModal_ = false;
    }

    // オブジェクト生成モーダルウィンドウ
    if (ImGui::BeginPopupModal("オブジェクト生成", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("新しいオブジェクトを作成します");

        static char objectNameBuffer[128] = "";

        // オブジェクト名入力欄
        ImGui::InputText("オブジェクト名", objectNameBuffer, IM_ARRAYSIZE(objectNameBuffer));

        ImGui::Separator();

        // モデルファイル選択セクション
        ImGui::Text("モデルファイル選択:");
        ImGui::BeginChild("ModelFileSelector", ImVec2(600, 300), true);
        ShowModelFile(modelPath_);
        ImGui::EndChild();

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

        if (!canCreate) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        }

        if (ImGui::Button("生成", ImVec2(120, 0))) {
            if (canCreate) {
                objectName_ = objectNameBuffer;
                CreateObject(objectName_, modelPath_, texturePath_);

                // 入力欄とパスをリセット
                memset(objectNameBuffer, 0, sizeof(objectNameBuffer));
                modelPath_ = "";
                texturePath_ = "";

                ImGui::CloseCurrentPopup();
            }
        }

        if (!canCreate) {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            // 入力欄とパスをリセット
            memset(objectNameBuffer, 0, sizeof(objectNameBuffer));
            modelPath_ = "";
            texturePath_ = "";

            ImGui::CloseCurrentPopup();
        }

        // 生成できない場合の理由を表示
        if (!canCreate) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "生成するには:");
            if (strlen(objectNameBuffer) == 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・オブジェクト名を入力してください");
            }
            if (modelPath_.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・モデルファイルを選択してください");
            }
        }

        ImGui::EndPopup();
    }
#endif // _DEBUG
}

void BaseObjectManager::DrawObjectLoadModel() {
#ifdef _DEBUG
    // メニューから呼び出された場合のモーダル表示
    if (showObjectLoadModal_) {
        ImGui::OpenPopup("保存済みオブジェクト呼び出し");
        showObjectLoadModal_ = false;
    }
    std::string startPath = "ObjectDatas";
    // オブジェクト呼び出しモーダルウィンドウ
    if (ImGui::BeginPopupModal("保存済みオブジェクト呼び出し", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        if (!selectedJsonPath_.empty()) {
            std::filesystem::path jsonPath(selectedJsonPath_);
            autoObjectName = jsonPath.stem().string(); // 拡張子なしのファイル名を取得
            ImGui::Text("オブジェクト名: %s", autoObjectName.c_str());
        }

        ImGui::Separator();

        // 読み込みボタンとキャンセルボタン
        bool canLoad = !selectedJsonPath_.empty();
        if (!canLoad) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        }
        if (ImGui::Button("読み込み", ImVec2(120, 0))) {
            if (canLoad) {
                LoadObjectFromJson(startPath, autoObjectName);
                // パスをリセット
                selectedJsonPath_ = "";
                ImGui::CloseCurrentPopup();
            }
        }
        if (!canLoad) {
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            // パスをリセット
            selectedJsonPath_ = "";
            ImGui::CloseCurrentPopup();
        }
        // 読み込みできない場合の理由を表示
        if (!canLoad) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "読み込みするには:");
            if (selectedJsonPath_.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "・JSONファイルを選択してください");
            }
        }
        ImGui::EndPopup();
    }
#endif // _DEBUG
}

void BaseObjectManager::LoadObjectFromJson(const std::string &startPath, const std::string &objectName) {
    // フルパスを構築 (startPath/objectName.json)
    std::string fullPath = "resources/jsons/" + startPath + "/" + objectName + ".json";

    // BaseObjectのLoadFromJson機能を使用してオブジェクトを作成
    auto newObject = std::make_unique<BaseObject>();
    newObject->Init(objectName);
    newObject->GetName() = objectName;
    newObject->LoadFromJson(startPath, objectName);
    if (!newObject->GetModelPath().empty()) {
        newObject->CreateModel(newObject->GetModelPath());
    } else {
        newObject->CreatePrimitiveModel(newObject->GetPrimitiveType());
    }

    // 親子関係の復元
    RestoreParentChildRelationshipForObject(newObject.get());

    this->AddObject(std::move(newObject));
    ImGuiNotification::Post("オブジェクトを読み込みました: " + objectName, {0.2f, 0.8f, 0.8f, 1.0f});
    Logger::Log("オブジェクト読み込み完了: " + objectName + " (" + fullPath + ")");
}

void BaseObjectManager::RestoreParentChildRelationshipForObject(BaseObject *object) {
    if (!object)
        return;

    // 親の復元
    std::string parentName = object->GetParentName();
    if (!parentName.empty()) {
        auto it = objects_.find(parentName);
        if (it != objects_.end()) {
            object->SetParent(it->second);
        }
    }

    // 子の復元
    std::vector<std::string> childrenNames = object->GetChildrenNames();
    for (const std::string &childName : childrenNames) {
        auto it = objects_.find(childName);
        if (it != objects_.end()) {
            object->AddChild(it->second);
        }
    }
}

void BaseObjectManager::DrawHierarchyEditor() {
#ifdef _DEBUG
    ImGui::Begin("オブジェクトマネージャ");

    ShowParentChildHierarchy();
    ShowSaveTargetManager();

    ImGui::End();
#endif // _DEBUG
}
} // namespace Hagine
