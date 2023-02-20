#include "D3DProject.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPSTR,
    _In_ int nCmdShow)
{
    D3DProject sample(1280, 720, TEXT("D3DProject"));
    return Win32Application::Run(&sample, hInstance, nCmdShow);
}