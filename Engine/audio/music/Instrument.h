#pragma once
#include "MusicTypes.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace Hagine {

/// <summary>
/// 楽器の種類。保存データの復元と、エディタの表示切り替えに使う
/// </summary>
enum class InstrumentKind
{
    Synth,       // 波形から音を作るシンセ（ピアノ・ベース・リードなど）
    DrumMachine, // 打楽器を合成で鳴らすドラムマシン
    Sampler,     // .wav を鳴らすサンプラー
};

/// <summary>
/// 音を生成するものの共通インターフェース。
///
/// NoteOn / NoteOff / Render はいずれもミキサースレッドから呼ばれる。
/// MusicEngine がロックを取ったうえで呼ぶので、実装側で同期を取る必要はないが、
/// 代わりに Render の中でメモリ確保やファイル入出力をしないこと（音が途切れる）。
/// </summary>
class Instrument
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    virtual ~Instrument() = default;

    /// <summary>
    /// 発音を開始する
    /// </summary>
    /// <param name="note">ノート番号</param>
    /// <param name="velocity">強さ (0〜1)</param>
    virtual void NoteOn(int note, float velocity) = 0;

    /// <summary>
    /// 発音を終了する（リリースへ移る）
    /// </summary>
    /// <param name="note">ノート番号</param>
    virtual void NoteOff(int note) = 0;

    /// <summary>
    /// 鳴っている音をすべて止める
    /// </summary>
    virtual void AllNotesOff() = 0;

    /// <summary>
    /// 音声を生成して out へ「加算」する
    /// </summary>
    /// <param name="out">出力先。ステレオインターリーブ（frames * 2 個の float）</param>
    /// <param name="frames">生成するフレーム数</param>
    virtual void Render(float *out, uint32_t frames) = 0;

    /// <summary>
    /// 楽器の種類を返す
    /// </summary>
    /// <returns>InstrumentKind: 種類</returns>
    virtual InstrumentKind GetKind() const = 0;

    /// <summary>
    /// 現在鳴っている音の数を返す（エディタの表示用）
    /// </summary>
    /// <returns>int: 発音数</returns>
    virtual int GetActiveVoiceCount() const = 0;

    /// <summary>
    /// 音程を持つ楽器かどうか。false ならエディタは鍵盤ではなくパッド一覧を出す
    /// </summary>
    /// <returns>bool: 音程を持つなら true</returns>
    virtual bool IsPitched() const { return true; }

    /// <summary>
    /// そのノート番号に割り当てられた名前を返す（ドラムのパッド名など）。
    /// 空文字を返した場合、呼び出し側は音名（C4 など）を表示する
    /// </summary>
    /// <param name="note">ノート番号</param>
    /// <returns>std::string: 表示名。無ければ空文字</returns>
    virtual std::string GetNoteLabel(int note) const
    {
        (void)note;
        return std::string();
    }

    /// <summary>
    /// 設定を JSON へ書き出す
    /// </summary>
    /// <param name="outJson">書き出し先</param>
    virtual void SaveTo(nlohmann::json &outJson) const = 0;

    /// <summary>
    /// 設定を JSON から読み込む
    /// </summary>
    /// <param name="json">読み込み元</param>
    virtual void LoadFrom(const nlohmann::json &json) = 0;

    /// <summary>
    /// 種類に応じた楽器を生成する
    /// </summary>
    /// <param name="kind">生成する種類</param>
    /// <returns>std::unique_ptr&lt;Instrument&gt;: 生成した楽器</returns>
    static std::unique_ptr<Instrument> Create(InstrumentKind kind);
};

} // namespace Hagine
