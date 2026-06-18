#include "MyGame.h"
#include "d3dx12.h"

using namespace Hagine;
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(152);

    std::unique_ptr<Framework> game = std::make_unique<MyGame>();

    game->Run();

    return 0;
}
