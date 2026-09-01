#include "ShortcutManager.h"
#include "Input.h"
#include "frame/Frame.h"
#include <algorithm>

namespace Hagine {
namespace {

// 組み合わせの判定で「押されていないこと」まで見る修飾キー
constexpr BYTE kModifierKeys[] = {DIK_LCONTROL, DIK_RCONTROL, DIK_LSHIFT, DIK_RSHIFT, DIK_LALT, DIK_RALT};

/// <summary>
/// 登録キーに含まれない修飾キーが押されているかを調べる。
///
/// 単純に「登録キーが全部押されているか」だけで判定すると、Ctrl+Shift+P を押したときに
/// Ctrl+P のショートカットまで一緒に発火してしまう（停止したのに即再生される等）。
/// 余計な修飾キーが押されている組み合わせは発火させない。
/// </summary>
/// <param name="keys">ショートカットに登録されたキー</param>
/// <returns>bool: 登録外の修飾キーが押されていれば true</returns>
bool HasExtraModifier(const std::vector<BYTE> &keys)
{
    for (BYTE modifier : kModifierKeys)
    {
        if (std::find(keys.begin(), keys.end(), modifier) != keys.end())
        {
            continue; // このショートカット自身が要求している修飾キー
        }
        if (Input::GetInstance()->PushKey(modifier))
        {
            return true;
        }
    }
    return false;
}

} // namespace

void ShortcutManager::RegisterShortcut(const std::string &name, BYTE key, std::function<void()> callback)
{
    shortcuts_[name] = Shortcut{std::vector<BYTE>{key}, callback};
}

void ShortcutManager::RegisterShortcut(const std::string &name, const std::vector<BYTE> &keys, std::function<void()> callback)
{
    shortcuts_[name] = Shortcut{keys, callback};
}

void ShortcutManager::Initialize(Input *input)
{
    pInput_ = input;
}

void ShortcutManager::Update()
{
    const float deltaTime = Frame::DeltaTime();

    for (auto &[name, shortcut] : shortcuts_)
    {
        // キーが1つだけのショートカットの場合
        if (shortcut.keys.size() == 1)
        {
            BYTE key = shortcut.keys[0];
            // 1回だけの入力判定にする
            if (Input::GetInstance()->TriggerKey(key) && shortcut.callback)
            {
                shortcut.callback();
            }
        }
        // 複数キーのショートカットの場合
        else
        {
            bool allPressed = true;
            for (BYTE key : shortcut.keys)
            {
                if (!Input::GetInstance()->PushKey(key))
                {
                    allPressed = false;
                    break;
                }
            }
            // 登録外の修飾キーが押されていたら別のショートカットとみなす
            if (allPressed && HasExtraModifier(shortcut.keys))
            {
                allPressed = false;
            }
            if (allPressed)
            {
                // 押しっぱなしで毎フレーム発火しないよう、発火後は一定時間待機する
                shortcut.repeatTimer -= deltaTime;
                if (shortcut.repeatTimer <= 0.0f && shortcut.callback)
                {
                    shortcut.callback();
                    shortcut.repeatTimer = kRepeatInterval;
                }
            }
            else
            {
                // キーを離したら待機を解除し、次の押下で即座に発火できるようにする
                shortcut.repeatTimer = 0.0f;
            }
        }
    }
}

void ShortcutManager::Finalize()
{
    shortcuts_.clear();
}
} // namespace Hagine
