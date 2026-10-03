#include "BaseObjectManager.h"
#include <attachment/AttachmentManager.h>
#include <functional>
#include <metaball/MetaBallGroupManager.h>
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
#include <icon/IconsFontAwesome5.h>
#include <debug/profiler/CpuProfiler.h>
#include <browser/ShowFolder.h>
#include <line/LineRenderer.h>
#include <DirectXCommon.h>
#include <graphics/model/ModelManager.h>
#include "render/DrawGroupManager.h"
#include <render/raytracing/RaytracingScene.h>

#ifdef USE_IMGUI
#include <edit/undo/ImGuiUndoTracker.h>
namespace {
// UI の編集ジェスチャを Undo 履歴へ積むトラッカー。シングルトンなので1つでよい。
// ヘッダーのメンバーにすると Undo 関連のヘッダーが 100 本以上の .cpp へ広がるので、ここに置く
Hagine::ImGuiUndoTracker g_undoTracker;
} // namespace
#endif // USE_IMGUI
namespace Hagine {
#ifdef USE_IMGUI
void BaseObjectManager::SkipUndoGesture()
{
    g_undoTracker.SkipCurrentGesture();
}
#endif // USE_IMGUI

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
    if (const auto it = objects_.find(name); it != objects_.end() && it->second != obj)
    {
        // 名前が同じだと後から来た方は登録されず、更新も描画もされない。
        // シーンファイルに同名の物を保存していると起きるので、気づけるように知らせる
        Logger::Warn("[BaseObjectManager] \"" + name + "\" は既に登録されているので登録できません。名前を変えてください");
        return;
    }
#ifdef USE_IMGUI
    ImGuizmoManager::GetInstance()->AddTarget(name, obj);
#endif
    // 種類をまたいだ親子付け（光源やパーティクルを付ける）の対象として登録する。
    // ギズモと違い Release でも要るので USE_IMGUI では囲まない。
    {
        AttachTarget target;
        target.kind = AttachKind::Object;
        target.name = name;
        target.worldTransform = obj->GetWorldTransform();
        AttachmentManager::GetInstance()->Register(target);
    }
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
    const auto it = objects_.find(obj->GetName());
    if (it != objects_.end() && it->second != obj)
    {
        // 同名の別オブジェクトが登録されている（こちらは RegisterExternal で断られた物）。
        // 名前で消すと相手の登録まで消えてしまうので、自分のポインタで持たれている物だけ外す
        MotionEditor::GetInstance()->Unregister(obj);
        return;
    }
    DetachRegistrations(obj, obj->GetName());
    if (it != objects_.end())
    {
        objects_.erase(it);
    }
}

// 破棄・登録解除の直前に、他マネージャが持つこのオブジェクトへの参照を全て落とす。
// これを忘れると解放済みポインタが編集UI側に残り、次にそれを触った時に落ちる。
void BaseObjectManager::DetachRegistrations(BaseObject *obj, const std::string &name)
{
#ifdef USE_IMGUI
    ImGuizmoManager::GetInstance()->RemoveTarget(name);
#endif
    AttachmentManager::GetInstance()->Unregister(name);
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

void BaseObjectManager::RequestDuplicate(const std::string &sourceName)
{
    pendingDuplicates_.push_back(sourceName);
}

void BaseObjectManager::Update()
{
    // UI から予約された複製をここで実行する。
    // インスペクタ描画中に objects_ を書き換えると走査中のイテレータが壊れるため
    if (!pendingDuplicates_.empty())
    {
        std::vector<std::string> requests;
        requests.swap(pendingDuplicates_);

#ifdef USE_IMGUI
        // ボタン起点の操作は ImGui の編集ジェスチャに乗らないので、
        // ショートカット経由の複製（ImGuizmoManager 側）と同じように明示的に履歴へ積む。
        // Undo 履歴はエディタ専用機能なので、ImGui を持たない構成（Release）では丸ごと省く
        nlohmann::json undoBefore = CaptureUndoState();
        bool duplicated = false;
#endif // USE_IMGUI

        for (const std::string &sourceName : requests)
        {
            BaseObject *created = DuplicateObject(sourceName);
            if (!created)
            {
                continue;
            }
#ifdef USE_IMGUI
            duplicated = true;
            // 複製直後に掴めるよう選択状態にする
            ImGuizmoManager::GetInstance()->SelectOnly(created->GetName());
#endif // USE_IMGUI
        }

#ifdef USE_IMGUI
        if (duplicated)
        {
            nlohmann::json undoAfter = CaptureUndoState();
            auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(undoBefore, undoAfter);
            UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
                "オブジェクト複製", std::move(diffBefore), std::move(diffAfter),
                [](const nlohmann::json &state) { BaseObjectManager::GetInstance()->RestoreUndoState(state); }));
        }
#endif // USE_IMGUI
    }

    for (auto &[name, obj] : objects_)
    {
        obj->UpdateHierarchy();
        obj->UpdateWorldTransformHierarchy();
    }

    // 足IK（接地）はワールド行列が確定してから解く。
    // 地面を探すレイがワールド空間なのと、書き換えたポーズを
    // 加速構造（BLAS）とスキニングの両方に間に合わせる必要があるため、この位置。
    // 足IKを持っていないオブジェクトでは即 return する
    {
        HAGINE_CPU_PROFILE("Update/Objects/FootIK");
        // IK・揺れ物の確認用の線は「デバッグ線」窓で「IK・揺れ物」として数える
        LineCategoryScope lineScope(LineCategory::Ik);
        for (auto &[name, obj] : objects_)
        {
            obj->SolveFootIk();
            // 注視（頭を見る先へ向ける）も同じ「アニメーション後・スキニング前」で掛ける
            obj->SolveLookAt();
            // 手を目標へ伸ばすのは、背骨の向き（注視）が決まってから
            obj->SolveHandIk();
            // 揺れ物は頭や体の向きが決まってから揺らす（注視・手のIKの後）
            obj->SolveSpringBone();
        }
    }

    // メタボールはワールド行列が確定してから場を組み直す。
    // 中身が前フレームと同じならここは何もしない
    MetaBallGroupManager::GetInstance()->Update();

    // レイトレーシングの加速構造へ積むのも、ワールド行列が確定したこの時点。
    // 非対応環境やRT機能を使っていないときは Submit が即 return する。
    //
    // ただし TLAS を組むのはここではない。スキニングで動くモデルは描画フェーズに入って
    // からポーズが確定する（＝BLASを作り直すのがその後）ので、TLAS はレイを飛ばす側が
    // 使う直前に EnsureTlas() で組む
    {
        RaytracingScene *pRaytracingScene = RaytracingScene::GetInstance();
        pRaytracingScene->BeginFrame();
        if (pRaytracingScene->IsActive())
        {
            for (auto &[name, obj] : objects_)
            {
                // 非表示の物をレイトレの影・反射に残さない
                if (!obj->IsRaytracingVisible())
                {
                    continue;
                }
                if (Object3d *obj3d = obj->GetObject3d())
                {
                    // 描画専用オフセット込みの行列を使う。
                    // これを外すと、傾けて描いているキャラの影だけ傾かない
                    pRaytracingScene->Submit(obj3d->GetModel(), obj->GetRenderWorldMatrix());
                }
            }
        }
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

    // 融合したメタボールの表面はグループ単位で 1 回だけ描く。
    // 個々の MetaBallObject は isModelDraw_ = false なので二重には出ない。
    // （カメラビュー窓では描かない。メタボールの定数バッファはメインの1組しか無い）
    if (!RenderView::IsExtra())
    {
        MetaBallGroupManager::GetInstance()->Draw(viewProjection);
    }
}

void BaseObjectManager::UpdateImGui()
{
#ifdef USE_IMGUI
    // オブジェクトへの編集ジェスチャ（ImGuiウィジェット・ギズモドラッグ）をUndo履歴として追跡する
    g_undoTracker.Begin([this] { return CaptureUndoState(); });

    DrawObjectCreationModel();
    DrawObjectLoadModel();

    g_undoTracker.End(
        "オブジェクト編集",
        [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); },
        ImGuizmo::IsUsing());
#endif // USE_IMGUI
}

// -------------------------------------------------------
// シーンファイル用の書き出し・作り直し（ファイルの読み書きは SceneSerializer）
// -------------------------------------------------------

bool BaseObjectManager::IsOwned(const BaseObject *obj) const
{
    if (!obj)
    {
        return false;
    }
    // 名前で引いたうえでポインタも突き合わせる（同名のゲーム側オブジェクトと取り違えない）
    const auto it = ownedObjects_.find(obj->GetName());
    return it != ownedObjects_.end() && it->second.get() == obj;
}

bool BaseObjectManager::IsSceneSaveTarget(BaseObject *obj) const
{
    if (!IsOwned(obj))
    {
        return false; // ゲーム側がコードで作る物は、毎回コードが作り直すので保存しない
    }
    for (BaseObject *p = obj; p; p = p->GetParent())
    {
        if (!IsOwned(p))
        {
            // ゲーム側オブジェクトの子に付けた物。親の名前を残して保存し、読み込み後に付け直す
            return true;
        }
        if (!p->GetShouldSave())
        {
            return false; // 自分か、所有している祖先が「保存しない」
        }
    }
    return true;
}

nlohmann::json BaseObjectManager::SerializeSceneObjects()
{
    nlohmann::json list = nlohmann::json::array();

    // 親を子より先に並べる（読み込み側は上から順に作るだけで親子付けできる）。
    // 兄弟は名前順にして、保存のたびに並びが変わらないようにする
    std::function<void(BaseObject *)> append = [&](BaseObject *obj) {
        list.push_back(obj->Serialize());

        std::vector<BaseObject *> children;
        for (BaseObject *child : *obj->GetChildren())
        {
            if (child && IsSceneSaveTarget(child))
            {
                children.push_back(child);
            }
        }
        std::sort(children.begin(), children.end(),
                  [](BaseObject *a, BaseObject *b) { return a->GetName() < b->GetName(); });
        for (BaseObject *child : children)
        {
            append(child);
        }
    };

    for (const std::string &name : GetSortedObjectNames())
    {
        BaseObject *obj = GetObjectByName(name);
        if (!IsSceneSaveTarget(obj))
        {
            continue;
        }
        // 親も保存対象なら、親から辿ったときに書かれる
        BaseObject *parent = obj->GetParent();
        if (parent && IsSceneSaveTarget(parent))
        {
            continue;
        }
        append(obj);
    }
    return list;
}

int BaseObjectManager::DeserializeSceneObjects(const nlohmann::json &objects)
{
    // 置き換える前に今の配置を片付ける。消すのは所有オブジェクトだけで、ゲーム側の物はそのまま
    RemoveAllObjects();
    pendingParents_.clear();

    if (!objects.is_array())
    {
        return 0;
    }

    // ファイル上の名前 → 実際に付いた名前。ゲーム側に同名の物がいると名前を変えて作るので、
    // 子が親を引くときはこちらを通す
    std::unordered_map<std::string, std::string> renamed;
    int created = 0;

    for (const nlohmann::json &state : objects)
    {
        if (!state.is_object())
        {
            continue;
        }
        const std::string savedName = state.value("name", std::string());
        if (savedName.empty())
        {
            Logger::Warn("[Scene] 名前の無いオブジェクトを飛ばしました");
            continue;
        }

        const std::string name = MakeUniqueObjectName(savedName);
        if (name != savedName)
        {
            Logger::Warn("[Scene] \"" + savedName + "\" は既に居るので \"" + name + "\" として読み込みました");
        }

        BaseObject *obj = CreateObjectFromState(name, state);
        if (!obj)
        {
            Logger::Warn("[Scene] \"" + savedName + "\" はモデルもプリミティブも無いので作れませんでした");
            continue;
        }
        renamed[savedName] = name;
        ++created;

        // 1体の中身が壊れていても、他のオブジェクトの読み込みは続ける
        try
        {
            obj->Deserialize(state);
        }
        catch (const nlohmann::json::exception &e)
        {
            Logger::Warn("[Scene] \"" + savedName + "\" の設定の一部を読めませんでした: " + e.what());
        }

        const std::string parentName = state.value("parent", std::string());
        if (parentName.empty())
        {
            continue;
        }
        const auto renamedParent = renamed.find(parentName);
        BaseObject *parent = GetObjectByName(renamedParent != renamed.end() ? renamedParent->second : parentName);
        if (parent && parent != obj)
        {
            obj->SetParent(parent);
        }
        else
        {
            // ゲーム側の親はシーンの Initialize で登録されるので、それまで待つ
            pendingParents_[name] = parentName;
        }
    }
    return created;
}

void BaseObjectManager::ResolvePendingParents()
{
    for (auto it = pendingParents_.begin(); it != pendingParents_.end();)
    {
        BaseObject *child = GetObjectByName(it->first);
        BaseObject *parent = GetObjectByName(it->second);
        if (!child)
        {
            it = pendingParents_.erase(it); // 子が消えていれば待つ意味が無い
            continue;
        }
        if (parent && parent != child)
        {
            child->SetParent(parent);
            it = pendingParents_.erase(it);
            continue;
        }
        ++it;
    }

    // 残った物は親が居ないまま。黙っていると「なぜか付いていない」になるので知らせる
    for (const auto &[childName, parentName] : pendingParents_)
    {
        Logger::Warn("[Scene] \"" + childName + "\" の親 \"" + parentName + "\" が見つからないので、親なしで置いています");
    }
    pendingParents_.clear();
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
    // エディタで置く物の中身はシーンファイルが持つ。同名の「オブジェクト単体の保存」は拾わない
    newObject->SetLoadObjectDataFile(false);
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
    // エディタで置く物の中身はシーンファイルが持つ。同名の「オブジェクト単体の保存」は拾わない
    newObject->SetLoadObjectDataFile(false);
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
    newObject->SetLoadObjectDataFile(false);
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

BaseObject *BaseObjectManager::CloneObject(BaseObject *pSource, const Vector3 &offset,
                                           const std::string &desiredName)
{
    if (!pSource)
    {
        return nullptr;
    }

    // 名前は Init より先に確定させる。あとから変えても DataHandler が
    // 複製元の名前で作られたままになり、保存先が元と衝突する
    const std::string name =
        MakeUniqueObjectName(desiredName.empty() ? pSource->GetName() : desiredName);

    // 元と同じ種類で作り直す。メタボールは Init が動的モデルの生成と
    // グループへの登録まで面倒を見るので、モデルを作り直してはいけない
    const bool isMetaBall = (dynamic_cast<MetaBallObject *>(pSource) != nullptr);
    // type_ が Count 以外ならプリミティブとして作られたオブジェクト
    const bool isPrimitive = (pSource->GetPrimitiveType() != PrimitiveType::Count);
    if (!isMetaBall && !isPrimitive && pSource->GetModelPath().empty())
    {
        // モデルもプリミティブも持たないものは作り直しようがない
        return nullptr;
    }

    std::unique_ptr<BaseObject> newObject =
        isMetaBall ? std::unique_ptr<BaseObject>(std::make_unique<MetaBallObject>())
                   : std::make_unique<BaseObject>();
    newObject->SetPrimitive(isPrimitive);
    newObject->SetLoadObjectDataFile(false);
    newObject->Init(name);

    if (!isMetaBall)
    {
        // モデル or プリミティブを同じもので作る
        const std::string &modelPath = pSource->GetModelPath();
        if (isPrimitive || modelPath.empty())
        {
            newObject->CreatePrimitiveModel(pSource->GetPrimitiveType());
        }
        else
        {
            newObject->CreateModel(modelPath);
        }
    }

    // 種類ごとの中身を写す（メタボールは要素リストとグループ名も含む）
    newObject->CopyPropertiesFrom(*pSource);
    newObject->SetShouldSave(pSource->GetShouldSave());

    newObject->GetLocalPosition() += offset;
    newObject->GetWorldTransform()->UpdateMatrix();

    BaseObject *result = newObject.get();
    this->AddObject(std::move(newObject));
    return result;
}

BaseObject *BaseObjectManager::DuplicateObject(const std::string &sourceName)
{
    // 元と完全に重ねると掴めないので少しずらす
    BaseObject *created = CloneObject(GetObjectByName(sourceName), {1.0f, 0.0f, 0.0f});
    if (!created)
    {
        return nullptr;
    }
    ImGuiNotification::Post("オブジェクトを複製しました: " + created->GetName(), {0.4f, 0.8f, 1.0f, 1.0f});
    return created;
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

#ifdef USE_IMGUI
// 階層ツリーの右クリックメニューで選んだ操作。削除・複製はツリーの構造を変えるので、
// 親子付けと同じくツリーを描き終えてから適用する
enum class HierarchyAction
{
    None,
    Focus,
    Duplicate,
    Delete,
};
HierarchyAction g_hierarchyAction = HierarchyAction::None;
std::string g_hierarchyActionTarget;

// 階層ツリーの検索語
char g_hierarchyFilter[128] = {};

// 範囲選択・キー移動のため、ツリーに並んだ順（開いている行だけ）を覚えておく。
// 今フレームの並びを集めながら、判定には前フレームの並びを使う（描き終えるまで全体が分からないため）
std::vector<std::string> g_hierarchyVisibleOrder;
std::vector<std::string> g_hierarchyVisibleOrderBuilding;
std::string g_hierarchyAnchor; // Shift+クリックの起点（最後に普通にクリックした行）

/// <summary>前フレームの並びで、from から to までをまとめて選ぶ</summary>
void SelectHierarchyRange(const std::string &from, const std::string &to)
{
    auto itFrom = std::find(g_hierarchyVisibleOrder.begin(), g_hierarchyVisibleOrder.end(), from);
    auto itTo = std::find(g_hierarchyVisibleOrder.begin(), g_hierarchyVisibleOrder.end(), to);
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    if (itFrom == g_hierarchyVisibleOrder.end() || itTo == g_hierarchyVisibleOrder.end())
    {
        gizmo->SelectOnly(to);
        return;
    }
    if (itFrom > itTo)
    {
        std::swap(itFrom, itTo);
    }
    gizmo->SelectOnly(*itFrom);
    for (auto it = itFrom; it != itTo + 1; ++it)
    {
        gizmo->AddToSelection(*it);
    }
}

/// <summary>名前に検索語が含まれるか（英字の大文字小文字は区別しない）</summary>
bool NameMatchesFilter(const std::string &name, const char *filter)
{
    if (!filter || filter[0] == '\0')
    {
        return true;
    }
    auto lower = [](std::string s) {
        for (char &c : s)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    };
    return lower(name).find(lower(filter)) != std::string::npos;
}

/// <summary>
/// オブジェクトが検索語に当たるか。先頭に書くと探す対象を変えられる:
///   t:タグ   … コライダーのタグ（t:Rock など）
///   m:モデル … モデルのパス（m:gltf / m:rock など。プリミティブは "primitive"）
///   それ以外 … 名前
/// </summary>
bool ObjectMatchesFilter(BaseObject *obj, const char *filter)
{
    if (!obj)
    {
        return false;
    }
    if (!filter || filter[0] == '\0')
    {
        return true;
    }
    const std::string text = filter;
    if (text.rfind("t:", 0) == 0)
    {
        const std::string query = text.substr(2);
        for (const auto &collider : obj->GetColliders())
        {
            if (collider && NameMatchesFilter(collider->GetTag(), query.c_str()))
            {
                return true;
            }
        }
        return false;
    }
    if (text.rfind("m:", 0) == 0)
    {
        const std::string query = text.substr(2);
        const std::string model = obj->IsPrimitive() ? std::string("primitive") : obj->GetModelPath();
        return NameMatchesFilter(model, query.c_str());
    }
    return NameMatchesFilter(obj->GetName(), filter);
}

/// <summary>自分か子孫のどれかが検索語に当たるか（当たる子を持つ親も一覧に残す）</summary>
bool SubtreeMatchesFilter(BaseObject *obj, const char *filter)
{
    if (!obj)
    {
        return false;
    }
    if (ObjectMatchesFilter(obj, filter))
    {
        return true;
    }
    for (BaseObject *child : *obj->GetChildren())
    {
        if (SubtreeMatchesFilter(child, filter))
        {
            return true;
        }
    }
    return false;
}
#endif // USE_IMGUI
} // namespace

namespace {
// 種類をまたいだ親子付け（光源・パーティクル）のドラッグ＆ドロップ結果。
// オブジェクト同士と同じく、ツリー描画中に構造を変えないようフレーム末へ持ち越す
std::string g_attachChild;  // 子にする対象の登録名
std::string g_attachParent; // 親にする対象の登録名（空文字 = 解除）
bool g_attachRequested = false;

#ifdef USE_IMGUI
/// <summary>
/// 光源・パーティクルのノードを描く（自分にぶら下がる子があれば再帰する）
/// </summary>
/// <param name="attachName">親子付けの登録名</param>
/// <param name="depth">インデントの深さ</param>
void ShowAttachNode(const std::string &attachName, int depth)
{
    AttachmentManager *attachment = AttachmentManager::GetInstance();
    const AttachTarget *target = attachment->FindTarget(attachName);
    if (!target)
    {
        return;
    }

    const std::vector<std::string> children = attachment->GetChildNames(attachName);

    const std::string indent(depth * 2, ' ');
    const std::string displayName = indent + attachName;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // 種類が一目で分かるよう色を分ける
    const ImVec4 color = (target->kind == AttachKind::Light) ? DebugTheme::kAccentYellow : DebugTheme::kAccentPurple;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);
    ImGui::PopStyleColor();

    // ドラッグ元
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::SetDragDropPayload("ATTACH_NODE", attachName.c_str(), attachName.size() + 1);
        ImGui::Text("移動: %s", attachName.c_str());
        ImGui::EndDragDropSource();
    }
    // ドロップ先: このノードを親にする（相手が光源でもオブジェクトでも受ける）
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("ATTACH_NODE"))
        {
            g_attachChild = static_cast<const char *>(p->Data);
            g_attachParent = attachName;
            g_attachRequested = true;
        }
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("OBJ_NODE"))
        {
            g_attachChild = static_cast<const char *>(p->Data);
            g_attachParent = attachName;
            g_attachRequested = true;
        }
        ImGui::EndDragDropTarget();
    }

    // 右クリックメニュー
    if (ImGui::BeginPopupContextItem((attachName + "##attachctx").c_str()))
    {
        ImGui::TextDisabled("%s", attachName.c_str());
        ImGui::Separator();
        const std::string parentName = attachment->GetParentName(attachName);
        if (ImGui::MenuItem("親子付けを解除", nullptr, false, !parentName.empty()))
        {
            g_attachChild = attachName;
            g_attachParent.clear();
            g_attachRequested = true;
        }
        ImGui::EndPopup();
    }

    if (nodeOpen)
    {
        for (const std::string &childName : children)
        {
            ShowAttachNode(childName, depth + 1);
        }
        ImGui::TreePop();
    }
}
#endif // USE_IMGUI
} // namespace

void BaseObjectManager::ShowAttachChildrenOf(const std::string &parentName, int depth)
{
#ifdef USE_IMGUI
    for (const std::string &childName : AttachmentManager::GetInstance()->GetChildNames(parentName))
    {
        ShowAttachNode(childName, depth);
    }
#else
    (void)parentName;
    (void)depth;
#endif // USE_IMGUI
}

void BaseObjectManager::ShowRootAttachNodes()
{
#ifdef USE_IMGUI
    AttachmentManager *attachment = AttachmentManager::GetInstance();
    for (AttachKind kind : {AttachKind::Light, AttachKind::Particle})
    {
        for (const std::string &name : attachment->GetTargetNames(kind))
        {
            // 親を持つものは親のノードの下に出るので、ここではルートのものだけ
            if (attachment->GetParentName(name).empty())
            {
                ShowAttachNode(name, 0);
            }
        }
    }
#endif // USE_IMGUI
}

void BaseObjectManager::ApplyPendingAttachRequest()
{
#ifdef USE_IMGUI
    if (!g_attachRequested)
    {
        return;
    }

    AttachmentManager *attachment = AttachmentManager::GetInstance();
    if (g_attachParent.empty())
    {
        attachment->Detach(g_attachChild);
        ImGuiNotification::Post("親子付けを解除しました: " + g_attachChild, {0.82f, 0.58f, 0.36f, 1.0f});
    }
    else
    {
        // 3Dオブジェクトを子にする場合、オブジェクト同士の親子付けと二重に効いてしまうので先に外す
        if (BaseObject *childObject = GetObjectByName(g_attachChild))
        {
            if (childObject->GetParent())
            {
                childObject->DetachParent();
            }
        }

        if (attachment->Attach(g_attachChild, g_attachParent))
        {
            ImGuiNotification::Post(g_attachChild + " を " + g_attachParent + " に付けました",
                                    {0.45f, 0.68f, 0.52f, 1.0f});
        }
        else
        {
            ImGuiNotification::Post("親子付けできません（循環参照など）", {0.82f, 0.58f, 0.36f, 1.0f});
        }
    }

    g_attachRequested = false;
    g_attachChild.clear();
    g_attachParent.clear();
#endif // USE_IMGUI
}

void BaseObjectManager::ShowParentChildHierarchy()
{
#ifdef USE_IMGUI

    if (ImGui::CollapsingHeader("階層エディター", ImGuiTreeNodeFlags_DefaultOpen))
    {

        // ---- 検索欄と操作の説明（説明は「？」にまとめて一覧の場所を広く取る）----
        const float helpWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - helpWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##hierarchyFilter", ICON_FA_SEARCH " 名前で絞り込み（t:タグ  m:モデル）", g_hierarchyFilter,
                                 sizeof(g_hierarchyFilter));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("名前の一部で絞り込みます\n"
                              "t:Rock … コライダーのタグで探す\n"
                              "m:gltf … モデルのパスで探す（プリミティブは m:primitive）\n"
                              "Esc で検索語を消します");
        }
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            g_hierarchyFilter[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_QUESTION_CIRCLE);
        ImGui::SetItemTooltip("クリック: 選択 / Ctrl+クリック: 選択に追加・解除 / Shift+クリック: 範囲選択\n"
                              "↑↓: 選択を1行ずつ動かす（Shift で範囲を広げる）\n"
                              "ダブルクリック: 選択してカメラを寄せる\n"
                              "目のアイコン: 表示・非表示の切り替え\n"
                              "右クリック: 親子付け・複製・削除など\n"
                              "ドラッグして別の行に重ねると親子付け（余白へ落とすと解除）");

        const std::vector<std::string> sortedNames = GetSortedObjectNames();
        const bool filtering = g_hierarchyFilter[0] != '\0';
        int shownCount = 0;
        if (filtering)
        {
            for (const std::string &name : sortedNames)
            {
                shownCount += ObjectMatchesFilter(GetObjectByName(name), g_hierarchyFilter) ? 1 : 0;
            }
            ImGui::TextDisabled("%d / %d 件が一致", shownCount, static_cast<int>(sortedNames.size()));
        }
        else
        {
            ImGui::TextDisabled("%d 個のオブジェクト", static_cast<int>(sortedNames.size()));
        }

        // 階層構造を表示（窓の高さに合わせて伸ばす。下の保存対象の欄のぶんは残す）
        const float treeHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.72f);
        ImGui::BeginChild("HierarchyView", ImVec2(0, treeHeight), ImGuiChildFlags_Borders);
        g_hierarchyVisibleOrderBuilding.clear();

        // 一覧にフォーカスがある間は ↑↓ で選択を1行ずつ動かす（Shift を押していれば範囲を広げる）
        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && !g_hierarchyVisibleOrder.empty())
        {
            const int direction = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0);
            if (direction != 0)
            {
                ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
                // 今の選択のうち、並びの中で一番端（進む向きの先頭）にあるものから動かす
                int current = -1;
                for (int i = 0; i < static_cast<int>(g_hierarchyVisibleOrder.size()); ++i)
                {
                    if (gizmo->IsSelected(g_hierarchyVisibleOrder[i]))
                    {
                        if (current < 0 || direction > 0)
                        {
                            current = i;
                        }
                    }
                }
                const int next = std::clamp(current < 0 ? 0 : current + direction, 0, static_cast<int>(g_hierarchyVisibleOrder.size()) - 1);
                const std::string &nextName = g_hierarchyVisibleOrder[next];
                if (ImGui::GetIO().KeyShift && !g_hierarchyAnchor.empty())
                {
                    SelectHierarchyRange(g_hierarchyAnchor, nextName);
                }
                else
                {
                    gizmo->SelectOnly(nextName);
                    g_hierarchyAnchor = nextName;
                }
            }
        }

        // objects_ は unordered_map なので、そのまま回すと並び順がハッシュ順（実質ランダム）になり、
        // オブジェクトを増減させるたびに一覧の位置が変わってしまう。名前順に並べて安定させる。
        for (const std::string &name : sortedNames)
        {
            BaseObject *obj = GetObjectByName(name);
            if (obj && !obj->GetParent())
            { // ルートオブジェクトのみ表示
                ShowObjectHierarchy(obj, 0);
            }
        }

        // どのオブジェクトにも付いていない光源・パーティクルもルートに並べる（検索中は出さない）
        if (!filtering)
        {
            ShowRootAttachNodes();
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
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("ATTACH_NODE"))
            {
                g_attachChild = static_cast<const char *>(p->Data);
                g_attachParent.clear();
                g_attachRequested = true;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndChild();

        // 今フレームの並びを次のフレームの範囲選択・キー移動に使う
        g_hierarchyVisibleOrder.swap(g_hierarchyVisibleOrderBuilding);

        // 右クリックメニューの操作もツリー描画後に適用する
        if (g_hierarchyAction != HierarchyAction::None)
        {
            ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
            gizmo->SelectOnly(g_hierarchyActionTarget);
            switch (g_hierarchyAction)
            {
            case HierarchyAction::Focus:
                gizmo->FocusOnSelection();
                break;
            case HierarchyAction::Duplicate:
                gizmo->DuplicateSelectedObjects();
                break;
            case HierarchyAction::Delete:
                gizmo->DeleteSelectedObjects();
                break;
            default:
                break;
            }
            g_hierarchyAction = HierarchyAction::None;
            g_hierarchyActionTarget.clear();
        }

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

        // 種類をまたいだ親子付けも同じタイミングで適用する
        ApplyPendingAttachRequest();

        // 組み合わせを選んで細かく設定するためのパネル
        ImGui::Spacing();
        AttachmentManager::GetInstance()->DrawImGui();
    }
#endif // USE_IMGUI
}

void BaseObjectManager::ShowObjectHierarchy(BaseObject *obj, int depth)
{
#ifdef USE_IMGUI

    if (!obj)
        return;
    (void)depth;

    // 検索中は、自分も子孫も当たらない枝ごと出さない
    const bool filtering = g_hierarchyFilter[0] != '\0';
    if (filtering && !SubtreeMatchesFilter(obj, g_hierarchyFilter))
    {
        return;
    }

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    const std::string &name = obj->GetName();
    const bool isSelected = gizmo->IsSelected(name);
    const bool isVisible = obj->GetIsModelDraw();
    const bool isOwned = IsOwned(obj);
    const bool isSaveTarget = IsSceneSaveTarget(obj);

    // ダブルクリックは「開く」ではなく「カメラを寄せる」に使うので OpenOnDoubleClick は付けない。
    // 右端の目のアイコンを重ねて置くので AllowOverlap を付ける
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    if (isSelected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 子がない場合は葉ノードフラグを追加
    if (obj->GetChildren()->empty() && !AttachmentManager::GetInstance()->HasChildren(name))
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    // 検索中は当たった物が見えるよう、枝を開いておく
    if (filtering)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    // 種類のアイコン（プリミティブ / メタボール / アニメーション付き / モデル）
    const char *typeIcon = ICON_FA_SHAPES;
    if (dynamic_cast<MetaBallObject *>(obj))
    {
        typeIcon = ICON_FA_CIRCLE;
    }
    else if (obj->IsPrimitive())
    {
        typeIcon = ICON_FA_CUBE;
    }
    else if (obj->GetObject3d() && obj->GetObject3d()->GetModel() && obj->GetObject3d()->GetHaveAnimation())
    {
        typeIcon = ICON_FA_RUNNING;
    }
    // プレハブから置いた物（の根）は箱のアイコン
    if (!obj->GetPrefabSource().empty())
    {
        typeIcon = ICON_FA_BOX;
    }

    // 非表示の物は行ごと薄くする
    if (!isVisible)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
    bool nodeOpen = ImGui::TreeNodeEx(name.c_str(), flags, "%s  %s", typeIcon, name.c_str());
    ImGui::PopStyleVar();
    if (!isVisible)
    {
        ImGui::PopStyleColor();
    }

    g_hierarchyVisibleOrderBuilding.push_back(name);

    // クリックで選択（矢印で開閉したときは選択を変えない）。Ctrl で追加・解除、Shift で範囲
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        if (ImGui::GetIO().KeyShift && !g_hierarchyAnchor.empty())
        {
            SelectHierarchyRange(g_hierarchyAnchor, name);
        }
        else if (ImGui::GetIO().KeyCtrl)
        {
            gizmo->ToggleSelect(name);
            g_hierarchyAnchor = name;
        }
        else
        {
            gizmo->SelectOnly(name);
            g_hierarchyAnchor = name;
        }
    }
    // ダブルクリックでカメラを寄せる
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        g_hierarchyAction = HierarchyAction::Focus;
        g_hierarchyActionTarget = name;
    }

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
        // 光源・パーティクルを重ねたらこのオブジェクトに付ける
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("ATTACH_NODE"))
        {
            g_attachChild = static_cast<const char *>(p->Data);
            g_attachParent = obj->GetName();
            g_attachRequested = true;
        }
        ImGui::EndDragDropTarget();
    }

    // 右クリックメニュー: よく使う操作 / 親を設定 / 親子解除 / 親のSRT継承設定
    if (ImGui::BeginPopupContextItem((obj->GetName() + "##ctx").c_str()))
    {
        ImGui::TextDisabled("%s", obj->GetName().c_str());
        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " 選択してカメラを寄せる", "ダブルクリック"))
        {
            g_hierarchyAction = HierarchyAction::Focus;
            g_hierarchyActionTarget = name;
        }
        if (ImGui::MenuItem(isVisible ? ICON_FA_EYE_SLASH " 非表示にする" : ICON_FA_EYE " 表示する"))
        {
            obj->SetIsModelDraw(!isVisible);
        }
        if (isOwned)
        {
            bool shouldSave = obj->GetShouldSave();
            if (ImGui::MenuItem(ICON_FA_SAVE " シーンに保存する", nullptr, &shouldSave))
            {
                obj->SetShouldSave(shouldSave);
            }
            ImGui::SetItemTooltip("外すと、この物と子はシーンファイルに書かれません（今の画面からは消えません）");
        }
        else
        {
            ImGui::MenuItem(ICON_FA_GAMEPAD " ゲーム側のオブジェクト", nullptr, false, false);
            ImGui::SetItemTooltip("コードで作られる物なので、シーンファイルには保存されません");
        }
        if (ImGui::MenuItem(ICON_FA_CLONE " 複製", "Ctrl+D"))
        {
            g_hierarchyAction = HierarchyAction::Duplicate;
            g_hierarchyActionTarget = name;
        }
        // プレハブの保存・反映・置き直し（ダイアログはギズモ側が毎フレーム描いている）
        if (obj->GetPrefabSource().empty())
        {
            gizmo->DrawPrefabLinkMenuItems(obj);
        }
        else if (ImGui::BeginMenu((std::string(ICON_FA_BOX " プレハブ: ") + obj->GetPrefabSource()).c_str()))
        {
            gizmo->DrawPrefabLinkMenuItems(obj);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_COPY " 名前をコピー"))
        {
            ImGui::SetClipboardText(name.c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.52f, 0.48f, 1.0f));
        if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除", "Delete"))
        {
            g_hierarchyAction = HierarchyAction::Delete;
            g_hierarchyActionTarget = name;
        }
        ImGui::PopStyleColor();
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

    // 右端の保存アイコン（シーンに保存する・しないの切り替え）。目のアイコンの左に重ねて置く
    {
        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushID((name + "##save").c_str());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        if (!isOwned)
        {
            // ゲーム側の物は切り替えられない。種類が分かるよう印だけ出す
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::BeginDisabled();
            ImGui::Button(ICON_FA_GAMEPAD, ImVec2(buttonWidth, 0.0f));
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("ゲーム側がコードで作る物です（シーンファイルには保存されません）");
        }
        else
        {
            // 自分は保存する設定でも、親が保存しない設定なら書かれない。その場合は薄く出す
            const bool blockedByParent = obj->GetShouldSave() && !isSaveTarget;
            const ImVec4 iconColor = isSaveTarget      ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                     : blockedByParent ? ImVec4(0.60f, 0.52f, 0.40f, 1.0f)
                                                       : ImVec4(0.95f, 0.66f, 0.38f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
            if (ImGui::Button(obj->GetShouldSave() ? ICON_FA_SAVE : ICON_FA_BAN, ImVec2(buttonWidth, 0.0f)))
            {
                obj->SetShouldSave(!obj->GetShouldSave());
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip(isSaveTarget      ? "シーンに保存します（クリックで保存しない）"
                                  : blockedByParent ? "親が「保存しない」なので保存されません"
                                                    : "シーンに保存しません（クリックで保存する）");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // 右端の目のアイコン（表示・非表示の切り替え）。行に重ねて置く
    {
        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);
        ImGui::PushID((name + "##vis").c_str());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, isVisible ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImVec4(0.95f, 0.66f, 0.38f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        if (ImGui::Button(isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(buttonWidth, 0.0f)))
        {
            obj->SetIsModelDraw(!isVisible);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip(isVisible ? "表示中（クリックで隠す）" : "非表示（クリックで表示）");
        ImGui::PopID();
    }

    if (nodeOpen)
    {
        // 子オブジェクトを表示
        for (BaseObject *pChild : *obj->GetChildren())
        {
            ShowObjectHierarchy(pChild, depth + 1);
        }
        // このオブジェクトに付いている光源・パーティクルも同じ階層に並べる
        ShowAttachChildrenOf(obj->GetName(), depth + 1);
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

int BaseObjectManager::ReloadModelFile(const std::string &modelPath)
{
    std::vector<BaseObject *> targets;
    for (const auto &[name, obj] : objects_)
    {
        if (obj && !obj->IsPrimitive() && obj->GetModelPath() == modelPath)
        {
            targets.push_back(obj);
        }
    }
    if (targets.empty())
    {
        // 使っている物が無くても、次に置くときに新しい形で出るよう読み込み結果だけは捨てておく
        ModelManager::GetInstance()->ForgetModelFile(modelPath);
        return 0;
    }

    // 差し替えるとスキンなどの GPU バッファが作り直しになる。前のフレームの描画が使い終わってから行う
    // （ホットリロードは時々しか起きないので、待つ分の引っかかりは許す）
    DirectXCommon::GetInstance()->WaitForGPU();
    ModelManager::GetInstance()->ForgetModelFile(modelPath);

    int reloaded = 0;
    for (BaseObject *obj : targets)
    {
        reloaded += obj->ReloadModel() ? 1 : 0;
    }
    return reloaded;
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

        static std::string objectNameBuffer;

        // オブジェクト名入力欄
        ImGui::InputText("オブジェクト名", &objectNameBuffer);

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
            objectNameBuffer = suggested;
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
        bool canCreate = !objectNameBuffer.empty() && !modelPath_.empty();

        // 名前とモデルが揃うまでは確定色にしない
        const bool createPressed = canCreate ? ConfirmButton("生成", ImVec2(120, 0))
                                             : NeutralButton("生成", ImVec2(120, 0));
        if (createPressed && canCreate)
        {
            objectName_ = objectNameBuffer;
            CreateObject(objectName_, modelPath_, texturePath_);

            // 入力欄とパスをリセット
            objectNameBuffer.clear();
            modelPath_ = "";
            texturePath_ = "";

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(120, 0)))
        {
            // 入力欄とパスをリセット
            objectNameBuffer.clear();
            modelPath_ = "";
            texturePath_ = "";

            ImGui::CloseCurrentPopup();
        }

        // 生成できない場合の理由を表示
        if (!canCreate)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "生成するには:");
            if (objectNameBuffer.empty())
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
    // 保存する／しないの切り替えは各行の右端のアイコンと右クリックメニューで行う
    ShowParentChildHierarchy();
#endif // USE_IMGUI
}

BaseObject *BaseObjectManager::CreateObjectFromState(const std::string &name, const nlohmann::json &s)
{
    const std::string modelPath = s.value("modelPath", std::string());
    const bool isPrimitive = s.value("isPrimitive", false);
    // メタボールは modelName が目印になっているだけでモデルファイルは無い。
    // 素の BaseObject として作ると "MetaBall" というモデルを読みに行ってしまう
    const bool isMetaBall = (modelPath == kMetaBallModelTag);
    if (!isMetaBall && modelPath.empty() && !isPrimitive)
    {
        return nullptr; // モデルもプリミティブも無い場合は再生成できない
    }

    std::unique_ptr<BaseObject> newObject =
        isMetaBall ? std::unique_ptr<BaseObject>(std::make_unique<MetaBallObject>())
                   : std::make_unique<BaseObject>();
    // 中身は呼び出し側が状態JSONから流し込む。同名の「オブジェクト単体の保存」を拾って混ぜない
    newObject->SetLoadObjectDataFile(false);
    newObject->Init(name);
    if (isMetaBall)
    {
        // MetaBallObject::Init が動的モデルの生成とグループ登録まで済ませている
    }
    else if (!modelPath.empty())
    {
        newObject->CreateModel(modelPath);
    }
    else
    {
        newObject->SetPrimitive(true);
        newObject->CreatePrimitiveModel(
            static_cast<PrimitiveType>(s.value("primitiveType", static_cast<int>(PrimitiveType::Count))));
    }
    AddObject(std::move(newObject));
    return GetObjectByName(name);
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
        if (owned)
        {
            state[name] = CaptureObjectState(owned.get());
        }
    }
    return state;
}

nlohmann::json BaseObjectManager::CaptureObjectState(BaseObject *obj) const
{
    using nlohmann::json;
    json s;
    if (!obj)
    {
        return s;
    }

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
    s["prefabSource"] = obj->GetPrefabSource();

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

    // コライダー（形状・タグ・マスク）。これが無いと、停止しても再生中に足した
    // コライダーが残り、消したコライダーが戻らない
    s["colliders"] = obj->CaptureColliderState();

    // メタボールはモデルファイルを持たないので、modelPath だけでは作り直せない。
    // 要素リストとグループ名まで残しておく（削除の Undo・貼り付けの Redo に必要）
    if (const MetaBallObject *metaBall = dynamic_cast<const MetaBallObject *>(obj))
    {
        json elements = json::array();
        for (const MetaBallElement &element : metaBall->GetElements())
        {
            json e;
            e["position"] = element.position;
            e["shape"] = static_cast<int>(element.shape);
            e["radius"] = element.radius;
            e["stiffness"] = element.stiffness;
            e["negative"] = element.negative;
            e["axis"] = element.axis;
            e["enabled"] = element.enabled;
            e["radiusScale"] = element.radiusScale;
            elements.push_back(e);
        }
        s["metaBallGroup"] = metaBall->GetGroupName();
        s["metaBallElements"] = elements;
    }
    return s;
}

void BaseObjectManager::ApplyObjectState(BaseObject *obj, const nlohmann::json &s)
{
    using nlohmann::json;
    if (!obj || !s.is_object())
    {
        return;
    }

    // メタボールの要素リストとグループ名を戻す
    if (MetaBallObject *metaBall = dynamic_cast<MetaBallObject *>(obj))
    {
        if (s.contains("metaBallGroup"))
        {
            metaBall->SetGroupName(s["metaBallGroup"].get<std::string>());
        }
        if (s.contains("metaBallElements") && s["metaBallElements"].is_array())
        {
            std::vector<MetaBallElement> elements;
            elements.reserve(s["metaBallElements"].size());
            for (const json &e : s["metaBallElements"])
            {
                MetaBallElement element{};
                element.position = e.value("position", Vector3{});
                element.shape = static_cast<MetaBallShape>(e.value("shape", 0));
                element.radius = e.value("radius", 1.0f);
                element.stiffness = e.value("stiffness", 1.0f);
                element.negative = e.value("negative", false);
                element.axis = e.value("axis", Vector3{});
                element.enabled = e.value("enabled", true);
                element.radiusScale = e.value("radiusScale", Vector3{1.0f, 1.0f, 1.0f});
                elements.push_back(element);
            }
            metaBall->GetElements() = std::move(elements);
            // 選択中の添字が要素数を超えたままになると、インスペクタが空振りする
            if (metaBall->GetSelectedElementIndex() >= static_cast<int>(metaBall->GetElements().size()))
            {
                metaBall->SetSelectedElementIndex(static_cast<int>(metaBall->GetElements().size()) - 1);
            }
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
    if (s.contains("prefabSource"))
    {
        obj->SetPrefabSource(s["prefabSource"].get<std::string>());
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

    // コライダーはモデルが揃ってから戻す
    // （メッシュコライダーは obj3d_ のモデルから三角形を組み直すため）
    if (s.contains("colliders"))
    {
        obj->RestoreColliderState(s["colliders"]);
    }
}

void BaseObjectManager::RestoreUndoState(const nlohmann::json &state)
{
    using nlohmann::json;
    if (!state.is_object())
    {
        return;
    }

    // 復元は人が押した操作ではないので、追加・削除のトーストは止める（履歴には残る）
    ImGuiNotification::ScopedMute mute;

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
            obj = CreateObjectFromState(name, s);
            if (!obj)
            {
                continue;
            }
        }

        ApplyObjectState(obj, s);
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
