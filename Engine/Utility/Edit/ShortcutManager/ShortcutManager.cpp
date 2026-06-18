#include "ShortcutManager.h"
#include "Input.h"

namespace Hagine {
void ShortcutManager::RegisterShortcut(const std::string &name, BYTE key, std::function<void()> callback) {
    shortcuts_[name] = Shortcut{std::vector<BYTE>{key}, callback};
}

void ShortcutManager::RegisterShortcut(const std::string &name, const std::vector<BYTE> &keys, std::function<void()> callback) {
    shortcuts_[name] = Shortcut{keys, callback};
}

void ShortcutManager::Initialize(Input *input) {
    input_ = input;
}

void ShortcutManager::Update() {
    for (const auto &[name, shortcut] : shortcuts_) {
        // キーが1つだけのショートカットの場合
        if (shortcut.keys.size() == 1) {
            BYTE key = shortcut.keys[0];
            // 1回だけの入力判定にする
            if (Input::GetInstance()->TriggerKey(key) && shortcut.callback) {
                shortcut.callback();
            }
        }
        // 複数キーのショートカットの場合
        else {
            bool allPressed = true;
            for (BYTE key : shortcut.keys) {
                if (!Input::GetInstance()->PushKey(key)) {
                    allPressed = false;
                    break;
                }
            }
            if (allPressed && shortcut.callback) {
                shortcut.callback();
            }
        }
    }
}

void ShortcutManager::Finalize() {
    shortcuts_.clear();
}
} // namespace Hagine
