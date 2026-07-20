#pragma once
#include "Asset/AssetPath.h"
#include "DirectXCommon.h"
#include "SpriteManager.h"
#include "WinApp.h"
#include "object/base/BaseObjectManager.h"
#include <Audio.h>
#include <BaseScene.h>

namespace Hagine {
class ImGuizmoManager;
class OffScreen;
class DrawSystem;
class ImGuiManager {
  private:
    ImGuizmoManager *pImGuizmoManager_ = nullptr;
    WinApp *pWinApp_ = nullptr;
    DrawSystem *pDrawSystem_ = nullptr;

  public:
    /// ====================================
    /// public method
    /// ====================================

    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(ImGuiManager &) = delete;
    ImGuiManager &operator=(ImGuiManager &) = delete;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp *winApp, ImGuizmoManager *imguizmoManager);

    void SetupTheme();

    /// <summary>
    /// 統計ウィンドウで参照する DrawSystem を設定（Framework が注入する）
    /// </summary>
    void SetDrawSystem(DrawSystem *drawSystem) { pDrawSystem_ = drawSystem; }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// ImGui受付開始
    /// </summary>
    void Begin();

    /// <summary>
    /// ImGui受付終了
    /// </summary>
    void End();

    /// <summary>
    /// 画面への描画
    /// </summary>
    void Draw();

    /// <summary>
    /// マルチビューポート描画（ドックから切り離したウィンドウを独立OSウィンドウとして描画）。
    /// メインビューポートの Present 後に呼ぶこと。
    /// </summary>
    void RenderMultiViewport();

    /// <summary>
    /// .iniファイル関連の更新
    /// </summary>
    void UpdateIni();

    /// <summary>
    /// メインUI表示
    /// </summary>
    void ShowMainUI(OffScreen *offscreen);

    /// <summary>
    /// メニュー表示
    /// </summary>
    void ShowMainMenu();

    /// <summary>
    /// ドックスペース追加
    /// </summary>
    void ShowDockSpace();

    void DisplayFPS();

    bool &GetIsShowMainUI();
    void SetCurrentScene(BaseScene *currentScene) { pCurrentScene_ = currentScene; };

    void SetImGuizmoManager(ImGuizmoManager *manager) {
        pImGuizmoManager_ = manager;
    }

    void SetShortcutWindow(bool show) {
        showShortcutWindow_ = show;
    }

    // 必要に応じてImGuizmoManagerへのアクセサを追加
    ImGuizmoManager *GetImGuizmoManager() const {
        return pImGuizmoManager_;
    }

    /// <summary>
    /// シーン表示
    /// </summary>
    void ShowSceneWindow(OffScreen *offScreen, const std::string &sceneName);
#ifdef USE_IMGUI
    // レイ計算用のシーン矩形（仮想解像度座標系。Mouse::GetMousePos と同じ空間）。
    // ImGui 座標のシーン矩形をマウスと同じレターボックス逆変換に通してあり、
    // ウィンドウサイズ変更・マルチビューポート時もレイ計算と整合する。
    Vector2 GetScenePosForRay() const {
        return Vector2(scenePosForRay_.x, scenePosForRay_.y);
    }
    Vector2 GetSceneSizeForRay() const {
        return Vector2(sceneSizeForRay_.x, sceneSizeForRay_.y);
    }
#endif // USE_IMGUI

    bool GetEditorMode() const {
        return isEditorMode_;
    }

  private:
    /// ====================================
    /// private method
    /// ====================================

    /// <summary>
    /// ヒープ作成
    /// </summary>
    void CreateDescriptorHeap();

    /// <summary>
    /// ヒエラルキー表示
    /// </summary>
    void ShowSceneSettingWindow();

    void ShowObjectSettingWindow();

    void ShowParticleSettingWindow();

    // GPUパーティクルのプレビュー窓（表示メニューでON/OFF）
    void ShowParticlePreviewWindow();

    void ShowStatisticsWindow();

    void ShowOffScreenSettingWindow(OffScreen *offscreen);

    void ShowLightSettingWindow();

    void ShowGizmoWindow();

    void ShowHierarchyWindow();

    void ShowMotionEditorWindow();

    void ShowSpriteManagerWindow();

    void ShowColliderTagManagerWindow();

    void ShowAudioManagerWindow();

    void ShowShadowMapWindow();

    void ShowDrawSystemWindow();

    // アセットブラウザ窓（Application/Assets/images をサムネ一覧表示、各サムネをD&Dのドラッグ元にする）
    void ShowAssetBrowserWindow();

    // ゲームパラメータHub窓（コード登録済みパラメータを実行中に仕分け・調整する）
    void ShowGameParamWindow();

    void FixAspectRatio();

    void BackupDockLayout();

    void RestoreDockLayout();

    void SwitchToEditorMode();
    void SwitchToGameMode();
    void SaveCurrentLayout();
    void LoadLayoutForCurrentMode();
    void ShowHelpWindow();

    void SaveFlag();
    void LoadFlag();

  private:
    /// ====================================
    /// private variaus
    /// ====================================

    std::string dockLayoutBackup_;

    // SRV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    SrvManager *pSrvManager_ = nullptr;
    BaseScene *pCurrentScene_ = nullptr;

    DirectXCommon *pDxCommon_;
#ifdef USE_IMGUI
    // ヒエラルキーウィンドウ
    ImVec2 hierarchyWindowPosition_ = {0.0f, 64.0f};

    // シーンウィンドウ
    ImVec2 sceneTextureSize_ = {800.0f, 450.0f};
    ImVec2 actualScenePos_ = {};  // ImGui座標系（ImGuizmo用）
    ImVec2 scenePosForRay_ = {};  // 仮想解像度座標系（レイ計算用）
    ImVec2 sceneSizeForRay_ = {}; // 仮想解像度座標系（レイ計算用）

#endif // USE_IMGUI
    int cubeCount_ = 0;
    int sphereCount_ = 0;
    int planeCount_ = 0;
    int cylinderCount_ = 0;
    int ringCount_ = 0;
    int triangleCount_ = 0;
    int capsuleCount_ = 0;
    int pyramidCount_ = 0;
    int coneCount_ = 0;

    // エンジンのウィンドウを描画するフラグ
    // 重いUIコンポーネントの表示状態管理
    bool isShowMainUI_ = false;
    bool showSceneView_ = true;
    bool showObjectView_ = true;
    bool showParticleView_ = true;
    bool showParticlePreviewView_ = false; // GPUパーティクル プレビュー窓
    bool showFPSView_ = true;
    bool showOfScreenView_ = true;
    bool showLightView_ = true;
    bool isEditorMode_ = true;   // エディターモードフラグ
    bool multiViewport_ = false; // マルチビューポート有効フラグ
    bool showShortcutWindow_ = false;
    bool showGizmoView_ = true;
    bool showHierarchyView_ = true;
    bool showMotionEditorView_ = true;
    bool showSpriteManagerView_ = true;
    bool showColliderTagManagerView_ = false;
    bool showAudioManagerView_ = false;
    bool showShadowMapView_ = true;
    bool showDrawSystemView_ = true;
    bool showGameParamView_ = true;     // ゲームパラメータHub窓
    bool showAssetBrowserView_ = false; // アセットブラウザ窓

    // グリッド設定用メンバ変数
    bool showGrid_ = true;
    float gridY_ = 0.0f;
    int gridDivision_ = 1000;
    float gridSize_ = 5000.0f;
    Vector4 gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f}; // グレー

    BaseObjectManager *pBaseObjectManager_ = nullptr;
    SpriteManager *pSpriteManager_ = nullptr;
    Audio *pAudio_ = nullptr;

    // ImGui レイアウト ini は Application/Config/ 配下に生成する（プロジェクトルートを散らかさない）。
    std::string editorIniFilePath_ = "Application/Config/imgui_editor.ini";
    std::string gameIniFilePath_ = "Application/Config/imgui_game.ini";
};
} // namespace Hagine
