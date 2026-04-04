#include "AppWindow.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	return AppWindow::Run() ? 0 : 1;
}
