#include "App/Application.h"
#include <Windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    Application app(hInstance);
    return app.Run();
}
