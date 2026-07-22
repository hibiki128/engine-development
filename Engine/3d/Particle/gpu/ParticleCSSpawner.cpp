#define NOMINMAX
#include "ParticleCSSpawner.h"
#include "render/DrawGroupManager.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <filesystem>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace Hagine {

ParticleCSEmitter *ParticleCSSpawner::Spawn(const std::string &templateName)
{
    if (templateName.empty())
    {
        return nullptr;
    }
    // テンプレートの json が無ければ「空のエミッター」が出来てしまい原因が分かりづらいので弾く。
    const std::filesystem::path jsonPath =
        std::filesystem::path(AssetPath::Json("ParticleCS")) / (templateName + ".json");
    if (!std::filesystem::exists(jsonPath))
    {
        return nullptr;
    }

    Instance inst;
    inst.emitter = std::make_unique<ParticleCSEmitter>();
    inst.templateName = templateName;
    // ギズモ登録はテンプレート（エディタ側の同名エミッター）の登録を奪うので行わない。
    // Initialize より前に決める必要がある。
    inst.emitter->SetRegisterGizmo(false);
    // Initialize が ParticleCS/<templateName>.json を読み、グループも含めて構築する。
    inst.emitter->Initialize(templateName);
    inst.emitter->SetName(MakeInstanceName(templateName));
    inst.emitter->SetActive(true);

    instances_.push_back(std::move(inst));
    return instances_.back().emitter.get();
}

void ParticleCSSpawner::Despawn(ParticleCSEmitter *emitter)
{
    if (!emitter)
    {
        return;
    }
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [emitter](const Instance &i) { return i.emitter.get() == emitter; });
    if (it != instances_.end())
    {
        instances_.erase(it);
    }
}

void ParticleCSSpawner::DespawnWhenFinished(ParticleCSEmitter *emitter)
{
    Instance *inst = Find(emitter);
    if (!inst)
    {
        return;
    }
    inst->despawnWhenFinished = true;
    // 新規発生を止める。残っている粒子は寿命どおり消えていく。
    inst->emitter->SetAuto(false);
}

void ParticleCSSpawner::SetPersistent(ParticleCSEmitter *emitter, bool persistent)
{
    if (Instance *inst = Find(emitter))
    {
        inst->persistent = persistent;
    }
}

bool ParticleCSSpawner::IsAlive(const ParticleCSEmitter *emitter) const
{
    return Find(emitter) != nullptr;
}

void ParticleCSSpawner::ClearSceneScoped()
{
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [](const Instance &i) { return !i.persistent; }),
                     instances_.end());
    // 生き残った常駐エミッターの親子付けは解除する。
    // 親は破棄される前シーンのオブジェクトなので、持ち越すとダングリングポインタになる。
    for (auto &inst : instances_)
    {
        inst.emitter->ClearParent();
    }
}

void ParticleCSSpawner::Finalize()
{
    instances_.clear();
}

void ParticleCSSpawner::DrawCompute(const ViewProjection &vp)
{
    // 自動破棄の回収を先に行う（今フレームぶんの無駄なディスパッチを避ける）。
    // GetTotalAliveParticles は readback ベースで 1〜2 フレーム遅延するため、
    // 「発生を止めた直後にまだ 0 でない」ことはあっても取りこぼしは起きない。
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [](const Instance &i) {
                                        return i.despawnWhenFinished &&
                                               i.emitter->GetTotalAliveParticles() == 0;
                                    }),
                     instances_.end());

    for (auto &inst : instances_)
    {
        inst.emitter->Update();
        inst.emitter->DrawCompute(vp);
    }
}

void ParticleCSSpawner::DrawGraphics(const ViewProjection &vp, int stageIndex)
{
    auto *groupManager = DrawGroupManager::GetInstance();
    for (auto &inst : instances_)
    {
        const std::string &group = inst.emitter->GetDrawGroup();
        if (DrawGroupManager::StageOf(group) != stageIndex)
        {
            continue;
        }
        if (!groupManager->IsGroupVisible(group))
        {
            continue;
        }
        inst.emitter->DrawGraphics(vp);
    }
}

ParticleCSSpawner::Instance *ParticleCSSpawner::Find(const ParticleCSEmitter *emitter)
{
    if (!emitter)
    {
        return nullptr;
    }
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [emitter](const Instance &i) { return i.emitter.get() == emitter; });
    return (it != instances_.end()) ? &(*it) : nullptr;
}

const ParticleCSSpawner::Instance *ParticleCSSpawner::Find(const ParticleCSEmitter *emitter) const
{
    if (!emitter)
    {
        return nullptr;
    }
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [emitter](const Instance &i) { return i.emitter.get() == emitter; });
    return (it != instances_.end()) ? &(*it) : nullptr;
}

std::string ParticleCSSpawner::MakeInstanceName(const std::string &templateName)
{
    return templateName + "#" + std::to_string(++instanceCounter_);
}

std::vector<std::string> ParticleCSSpawner::GetTemplateNames()
{
    std::vector<std::string> names;
    const std::filesystem::path baseDir = AssetPath::Json("ParticleCS");
    if (!std::filesystem::exists(baseDir) || !std::filesystem::is_directory(baseDir))
    {
        return names;
    }
    for (const auto &entry : std::filesystem::directory_iterator(baseDir))
    {
        if (entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void ParticleCSSpawner::DrawImGui()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("シーンへの配置##ParticleCSSpawner"))
    {
        return;
    }
    ImGui::Indent();

    ImGui::TextWrapped("json を選んでシーンに出します。エディタのエミッターと違い、"
                       "ここで出したものはゲーム画面にそのまま描画されます。");
    ImGui::Separator();

    // ---- 生成 ----
    // ディレクトリ走査は毎フレームだと重いので、開いている間だけ間隔をあけて更新する。
    static std::vector<std::string> templates;
    static int refreshCountdown = 0;
    if (templates.empty() || --refreshCountdown <= 0)
    {
        templates = GetTemplateNames();
        refreshCountdown = 60;
    }

    if (templates.empty())
    {
        ImGui::TextDisabled("Assets/jsons/ParticleCS に json がありません");
    }
    else
    {
        selectedTemplateIndex_ = std::clamp(selectedTemplateIndex_, 0, static_cast<int>(templates.size()) - 1);
        if (ImGui::BeginCombo("テンプレート##spawnTemplate", templates[selectedTemplateIndex_].c_str()))
        {
            for (int i = 0; i < static_cast<int>(templates.size()); ++i)
            {
                const bool selected = (i == selectedTemplateIndex_);
                if (ImGui::Selectable(templates[i].c_str(), selected))
                {
                    selectedTemplateIndex_ = i;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("シーンに出す##spawn"))
        {
            const std::string &templateName = templates[selectedTemplateIndex_];
            if (ParticleCSEmitter *spawned = Spawn(templateName))
            {
                selectedInstanceIndex_ = static_cast<int>(instances_.size()) - 1;
                ImGuiNotification::Post("シーンに配置しました: " + spawned->GetName(),
                                        {0.4f, 0.8f, 1.0f, 1.0f});
            }
            else
            {
                ImGuiNotification::Post("配置に失敗しました: " + templateName, {0.9f, 0.4f, 0.2f, 1.0f});
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("エディタで保存した設定をそのまま読み込んで複製します。\n"
                              "元の json は変更されません。");
    }

    ImGui::Separator();
    ImGui::Text("配置済み: %zu 件", instances_.size());

    if (instances_.empty())
    {
        ImGui::TextDisabled("まだ何も配置されていません");
        ImGui::Unindent();
        return;
    }

    // ---- 一覧 ----
    if (ImGui::BeginChild("##spawnedList", ImVec2(0, 120), true))
    {
        for (int i = 0; i < static_cast<int>(instances_.size()); ++i)
        {
            ImGui::PushID(i);
            std::string label = instances_[i].emitter->GetName();
            if (instances_[i].persistent)
            {
                label += "  [常駐]";
            }
            if (ImGui::Selectable(label.c_str(), i == selectedInstanceIndex_))
            {
                selectedInstanceIndex_ = i;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("全部消す##despawnAll"))
    {
        instances_.clear();
        selectedInstanceIndex_ = -1;
        ImGui::Unindent();
        return;
    }

    // ---- 選択中インスタンスの操作 ----
    if (selectedInstanceIndex_ < 0 || selectedInstanceIndex_ >= static_cast<int>(instances_.size()))
    {
        ImGui::Unindent();
        return;
    }

    Instance &inst = instances_[selectedInstanceIndex_];
    ParticleCSEmitter *emitter = inst.emitter.get();

    ImGui::SeparatorText(emitter->GetName().c_str());
    ImGui::Text("テンプレート: %s", inst.templateName.c_str());
    ImGui::Text("生存パーティクル: %zu", emitter->GetTotalAliveParticles());

    if (emitter->GetParentTransform())
    {
        // 親がいる間はワールド座標が毎フレーム上書きされるので、ローカル座標を編集させる
        Vector3 local = emitter->GetLocalTranslate();
        if (ImGui::DragFloat3("親からの座標##spawnLocal", &local.x, 0.1f))
        {
            emitter->SetLocalTranslate(local);
        }
        ImGui::TextDisabled("親に追従中（ワールド座標は自動計算）");
        if (ImGui::Button("親子付けを解除##spawnUnparent"))
        {
            emitter->ClearParent();
        }
    }
    else
    {
        Vector3 pos = emitter->GetTranslate();
        if (ImGui::DragFloat3("座標##spawnPos", &pos.x, 0.1f))
        {
            emitter->SetTranslate(pos);
        }
    }

    Vector3 scale = emitter->GetScale();
    if (ImGui::DragFloat3("大きさ##spawnScale", &scale.x, 0.1f))
    {
        emitter->SetScale(scale);
    }

    float frequency = emitter->GetEmitterMesh().frequency;
    if (ImGui::DragFloat("発生間隔##spawnFreq", &frequency, 0.001f, 0.001f, 10.0f, "%.4f"))
    {
        emitter->SetFrequency(frequency);
    }

    {
        bool bb = emitter->GetBillboardEmitter();
        if (ImGui::Checkbox("ビルボード（常にカメラへ正対）##spawnBillboard", &bb))
        {
            emitter->SetBillboardEmitter(bb);
        }
    }
    {
        bool persistent = inst.persistent;
        if (ImGui::Checkbox("シーンをまたいで残す##spawnPersistent", &persistent))
        {
            inst.persistent = persistent;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("オフだとシーン遷移時に自動で破棄されます");
    }

    if (ImGui::Button("消す##despawn"))
    {
        Despawn(emitter);
        selectedInstanceIndex_ = -1;
        ImGui::Unindent();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("発生を止めて消す##despawnFinish"))
    {
        DespawnWhenFinished(emitter);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("新規発生を止め、残った粒子が消えたら自動で破棄します");

    ImGui::Unindent();
#endif // USE_IMGUI
}

} // namespace Hagine
