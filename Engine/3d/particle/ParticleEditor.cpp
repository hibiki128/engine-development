#define NOMINMAX
#include "ParticleEditor.h"
#ifdef USE_IMGUI
#include <edit/play/PlayModeManager.h>
#endif
#include <asset/AssetPath.h>
#include "debug/imgui/ImGuiManager.h"
#include <utility/debug/imgui/ImGuiNotification.h>
#include "render/DrawGroupManager.h"
#ifdef USE_IMGUI
#include "browser/ShowFolder.h"
#include "ImGuizmo.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#ifdef USE_IMGUI
#include <edit/undo/ImGuiUndoTracker.h>
namespace {
// UI の編集ジェスチャを Undo 履歴へ積むトラッカー。シングルトンなので1つでよい。
// ヘッダーのメンバーにすると Undo 関連のヘッダーが 100 本以上の .cpp へ広がるので、ここに置く
Hagine::ImGuiUndoTracker g_undoTracker;
} // namespace
#endif // USE_IMGUI
namespace Hagine {
void ParticleEditor::Finalize()
{
    emitters_.clear();
    currentFrameStats_.clear();
    displayStats_.clear();
}

void ParticleEditor::Initialize()
{
    pParticleGroupManager_ = ParticleGroupManager::GetInstance();
    // カラーテーマの初期設定
    SetupColors();
}

// カラーテーマを設定する
void ParticleEditor::SetupColors()
{
#ifdef USE_IMGUI
    // 各見出しのアクセント色（エディタ共通の配色からとる）
    headerColors_[0] = DebugTheme::kAccentBlue;
    headerColors_[1] = DebugTheme::kAccentOrange;
    headerColors_[2] = DebugTheme::kAccentGreen;
    headerColors_[3] = DebugTheme::kAccentPurple;
    headerColors_[4] = DebugTheme::kAccentYellow;
    headerColors_[5] = DebugTheme::kTextDim;
#endif // USE_IMGUI
}

void ParticleEditor::AddParticleEmitter(const std::string &name, const std::string &fileName, const std::string &texturePath)
{
    // 新しい ParticleEmitter を作成
    auto emitter = std::make_unique<ParticleEmitter>();

    // 初期化処理
    emitter->Initialize(name);
    // マップに追加
    emitters_[name] = std::move(emitter);
    DrawGroupManager::GetInstance()->RegisterGroup(emitters_[name]->GetDrawGroup()); // 所属グループを登録
    ImGuiNotification::Post("パーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleEditor::Load()
{
}

void ParticleEditor::AddParticleEmitter(const std::string &name)
{
    // 新しい ParticleEmitter を作成
    auto emitter = std::make_unique<ParticleEmitter>();

    // 初期化処理
    emitter->Initialize(name);
    // マップに追加
    emitters_[name] = std::move(emitter);
    DrawGroupManager::GetInstance()->RegisterGroup(emitters_[name]->GetDrawGroup()); // 所属グループを登録
    ImGuiNotification::Post("パーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleEditor::AddParticleGroup(const std::string &name, const std::string &fileName, const std::string &texturePath)
{
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleGroup>();
    // 初期化処理
    group->Initialize();
    // パーティクルグループを作成
    group->CreateParticleGroup(name, fileName, texturePath);

    // マップに追加
    pParticleGroupManager_->AddParticleGroup(std::move(group));
    ImGuiNotification::Post("パーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleEditor::AddPrimitiveParticleGroup(const std::string &name, const std::string &texturePath, PrimitiveType type)
{
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleGroup>();
    // 初期化処理
    group->Initialize();
    // パーティクルグループを作成
    group->CreatePrimitiveParticleGroup(name, type, texturePath);

    // マップに追加
    pParticleGroupManager_->AddParticleGroup(std::move(group));
    ImGuiNotification::Post("プリミティブパーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleEditor::SetExternalParticleCount(const std::string &baseName, size_t count)
{
    // 新しいフレームが始まった場合、現在フレームの統計をクリア
    if (currentFrameNumber_ != lastUpdateFrame_)
    {
        currentFrameStats_.clear();
        lastUpdateFrame_ = currentFrameNumber_;
    }

    // 現在フレームの統計データを更新
    currentFrameStats_[baseName].count += count;
    currentFrameStats_[baseName].instanceCount++;
}

void ParticleEditor::UpdateFrameStats()
{
    // 現在フレームの統計を表示用にコピー
    displayStats_ = currentFrameStats_;

    // フレーム番号を進める
    currentFrameNumber_++;
}

void ParticleEditor::DrawAll(const ViewProjection &vp_)
{
    // 自動エミッターの発生はゲーム世界の更新なので、一時停止・停止中は進めない。
    // 描画はそのまま行う（止めた瞬間の粒が画面に残る）。
#ifdef USE_IMGUI
    const bool updateEmitters = PlayModeManager::GetInstance()->ShouldUpdateGame();
#else
    const bool updateEmitters = true;
#endif // USE_IMGUI

    for (auto &[name, emitter] : emitters_)
    {
        if (emitter)
        {
            if (updateEmitters && emitter->GetIsAuto())
            {
                emitter->Update();
            }
            emitter->Draw(vp_);
        }
    }
}

std::vector<std::string> ParticleEditor::GetEmitterNames() const
{
    std::vector<std::string> names;
    names.reserve(emitters_.size());
    for (const auto &[name, emitter] : emitters_)
    {
        names.push_back(name);
    }
    return names;
}

ParticleEmitter *ParticleEditor::GetEmitterByName(const std::string &name)
{
    auto it = emitters_.find(name);
    return (it != emitters_.end()) ? it->second.get() : nullptr;
}

void ParticleEditor::DrawSelectedForPreview(const ViewProjection &vp)
{
    if (selectedEmitterName_.empty())
    {
        return;
    }
    auto it = emitters_.find(selectedEmitterName_);
    if (it == emitters_.end() || !it->second)
    {
        return;
    }
    ParticleEmitter *emitter = it->second.get();
    // 自動発生中なら時間ベースで Emit を進める（Draw 内ではなくここで駆動）。
    if (emitter->GetIsAuto())
    {
        emitter->Update();
    }
    // ParticleManager を直接駆動して更新＋描画する（DrawEmitter のワイヤーは描かない）。
    ParticleManager *mgr = emitter->GetParticleManager();
    if (mgr)
    {
        mgr->SetEmitterCenter(emitter->GetPosition());
        mgr->Update(vp);
        mgr->Draw();
    }
}

void ParticleEditor::DebugAll(bool ownTabBar)
{
#ifdef USE_IMGUI
    // ownTabBar=false のときは呼び出し元のタブの中身として描く（タブバー・タブ項目を作らない）
    if (!ownTabBar || ImGui::BeginTabBar("CPUパーティクル"))
    {
        if (!ownTabBar || ImGui::BeginTabItem("CPUエミッター設定"))
        {
            if (emitters_.empty())
            {
                ImGui::Text("エミッターがありません");
            }
            else
            {
                // エミッター名は名前順に並べる（unordered_map のままだと並びが毎回変わって探しにくい）
                std::vector<std::string> emitterNames = GetEmitterNames();
                std::sort(emitterNames.begin(), emitterNames.end());

                // 選択が消えていたら先頭へ
                if (selectedEmitterName_.empty() || emitters_.find(selectedEmitterName_) == emitters_.end())
                {
                    selectedEmitterName_ = emitterNames.front();
                }

                // ---- 検索 ----
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##emitterSearch", ICON_FA_SEARCH " エミッターを名前で絞り込み", &emitterSearch_);
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    emitterSearch_.clear();
                }
                auto lower = [](std::string text) {
                    for (char &c : text)
                    {
                        if (c >= 'A' && c <= 'Z')
                            c = static_cast<char>(c - 'A' + 'a');
                    }
                    return text;
                };
                const std::string query = lower(emitterSearch_);

                // ---- 一覧（● = 自動発生中 / 目の斜線 = 発生範囲の枠を出していない）。高さは下端をドラッグで変えられる ----
                ImGui::BeginChild("##emitterList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 6.5f),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
                int shown = 0;
                for (size_t i = 0; i < emitterNames.size(); ++i)
                {
                    const std::string &name = emitterNames[i];
                    if (!query.empty() && lower(name).find(query) == std::string::npos)
                    {
                        continue;
                    }
                    ++shown;
                    ParticleEmitter *emitter = emitters_[name].get();
                    const bool isAuto = emitter && emitter->GetIsAuto();
                    const bool visible = !emitter || emitter->GetVisible();
                    ImGui::PushID(name.c_str());
                    ImGui::TextColored(isAuto ? DebugTheme::kAccentGreen : DebugTheme::kTextDim, isAuto ? ICON_FA_CIRCLE : ICON_FA_CIRCLE_NOTCH);
                    ImGui::SetItemTooltip(isAuto ? "自動発生中" : "自動発生していない");
                    ImGui::SameLine();
                    if (!visible)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    }
                    if (ImGui::Selectable(name.c_str(), selectedEmitterName_ == name))
                    {
                        selectedEmitterName_ = name;
                        selectedEmitterIndex_ = static_cast<int>(i);
                    }
                    if (!visible)
                    {
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        ImGui::TextDisabled(ICON_FA_EYE_SLASH);
                    }
                    ImGui::PopID();
                }
                if (shown == 0)
                {
                    ImGui::TextDisabled("一致するエミッターがありません");
                }
                ImGui::EndChild();

                // ---- 選択中のエミッターの操作バー ----
                auto it = emitters_.find(selectedEmitterName_);
                if (it != emitters_.end() && it->second)
                {
                    ParticleEmitter *emitter = it->second.get();
                    bool isAuto = emitter->GetIsAuto();
                    if (ThemedToggle("##emitterAuto", &isAuto, DebugTheme::kAccentGreen))
                    {
                        emitter->SetIsAuto(isAuto);
                    }
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(isAuto ? "自動発生 ON" : "自動発生 OFF");
                    ImGui::SameLine();
                    if (PrimaryButton(ICON_FA_BOLT " 1回出す"))
                    {
                        emitter->UpdateOnce();
                    }
                    ImGui::SetItemTooltip("自動発生を止めたまま、1回ぶんだけ発生させて形を確かめる");
                    ImGui::SameLine();
                    bool visible = emitter->GetVisible();
                    if (NeutralButton(visible ? ICON_FA_EYE " 枠を表示中" : ICON_FA_EYE_SLASH " 枠なし"))
                    {
                        emitter->SetVisible(!visible);
                    }
                    ImGui::SetItemTooltip("発生範囲の枠（白い箱）をシーンに描くか");
                    ImGui::Spacing();

                    // 選択されたエミッターの詳細
                    emitter->Debug();
                }
            }
            if (ownTabBar)
                ImGui::EndTabItem();
        }
        if (ownTabBar)
            ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}

// std::unique_ptr<ParticleEmitter> ParticleEditor::GetEmitter(const std::string &name) {
//     auto it = emitters_.find(name);
//     if (it != emitters_.end()) {
//         // マップから取り出し、所有権を呼び出し元に移動
//         return std::move(it->second);
//     }
//     return nullptr;
// }

size_t ParticleEditor::GetSceneParticleCount() const
{
    size_t total = 0;
    for (const auto &[name, stats] : displayStats_)
    {
        total += stats.count;
    }
    return total;
}

void ParticleEditor::SceneParticleCount()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("パーティクル統計(CPU)"))
    {
        size_t grandTotal = 0;
        size_t totalInstances = 0;

        // 合計を計算
        for (const auto &[name, stats] : displayStats_)
        {
            grandTotal += stats.count;
            totalInstances += stats.instanceCount;
        }

        // ヘッダー情報（くすみ色で控えめに）
        ImGui::TextColored(DebugTheme::kAccentYellow, "合計: %zu個", grandTotal);
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kTextDim, "(%zu種類)", displayStats_.size());

        if (!displayStats_.empty())
        {
            ImGui::Separator();

            // シンプルなリスト表示
            for (const auto &[name, stats] : displayStats_)
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(DebugTheme::kAccentBlue, "%s", name.c_str());
                ImGui::SameLine();
                ImGui::Text(": %zu", stats.count);

                // インスタンス数が1より多い場合のみ表示
                if (stats.instanceCount > 1)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(DebugTheme::kTextDim, "×%zu", stats.instanceCount);
                }
            }
        }
        else
        {
            ImGui::TextColored(DebugTheme::kTextDim, "エミッターなし");
        }
    }
#endif // USE_IMGUI
}

std::unique_ptr<ParticleEmitter> ParticleEditor::CreateEmitterFromTemplate(const std::string &name)
{
    auto it = emitters_.find(name);
    if (it != emitters_.end() && it->second)
    {
        return it->second->Clone(); // コピーを作って返す
    }
    return nullptr;
}

void ParticleEditor::EditorWindow()
{
#ifdef USE_IMGUI
    // エディタでの編集ジェスチャ（ウィジェット操作・ギズモドラッグ）をUndo履歴として追跡する
    g_undoTracker.Begin([this] { return CaptureUndoState(); });

    ImGui::Begin("パーティクルエディター");
    ShowImGuiEditor();
    ImGui::End();

    g_undoTracker.End(
        "パーティクル編集",
        [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { ParticleEditor::GetInstance()->RestoreUndoState(s); },
        ImGuizmo::IsUsing());
#endif // USE_IMGUI
}

// カラー付きCollapsingHeaderを表示するヘルパー関数
bool ParticleEditor::ColoredCollapsingHeader(const char *label, int colorIndex)
{
#ifdef USE_IMGUI
    return ThemedHeader(label, headerColors_[colorIndex % 6]);
#else
    (void)label;
    (void)colorIndex;
    return false;
#endif // USE_IMGUI
}

void ParticleEditor::ShowImGuiEditor(bool ownTabBar)
{
#ifdef USE_IMGUI
    // ownTabBar=false のときはタブ項目だけを出す（呼び出し元のタブバーに並べる）
    if (!ownTabBar || ImGui::BeginTabBar("CPUパーティクル"))
    {
        if (ImGui::BeginTabItem(ICON_FA_PLUS " 作成"))
        {
            DrawQuickCreate();

            // グループ（粒の形）を1つずつ指定して作る従来の画面
            ImGui::Spacing();
            if (ImGui::CollapsingHeader(ICON_FA_SLIDERS_H " 詳しく作る（グループを指定）"))
            {
                // エミッター追加のCollapsingHeader
                if (ColoredCollapsingHeader("エミッター追加", 0))
                {
                    // 名前の入力
                    char nameBuffer[256];
                    strcpy_s(nameBuffer, sizeof(nameBuffer), localEmitterName_.c_str());
                    ImGui::Text("エミッターの名前");
                    if (ImGui::InputText(" ", nameBuffer, sizeof(nameBuffer)))
                    {
                        localEmitterName_ = std::string(nameBuffer);
                    }

                    // エミッター作成ボタン
                    ImGui::Spacing();
                    if (!localEmitterName_.empty())
                    {
                        if (ImGui::Button("エミッター生成"))
                        {
                            AddParticleEmitter(localEmitterName_);
                            localEmitterName_.clear();
                        }
                    }
                }

                // パーティクルグループ作成のCollapsingHeader
                if (ColoredCollapsingHeader("パーティクルグループ作成", 1))
                {
                    // 名前の入力
                    char nameBuffer[256];
                    strcpy_s(nameBuffer, sizeof(nameBuffer), localName_.c_str());
                    ImGui::Text("パーティクルグループの名前");
                    if (ImGui::InputText("  ", nameBuffer, sizeof(nameBuffer)))
                    {
                        localName_ = std::string(nameBuffer);
                    }

                    // パーティクルタイプ選択（ラジオボタン）
                    ImGui::Spacing();
                    ImGui::Text("パーティクルタイプ選択");

                    static int selectedType = 0; // 0: モデル, 1: プリミティブ
                    ImGui::RadioButton("モデルパーティクル", &selectedType, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("プリミティブモデル", &selectedType, 1);
                    ImGui::Separator();

                    // モデルパーティクル選択時
                    if (selectedType == 0)
                    {
                        // モデル選択セクション (青色)
                        if (ColoredCollapsingHeader("モデル選択", 2))
                        {
                            // モデルファイル選択
                            // models はエンジン(debug)とアプリの 2 ルートに分割。ラジオで切り替える。
                            static const std::vector<std::string> kRootsObj = AssetPath::ModelScanRoots(); // [0]=エンジン, [1]=アプリ
                            static int rootSelObj = 1; // 既定: App
                            static std::filesystem::path currentDirObj = kRootsObj[rootSelObj];
                            static std::string selectedFolderObj = "";
                            static std::string selectedFileObj = "";

                            for (int i = 0; i < 2; ++i)
                            {
                                if (i > 0)
                                    ImGui::SameLine();
                                if (ImGui::RadioButton(i == 0 ? "Engine(debug)##objr" : "App##objr", rootSelObj == i))
                                {
                                    rootSelObj = i;
                                    currentDirObj = kRootsObj[rootSelObj];
                                    selectedFolderObj = selectedFileObj = "";
                                }
                            }
                            const std::filesystem::path baseDirObj = kRootsObj[rootSelObj];

                            // 「戻る」ボタン（上の階層に戻る）
                            if (currentDirObj != baseDirObj)
                            {
                                if (ImGui::Button("< 戻る(Model)"))
                                {
                                    currentDirObj = currentDirObj.parent_path();
                                    selectedFolderObj = "";
                                    selectedFileObj = "";
                                }
                            }

                            // フォルダ一覧
                            std::vector<std::string> foldersObj;
                            std::vector<std::string> objFiles;

                            for (const auto &entry : std::filesystem::directory_iterator(currentDirObj))
                            {
                                if (entry.is_directory())
                                {
                                    foldersObj.push_back(entry.path().filename().string());
                                }
                                else if (entry.path().extension() == ".obj")
                                {
                                    objFiles.push_back(entry.path().filename().string());
                                }
                            }

                            // フォルダ選択 (クリックで移動)
                            if (!foldersObj.empty())
                            {
                                ImGui::Text("フォルダ");
                                ImGui::Separator();
                                for (const auto &folder : foldersObj)
                                {
                                    std::string folderNameTex = folder + " (Model)"; // フォルダ名に "(Model)" を追加
                                    if (ImGui::Selectable(folderNameTex.c_str(), selectedFolderObj == folder))
                                    {
                                        selectedFolderObj = folderNameTex;
                                        currentDirObj = currentDirObj / folder; // フォルダ移動
                                        selectedFileObj = "";                   // 新しいフォルダを開いたらファイル選択をリセット
                                    }
                                    ImGui::Separator();
                                }
                            }

                            // `.obj` ファイル選択
                            if (!objFiles.empty())
                            {
                                ImGui::Text("モデルファイル:");
                                if (ImGui::BeginCombo("ファイル選択", selectedFileObj.empty() ? "なし" : selectedFileObj.c_str()))
                                {
                                    for (const auto &file : objFiles)
                                    {
                                        bool isSelected = (file == selectedFileObj);
                                        if (ImGui::Selectable(file.c_str(), isSelected))
                                        {
                                            selectedFileObj = file;

                                            // `baseDirObj` からの相対パスを取得
                                            std::filesystem::path relativePath = (currentDirObj / file).lexically_relative(baseDirObj);

                                            // Windowsのバックスラッシュをスラッシュに変換
                                            std::string pathStr = relativePath.string();
                                            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                            // `fileNameObj_` に保存
                                            localFileObj_ = pathStr;
                                        }
                                        if (isSelected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                        }

                        // テクスチャ選択セクション (緑色)
                        if (ColoredCollapsingHeader("テクスチャ選択", 3))
                        {
    #ifdef USE_IMGUI
                            ShowTextureFile(localTexturePath_);
    #endif // USE_IMGUI
                        }

                        // パーティクルグループ作成ボタン
                        ImGui::Spacing();
                        if (!localName_.empty() && !localFileObj_.empty())
                        {
                            if (ImGui::Button("モデルパーティクルグループ生成"))
                            {
                                AddParticleGroup(localName_, localFileObj_, localTexturePath_);
                                localName_.clear();
                                localFileObj_.clear();
                                localTexturePath_.clear(); // テクスチャのパスもクリア
                            }
                        }
                    }
                    // プリミティブモデル選択時
                    else if (selectedType == 1)
                    {
                        // プリミティブタイプ選択セクション (紫色)
                        if (ColoredCollapsingHeader("プリミティブタイプ選択", 4))
                        {
                            const char *primitiveType[] = {"未選択", "プレーン", "球", "キューブ", "シリンダー", "リング", "三角形", "円錐", "四角錐"};
                            int currentPrimitiveType = static_cast<int>(localType_);
                            // 初期値が未選択（None = -1）の場合に対応するため +1 して選択肢に表示
                            if (ImGui::Combo("タイプ選択", &currentPrimitiveType, primitiveType, IM_ARRAYSIZE(primitiveType)))
                            {
                                localType_ = static_cast<PrimitiveType>(currentPrimitiveType);
                            }
                        }

                        // テクスチャ選択セクション (オレンジ色)
                        if (ColoredCollapsingHeader("テクスチャ選択", 5))
                        {
    #ifdef USE_IMGUI
                            ShowTextureFile(localTexturePath_);
    #endif // USE_IMGUI
                        }

                        // パーティクルグループ作成ボタン
                        ImGui::Spacing();
                        if (!localName_.empty())
                        {
                            // localType_ が None（未選択）のときはボタンを無効化
                            bool isTypeInvalid = (localType_ == PrimitiveType::None);
                            if (isTypeInvalid)
                            {
                                ImGui::BeginDisabled();
                            }

                            if (ImGui::Button("プリミティブパーティクルグループ生成"))
                            {
                                AddPrimitiveParticleGroup(localName_, localTexturePath_, localType_);
                                localName_.clear();
                                localTexturePath_.clear();        // テクスチャのパスもクリア
                                localType_ = PrimitiveType::None; // 初期化
                            }

                            if (isTypeInvalid)
                            {
                                ImGui::EndDisabled();
                            }
                        }
                    }
                }

            } // 詳しく作る

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_TRASH " 削除"))
        {
            DrawDeleteTab();
            ImGui::EndTabItem();
        }
        if (ownTabBar)
            ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}


#ifdef USE_IMGUI
// -------------------------------------------------------
// Undo/Redo 用の状態キャプチャ・復元
// -------------------------------------------------------

nlohmann::json ParticleEditor::CaptureUndoState()
{
    nlohmann::json state = nlohmann::json::object();
    for (const auto &[name, emitter] : emitters_)
    {
        if (emitter)
        {
            state[name] = emitter->CaptureUndoState();
        }
    }
    return state;
}

void ParticleEditor::RestoreUndoState(const nlohmann::json &state)
{
    if (!state.is_object())
    {
        return;
    }
    for (auto it = state.begin(); it != state.end(); ++it)
    {
        // エミッターの追加・削除自体は対象外（既存エミッターへの反映のみ）
        if (it.value().is_null())
        {
            continue;
        }
        ParticleEmitter *emitter = GetEmitterByName(it.key());
        if (emitter)
        {
            emitter->RestoreUndoState(it.value());
        }
    }
}
#endif // USE_IMGUI
} // namespace Hagine
