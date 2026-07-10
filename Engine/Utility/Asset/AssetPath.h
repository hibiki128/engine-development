#pragma once
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// エンジン／アプリのアセットルートと、相対パスからの実パス解決を集約するヘルパー。
///
/// アセットは 2 つのルートに分割されている:
///   - エンジンアセット : Engine/EngineAssets/ (fonts / shaders / images/debug / models/debug)
///   - アプリアセット   : Application/Assets/  (それ以外の images / models / jsons / sounds)
///
/// images / models は相対パスの先頭が "debug/" ならエンジン側、それ以外はアプリ側へ振り分ける。
/// 返すパスはいずれも作業ディレクトリ (＝プロジェクトルート) からの相対パス。
/// </summary>
namespace AssetPath {

/// <summary>エンジンアセットのルート (末尾スラッシュ付き)。</summary>
inline const std::string &EngineRoot() {
    static const std::string kRoot = "Engine/EngineAssets/";
    return kRoot;
}

/// <summary>アプリケーションアセットのルート (末尾スラッシュ付き)。</summary>
inline const std::string &AppRoot() {
    static const std::string kRoot = "../Assets/app/Assets/";
    return kRoot;
}

/// <summary>相対パスがエンジン側 (debug 配下) かどうかを判定する。</summary>
/// <param name="rel">images / models ルートからの相対パス</param>
inline bool IsEngineRelative(const std::string &rel) {
    return rel.rfind("debug/", 0) == 0 || rel.rfind("debug\\", 0) == 0 || rel == "debug";
}

// --- fonts / shaders (エンジン専用) --------------------------------------

/// <summary>フォントの実パス。rel は fonts ルートからの相対パス。</summary>
inline std::string Font(const std::string &rel) { return EngineRoot() + "fonts/" + rel; }

/// <summary>シェーダの実パス。rel は shaders ルートからの相対パス。</summary>
inline std::string Shader(const std::string &rel) { return EngineRoot() + "shaders/" + rel; }

// --- images (debug=エンジン / その他=アプリ) -----------------------------

/// <summary>rel が属する images ルート (末尾スラッシュ付き)。</summary>
inline std::string ImagesRoot(const std::string &rel) {
    return (IsEngineRelative(rel) ? EngineRoot() : AppRoot()) + "images/";
}

/// <summary>テクスチャの実パス (＝TextureManager のマップキー)。rel は images ルートからの相対パス。</summary>
inline std::string Image(const std::string &rel) { return ImagesRoot(rel) + rel; }

// --- models (debug=エンジン / その他=アプリ) -----------------------------

/// <summary>rel が属する models ルート (末尾スラッシュ付き)。</summary>
inline std::string ModelsRoot(const std::string &rel) {
    return (IsEngineRelative(rel) ? EngineRoot() : AppRoot()) + "models/";
}

/// <summary>モデルの実パス。rel は models ルートからの相対パス。</summary>
inline std::string Model(const std::string &rel) { return ModelsRoot(rel) + rel; }

// --- jsons / sounds (アプリ専用) -----------------------------------------

/// <summary>JSON ルート (末尾スラッシュ無し。DataHandler の basePath_ 用)。</summary>
inline std::string JsonRoot() { return AppRoot() + "jsons"; }

/// <summary>JSON の実パス。rel は jsons ルートからの相対パス。</summary>
inline std::string Json(const std::string &rel) { return AppRoot() + "jsons/" + rel; }

/// <summary>サウンドルート (末尾スラッシュ無し)。</summary>
inline std::string SoundRoot() { return AppRoot() + "sounds"; }

// --- ブラウザ用: 全走査ルート一覧 (エンジン→アプリの順) ------------------

/// <summary>images を全走査するときの物理ルート一覧 (末尾スラッシュ無し)。</summary>
inline std::vector<std::string> ImageScanRoots() {
    return {EngineRoot() + "images", AppRoot() + "images"};
}

/// <summary>models を全走査するときの物理ルート一覧 (末尾スラッシュ無し)。</summary>
inline std::vector<std::string> ModelScanRoots() {
    return {EngineRoot() + "models", AppRoot() + "models"};
}

} // namespace AssetPath
} // namespace Hagine
