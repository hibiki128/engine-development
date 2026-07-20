#include "ShortcutManager.h"
#include "Input.h"
#include "frame/Frame.h"

namespace Hagine {
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
