#pragma once
#include <d3d12.h>
#include <string>
#include <unordered_map>

namespace Hagine {

/// <summary>
/// テクスチャプレビュー用のキャッシュ情報
/// </summary>
struct TextureCache {
    uint32_t srvIndex = 0;              // SRVインデックス
    D3D12_GPU_DESCRIPTOR_HANDLE handle; // GPUハンドル
    int width = 0;                      // 幅
    int height = 0;                     // 高さ
};

/// <summary>
/// テクスチャファイルの選択UIを表示
/// </summary>
/// <param name="selectedTexturePath">選択されたテクスチャパス（出力）</param>
void ShowTextureFile(std::string &selectedTexturePath);

/// <summary>
/// モデルファイルの選択UIを表示
/// </summary>
/// <param name="selectedModelPath">選択されたモデルパス（出力）</param>
void ShowModelFile(std::string &selectedModelPath);

/// <summary>
/// Jsonファイルの選択UIを表示
/// </summary>
/// <param name="selectedJsonPath">選択されたJsonパス（出力）</param>
/// <param name="startPath">表示開始パス</param>
void ShowJsonFile(std::string &selectedJsonPath, std::string &startPath);

/// <summary>
/// glTFファイルの選択UIを表示
/// </summary>
/// <param name="selectedGltfPath">選択されたglTFパス（出力）</param>
void ShowGltfFile(std::string &selectedGltfPath);

static std::unordered_map<std::string, TextureCache> textureCache; // テクスチャプレビューのキャッシュ
} // namespace Hagine
