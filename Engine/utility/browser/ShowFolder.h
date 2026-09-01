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
/// <param name="uiId">UIインスタンス識別子。同一ウィンドウ内に複数の
/// ブラウザ（アルベド用・法線マップ用など）を並べる場合に、
/// 閲覧中フォルダなどの状態とウィジェットIDを分離するために使う</param>
void ShowTextureFile(std::string &selectedTexturePath, const char *uiId = "default");

/// <summary>
/// モデルファイルの選択UIを表示
/// 一覧の各項目はシーンウィンドウへのドラッグ&ドロップ配置に対応している
/// </summary>
/// <param name="selectedModelPath">選択されたモデルパス（出力）</param>
/// <param name="uiId">UIインスタンス識別子。アセットブラウザと生成モーダルなど、
/// 複数箇所に並べる場合に閲覧中フォルダなどの状態とウィジェットIDを分離するために使う</param>
void ShowModelFile(std::string &selectedModelPath, const char *uiId = "default");

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

} // namespace Hagine
