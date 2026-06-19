#pragma once
#include "DirectXCommon.h"
#include "WinApp.h"
#include <BaseScene.h>
#include "Object/Base/BaseObjectManager.h"
#include"SpriteManager.h"
#include <Audio.h>
#include <functional>

namespace Hagine {
class ImGuizmoManager;
class OffScreen;
class ImGuiManager {
  private:
    /// ====================================
    /// public method
    /// ====================================

    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(ImGuiManager &) = delete;
    ImGuiManager &operator=(ImGuiManager &) = delete;

    ImGuizmoManager *imGuizmoManager_ = nullptr;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp *winApp, ImGuizmoManager *imguizmoManager);

    void SetupTheme();

    /// <summary>
    /// 「モーションエディター」ウィンドウの中身を描画するコールバックを設定する。
    /// エンジンは具体的な MotionEditor（アプリ側機能）を知らずに済む。
    /// </summary>
    void SetMotionEditorDrawCallback(std::function<void()> cb) { motionEditorDrawCallback_ = std::move(cb); }

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static ImGuiManager* GetInstance() {
        static ImGuiManager instance;
        return &instance;
    }

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
    void SetCurrentScene(BaseScene *currentScene) { currentScene_ = currentScene; };

    void SetImGuizmoManager(ImGuizmoManager *manager) {
        imGuizmoManager_ = manager;
    }

    void SetShortcutWindow(bool show) {
        showShortcutWindow_ = show;
    }

    // 必要に応じてImGuizmoManagerへのアクセサを追加
    ImGuizmoManager *GetImGuizmoManager() const {
        return imGuizmoManager_;
    }

    /// <summary>
    /// シーン表示
    /// </summary>
    void ShowSceneWindow(OffScreen *offScreen, const std::string &sceneName);
#ifdef USE_IMGUI
    Vector2 GetSceneSize() const {
        return Vector2(sceneTextureSize_.x, sceneTextureSize_.y);
    }
    Vector2 GetScenePos() const {
        return Vector2(actualScenePos_.x, actualScenePos_.y);
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
    SrvManager *srvManager_ = nullptr;
    BaseScene *currentScene_ = nullptr;

    DirectXCommon *dxCommon_;
#ifdef USE_IMGUI
    // ヒエラルキーウィンドウ
    ImVec2 hierarchyWindowPosition_ = {0.0f, 64.0f};

    // シーンウィンドウ
    ImVec2 sceneTextureSize_ = {800.0f, 450.0f};
    ImVec2 actualScenePos_ = {};

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
    bool isEditorMode_ = true; // エディターモードフラグ
    bool showShortcutWindow_ = false;
    bool showGizmoView_ = true;
    bool showHierarchyView_ = true;
    bool showMotionEditorView_ = true;
    bool showSpriteManagerView_ = true;
    bool showColliderTagManagerView_ = false;
    bool showAudioManagerView_ = false;
    bool showShadowMapView_ = true;
    bool showDrawSystemView_ = true;

    // 「モーションエディター」ウィンドウの中身を描画するコールバック（アプリ側が差し込む）
    std::function<void()> motionEditorDrawCallback_;

    // グリッド設定用メンバ変数
    bool showGrid_ = true;
    float gridY_ = 0.0f;
    int gridDivision_ = 1000;
    float gridSize_ = 5000.0f;
    Vector4 gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f}; // グレー

    BaseObjectManager *baseObjectManager_ = nullptr;
    SpriteManager *spriteManager_ = nullptr;
    Audio *audio_ = nullptr;

    std::string editorIniFilePath_ = "imgui_editor.ini";
    std::string gameIniFilePath_ = "imgui_game.ini";
};
} // namespace Hagine
