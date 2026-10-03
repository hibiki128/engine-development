#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {

/// <summary>
/// 割り当て1つの入力元
/// </summary>
enum class InputSource : uint8_t
{
    Key = 0,       // キーボード（DIK_ コード）
    PadButton = 1, // パッドのボタン（XINPUT_GAMEPAD_ のビット）
    PadAxis = 2,   // パッドのスティック・トリガー（InputPadAxis）
    Mouse = 3,     // マウスのボタン（0=左 1=右 2=中）
};

/// <summary>
/// パッドの軸（InputSource::PadAxis の code）
/// </summary>
enum class InputPadAxis : uint8_t
{
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

/// <summary>
/// 行動への割り当て1つ（「ジャンプ = Space」「ジャンプ = パッドの A」のように1つの行動に複数付けられる）
/// </summary>
struct InputBinding
{
    InputSource source = InputSource::Key;
    uint16_t code = 0;  // 入力元ごとの番号（DIK / XINPUT_GAMEPAD_ / InputPadAxis / マウスのボタン）
    float scale = 1.0f; // 値に掛ける向き（軸の行動で、左キー = -1 / 右キー = +1 のように使う）

    bool operator==(const InputBinding &other) const
    {
        return source == other.source && code == other.code && scale == other.scale;
    }
};

/// <summary>
/// 行動の登録内容
/// </summary>
struct InputActionDesc
{
    std::string name;                     // コードから引く名前（"Jump" など。保存のキーにもなる）
    std::string label;                    // 画面に出す名前（"ジャンプ"）
    std::string category = "ゲーム";       // 窓での見出し（同じ見出しの中で割り当てが重なると知らせる）
    bool isAxis = false;                  // 軸（-1〜1 の値）として使うか。false ならボタン
    std::vector<InputBinding> defaults;   // 既定の割り当て
};

/// <summary>
/// 入力を「行動の名前」で扱う（キーコンフィグ）。
///
/// ゲーム側は起動時に Register で行動と既定の割り当てを登録し、あとは
/// IsTriggered("Jump") / GetValue("MoveX") のように名前で問い合わせる。キーかパッドかを気にしなくてよい。
/// 割り当ては「入力（キーコンフィグ）」窓で付け替えて jsons/Settings/InputBindings.json に保存でき、
/// 次に起動したときは保存した割り当てが既定より優先される。
/// Engine はゲームの行動名を知らない（登録するのはゲーム側）。
/// </summary>
class InputActions
{
  public:
    static InputActions *GetInstance();

    /// <summary>
    /// 行動を登録する（同じ名前なら上書き）。保存した割り当てがあればそちらを使う
    /// </summary>
    void Register(const InputActionDesc &desc);

    /// <summary>行動を外す</summary>
    void Unregister(const std::string &name);

    /// <summary>
    /// 全行動の今の値を求める。Input::Update の直後に1回呼ぶ（Framework が呼ぶ）
    /// </summary>
    void Update();

    /// <summary>押している間 true（軸の行動は値の大きさが半分を超えている間）</summary>
    bool IsPressed(const std::string &name) const;
    /// <summary>押した瞬間だけ true</summary>
    bool IsTriggered(const std::string &name) const;
    /// <summary>離した瞬間だけ true</summary>
    bool IsReleased(const std::string &name) const;
    /// <summary>値（ボタンは 0 か 1、軸は -1〜1）</summary>
    float GetValue(const std::string &name) const;

    /// <summary>割り当ての取得・付け替え（窓や設定画面から）</summary>
    const std::vector<InputBinding> *GetBindings(const std::string &name) const;
    void SetBindings(const std::string &name, const std::vector<InputBinding> &bindings);
    void ResetToDefault(const std::string &name);
    void ResetAllToDefault();

    /// <summary>今の割り当てを保存する / 保存した物を読み直す</summary>
    void Save();
    void Load();

    /// <summary>割り当ての表示名（"Space" / "パッド A" / "左スティック →" など）</summary>
    static std::string BindingName(const InputBinding &binding);

#ifdef USE_IMGUI
    /// <summary>
    /// 「入力（キーコンフィグ）」窓の中身。窓の Begin/End は呼び出し元
    /// </summary>
    void DrawImGui();
#endif // USE_IMGUI

  private:
    InputActions() = default;
    ~InputActions() = default;
    InputActions(const InputActions &) = delete;
    InputActions &operator=(const InputActions &) = delete;

    struct Action
    {
        InputActionDesc desc;
        std::vector<InputBinding> bindings; // 今の割り当て
        float value = 0.0f;                 // 今の値
        bool pressed = false;               // 今押しているか
        bool pressedPrevious = false;       // 前のフレームに押していたか
    };

    /// <summary>割り当て1つの今の値（ボタンは 0 か scale、軸は値×scale）</summary>
    static float BindingValue(const InputBinding &binding);

    const Action *Find(const std::string &name) const;
    std::string SavePath() const;

#ifdef USE_IMGUI
    /// <summary>付け替えの待ち受け（次に押された入力をその割り当てにする）</summary>
    bool CaptureNextInput(InputBinding &outBinding);
#endif // USE_IMGUI

    std::unordered_map<std::string, Action> actions_;
    std::vector<std::string> order_;                                      // 登録した順（窓の並び）
    std::unordered_map<std::string, std::vector<InputBinding>> saved_;    // 保存から読んだ割り当て（まだ登録されていない物も持つ）
    bool loaded_ = false;

#ifdef USE_IMGUI
    std::string listeningAction_;  // 付け替えを待ち受けている行動（空なら待ち受けていない）
    int listeningSlot_ = -1;       // 何番目の割り当てか（範囲外なら追加）
    float listeningScale_ = 1.0f;  // 軸の行動で、足す向き
    int listeningFrames_ = 0;      // 待ち受けを始めてからのフレーム（押したクリックを拾わないため）
    bool dirty_ = false;           // 保存していない変更がある
#endif // USE_IMGUI
};

} // namespace Hagine
