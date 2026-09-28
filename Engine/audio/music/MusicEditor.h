#pragma once
#ifdef USE_IMGUI
#include "DrumMachineInstrument.h"
#include "MusicEngine.h"
#include "MusicTheory.h"
#include "SamplerInstrument.h"
#include "SynthInstrument.h"

#include <imgui.h>
#include <array>
#include <set>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 音楽制作ウィンドウ。鍵盤を弾く／ピアノロールで打ち込む／音色を作る／
/// .wav を読み込んで加工する／曲を .wav へ書き出す、をこの1枚でまかなう。
///
/// 実際の発音・再生は MusicEngine が受け持ち、ここは操作と表示だけを担当する。
/// </summary>
class MusicEditor
{
  private:
    MusicEditor() = default;
    ~MusicEditor() = default;
    MusicEditor(const MusicEditor &) = delete;
    MusicEditor &operator=(const MusicEditor &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static MusicEditor *GetInstance();

    /// <summary>
    /// ウィンドウを描画する。ウィンドウの開閉自体もここで面倒を見る
    /// </summary>
    /// <param name="open">表示フラグ。閉じるボタンで false になる</param>
    void Draw(bool *open);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// ピアノロール上でいま何を掴んでいるか
    enum class DragMode
    {
        None,
        MoveNotes,   // 選択したノートを動かす
        ResizeNote,  // ノートの長さを変える
        BoxSelect,   // 範囲で選ぶ
        Playhead,    // 再生位置をなぞる
        LoopRange,   // ループ範囲を引く
    };

    /// ピアノロールの1フレームぶんの座標計算結果
    struct RollLayout
    {
        ImVec2 contentOrigin = {};  // スクロールを含んだ内容左上（画面座標）
        ImVec2 windowPos = {};      // 子ウィンドウの左上（画面座標）
        ImVec2 windowSize = {};     // 子ウィンドウの大きさ
        float keyColumnWidth = 0.0f;
        float rulerHeight = 0.0f;

        /// ティック位置を画面 X 座標へ
        float TickToX(double tick) const
        {
            return contentOrigin.x + keyColumnWidth + static_cast<float>(tick) * pixelsPerTick;
        }
        /// ノート番号を画面 Y 座標（行の上端）へ
        float NoteToY(int note) const
        {
            return contentOrigin.y + rulerHeight +
                   static_cast<float>(MusicConst::kHighestNote - note) * rowHeight;
        }
        /// 画面 X 座標をティック位置へ
        double XToTick(float x) const
        {
            return static_cast<double>(x - contentOrigin.x - keyColumnWidth) / pixelsPerTick;
        }
        /// 画面 Y 座標をノート番号へ
        int YToNote(float y) const
        {
            const float local = y - contentOrigin.y - rulerHeight;
            return MusicConst::kHighestNote - static_cast<int>(std::floor(local / rowHeight));
        }

        float pixelsPerTick = 0.5f;
        float rowHeight = 14.0f;
    };

    /// ====================================
    /// private method
    /// ====================================

    // --- 画面の各部（MusicEditor.cpp） ---

    /// メニューバー（ファイル・編集・書き出し）
    void DrawMenuBar();

    /// 再生・録音・テンポなどの操作列
    void DrawTransport();

    /// トラック一覧（左ペイン）
    void DrawTrackList();

    /// 下部の鍵盤。マウスで弾ける
    void DrawKeyboardBar();

    /// プロジェクトの保存・読み込み・書き出しのダイアログ
    void DrawProjectDialogs();

    /// PCキーボードでの演奏を処理する
    void HandlePcKeyboard();

    /// 音を鳴らす（重複して押されても1回だけ鳴らす）
    void PressNote(int note, float velocity);

    /// 音を止める
    void ReleaseNote(int note);

    /// 短く鳴らして確認する（ノートを置いたときなど）
    void PreviewNote(int note, float velocity);

    /// 鳴らしっぱなしの確認音を時間で止める
    void UpdatePreviewNotes();

    /// 今鳴らしている音をすべて止める
    void ReleaseAllHeldNotes();

    /// いま編集対象のトラック
    MusicTrack *GetSelectedTrack() const;

    // --- ピアノロール（MusicEditorPianoRoll.cpp） ---

    /// ピアノロール本体
    void DrawPianoRoll();

    /// 目盛りと小節線
    void DrawRollGrid(ImDrawList *drawList, const RollLayout &layout, int totalTicks);

    /// ノートの長方形
    void DrawRollNotes(ImDrawList *drawList, const RollLayout &layout);

    /// 左端の鍵盤列
    void DrawRollKeyColumn(ImDrawList *drawList, const RollLayout &layout);

    /// 上端の小節目盛り
    void DrawRollRuler(ImDrawList *drawList, const RollLayout &layout, int totalTicks);

    /// マウス操作（ノートの追加・移動・長さ変更・削除・範囲選択）
    void HandleRollInput(const RollLayout &layout, bool canvasHovered);

    /// 選択中のノートを消す
    void DeleteSelectedNotes();

    /// グリッドに合わせて位置を丸める
    int SnapToGrid(double tick) const;

    /// グリッド1目盛りのティック数
    int GetGridTicks() const;

    // --- 楽器パネル（MusicEditorInstrument.cpp） ---

    /// 選択中トラックの音色編集
    void DrawInstrumentPanel();

    /// シンセの音色編集
    void DrawSynthPanel(SynthInstrument *synth);

    /// ドラムマシンの音色編集
    void DrawDrumPanel(DrumMachineInstrument *drum);

    /// サンプラー（.wav）の編集
    void DrawSamplerPanel(SamplerInstrument *sampler);

    /// 波形を折れ線で描く
    void DrawWaveformPreview(const char *id, const AudioClip &clip, float height);

    /// sounds ルート配下の .wav 一覧を作り直す
    void RescanWavFiles();

    /// .wav 一覧から選ばせる（選ばれたらそのパスを返す）
    bool DrawWavPicker(const char *id, std::string &outPath);

    // --- 作曲アシスト・エフェクト（MusicEditorAssist.cpp） ---

    /// キーとスケールの選択、コード入力、コード進行の流し込み
    void DrawCompositionAssist();

    /// やまびこ・残響・アルペジエータの設定
    void DrawEffectsPanel();

    /// <summary>
    /// 選んだコード進行を、選択中トラックへ1小節ずつ書き込む
    /// </summary>
    /// <param name="startTick">書き始める位置</param>
    void ApplyProgression(int startTick);

  private:
    /// ====================================
    /// private variables
    /// ====================================

    // --- 選択状態 ---
    int selectedTrack_ = 0;
    std::set<uint32_t> selectedNotes_;
    int selectedPad_ = 0;      // ドラム／サンプラーで編集中のパッド
    int hoveredNote_ = -1;     // 鍵盤列でホバー中のノート（表示用）

    // --- 打ち込みの設定 ---
    int gridDivision_ = 4;             // 1拍あたりの分割数（4 = 16分音符）
    bool snapEnabled_ = true;          // グリッドへそろえるか
    int defaultNoteLength_ = MusicConst::kTicksPerBeat / 4;
    float noteVelocity_ = 0.8f;        // 置くノートの強さ

    // --- 作曲アシスト ---
    int scaleRoot_ = 0;                                    // キーの主音 (0=C)
    int scaleType_ = static_cast<int>(ScaleType::Major);   // 使う音の並び
    bool highlightScale_ = true;                           // ピアノロールで使える音を目立たせる
    bool chordMode_ = false;                               // クリックで和音を置くか
    int chordNoteCount_ = 3;                               // 和音に重ねる音の数
    int progressionIndex_ = 0;                             // 選んでいるコード進行
    int progressionOctave_ = 3;                            // 進行を書き込むオクターブ

    // --- 表示の設定 ---
    float pixelsPerTick_ = 0.55f;
    float rowHeight_ = 13.0f;
    bool showOtherTracks_ = true;  // 他トラックのノートを薄く出す
    bool followPlayhead_ = true;   // 再生に合わせて横スクロールする
    bool rollInitialized_ = false; // 初回だけ中央の音域までスクロールする
    float selectionVelocity_ = 0.8f; // 選択中ノートへまとめて設定する強さ

    // --- 演奏 ---
    bool pcKeyboardEnabled_ = true;
    int keyboardOctave_ = 4;              // PCキーボードの基準オクターブ
    float playVelocity_ = 0.85f;
    std::array<int, 128> noteRefCount_{}; // 同じ音を複数の操作で押したときの重なり数
    std::array<bool, 64> pcKeyPressed_{}; // PCキーボードの各キーの押し下げ状態
    std::array<int, 64> pcKeyNote_{};     // そのキーで鳴らし始めた音（離すときに使う）
    std::vector<int> mouseHeldNotes_;     // 鍵盤をマウスで押している音
    std::vector<std::pair<int, float>> previewNotes_; // 確認用に鳴らした音と残り時間

    // --- ドラッグ状態 ---
    DragMode dragMode_ = DragMode::None;
    uint32_t dragNoteId_ = 0;
    int dragStartTick_ = 0;
    int dragStartNote_ = 0;
    int dragLastTickDelta_ = 0;
    int dragLastNoteDelta_ = 0;
    ImVec2 boxSelectStart_ = {};
    ImVec2 boxSelectEnd_ = {};
    bool draggingNewNote_ = false;

    // --- 波形表示のキャッシュ（毎フレーム作り直すと重いので変化したときだけ作る） ---
    const void *waveformOwner_ = nullptr;
    size_t waveformSize_ = 0;
    bool waveformDirty_ = true;
    std::vector<float> waveformMin_;
    std::vector<float> waveformMax_;

    // --- .wav 加工の作業値 ---
    float trimStartSeconds_ = 0.0f;
    float trimEndSeconds_ = 0.0f;
    float fadeInSeconds_ = 0.0f;
    float fadeOutSeconds_ = 0.0f;
    float sampleGain_ = 1.0f;
    std::string wavFilterText_;

    // --- ファイル操作 ---
    std::string projectFileName_ = "NewSong";
    std::string exportFileName_ = "NewSong";
    float exportTailSeconds_ = 2.0f;
    std::vector<std::string> projectFiles_;
    std::vector<std::string> wavFiles_;
    bool wavScanned_ = false;
    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    // --- キーボード入力の横取り ---
    bool keyboardCaptured_ = false;
};

} // namespace Hagine
#endif // USE_IMGUI
