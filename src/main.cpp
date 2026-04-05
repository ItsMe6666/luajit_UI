// 程式進入點：Unicode WinMain，實際初始化與主迴圈在 AppWindow::Run()。
#include "AppWindow.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	return AppWindow::Run() ? 0 : 1;
}
