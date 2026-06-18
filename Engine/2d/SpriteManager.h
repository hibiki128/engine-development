#pragma once
#include "Sprite.h"
#include <Graphics/PipeLine/PipeLineManager.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>
/// インスタンス単位でのSRTデータ構造体
/// </summary>
namespace Hagine {
struct InstanceSRT {
    Vector3 scale = {1.0f, 1.0f, 1.0f};       // スケール
    Vector3 rotation = {0.0f, 0.0f, 0.0f};    // 回転
    Vector3 translation = {0.0f, 0.0f, 0.0f}; // 移動
    bool isActive = true;                     // 描画フラグ
};

/// <summary>
/// スプライト情報を管理する構造体
/// </summary>
struct SpriteTransform {
    Vector2 position = {0.0f, 0.0f};           // 位置
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};  // 色
    Vector2 anchorPoint = {0.0f, 0.0f};        // アンカーポイント
    bool isFlipX = false;                      // 左右反転フラグ
    bool isFlipY = false;                      // 上下反転フラグ
    uint32_t instanceCount = 1;                // インスタンス数

    /// <summary>
    /// デフォルトコンストラクタ
    /// </summary>
    SpriteTransform() = default;

    /// <summary>
    /// パラメータ付きコンストラクタ
    /// </summary>
    SpriteTransform(Vector2 pos, Vector4 col = {1.0f, 1.0f, 1.0f, 1.0f},
                    Vector2 anchor = {0.0f, 0.0f}, bool flipX = false, bool flipY = false, uint32_t count = 1)
        : position(pos), color(col), anchorPoint(anchor), isFlipX(flipX), isFlipY(flipY), instanceCount(count) {}
};

/// <summary>
/// スプライトデータを管理する構造体
/// </summary>
struct SpriteData {
    std::unique_ptr<Sprite> sprite;                          // スプライト本体
    std::string name;                                        // スプライト名
    std::string textureFilePath;                             // テクスチャファイルパス
    std::vector<InstanceSRT> instanceData;                   // インスタンスデータ
    std::function<void(SpriteData &, float)> updateFunction; // カスタム更新関数
    bool isVisible = true;                                   // 表示フラグ
    bool isBackMost = false;                                 // 背面フラグ
    BlendMode blendMode = BlendMode::kNormal;                // ブレンドモード
    bool lockAspectRatio = false;                            // アスペクト比維持フラグ
    std::string drawGroup = "UI";                            // 描画グループ＝描画ステージ（スプライトは既定でUIレイヤー）

    /// <summary>
    /// コンストラクタ
    /// </summary>
    SpriteData(const std::string &spriteName, const std::string &texturePath, uint32_t instanceCount = 1)
        : name(spriteName), textureFilePath(texturePath), instanceData(instanceCount) {}
};

/// <summary>
/// スプライト管理のシングルトンクラス
/// 複数のスプライトの登録、更新、描画を一元管理
/// </summary>
class SpriteManager {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static SpriteManager *GetInstance() {
        static SpriteManager instance;
        return &instance;
    }

    /// <summary>
    /// スプライトを登録
    /// </summary>
    void RegisterSprite(const std::string &name, const std::string &textureFilePath, const SpriteTransform &transform = SpriteTransform());

    /// <summary>
    /// スプライトを削除
    /// </summary>
    void UnregisterSprite(const std::string &name);

    /// <summary>
    /// すべてのスプライトを描画
    /// </summary>
    void DrawAll();

    /// <summary>
    /// 外部所有スプライトを描画リストに追加（非所有登録）
    /// </summary>
    void RegisterExternal(Sprite* sprite);

    /// <summary>
    /// 外部所有スプライトを描画リストから削除
    /// </summary>
    void UnregisterExternal(Sprite* sprite);

    /// <summary>
    /// すべてのスプライトを更新
    /// </summary>
    void UpdateAll(float deltaTime);

    /// <summary>
    /// ImGui更新処理
    /// </summary>
    void UpdateImGui();

    /// <summary>
    /// スプライト作成モーダルを表示
    /// </summary>
    void ShowSpriteCreationModal() { showSpriteCreationModal_ = true; }

    /// <summary>
    /// スプライト作成モーダルを描画
    /// </summary>
    void DrawSpriteCreationModal();

    /// <summary>
    /// スプライトマネージャーUIを描画
    /// </summary>
    void DrawSpriteManager();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// ===================================================
    /// Getter
    /// ===================================================
    SpriteData *GetSprite(const std::string &name);
    std::string GetTextureFilePath(const std::string &name);
    std::vector<SpriteData *>GetAllSprites();

    /// ===================================================
    /// Setter
    /// ===================================================
    void SetInstanceSRT(const std::string &name, uint32_t index, const InstanceSRT &srt);
    void SetInstanceScale(const std::string &name, uint32_t index, const Vector3 &scale);
    void SetInstanceRotation(const std::string &name, uint32_t index, const Vector3 &rotation);
    void SetInstanceTranslation(const std::string &name, uint32_t index, const Vector3 &translation);
    void SetInstanceActive(const std::string &name, uint32_t index, bool isActive);
    InstanceSRT *GetInstanceSRT(const std::string &name, uint32_t index);
    void SetSpriteVisible(const std::string &name, bool visible);
    void SetSpriteBackMost(const std::string &name, bool isBackMost);
    void SetSpritePosition(const std::string &name, const Vector2 &position);
    void SetSpriteSize(const std::string &name, const Vector2 &size);
    void SetSpriteColor(const std::string &name, const Vector4 &color);
    void SetTextureFilePath(const std::string &name, const std::string &textureFilePath);
    void SetUpdateFunction(const std::string &name, std::function<void(SpriteData &, float)> updateFunc);
    void SetSaveFolder(const std::string &folderName);
    void SetSpriteBlendMode(const std::string &name, BlendMode blendMode);

    /// <summary>
    /// 保存・読み込み関連
    /// </summary>
    void SaveAllSprites();
    void LoadAllSprites();
    void Clear();

  private:
    /// ===================================================
    /// private method
    /// ===================================================
    SpriteManager() = default;
    ~SpriteManager() = default;
    SpriteManager(const SpriteManager &) = delete;
    SpriteManager &operator=(const SpriteManager &) = delete;
    void SaveDrawOrder();
    void LoadDrawOrder();
    SpriteData *FindSpriteByName(const std::string &name);
    int FindSpriteIndex(const std::string &name);
    void UpdateSpriteInstances(SpriteData *spriteData);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================
    std::vector<std::unique_ptr<SpriteData>> sprites_; // スプライトリスト
    std::vector<Sprite*> externalSprites_;             // 外部所有スプライトリスト（非所有）
    bool showSpriteCreationModal_ = false;              // 作成モーダル表示フラグ
    std::string texturePath_ = "";                     // テクスチャパス
    std::string saveFolder_ = "Sprite";                // 保存先フォルダ
};
} // namespace Hagine
