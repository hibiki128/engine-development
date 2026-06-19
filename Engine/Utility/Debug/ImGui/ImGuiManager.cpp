#include "ImGuiManager.h"
#ifdef _DEBUG
#include "Collider/CollisionManager.h"
#include "Engine/2d/Text/TextRenderer.h"
#include "Engine/OffScreen/OffScreen.h"
#include "ImGuiNotification.h"
#include "ImGuizmo.h"
#include "ImGuizmoManager.h"
#include "Object/Base/BaseObject.h"
#include "Scene/SceneManager.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include <Data/DataHandler.h>
#include <Engine/Frame/Frame.h>
#include <Line/DrawLine3D.h>
#include <Particle/CSParticle/ParticleCSFieldManager.h>
#include <Engine/Render/DrawSystem.h>
#include <Shadow/ShadowMap.h>
#include <externals/icon/IconsFontAwesome5.h>
#include <imgui_impl_dx12.h>
#include <implot.h>
#endif // _DEBUG

namespace Hagine {
#ifdef _DEBUG
void ImGuiManager::Initialize(WinApp *winApp, ImGuizmoManager *imguizmoManager) {

    dxCommon_ = DirectXCommon::GetInstance();
    baseObjectManager_ = BaseObjectManager::GetInstance();
    spriteManager_ = SpriteManager::GetInstance();
    audio_ = Audio::GetInstance();
    LoadFlag();
    // ImGuiのコンテキストを生成
    ImGui::CreateContext();
    ImPlot::CreateContext();

    // Docking機能を有効化
    ImGuiIO &io = ImGui::GetIO();
    // 高度な機能を有効化
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // ドッキング機能
    io.ConfigWindowsResizeFromEdges = true;           // エッジからリサイズ
    io.ConfigWindowsMoveFromTitleBarOnly = true;      // タイトルバーからの移動

    // パフォーマンス関連の設定
    io.ConfigMemoryCompactTimer = 300.0f; // メモリ圧縮の間隔を長く
    io.IniFilename = nullptr;             // 設定ファイルの保存場所
                                          // 設定ファイルの保存場所
    LoadLayoutForCurrentMode();

    io.Fonts->Clear(); // 既存のフォントをクリア

    float fontSize = 16.0f;

    io.Fonts->AddFontFromFileTTF("resources/fonts/PixelMplus12-Regular.ttf", 14.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());

    // アイコンフォント読み込み（FontAwesomeなど）
    // FontAwesomeの設定
    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = fontSize;
    io.Fonts->AddFontFromFileTTF("resources/fonts/fa-solid-900.ttf", fontSize, &icons_config, icon_ranges);

    // フォントの生成
    unsigned char *tex_pixels = nullptr;
    int tex_width, tex_height;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_width, &tex_height);

    // カスタムテーマを設定
    SetupTheme();

    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // srvインデックスを割り
    srvManager_ = SrvManager::GetInstance();

    uint32_t srvIndex = srvManager_->Allocate();
    ImGui_ImplDX12_Init(
        dxCommon_->GetDevice().Get(),
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvManager_->GetDescriptorHeap(),
        srvManager_->GetCPUDescriptorHandle(srvIndex),
        srvManager_->GetGPUDescriptorHandle(srvIndex));
    imGuizmoManager_ = imguizmoManager;
}

void ImGuiManager::SetupTheme() {
    // シックなモノクローム調ダークテーマ
    // 無彩色のグラファイトを基調とし、選択・操作中のみ淡いスチールブルーで強調する
    ImGuiStyle &style = ImGui::GetStyle();

    // アクセントカラー（淡いスチールブルー）。色味はこの3段階に統一する
    const ImVec4 accentDim = ImVec4(0.369f, 0.471f, 0.580f, 1.00f);    // 通常
    const ImVec4 accent = ImVec4(0.435f, 0.541f, 0.659f, 1.00f);       // ホバー
    const ImVec4 accentBright = ImVec4(0.533f, 0.635f, 0.745f, 1.00f); // アクティブ

    // カラースキーム
    ImVec4 *colors = style.Colors;

    // テキスト（純白を避けたやわらかいオフホワイト）
    colors[ImGuiCol_Text] = ImVec4(0.878f, 0.882f, 0.890f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.416f, 0.424f, 0.439f, 1.00f);

    // 背景・枠
    colors[ImGuiCol_WindowBg] = ImVec4(0.090f, 0.094f, 0.102f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.106f, 0.110f, 0.122f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.078f, 0.086f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.227f, 0.235f, 0.251f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // 入力フィールド
    colors[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.149f, 0.165f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.188f, 0.192f, 0.204f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.227f, 0.235f, 0.251f, 1.00f);

    // タイトルバー・メニューバー
    colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.078f, 0.086f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.122f, 0.125f, 0.137f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.075f, 0.078f, 0.086f, 0.80f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.102f, 0.106f, 0.114f, 1.00f);

    // スクロールバー（無彩色のグレー）
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.075f, 0.078f, 0.086f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.227f, 0.235f, 0.251f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.290f, 0.302f, 0.322f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.353f, 0.369f, 0.392f, 1.00f);

    // チェック・スライダー（アクセント）
    colors[ImGuiCol_CheckMark] = accentBright;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentBright;

    // ボタン（無彩色ベース、ホバー時のみ淡くスチールへ寄せる）
    colors[ImGuiCol_Button] = ImVec4(0.180f, 0.188f, 0.204f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.243f, 0.306f, 0.369f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.204f, 0.255f, 0.310f, 1.00f);

    // ヘッダー（CollapsingHeader / Selectable など）
    colors[ImGuiCol_Header] = ImVec4(0.165f, 0.173f, 0.188f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.173f, 0.216f, 0.259f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.204f, 0.255f, 0.310f, 1.00f);

    // セパレータ
    colors[ImGuiCol_Separator] = ImVec4(0.227f, 0.235f, 0.251f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_SeparatorActive] = accentBright;

    // リサイズグリップ
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.227f, 0.235f, 0.251f, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.90f);

    // タブ
    colors[ImGuiCol_Tab] = ImVec4(0.102f, 0.106f, 0.114f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    colors[ImGuiCol_TabActive] = ImVec4(0.173f, 0.216f, 0.259f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.090f, 0.094f, 0.102f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.122f, 0.125f, 0.137f, 1.00f);

    // ドッキング
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.075f, 0.078f, 0.086f, 1.00f);

    // プロット（アクセントに統一）
    colors[ImGuiCol_PlotLines] = accentBright;
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.643f, 0.737f, 0.847f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentBright;

    // 選択範囲・ドラッグ＆ドロップ・ナビゲーション
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.90f);
    colors[ImGuiCol_NavHighlight] = accentBright;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.050f, 0.050f, 0.060f, 0.60f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.020f, 0.020f, 0.030f, 0.65f);

    // スタイル設定
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 6);
    style.CellPadding = ImVec2(6, 3);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 20;
    style.ScrollbarSize = 14;
    style.GrabMinSize = 12;

    // 外観
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.TabBorderSize = 0;

    // 丸み
    style.WindowRounding = 6;
    style.ChildRounding = 6;
    style.FrameRounding = 4;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 4;
    style.TabRounding = 4;

    // Viewportsの設定（マルチウィンドウモード）
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void ImGuiManager::CreateDescriptorHeap() {
    HRESULT result;

    // デスクリプタヒープ設定
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // デスクリプタヒープ生成
    result = dxCommon_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));

    ImGui_ImplDX12_Init(
        dxCommon_->GetDevice().Get(),
        static_cast<int>(dxCommon_->GetBackBufferCount()),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvHeap_.Get(),
        srvHeap_->GetCPUDescriptorHandleForHeapStart(),
        srvHeap_->GetGPUDescriptorHandleForHeapStart());
}

void ImGuiManager::Finalize() {
    SaveCurrentLayout();
// 後始末
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
#endif // USE_IMGUI

    // デスクリプタヒープを解放
    srvHeap_.Reset();

    SaveFlag();
}

void ImGuiManager::Begin() {
    // ImGuiフレーム開始
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::End() {
    ImGuiNotification::Draw();
    // 描画前準備
    ImGui::Render();
}

void ImGuiManager::Draw() {
    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList().Get();

    //// デスクリプタヒープの配列をセットするコマンド
    // ID3D12DescriptorHeap *ppHeaps[] = {srvHeap_.Get()};
    // commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    //  描画コマンドを発行
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiManager::UpdateIni() {
    if (showGrid_) {
        DrawLine3D::GetInstance()->DrawGrid(gridY_, gridDivision_, gridSize_, gridColor_);
    }
    if (!isShowMainUI_) {
        SwitchToGameMode();
    } else {
        SwitchToEditorMode();
    }
}

void ImGuiManager::ShowMainMenu() {
    // メインメニューバー作成
    if (ImGui::BeginMainMenuBar()) {
        // ファイルメニュー
        if (ImGui::BeginMenu(ICON_FA_FILE " ファイル")) {
            // シーン管理セクション
            if (ImGui::MenuItem(ICON_FA_DOWNLOAD " シーン保存", "Ctrl+Shift+S")) {
                // BaseObjectManagerのシーン保存モーダルを開く
                baseObjectManager_->OpenSceneSaveModal();
            }
            if (ImGui::MenuItem(ICON_FA_UPLOAD " シーン読み込み", "Ctrl+Shift+L")) {
                // BaseObjectManagerのシーン読み込みモーダルを開く
                baseObjectManager_->OpenSceneLoadModal();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " 終了", "Alt+F4")) {
                // アプリケーション終了処理
                WinApp::GetInstance()->ClosedWindow(); // 終了メッセージ送信
            }
            ImGui::EndMenu();
        }

        // 編集メニュー
        if (ImGui::BeginMenu(ICON_FA_EDIT " 編集")) {
            if (ImGui::MenuItem(ICON_FA_UNDO " 元に戻す", "Ctrl+Z", false, false)) {
            }
            if (ImGui::MenuItem(ICON_FA_REDO " やり直し", "Ctrl+Y", false, false)) {
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CUT " 切り取り", "Ctrl+X")) {
            }
            if (ImGui::MenuItem(ICON_FA_COPY " コピー", "Ctrl+C")) {
            }
            if (ImGui::MenuItem(ICON_FA_PASTE " 貼り付け", "Ctrl+V")) {
            }
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除", "Delete")) {
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_COG " 環境設定", "Ctrl+,")) {
            }
            ImGui::EndMenu();
        }

        // 表示メニュー
        if (ImGui::BeginMenu(ICON_FA_EYE " 表示")) {
            // ウィンドウ表示設定（カテゴリ別にまとめて見やすくする）
            if (ImGui::BeginMenu(ICON_FA_WINDOW_MAXIMIZE " ウィンドウ")) {
                // チェック付きのトグル行。クリックしてもメニューは閉じない
                auto windowToggle = [](const char *label, bool &flag) {
                    if (ImGui::Selectable(label, flag, ImGuiSelectableFlags_DontClosePopups))
                        flag = !flag;
                };

                ImGui::SeparatorText("シーン・オブジェクト");
                windowToggle(ICON_FA_BOOK_OPEN " シーンビュー", showSceneView_);
                windowToggle(ICON_FA_CUBE " オブジェクトビュー", showObjectView_);
                windowToggle(ICON_FA_PROJECT_DIAGRAM " オブジェクトマネージャ", showHierarchyView_);
                windowToggle(ICON_FA_ARROWS_ALT " ギズモ", showGizmoView_);

                ImGui::SeparatorText("アセット・エディタ");
                windowToggle(ICON_FA_SQUARE " スプライトマネージャ", showSpriteManagerView_);
                windowToggle(ICON_FA_SHAPES " コライダー", showColliderTagManagerView_);
                windowToggle(ICON_FA_BULLHORN " オーディオ", showAudioManagerView_);
                windowToggle(ICON_FA_CODE_BRANCH " モーションエディター", showMotionEditorView_);

                ImGui::SeparatorText("パーティクル");
                windowToggle(ICON_FA_STAR " パーティクルビュー", showParticleView_);
                windowToggle(ICON_FA_IMAGE " パーティクルプレビュー", showParticlePreviewView_);

                ImGui::SeparatorText("レンダリング");
                windowToggle(ICON_FA_STAR_OF_DAVID " オフスクリーン", showOfScreenView_);
                windowToggle(ICON_FA_LIGHTBULB " ライト", showLightView_);
                windowToggle(ICON_FA_ADJUST " シャドウマップ", showShadowMapView_);
                windowToggle(ICON_FA_LAYER_GROUP " 描画システム", showDrawSystemView_);

                ImGui::SeparatorText("統計・デバッグ");
                windowToggle(ICON_FA_DATABASE " FPS統計", showFPSView_);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(ICON_FA_BORDER_ALL " グリッド設定")) {
                // グリッド表示のON/OFFチェックボックス
                ImGui::MenuItem(ICON_FA_BORDER_ALL " グリッド表示", nullptr, &showGrid_);

                if (showGrid_) {
                    ImGui::Separator();

                    // Y座標設定
                    ImGui::PushItemWidth(120.0f);
                    if (ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " Y座標", &gridY_, 0.1f, -100.0f, 100.0f, "%.1f")) {
                    }

                    // 分割数設定
                    if (ImGui::DragInt(ICON_FA_TH " 分割数", &gridDivision_, 1, 1, 100)) {
                    }

                    // サイズ設定
                    if (ImGui::DragFloat(ICON_FA_EXPAND_ARROWS_ALT " サイズ", &gridSize_, 0.1f, 0.1f, 500.0f, "%.1f")) {
                    }
                    ImGui::PopItemWidth();

                    // 色設定
                    ImGui::ColorEdit4(ICON_FA_PALETTE " グリッド色", &gridColor_.x, ImGuiColorEditFlags_NoInputs);

                    // プリセット（サブメニュー）
                    if (ImGui::BeginMenu(ICON_FA_SWATCHBOOK " プリセット")) {
                        if (ImGui::MenuItem("デフォルト (グレー)")) {
                            gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f};
                        }
                        if (ImGui::MenuItem("白")) {
                            gridColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
                        }
                        if (ImGui::MenuItem("青")) {
                            gridColor_ = {0.3f, 0.5f, 1.0f, 1.0f};
                        }
                        if (ImGui::MenuItem("緑")) {
                            gridColor_ = {0.3f, 1.0f, 0.5f, 1.0f};
                        }
                        ImGui::EndMenu();
                    }

                    // リセットボタン
                    if (ImGui::Button(ICON_FA_UNDO " リセット")) {
                        gridY_ = 0.0f;
                        gridDivision_ = 10;
                        gridSize_ = 1.0f;
                        gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f};
                    }
                }

                ImGui::EndMenu();
            }
            ImGui::Separator();

            // 表示モード切替
            ImGui::Separator();
            if (isShowMainUI_) {
                if (ImGui::MenuItem(ICON_FA_GAMEPAD " ゲームモードに切替", "F5")) {
                    isShowMainUI_ = false;
                    SwitchToGameMode();
                }
            } else {
                if (ImGui::MenuItem(ICON_FA_WRENCH " エディターモードに切替", "F5")) {
                    isShowMainUI_ = true;
                    SwitchToEditorMode();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_EXPAND " フルスクリーン切替", "F11")) {
                WinApp::GetInstance()->ToggleFullScreen();
            }
            ImGui::EndMenu();
        }

        // オブジェクトメニュー
        if (ImGui::BeginMenu(ICON_FA_CUBE " オブジェクト")) {
            if (ImGui::MenuItem(ICON_FA_PLUS " 新規オブジェクト", "Ctrl+Shift+N")) {
                // 新規オブジェクト作成
                baseObjectManager_->OpenObjectCreationModal();
            }

            if (ImGui::MenuItem(ICON_FA_PLUS " オブジェクト読み込み", "Ctrl+Shift+M")) {
                // 新規オブジェクト作成
                baseObjectManager_->OpenObjectLoadModal();
            }

            // 3Dオブジェクト
            if (ImGui::BeginMenu(ICON_FA_CUBE " 3Dオブジェクト")) {
                if (ImGui::MenuItem(ICON_FA_CUBE " キューブ")) {
                    std::string name = "cube_" + std::to_string(++cubeCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "cube_" + std::to_string(++cubeCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Cube);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_CIRCLE " 球体")) {
                    std::string name = "sphere_" + std::to_string(++sphereCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "sphere_" + std::to_string(++sphereCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Sphere);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_CUBE " 平面")) {
                    std::string name = "plane_" + std::to_string(++planeCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "plane_" + std::to_string(++planeCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Plane);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_CIRCLE " シリンダー")) {
                    std::string name = "cylinder_" + std::to_string(++cylinderCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "cylinder_" + std::to_string(++cylinderCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Cylinder);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_RING " リング")) {
                    std::string name = "ring_" + std::to_string(++ringCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "ring_" + std::to_string(++ringCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Ring);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_CARET_UP " 三角形")) {
                    std::string name = "triangle_" + std::to_string(++triangleCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "triangle_" + std::to_string(++triangleCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Triangle);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_MOUNTAIN " ピラミッド")) {
                    std::string name = "pyramid_" + std::to_string(++pyramidCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "pyramid_" + std::to_string(++pyramidCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Pyramid);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_CHART_AREA " 円柱")) {
                    std::string name = "cone_" + std::to_string(++coneCount_);
                    if (BaseObjectManager::GetInstance()->GetObjectByName(name)) {
                        name = "cone_" + std::to_string(++coneCount_);
                    }
                    std::unique_ptr<BaseObject> object = std::make_unique<BaseObject>();
                    object->SetPrimitive(true);
                    object->Init(name);
                    object->CreatePrimitiveModel(PrimitiveType::Cone);
                    baseObjectManager_->AddObject(std::move(object));
                }

                if (ImGui::MenuItem(ICON_FA_TRASH_ALT " オブジェクト全削除")) {
                    baseObjectManager_->RemoveAllObjects();
                    imGuizmoManager_->DeleteTarget();
                    ImGuiNotification::Post("全オブジェクトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
                }

                ImGui::EndMenu();
            }

            // 2Dオブジェクト
            if (ImGui::BeginMenu(ICON_FA_SQUARE " 2Dオブジェクト")) {
                if (ImGui::MenuItem(ICON_FA_SQUARE " スプライト")) {
                    spriteManager_->ShowSpriteCreationModal();
                }
                if (ImGui::MenuItem(ICON_FA_FONT " テキスト")) {
                }
                if (ImGui::MenuItem(ICON_FA_IMAGE " 画像")) {
                }
                ImGui::EndMenu();
            }

            // エフェクト
            if (ImGui::BeginMenu(ICON_FA_MAGIC " エフェクト")) {
                if (ImGui::MenuItem(ICON_FA_SNOWFLAKE " パーティクルシステム")) {
                }
                if (ImGui::MenuItem(ICON_FA_LIGHTBULB " ライト")) {
                }
                if (ImGui::MenuItem(ICON_FA_VIDEO " カメラ")) {
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        // ヘルプメニュー
        if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " ヘルプ")) {
            if (ImGui::MenuItem(ICON_FA_KEYBOARD " ショートカット一覧", "F1")) {
                showShortcutWindow_ = !showShortcutWindow_;
            }
            if (ImGui::MenuItem(ICON_FA_INFO_CIRCLE " バージョン情報")) {
            }
            ImGui::EndMenu();
        }

        // シーンメニュー
        if (ImGui::BeginMenu(ICON_FA_GLOBE " シーン選択")) { // 地球アイコン（意味：全体メニュー）

            if (ImGui::MenuItem(ICON_FA_HOME " タイトルシーン", "Ctrl+1")) { // home アイコン
                SceneManager::GetInstance()->NextSceneReservation("TITLE");
                ImGuiNotification::Post("タイトルシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }
            if (ImGui::MenuItem(ICON_FA_BARS " セレクトシーン", "Ctrl+2")) { // bars アイコン（メニュー選択感）
                SceneManager::GetInstance()->NextSceneReservation("SELECT");
                ImGuiNotification::Post("セレクトシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }
            if (ImGui::MenuItem(ICON_FA_GAMEPAD " ゲームシーン", "Ctrl+3")) { // gamepad アイコン
                SceneManager::GetInstance()->NextSceneReservation("GAME");
                ImGuiNotification::Post("ゲームシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }
            if (ImGui::MenuItem(ICON_FA_TROPHY " クリアシーン", "Ctrl+4")) { // trophy アイコン
                SceneManager::GetInstance()->NextSceneReservation("CLEAR");
                ImGuiNotification::Post("クリアシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }
            if (ImGui::MenuItem(ICON_FA_FILM " デモシーン", "Ctrl+5")) { // film アイコン
                SceneManager::GetInstance()->NextSceneReservation("DEMO");
                ImGuiNotification::Post("デモシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }
            if (ImGui::MenuItem(ICON_FA_BOOK_OPEN " チュートリアルシーン", "Ctrl+6")) {
                SceneManager::GetInstance()->NextSceneReservation("TUTORIAL");
                ImGuiNotification::Post("チュートリアルシーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void ImGuiManager::ShowSceneSettingWindow() {
    if (!showSceneView_)
        return; // 表示しない場合は早期リターン

    // 出現時にフォーカスを奪わない軽量フラグ
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    

    ImGui::Begin("シーン設定", &showSceneView_, flags);

    currentScene_->AddSceneSetting();

    ImGui::End();
}

void ImGuiManager::ShowObjectSettingWindow() {
    if (!showObjectView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    

    ImGui::Begin("オブジェクト設定", &showObjectView_, flags);

    currentScene_->AddObjectSetting();
    // baseObjectManager_->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowParticleSettingWindow() {
    if (!showParticleView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    

    ImGui::Begin("パーティクル設定", &showParticleView_, flags);

    currentScene_->AddParticleSetting();

    ParticleCSFieldManager::GetInstance()->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowParticlePreviewWindow() {
    // 表示メニューの「パーティクルプレビュー」でON/OFF。ウィンドウのXボタンとフラグを同期させる。
    if (!showParticlePreviewView_)
        return;
    ParticleCSEditor::GetInstance()->ShowPreviewWindow(&showParticlePreviewView_);
}

void ImGuiManager::ShowStatisticsWindow() {
    if (!showFPSView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    

    ImGui::Begin("統計", &showFPSView_, flags);

    DisplayFPS();

    ParticleEditor::GetInstance()->SceneParticleCount();

    ParticleCSEditor::GetInstance()->ShowGPUParticleStatistics();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("ログ履歴", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("履歴をクリア")) {
            ImGuiNotification::ClearHistory();
        }
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);
        const auto &history = ImGuiNotification::GetHistory();
        for (const auto &n : history) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(n.color.x, n.color.y, n.color.z, n.color.w));
            ImGui::TextUnformatted(n.message.c_str());
            ImGui::PopStyleColor();
        }
        // 新しいログがあれば自動スクロール
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

void ImGuiManager::ShowOffScreenSettingWindow(OffScreen *offscreen) {
    if (!showOfScreenView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オフスクリーン設定", &showOfScreenView_, flags);

    offscreen->Setting();

    ImGui::End();
}

void ImGuiManager::ShowLightSettingWindow() {
    if (!showLightView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("ライト設定", &showLightView_, flags);

    LightGroup::GetInstance()->imgui();

    ImGui::End();
}

void ImGuiManager::ShowGizmoWindow() {
    if (!showGizmoView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("トランスフォームマネージャ", &showGizmoView_, flags);

    imGuizmoManager_->imgui();

    ImGui::End();
}

void ImGuiManager::ShowHierarchyWindow() {
    if (!showHierarchyView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オブジェクトマネージャ", &showHierarchyView_, flags);

    baseObjectManager_->DrawHierarchyEditor();

    ImGui::End();
}

void ImGuiManager::ShowMotionEditorWindow() {
    if (!showMotionEditorView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("モーションエディター", &showMotionEditorView_, flags);

    // 中身はアプリ側が差し込んだコールバックで描画（エンジンは MotionEditor を知らない）
    if (motionEditorDrawCallback_) {
        motionEditorDrawCallback_();
    }

    ImGui::End();
}

void ImGuiManager::ShowSpriteManagerWindow() {
    if (!showSpriteManagerView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("スプライトマネージャ", &showSpriteManagerView_, flags);

    spriteManager_->DrawSpriteManager();

    ImGui::End();

    TextRenderer::GetInstance()->UpdateImGui();
}

void ImGuiManager::ShowColliderTagManagerWindow() {
    if (!showColliderTagManagerView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("コライダー", &showColliderTagManagerView_, flags);

    if (ImGui::BeginTabBar("##ColliderTabs")) {
        // コライダーの選択・サイズ調整・デバッグ表示の切り替え
        if (ImGui::BeginTabItem("コライダー設定")) {
            CollisionManager::GetInstance()->ImGuiColliderInspector();
            ImGui::EndTabItem();
        }
        // タグの追加・削除
        if (ImGui::BeginTabItem("タグ管理")) {
            CollisionManager::GetInstance()->ImGuiTagManager();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ImGuiManager::ShowAudioManagerWindow() {
    if (!showAudioManagerView_)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オーディオ", &showAudioManagerView_, flags);

    audio_->Debug();

    ImGui::End();
}

void ImGuiManager::ShowShadowMapWindow() {
    if (!showShadowMapView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは ShadowMap 側に委譲する
    ShadowMap::GetInstance()->UpdateImGui(&showShadowMapView_);
}

void ImGuiManager::ShowDrawSystemWindow() {
    if (!showDrawSystemView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは DrawSystem 側に委譲する
    DrawSystem::GetInstance()->UpdateImGui(&showDrawSystemView_);
}

void ImGuiManager::FixAspectRatio() {

    // 横幅ベースで16:9に合わせた高さ
    float adjustedHeight = (sceneTextureSize_.x * 9.0f / 16.0f);
    // 高さベースで16:9に合わせた横幅
    float adjustedWidth = (sceneTextureSize_.y * 16.0f / 9.0f);

    // 元のサイズとの差を計算
    float deltaFromWidth = std::abs(adjustedHeight - sceneTextureSize_.y);
    float deltaFromHeight = std::abs(adjustedWidth - sceneTextureSize_.x);

    // 近い方を採用
    if (deltaFromWidth < deltaFromHeight) {
        sceneTextureSize_.y = adjustedHeight;
    } else {
        sceneTextureSize_.x = adjustedWidth;
    }
}

void ImGuiManager::ShowSceneWindow(OffScreen *offScreen, const std::string &sceneName) {
    // ImGuiウィンドウ開始前にNextWindowSizeは設定しない（手動サイズ変更を許可）
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    // フォーカスされていない場合は描画を最適化
    if (!isShowMainUI_) {
        flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    }
    ImGui::Begin("Scene", nullptr, flags);
    sceneName;
    // ウィンドウ内の位置を取得（ImGuizmoのためにシーンウィンドウの絶対位置を計算）
    ImVec2 sceneWindowPos = ImGui::GetWindowPos();
    ImVec2 contentPos = ImGui::GetCursorScreenPos();

    // 以下は既存の処理を最適化
    // キャッシュされた値を使用し、毎フレーム計算しないようにする
    static ImVec2 lastContentRegion = ImVec2(0, 0);
    static ImVec2 lastSceneTextureSize = ImVec2(0, 0);

    // ウィンドウがリサイズされたか、フォーカスがあるときのみ再計算
    ImVec2 contentRegion = ImGui::GetContentRegionAvail();
    if (contentRegion.x != lastContentRegion.x ||
        contentRegion.y != lastContentRegion.y ||
        ImGui::IsWindowFocused()) {
        lastContentRegion = contentRegion;

        // 横幅ベースで16:9にしたときの高さ
        float adjustedHeight = contentRegion.x * 9.0f / 16.0f;
        // 高さベースで16:9にしたときの横幅
        float adjustedWidth = contentRegion.y * 16.0f / 9.0f;

        // 画面内に収まるように調整
        if (adjustedHeight <= contentRegion.y) {
            lastSceneTextureSize.x = contentRegion.x;
            lastSceneTextureSize.y = adjustedHeight;
        } else {
            lastSceneTextureSize.x = adjustedWidth;
            lastSceneTextureSize.y = contentRegion.y;
        }

        // 計算結果を保存
        sceneTextureSize_ = lastSceneTextureSize;
    }

    // 背景カラー設定
    static ImVec4 lastBgColor = ImVec4(0, 0, 0, 0);
    ImVec4 backgroundColor;

    // 背景色も必要時のみ更新
    if (ImGui::IsWindowFocused()) {
        backgroundColor = ImVec4(
            dxCommon_->GetClearColor().x,
            dxCommon_->GetClearColor().y,
            dxCommon_->GetClearColor().z,
            dxCommon_->GetClearColor().w);
        lastBgColor = backgroundColor;
    } else {
        backgroundColor = lastBgColor;
    }

    // シーンテクスチャの中央配置のための計算
    ImVec2 sceneOffset;
    sceneOffset.x = (contentRegion.x - sceneTextureSize_.x) * 0.5f;
    sceneOffset.y = (contentRegion.y - sceneTextureSize_.y) * 0.5f;

    // テクスチャ描画位置を調整
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + sceneOffset.x, ImGui::GetCursorPosY() + sceneOffset.y));

    // ポストエフェクトが適用された最終結果のテクスチャを取得
    uint32_t srvIndex;
    if (offScreen != nullptr) {
        // ポストエフェクトが適用された最終結果を使用
        srvIndex = offScreen->GetFinalResultSrvIndex();
    } else {
        // フォールバック：通常のオフスクリーンバッファを使用
        srvIndex = dxCommon_->GetOffScreenSrvIndex();
    }

    // レンダーテクスチャをImGuiウィンドウに描画
    ImGui::ImageWithBg(
        static_cast<ImTextureID>(SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex).ptr),
        sceneTextureSize_, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
        backgroundColor);

    // ImGuizmoのために正確なシーン位置を計算
    actualScenePos_ = ImVec2(
        contentPos.x + sceneOffset.x,
        contentPos.y + sceneOffset.y);
    imGuizmoManager_->Update(actualScenePos_, sceneTextureSize_);

    ImGui::End();
}

void ImGuiManager::ShowMainUI(OffScreen *offscreen) {

    // ヒエラルキーウィンドウ
    ShowSceneSettingWindow();
    // インスペクターウィンドウ
    ShowObjectSettingWindow();
    // プロジェクトウィンドウを描画
    ShowParticleSettingWindow();
    // GPUパーティクル プレビュー窓を描画
    ShowParticlePreviewWindow();
    // FPSを描画
    ShowStatisticsWindow();
    // オフスクリーンウィンドウを描画
    ShowOffScreenSettingWindow(offscreen);
    // ライトウィンドウを描画
    ShowLightSettingWindow();
    // ギズモウィンドウを描画
    ShowGizmoWindow();
    // 階層エディターウィンドウを描画
    ShowHierarchyWindow();
    // モーションエディターウィンドウを描画
    ShowMotionEditorWindow();
    // スプライトマネージャウィンドウを描画
    ShowSpriteManagerWindow();
    // コライダータグマネージャウィンドウを描画
    ShowColliderTagManagerWindow();
    // オーディオマネージャウィンドウを描画
    ShowAudioManagerWindow();
    // シャドウマップ設定ウィンドウを描画
    ShowShadowMapWindow();
    // 描画システム設定ウィンドウを描画
    ShowDrawSystemWindow();

    ShowHelpWindow();
    baseObjectManager_->UpdateImGui();
    spriteManager_->UpdateImGui();
}

bool &ImGuiManager::GetIsShowMainUI() {
    return isShowMainUI_;
}

void ImGuiManager::ShowDockSpace() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("DockSpaceWindow", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // DockSpaceの生成
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

#endif //_DEBUG

void ImGuiManager::DisplayFPS() {
#ifdef _DEBUG
    if (ImGui::CollapsingHeader("FPS")) {

        // 履歴サイズ（フレーム数）
        static const int kHistSize = 256;

        // 循環バッファ
        static float fpsHistory[kHistSize] = {};
        static float frameTimeHistory[kHistSize] = {};
        static int offset = 0;
        static float fpsMax = 70.0f; // Y軸上限（適宜調整）

        // 今フレームの値を記録
        float fps = Frame::GetFPS();
        float frameTime = Frame::DeltaTime() * 1000.0f; // ms

        fpsHistory[offset] = fps;
        frameTimeHistory[offset] = frameTime;
        offset = (offset + 1) % kHistSize;

        // 色判定（数値表示用）
        ImVec4 color =
            fps >= 59.0f ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : fps >= 30.0f ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
                                                                         : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

        // 数値をコンパクトに横並び表示
        ImGui::TextColored(color, "FPS: %.1f", fps);
        ImGui::SameLine(100);
        ImGui::TextColored(color, "Frame: %.2f ms", frameTime);

        // -----------------------------------------------
        // FPS 折れ線グラフ
        // -----------------------------------------------
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("FPS 履歴");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.45f, 0.65f, 0.85f, 1.0f));

        if (ImPlot::BeginPlot("##FPSPlot", ImVec2(-1, 90),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, "FPS",
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistSize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, fpsMax, ImPlotCond_Always);

            // 60fps / 30fps の基準線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.0f, 1.0f, 0.0f, 0.4f));
            double y60 = 60.0;
            ImPlot::PlotInfLines("##60", &y60, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 1.0f, 0.0f, 0.4f));
            double y30 = 30.0;
            ImPlot::PlotInfLines("##30", &y30, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            // FPS折れ線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.45f, 0.65f, 0.85f, 1.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PlotLine("FPS", fpsHistory, kHistSize, 1.0, 0.0,
                             ImPlotLineFlags_None, offset);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(3);

        // -----------------------------------------------
        // フレームタイム 折れ線グラフ
        // -----------------------------------------------
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("フレームタイム 履歴 (ms)");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));

        if (ImPlot::BeginPlot("##FrameTimePlot", ImVec2(-1, 70),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, "ms",
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistSize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 50.0, ImPlotCond_Always);

            // 16.6ms（60fps相当）の基準線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.0f, 1.0f, 0.0f, 0.4f));
            double y16 = 16.6;
            ImPlot::PlotInfLines("##16ms", &y16, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            // フレームタイム折れ線（遅いほど赤寄り）
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PlotLine("ms", frameTimeHistory, kHistSize, 1.0, 0.0,
                             ImPlotLineFlags_None, offset);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(2);
    }
#endif // _DEBUG
}

// 現在のDockレイアウトを保存
void ImGuiManager::BackupDockLayout() {
#ifdef USE_IMGUI
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (context) {
        // Dockingレイアウトを文字列として保存
        dockLayoutBackup_ = ImGui::SaveIniSettingsToMemory();
    }
#endif // USE_IMGUI
}

// 保存したレイアウトを復元
void ImGuiManager::RestoreDockLayout() {
#ifdef USE_IMGUI
    if (!dockLayoutBackup_.empty()) {
        // メモリ上の設定を再適用
        ImGui::LoadIniSettingsFromMemory(dockLayoutBackup_.c_str(), dockLayoutBackup_.size());
    }
#endif // USE_IMGUI
}

void ImGuiManager::SwitchToEditorMode() {
    if (!isEditorMode_) {
        // ゲームモードからエディターモードへの切替
        SaveCurrentLayout(); // 現在のゲームモードレイアウトを保存
        isEditorMode_ = true;
        LoadLayoutForCurrentMode(); // エディターモードのレイアウトをロード
#ifdef _DEBUG
        ImGuiNotification::Post("エディターモードに切り替えました", {0.4f, 0.8f, 1.0f, 1.0f});
#endif // _DEBUG
    }
}

void ImGuiManager::SwitchToGameMode() {
    if (isEditorMode_) {
        // エディターモードからゲームモードへの切替
        SaveCurrentLayout(); // 現在のエディターモードレイアウトを保存
        isEditorMode_ = false;
        LoadLayoutForCurrentMode(); // ゲームモードのレイアウトをロード
#ifdef _DEBUG
        ImGuiNotification::Post("ゲームモードに切り替えました", {0.4f, 0.8f, 1.0f, 1.0f});
#endif // _DEBUG
    }
}

// ゲームモードini用: [Docking]セクション全体と各[Window]のDockId行を除去する
// F5でアンドックしたウィンドウのノードIDが次回起動時に存在せず
// imgui.cpp の "node != 0" アサーションを引き起こすのを防ぐ
#ifdef USE_IMGUI
static std::string StripDockDataFromIni(const char* src, size_t srcSize) {
    std::string result;
    result.reserve(srcSize);
    size_t pos = 0;
    bool skipSection = false;

    while (pos < srcSize) {
        size_t lineEnd = pos;
        while (lineEnd < srcSize && src[lineEnd] != '\n') ++lineEnd;
        // line は src[pos..lineEnd) ('\n' を含まない)
        const char* linePtr = src + pos;
        size_t lineLen = lineEnd - pos;
        pos = lineEnd + 1; // 次の行へ

        // セクションヘッダ判定
        if (lineLen > 0 && linePtr[0] == '[') {
            // [Docking] セクションはスキップフラグを立てる
            bool isDocking = (lineLen >= 9 &&
                              linePtr[1] == 'D' && linePtr[2] == 'o' &&
                              linePtr[3] == 'c' && linePtr[4] == 'k' &&
                              linePtr[5] == 'i' && linePtr[6] == 'n' &&
                              linePtr[7] == 'g' && linePtr[8] == ']');
            skipSection = isDocking;
            if (isDocking) continue;
        }

        if (skipSection) continue;

        // DockId= 行はスキップ（ウィンドウの古いノード参照を除去）
        if (lineLen >= 7 &&
            linePtr[0] == 'D' && linePtr[1] == 'o' && linePtr[2] == 'c' &&
            linePtr[3] == 'k' && linePtr[4] == 'I' && linePtr[5] == 'd' &&
            linePtr[6] == '=') {
            continue;
        }

        result.append(linePtr, lineLen);
        result += '\n';
    }
    return result;
}
#endif // USE_IMGUI

void ImGuiManager::SaveCurrentLayout() {
#ifdef USE_IMGUI
    // 現在のモードに応じたファイルにレイアウトを保存
    const char *iniFilePath = isEditorMode_ ? editorIniFilePath_.c_str() : gameIniFilePath_.c_str();

    // メモリからiniデータを取得
    size_t size = 0;
    const char *iniData = ImGui::SaveIniSettingsToMemory(&size);

    // ファイルに書き込み
    FILE *f = nullptr;
    if (fopen_s(&f, iniFilePath, "wt") == 0 && f) {
        if (!isEditorMode_) {
            // ゲームモード: ドックノード参照を除去して保存
            std::string stripped = StripDockDataFromIni(iniData, size);
            fwrite(stripped.c_str(), sizeof(char), stripped.size(), f);
        } else {
            fwrite(iniData, sizeof(char), size, f);
        }
        fclose(f);
    }
    ImGuiNotification::Post("レイアウトを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // USE_IMGUI
}

void ImGuiManager::LoadLayoutForCurrentMode() {
#ifdef USE_IMGUI
    // モードに応じたiniファイルをロード
    const char *iniFilePath = isEditorMode_ ? editorIniFilePath_.c_str() : gameIniFilePath_.c_str();

    // ファイルが存在する場合はロード
    FILE *f = nullptr;
    if (fopen_s(&f, iniFilePath, "rt") == 0 && f) {
        // ファイルサイズを取得
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        // バッファを確保してファイル内容を読み込む
        char *buf = new char[size + 1];
        if (buf) {
            size_t read_size = fread(buf, 1, size, f);
            buf[read_size] = 0;

            if (!isEditorMode_) {
                // ゲームモード: 古いドックノード参照を除去してからロード
                std::string stripped = StripDockDataFromIni(buf, read_size);
                ImGui::LoadIniSettingsFromMemory(stripped.c_str(), stripped.size());
            } else {
                ImGui::LoadIniSettingsFromMemory(buf, read_size);
            }

            delete[] buf;
        }
        fclose(f);
    }
    // ファイルが存在しない場合は新規に作成される
#endif // USE_IMGUI
}

void ImGuiManager::ShowHelpWindow() {
#ifdef _DEBUG

    // ショートカット一覧ウィンドウの表示
    if (showShortcutWindow_) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(ICON_FA_KEYBOARD " ショートカット一覧", &showShortcutWindow_, ImGuiWindowFlags_NoCollapse)) {

            // テーブルでショートカットを整理して表示
            if (ImGui::BeginTable("ShortcutTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("機能", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("ショートカットキー", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableHeadersRow();

                // システム操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_COG " システム操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  デバッグカメラ切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F3");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  アプリケーション終了");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Alt + F4");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  デバッグUI切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F5");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  フルスクリーン切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F11");

                // シーン操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_FOLDER " シーン操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  シーン保存");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + S");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  シーン読み込み");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + L");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  モデル作成");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + N");

                // シーン切り替え
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_EXCHANGE_ALT " シーン切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  タイトルシーン");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + 1");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  セレクトシーン");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + 2");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  ゲームシーン");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + 3");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  クリアシーン");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + 4");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  デモシーン");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + 5");

                // オブジェクト操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_CUBES " 選択オブジェクト操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクトコピー");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + C");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクトペースト");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + V");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクト削除");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Delete");

                ImGui::EndTable();
            }

            // 注記
            ImGui::Separator();
            ImGui::TextWrapped("注意: これらのショートカットはデバッグビルドでのみ有効です。");

            // 閉じるボタン
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 35);
            if (ImGui::Button("閉じる", ImVec2(100, 25))) {
                showShortcutWindow_ = false;
            }
        }
        ImGui::End();
    }
#endif // _DEBUG
}

void ImGuiManager::SaveFlag() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ImGuiSetting", "Frags");
    data->Save("IsShowMainUI", isShowMainUI_);
    data->Save("ShowGrid", showGrid_);
    data->Save("showSceneView", showSceneView_);
    data->Save("showObjectView", showObjectView_);
    data->Save("showParticleView", showParticleView_);
    data->Save("showParticlePreviewView", showParticlePreviewView_);
    data->Save("showFPSView", showFPSView_);
    data->Save("showOfScreenView", showOfScreenView_);
    data->Save("showLightView", showLightView_);
    data->Save("showGizmoView", showGizmoView_);
    data->Save("showHierarchyView", showHierarchyView_);
    data->Save("showMotionEditorView", showMotionEditorView_);
    data->Save("showShortcutWindow", showShortcutWindow_);
    data->Save("showSpriteManagerView", showSpriteManagerView_);
    data->Save("showShadowMapView", showShadowMapView_);
    data->Save("showDrawSystemView", showDrawSystemView_);
    data->Save("isEditorMode", isEditorMode_);
    data->Save("gridColor", gridColor_);
#ifdef _DEBUG
    ImGuiNotification::Post("UI設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // _DEBUG
}

void ImGuiManager::LoadFlag() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ImGuiSetting", "Frags");
    isShowMainUI_ = data->Load("IsShowMainUI", true);
    showGrid_ = data->Load("ShowGrid", true);
    showSceneView_ = data->Load("showSceneView", true);
    showObjectView_ = data->Load("showObjectView", true);
    showParticleView_ = data->Load("showParticleView", false);
    showParticlePreviewView_ = data->Load("showParticlePreviewView", false);
    showFPSView_ = data->Load("showFPSView", true);
    showOfScreenView_ = data->Load("showOfScreenView", false);
    showLightView_ = data->Load("showLightView", false);
    showGizmoView_ = data->Load("showGizmoView", false);
    showHierarchyView_ = data->Load("showHierarchyView", true);
    showMotionEditorView_ = data->Load("showMotionEditorView", false);
    showShortcutWindow_ = data->Load("showShortcutWindow", false);
    showSpriteManagerView_ = data->Load("showSpriteManagerView", false);
    showShadowMapView_ = data->Load("showShadowMapView", true);
    showDrawSystemView_ = data->Load("showDrawSystemView", true);
    isEditorMode_ = data->Load("isEditorMode", true);
    gridColor_ = data->Load("gridColor", Vector4(0.5f, 0.5f, 0.5f, 1.0f));
}
} // namespace Hagine
