#pragma once
#include "BaseScene.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Hagine {

/// <summary>
/// シーン名 → 生成関数 のレジストリ（シングルトン）
/// アプリ側の各シーンが REGISTER_SCENE マクロで自己登録するため、
/// エンジンは具体的なシーンクラスを一切知らなくてよい。
/// </summary>
class SceneRegistry {
  public:
    using Creator = std::function<std::unique_ptr<BaseScene>()>;

    /// <summary>
    /// シングルトンインスタンスの取得
    /// （関数ローカル static で静的初期化順序問題を回避）
    /// </summary>
    static SceneRegistry *GetInstance() {
        static SceneRegistry instance;
        return &instance;
    }

    /// <summary>
    /// シーン生成関数を登録
    /// </summary>
    /// <param name="name">シーン名</param>
    /// <param name="creator">生成関数</param>
    void Register(const std::string &name, Creator creator) {
        creators_[name] = std::move(creator);
    }

    /// <summary>
    /// シーンを生成（未登録なら nullptr）
    /// </summary>
    /// <param name="name">シーン名</param>
    /// <returns>生成された BaseScene（未登録時は nullptr）</returns>
    std::unique_ptr<BaseScene> Create(const std::string &name) const {
        auto it = creators_.find(name);
        if (it == creators_.end()) {
            return nullptr;
        }
        return it->second();
    }

    /// <summary>
    /// シーンが登録済みか
    /// </summary>
    bool Contains(const std::string &name) const {
        return creators_.find(name) != creators_.end();
    }

  private:
    SceneRegistry() = default;
    ~SceneRegistry() = default;
    SceneRegistry(SceneRegistry &) = delete;
    SceneRegistry &operator=(SceneRegistry &) = delete;

    std::unordered_map<std::string, Creator> creators_;
};

/// <summary>
/// 静的初期化時にシーンを自己登録するためのヘルパ
/// </summary>
struct SceneRegistrar {
    SceneRegistrar(const std::string &name, SceneRegistry::Creator creator) {
        SceneRegistry::GetInstance()->Register(name, std::move(creator));
    }
};

} // namespace Hagine

/// <summary>
/// シーンを自己登録するマクロ。
/// 各シーンの .cpp に次のように 1 行書くだけで登録できる:
///   REGISTER_SCENE("GAME", GameScene)
/// 中央の SceneFactory を編集する必要はない。
/// </summary>
#define REGISTER_SCENE(NAME, TYPE)                                                                     \
    namespace {                                                                                        \
    const ::Hagine::SceneRegistrar _scene_registrar_##TYPE{NAME, [] { return std::make_unique<TYPE>(); }}; \
    }
