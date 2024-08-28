#include <stdio.h>
#include <windows.h>

#include "glide.h"

#define CLASS_NAME "My Window Class"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, PSTR cmd_line, int cmd_show)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_APPLICATION));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, "Call to RegisterClassEx failed!", "Windows Desktop Guided Tour", 0);

        return 1;
    }

    HWND window = CreateWindowEx(0, CLASS_NAME, "My test window", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, instance, NULL);
    if (window == NULL)
        fprintf(stderr, "Window creation failed\n");

    ShowWindow(window, cmd_show);
    SetForegroundWindow(window);
    SetFocus(window);

    grGlideInit();
    grSstWinOpen(window, 0, 0, 0, 0, 0, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;
    TCHAR greeting[] = "Hello, Windows desktop!";

    switch (message)
    {
        case WM_PAINT:
            ValidateRect(window, NULL);
            break;

        case WM_DESTROY:
            DestroyWindow(window);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(window, message, wParam, lParam);
            break;
    }

    return 0;
}
