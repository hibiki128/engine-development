#pragma once
#include "BehaviorTreeRegistry.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// ピンIDの決まり。ノードID n のピンは n*10 + 番号 + kOffset
///   1=入力 2=出力 3=成功 4=失敗 5,6=重み付きの最初の2本（3本目以降は 200000 から払い出す）
/// </summary>
namespace BTPin {
inline constexpr int kOffset = 100000;
inline constexpr int kExtraStart = 200000; // 追加した重み付き出力の払い出し開始
inline constexpr int Input(int nodeId) { return nodeId * 10 + 1 + kOffset; }
inline constexpr int Output(int nodeId) { return nodeId * 10 + 2 + kOffset; }
inline constexpr int Success(int nodeId) { return nodeId * 10 + 3 + kOffset; }
inline constexpr int Failure(int nodeId) { return nodeId * 10 + 4 + kOffset; }
inline constexpr int FirstWeighted(int nodeId) { return nodeId * 10 + 5 + kOffset; }
/// <summary>入力ピンからノードIDへ戻す</summary>
inline constexpr int NodeOfInput(int pinId) { return (pinId - kOffset - 1) / 10; }
/// <summary>決まった番号のピンなら 1〜6、追加の重み付き出力などは 0</summary>
inline constexpr int Slot(int pinId) { return (pinId >= kOffset && pinId < kExtraStart) ? (pinId - kOffset) % 10 : 0; }
} // namespace BTPin

/// <summary>
/// ノード同士の繋がり1本（出力ピン → 入力ピン）
/// </summary>
struct BTLinkData
{
    int id = 0;
    int start = 0; // 出力側のピン
    int end = 0;   // 入力側のピン
};

/// <summary>
/// ビヘイビアツリー1本分のデータ（ファイルの中身そのもの）。
/// 実行用のツリーは Build で組み立てる。
/// </summary>
struct BehaviorTreeAsset
{
    std::vector<BTNodeData> nodes;
    std::vector<BTLinkData> links;

    /// <summary>
    /// ファイルから読み込む（folder はデータ置き場からの相対フォルダ、file は拡張子なし）
    /// </summary>
    bool Load(const std::string &folder, const std::string &file);

    /// <summary>
    /// ファイルへ保存する
    /// </summary>
    void Save(const std::string &folder, const std::string &file) const;

    /// <summary>
    /// ノードを探す
    /// </summary>
    const BTNodeData *FindNode(int nodeId) const;
    BTNodeData *FindNode(int nodeId);

    /// <summary>
    /// 根のノード（入力ピンにどこからも繋がっていない最初のノード）。無ければ -1
    /// </summary>
    int FindRootNodeId() const;

    /// <summary>
    /// 出力ピンに繋がった子ノードのID（繋いだ順）
    /// </summary>
    std::vector<int> FindChildren(int outputPinId) const;

    /// <summary>
    /// 実行用のツリーを組み立てる。
    /// outInstances を渡すと「エディタのノードID → 実際に動くノード」の対応を返す（状態の色付け用）
    /// </summary>
    /// <param name="nodeId">組み立てを始めるノード（-1 で根から）</param>
    std::shared_ptr<BTNode> Build(int nodeId = -1, std::map<int, std::shared_ptr<BTNode>> *outInstances = nullptr) const;

    /// <summary>
    /// 次に使うノードID・リンクID・追加ピンID（今のデータから求める）
    /// </summary>
    int NextNodeId() const;
    int NextLinkId() const;
    int NextExtraPinId() const;

  private:
    std::shared_ptr<BTNode> BuildRecursive(int nodeId, std::map<int, std::shared_ptr<BTNode>> *outInstances, int depth) const;
};

/// <summary>
/// 保存済みのツリーを読み込んで、実行用のツリーを組み立てる（Release でも使う）
/// </summary>
class BehaviorTreeLoader
{
  public:
    /// <summary>
    /// 指定ファイルから実行用のツリーを組み立てて返す
    /// </summary>
    /// <param name="folder">データ置き場からのフォルダ名 (例: "BehaviorTree")</param>
    /// <param name="file">ファイル名 (.json なし)</param>
    /// <returns>根のノード（失敗時は nullptr）</returns>
    static std::shared_ptr<BTNode> LoadAndBuild(const std::string &folder, const std::string &file);
};

} // namespace Hagine
