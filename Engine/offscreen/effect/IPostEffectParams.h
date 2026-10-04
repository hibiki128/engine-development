#pragma once
#include <string>
#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include "DirectXCommon.h"
#include "graphics/srv/SrvManager.h"
#include "data/DataHandler.h"
#include "graphics/pipeline/PipelineManager.h"
#include <type/Matrix4x4.h>
#include <type/Vector3.h>

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
    /// 平行光源から見た深度（シャドウマップ）。
    /// 光の筋のように「その点に光が届いているか」を後段で知りたいときに使う。
    /// シャドウが無効なときは中身が古いままなので、使う側でフラグを見ること
    ShadowMap,
    /// G-Buffer の法線＋光沢度（xyz=ワールド法線, w=光沢度）。
    /// ディファードが無効なときは中身が無いので、使う側でフラグを見ること
    GBufferNormal,
    /// レイトレーシングの加速構造（TLAS）。
    /// これを要求したエフェクトは、シェーダーを cs_6_5 以上でコンパイルする必要がある
    /// （GetComputeShaderProfile を上書きすること）。
    /// 非対応環境や加速構造が無いときは差せないので、使う側でフラグを見ること
    Tlas,
    /// 環境マップ（スカイボックスのキューブマップ）。
    /// レイが何にも当たらなかった方向の色として使う
    EnvironmentCube,
    /// エフェクト自身が持つ「前のフレームの結果」（GetHistoryResource が返す物）。
    /// 残像のように過去の画を重ねるエフェクトで使う。持っていなければ入力画像で代用される
    History,
};

/// <summary>
/// ポストエフェクトへ毎フレーム渡すカメラの情報。
/// 深度からワールド座標を戻す・前のフレームと比べる・太陽を画面へ写す、などに使う
/// </summary>
struct PostEffectCameraInfo
{
    Matrix4x4 view;                  // ビュー行列
    Matrix4x4 projection;            // 射影行列
    Matrix4x4 viewProjection;        // ビュー射影行列
    Matrix4x4 inverseViewProjection; // ビュー射影行列の逆行列（NDC → ワールド）
    Vector3 cameraPosition;          // カメラのワールド座標
    Vector3 sunDirection;            // 平行光源が進む向き
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

    /// @brief CSをコンパイルするシェーダープロファイル。
    ///        RayQuery（インラインRT）を使うエフェクトは cs_6_5 以上でないと通らない。
    virtual const wchar_t *GetComputeShaderProfile() const { return L"cs_6_0"; }

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

    /// @brief カメラの情報を受け取る（毎フレーム、カメラが決まった後に呼ばれる）。
    ///        深度からワールド座標を戻すエフェクトなどが上書きする
    virtual void SetCameraInfo(const PostEffectCameraInfo & /*info*/) {}

    /// @brief ComputeInput::History で差す「前のフレームの結果」。持たないエフェクトは nullptr
    virtual ID3D12Resource *GetHistoryResource() const { return nullptr; }

    /// @brief CS の最後のパスを書き終えた直後に呼ばれる。
    ///        出力（GENERIC_READ 状態）を自分の History へ写すなど、結果を次のフレームへ持ち越すときに使う
    /// @param pCommandList コマンドリスト
    /// @param pOutput      このエフェクトが書いた出力
    /// @param pDxCommon    バリア用
    virtual void OnComputeFinished(ID3D12GraphicsCommandList * /*pCommandList*/,
                                   ID3D12Resource * /*pOutput*/,
                                   DirectXCommon * /*pDxCommon*/) {}
};
} // namespace Hagine
