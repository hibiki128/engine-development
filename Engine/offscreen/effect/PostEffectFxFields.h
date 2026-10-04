#pragma once
#include "IPostEffectParams.h"
#include <cstddef>
#include <span>
#include <string>

namespace Hagine {

/// <summary>
/// OffScreen/Fx/ のエフェクトの定数バッファの先頭に置く共通部。
/// PostFxCommon.hlsli の POSTFX_FRAME_HEADER と同じ並び
/// </summary>
struct PostFxFrameHeader
{
    int width = 0;          // 処理する画像の幅
    int height = 0;         // 処理する画像の高さ
    float time = 0.0f;      // エフェクトが有効になってからの時間（秒）
    float deltaTime = 0.0f; // 前のフレームからの時間（秒）
};

/// <summary>調整項目の種類</summary>
enum class FxFieldType
{
    Section, // 見出し（値を持たない）
    Note,    // 説明の文（値を持たない）
    Float,   // 小数
    Int,     // 整数
    Toggle,  // オン/オフ（int で持つ。HLSL の bool と同じ4バイト）
    Combo,   // 選択肢（int で持つ）
    Float2,  // 2つの小数
    Float3,  // 3つの小数
    Color3,  // 色（RGB）
    Color4,  // 色（RGBA）
};

/// <summary>
/// 定数バッファの1項目ぶんの説明。
/// 「どこに・どんな型で・どう見せるか」を並べておけば、調整UI・保存・読み込み・既定値へ戻すは共通の処理で済む
/// </summary>
struct FxField
{
    FxFieldType type = FxFieldType::Float;
    const char *key = nullptr;     // 保存キー（値を持たない項目は nullptr）
    const char *label = nullptr;   // 表示名
    size_t offset = 0;             // Data の先頭からの位置（バイト）
    float speed = 0.01f;           // ドラッグ1ピクセルあたりの変化量
    float minValue = 0.0f;         // 下限
    float maxValue = 1.0f;         // 上限
    const char *tooltip = nullptr; // 説明（マウスを乗せたとき）
    const char *items = nullptr;   // Combo の選択肢（"A\0B\0C\0" のように \0 区切り）
};

// 項目を並べるときの書き方（Type は Data 構造体、member はそのメンバ名）
#define HAGINE_FX_SECTION(text) ::Hagine::FxField{::Hagine::FxFieldType::Section, nullptr, text}
#define HAGINE_FX_NOTE(text) ::Hagine::FxField{::Hagine::FxFieldType::Note, nullptr, text}
#define HAGINE_FX_FLOAT(Type, member, text, spd, lo, hi, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Float, #member, text, offsetof(Type, member), spd, lo, hi, tip}
#define HAGINE_FX_INT(Type, member, text, lo, hi, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Int, #member, text, offsetof(Type, member), 0.1f, lo, hi, tip}
#define HAGINE_FX_TOGGLE(Type, member, text, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Toggle, #member, text, offsetof(Type, member), 0.0f, 0.0f, 1.0f, tip}
#define HAGINE_FX_COMBO(Type, member, text, choices, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Combo, #member, text, offsetof(Type, member), 0.0f, 0.0f, 0.0f, tip, choices}
#define HAGINE_FX_FLOAT2(Type, member, text, spd, lo, hi, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Float2, #member, text, offsetof(Type, member), spd, lo, hi, tip}
#define HAGINE_FX_FLOAT3(Type, member, text, spd, lo, hi, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Float3, #member, text, offsetof(Type, member), spd, lo, hi, tip}
#define HAGINE_FX_COLOR3(Type, member, text, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Color3, #member, text, offsetof(Type, member), 0.0f, 0.0f, 1.0f, tip}
#define HAGINE_FX_COLOR4(Type, member, text, tip) \
    ::Hagine::FxField{::Hagine::FxFieldType::Color4, #member, text, offsetof(Type, member), 0.0f, 0.0f, 1.0f, tip}

/// <summary>
/// 項目の説明（FxField の並び）から、調整UI・保存・読み込みを行う共通処理
/// </summary>
namespace FxFields {

/// <summary>
/// 調整UIを出す。最後に「既定に戻す」ボタンも出す
/// </summary>
/// <param name="fields">項目の並び</param>
/// <param name="data">値の入っている構造体</param>
/// <param name="defaults">既定値の入っている同じ型の構造体</param>
/// <returns>bool: 何か変わったら true</returns>
bool DrawUI(std::span<const FxField> fields, void *data, const void *defaults);

/// <summary>
/// 値を保存する
/// </summary>
void Save(std::span<const FxField> fields, const void *data, DataHandler *handler, const std::string &prefix);

/// <summary>
/// 値を読み込む（保存されていない項目は既定値）
/// </summary>
void Load(std::span<const FxField> fields, void *data, const void *defaults, DataHandler *handler, const std::string &prefix);

/// <summary>
/// 項目の値をすべて既定値へ戻す
/// </summary>
void ResetToDefaults(std::span<const FxField> fields, void *data, const void *defaults);

} // namespace FxFields
} // namespace Hagine
