#include "SceneFactory.h"
#include "Application/Scene/ClearScene.h"
#include "Application/Scene/DemoScene.h"
#include "Application/Scene/GameScene.h"
#include "Application/Scene/SelectScene.h"
#include "Application/Scene/TitleScene.h"
#include "Application/Scene/TutorialScene.h"

namespace Hagine {
std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string &sceneName) {
    if (sceneName == "TITLE")
        return std::make_unique<TitleScene>();
    if (sceneName == "SELECT")
        return std::make_unique<SelectScene>();
    if (sceneName == "GAME")
        return std::make_unique<GameScene>();
    if (sceneName == "CLEAR")
        return std::make_unique<ClearScene>();
    if (sceneName == "DEMO")
        return std::make_unique<DemoScene>();
    if (sceneName == "TUTORIAL")
        return std::make_unique<TutorialScene>();
    return nullptr;
}
} // namespace Hagine
