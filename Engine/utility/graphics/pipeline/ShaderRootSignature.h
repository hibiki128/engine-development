#pragma once
#include "d3d12.h"
// dxcapi.h を先に include する必要がある（ShaderCompiler.h が面倒を見ている）
#include <shader/ShaderCompiler.h>
#include <string>
#include <vector>
#include <wrl.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// シェーダーリフレクションからルートシグネチャを自動生成するヘルパー。
///
/// コンパイル済みバイナリにはどのレジスタに何がバインドされているかが入っているので、
/// それを読んでルートシグネチャを組む。C++ 側にレジスタの記述は要らない。
///
/// 生成されるルートシグネチャの規約:
///   - b# (ConstantBuffer)  → ルートCBV（レジスタ番号の昇順）。BuildOptions で32bit定数にもできる
///   - t# (Texture/SRV)     → デスクリプタテーブル（TableMode で1本にまとめるか個別かを選ぶ）
///   - u# (RWTexture/UAV)   → 同上
///   - s# (SamplerState)    → スタティックサンプラー（SamplerPreset から選ぶ）
/// ルートパラメータの並びは「CBV群 → SRV → UAV」の順で、
/// 各リソースのルートパラメータ番号は GetRootParameterIndex() で引く。
///
/// 【重要】DXC は使われていないリソースをリフレクションから除外する。
/// そのためシェーダーを書き換えるとルートパラメータの並びが変わりうる。
/// 番号を直書きせず、必ず GetRootParameterIndex() を経由すること。
/// </summary>
class ShaderRootSignature
{
  public:
    /// <summary>
    /// スタティックサンプラーの内容。シェーダー側の s# の並びに対応させる。
    /// </summary>
    enum class SamplerPreset
    {
        LinearClamp,       // 線形補間・端はクランプ（画面全体を扱うポストエフェクトの既定）
        PointClamp,        // 補間なし・端はクランプ（深度など補間してはいけないもの）
        LinearWrap,        // 線形補間・繰り返し
        ShadowComparison,  // シャドウ用の比較サンプラー（範囲外は白＝影なし扱い）
    };

    /// <summary>
    /// SRV / UAV をデスクリプタテーブルへまとめる単位
    /// </summary>
    enum class TableMode
    {
        /// <summary>
        /// t0..tN を1本のテーブルにまとめる。
        /// デスクリプタがヒープ上で連続している前提（コンピュート版ポストエフェクトはこれ）。
        /// </summary>
        Merged,
        /// <summary>
        /// レジスタごとに1本ずつテーブルを作る。
        /// 「頂点シェーダーの t0 と ピクセルシェーダーの t1 を別々の場所から差す」
        /// といった通常の描画パスはこちら。
        /// </summary>
        PerRegister,
    };

    /// <summary>
    /// リフレクション1件ぶんの入力。どのステージのものかを併せて渡す。
    /// </summary>
    struct StageReflection
    {
        ID3D12ShaderReflection *pReflection = nullptr;
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
    };

    /// <summary>
    /// 32bit ルート定数にしたい定数バッファの指定。
    /// リフレクションからは「b# に定数バッファがある」ことしか分からず、
    /// ルートCBVなのかルート定数なのかは判別できないため、使う側が明示する。
    /// </summary>
    struct RootConstantDesc
    {
        UINT shaderRegister = 0;  // b# の番号
        UINT num32BitValues = 1;  // 送る DWORD 数
    };

    /// <summary>
    /// 生成時の指定
    /// </summary>
    struct BuildOptions
    {
        /// <summary>グラフィックス用なら true（入力アセンブラの使用を許可するフラグが立つ）</summary>
        bool allowInputAssembler = false;
        /// <summary>
        /// 追加で立てたいフラグ。使わないステージへのアクセスを落とす
        /// D3D12_ROOT_SIGNATURE_FLAG_DENY_XXX_SHADER_ROOT_ACCESS を渡す用途を想定している
        /// </summary>
        D3D12_ROOT_SIGNATURE_FLAGS extraFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        TableMode srvTableMode = TableMode::Merged;
        TableMode uavTableMode = TableMode::Merged;
        /// <summary>s0 から順に割り当てるサンプラー設定。足りない分は LinearClamp で埋める</summary>
        std::vector<SamplerPreset> samplerPresets;
        /// <summary>
        /// プリセットで表せないサンプラーを直接指定する（s0 から順）。
        /// 軸ごとにアドレッシングが違う場合など、既存の設定をそのまま残したいときに使う。
        /// 空でない要素があればその s# は samplerPresets より優先される。
        /// ShaderRegister はここで設定しなくてよい（並び順から自動で入る）。
        /// </summary>
        std::vector<D3D12_STATIC_SAMPLER_DESC> explicitSamplers;
        /// <summary>ルート定数として置きたい b#</summary>
        std::vector<RootConstantDesc> rootConstants;
        /// <summary>
        /// デスクリプタテーブルではなくルートSRVとして置きたい t#。
        /// StructuredBuffer をGPUアドレスで直接差す（SetGraphicsRootShaderResourceView）場合に指定する。
        /// リフレクションからは両者を区別できないので、使う側が明示する。
        /// </summary>
        std::vector<UINT> rootDescriptorSrvRegisters;
        /// <summary>
        /// 同じレジスタ番号をステージごとに別のルートパラメータとして扱うか。
        ///
        /// このエンジンでは「頂点シェーダーの b0 は変換行列、ピクセルシェーダーの b0 はマテリアル」
        /// のように、同じ番号を別リソースとして使っている。リフレクションはレジスタ番号しか
        /// 教えてくれないので、分けたい場合はここを true にする。
        /// true のときは GetCbvIndex(0, VERTEX) のように可視性まで指定して引くこと。
        /// </summary>
        bool separateResourcesByStage = false;
    };

    /// <summary>
    /// 複数ステージのリフレクションを合成してルートシグネチャを構築する。
    /// 同じレジスタを複数のステージが使っていれば可視性は ALL になる。
    /// </summary>
    /// <param name="pDxCommon">デバイス取得用</param>
    /// <param name="stages">対象シェーダーのリフレクション（VS / PS など）</param>
    /// <param name="options">生成時の指定</param>
    /// <param name="debugName">失敗時のログに出す名前</param>
    /// <returns>bool: 構築に成功したか</returns>
    bool Build(DirectXCommon *pDxCommon,
               const std::vector<StageReflection> &stages,
               const BuildOptions &options,
               const std::string &debugName);

    /// <summary>
    /// リフレクション1件からルートシグネチャを構築する（コンピュートシェーダー用の簡易版）
    /// </summary>
    /// <param name="pDxCommon">デバイス取得用</param>
    /// <param name="pReflection">対象シェーダーのリフレクション</param>
    /// <param name="samplerPresets">s0 から順に割り当てるサンプラー設定</param>
    /// <param name="debugName">失敗時のログに出す名前</param>
    /// <returns>bool: 構築に成功したか</returns>
    bool BuildFromReflection(DirectXCommon *pDxCommon,
                             ID3D12ShaderReflection *pReflection,
                             const std::vector<SamplerPreset> &samplerPresets,
                             const std::string &debugName);

    /// <summary>
    /// 構築したルートシグネチャを取得する
    /// </summary>
    ID3D12RootSignature *Get() const { return rootSignature_.Get(); }

    /// <summary>
    /// 指定レジスタに対応するルートパラメータ番号を取得する。
    ///
    /// 探索は「レジスタと可視性の完全一致 → レジスタだけで一意に決まるならそれ」の順。
    /// 同じレジスタが複数ステージに分かれていて可視性を省略した場合は、
    /// どちらか決められないので UINT_MAX を返す（黙って誤ったスロットへ差すのを防ぐ）。
    /// </summary>
    /// <param name="type">リソース種別</param>
    /// <param name="shaderRegister">レジスタ番号（b0 なら 0）</param>
    /// <param name="visibility">どのステージのものか。省略時は一意に決まる場合のみ有効</param>
    /// <returns>UINT: ルートパラメータ番号。見つからない・一意に決まらない場合は UINT_MAX</returns>
    UINT GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT shaderRegister,
                               D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) const;

    /// <summary>b# のルートパラメータ番号（見つからなければ UINT_MAX）</summary>
    UINT GetCbvIndex(UINT shaderRegister,
                     D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) const
    {
        return GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, shaderRegister, visibility);
    }
    /// <summary>t# のルートパラメータ番号（見つからなければ UINT_MAX）</summary>
    UINT GetSrvIndex(UINT shaderRegister,
                     D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) const
    {
        return GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, shaderRegister, visibility);
    }
    /// <summary>u# のルートパラメータ番号（見つからなければ UINT_MAX）</summary>
    UINT GetUavIndex(UINT shaderRegister,
                     D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) const
    {
        return GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, shaderRegister, visibility);
    }

    /// <summary>SRV をまとめたデスクリプタテーブルのルートパラメータ番号（Merged 時のみ意味を持つ）</summary>
    UINT GetSrvTableIndex() const { return srvTableIndex_; }

    /// <summary>UAV をまとめたデスクリプタテーブルのルートパラメータ番号（Merged 時のみ意味を持つ）</summary>
    UINT GetUavTableIndex() const { return uavTableIndex_; }

    /// <summary>シェーダーが宣言している SRV の数</summary>
    UINT GetSrvCount() const { return srvCount_; }

    /// <summary>シェーダーが宣言している UAV の数</summary>
    UINT GetUavCount() const { return uavCount_; }

    /// <summary>
    /// 生成されたルートパラメータの並びを文字列にする（例: "0:b0(PS) 1:t0(VS) 2:t0(PS)"）。
    /// どのレジスタが何番になったかの確認用
    /// </summary>
    /// <returns>std::string: ルートパラメータ番号順の一覧</returns>
    std::string DescribeLayout() const;

  private:
    /// <summary>リソース1件と、それが割り当てられたルートパラメータ番号</summary>
    struct Binding
    {
        D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        UINT shaderRegister = 0;
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
        UINT rootIndex = UINT_MAX;
    };

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::vector<Binding> bindings_;

    UINT srvTableIndex_ = UINT_MAX; // t# をまとめたテーブルのルートパラメータ番号
    UINT uavTableIndex_ = UINT_MAX; // u# をまとめたテーブルのルートパラメータ番号
    UINT srvCount_ = 0;
    UINT uavCount_ = 0;
};

/// <summary>
/// 頂点シェーダーのリフレクションから入力レイアウトを組み立てるヘルパー。
/// セマンティック名と使用しているコンポーネント数からフォーマットを復元するので、
/// C++ 側に D3D12_INPUT_ELEMENT_DESC を並べる必要がなくなる。
///
/// 【注意】フォーマットは HLSL の宣言（float3 なら 3成分）から決まる。
/// 頂点バッファ側の構造体と成分数が食い違っているとオフセットがずれるので、
/// HLSL と C++ の頂点構造体は必ず一致させること。
/// </summary>
class ShaderInputLayout
{
  public:
    /// <summary>
    /// 頂点シェーダーのリフレクションから入力要素を組み立てる。
    /// SV_ 付きのシステム値（SV_VertexID / SV_InstanceID など）は入力アセンブラを通らないので除外する。
    /// </summary>
    /// <param name="pVsReflection">頂点シェーダーのリフレクション</param>
    /// <param name="debugName">失敗時のログに出す名前</param>
    /// <returns>bool: 組み立てに成功したか</returns>
    bool BuildFromReflection(ID3D12ShaderReflection *pVsReflection, const std::string &debugName);

    /// <summary>
    /// PSO へ渡す入力レイアウトを取得する。
    /// 返り値は内部のバッファを指すので、この ShaderInputLayout より長生きさせないこと
    /// </summary>
    D3D12_INPUT_LAYOUT_DESC Get() const;

    /// <summary>要素数（0 なら入力なし）</summary>
    size_t GetElementCount() const { return elements_.size(); }

  private:
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements_;
    // SemanticName は const char* を保持するだけなので、文字列の実体をここで生かしておく
    std::vector<std::string> semanticNames_;
};
} // namespace Hagine
