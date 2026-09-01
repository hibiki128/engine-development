#pragma once
#include <string>
#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include "DirectXCommon.h"
#include "graphics/srv/SrvManager.h"
#include "data/DataHandler.h"
#include "graphics/pipeline/PipelineManager.h"

/// @brief ポストエフェクトパラメータの基底インターフェース
/// 各エフェクトはこのインターフェースを実装し、自身のパラメータを所有する
namespace Hagine {

/// @brief コンピュートシェーダー版エフェクトが要求する入力テクスチャの種類。
/// 並べた順に t0, t1, ... へバインドされる。
enum class ComputeInput
{
    /// そのパスへの入力画像。
    /// 1パス目はエフェクトへの入力、2パス目以降は前のパスが書いた中間結果になる。
    SourceColor,
    /// エフェクトへの入力画像そのもの（パスが進んでも中間結果に差し替わらない）。
    /// ブルームのように「ぼかした結果を元画像に加算する」エフェクトで、
    /// 最後のパスから元画像を参照するために使う。
    EffectInput,
    /// シーンの深度バッファ
    SceneDepth,
};

class IPostEffectParams
{
  public:
    virtual ~IPostEffectParams() = default;

    /// @brief GPUバッファの初期化
    virtual void Initialize(DirectXCommon *pDxCommon) = 0;

    /// @brief このパラメータが対応するシェーダーモードを返す
    virtual ShaderMode GetMode() const = 0;

    /// @brief コマンドリストにパラメータをバインドする
    virtual void Apply(ID3D12GraphicsCommandList *pCommandList,
                       SrvManager *pSrvManager,
                       DirectXCommon *pDxCommon) = 0;

    /// @brief ImGuiによるパラメータ編集UI
    virtual void DrawUI() = 0;

    /// @brief パラメータを保存（prefixでスロット番号を区別）
    virtual void Save(DataHandler *handler, const std::string &prefix) const = 0;

    /// @brief パラメータを読み込み
    virtual void Load(DataHandler *handler, const std::string &prefix) = 0;

    /// @brief 時間更新が必要なエフェクト向け（デフォルトは何もしない）
    virtual void UpdateTime(float /*deltaTime*/) {}

    // ===================================================
    //  コンピュートシェーダー版（任意）
    //  GetComputeShaderFile() が空文字を返す間は、従来どおりピクセルシェーダー版で描画される。
    //  CS化したいエフェクトだけこれらを実装すればよい。
    // ===================================================

    /// @brief CSで実行する場合のシェーダーファイル名（shaders ルートからの相対パス）。
    ///        空文字ならピクセルシェーダー版を使う。
    virtual std::string GetComputeShaderFile() const { return {}; }

    /// @brief CSに必要な入力テクスチャ。並べた順に t0, t1, ... へバインドされる。
    virtual std::vector<ComputeInput> GetComputeInputs() const { return {ComputeInput::SourceColor}; }

    /// @brief 何回ディスパッチするか。分離フィルタ（横方向→縦方向）などで2以上を返す。
    ///        2以上の場合、各パスの出力が次のパスの入力になる。
    virtual int GetComputePassCount() const { return 1; }

    /// @brief CS用の定数バッファをバインドする。
    /// @param pCommandList  コマンドリスト
    /// @param cbvRootIndex  b0 に対応するルートパラメータ番号（UINT_MAX なら b0 なし）
    /// @param passIndex     0 から GetComputePassCount()-1
    /// @param textureWidth  処理対象の幅（ピクセル）
    /// @param textureHeight 処理対象の高さ（ピクセル）
    virtual void ApplyCompute(ID3D12GraphicsCommandList * /*pCommandList*/,
                              UINT /*cbvRootIndex*/,
                              int /*passIndex*/,
                              uint32_t /*textureWidth*/,
                              uint32_t /*textureHeight*/) {}
};
} // namespace Hagine
