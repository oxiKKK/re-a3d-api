// Project-written documentation example. See first-application.md.
// Load a WAV and play it to the listener's right for three seconds.

#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include "ia3dapi.h"
#include <stdio.h>

typedef HRESULT (STDAPICALLTYPE *GetClassObjectProc)(REFCLSID, REFIID, void **);

static bool Check(HRESULT result, const char *operation)
{
    if (SUCCEEDED(result))
        return true;

    // Include the failed operation so setup errors are easy to locate.
    fprintf(stderr, "%s: 0x%08lX\n", operation, (unsigned long)result);
    return false;
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(window, message, w, l);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: first-application <a3dapi.dll> <input.wav>\n");
        return 2;
    }

    // Resolve the requested DLL before loading it. This example selects the
    // DLL explicitly and leaves the machine's COM registration unchanged.
    char dllPath[MAX_PATH];
    DWORD pathLength = GetFullPathNameA(argv[1], MAX_PATH, dllPath, NULL);
    if (!pathLength || pathLength >= MAX_PATH)
        return 1;

    // A3D exposes COM interfaces. Balance successful initialization with
    // CoUninitialize after releasing those interfaces.
    HRESULT comResult = CoInitialize(NULL);
    if (!Check(comResult, "CoInitialize"))
        return 1;

    // DirectSound needs an application window for its cooperative level.
    // Closing this window will end the playback loop early.
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "A3dDocumentationExample";
    RegisterClassA(&windowClass);

    HWND window = CreateWindowA(windowClass.lpszClassName, "A3D WAV example",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 180,
        NULL, NULL, instance, NULL);

    // Keep each acquired interface here so a failed setup step can use the
    // same cleanup path as successful playback.
    HMODULE module = NULL;
    IClassFactory *factory = NULL;
    IA3d5 *root = NULL;
    IA3dListener *listener = NULL;
    IA3dSource2 *source = NULL;
    int result = 1;

    // This block runs once. Each failure exits to the cleanup below.
    do
    {
        if (!window)
            break;

        ShowWindow(window, SW_SHOW);

        // 1. Load the DLL and obtain its COM class factory.
        module = LoadLibraryExA(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module)
        {
            fprintf(stderr, "LoadLibraryEx: %lu\n", GetLastError());
            break;
        }

        GetClassObjectProc getClassObject = reinterpret_cast<GetClassObjectProc>(
            GetProcAddress(module, "DllGetClassObject"));
        if (!getClassObject)
            break;

        if (!Check(getClassObject(CLSID_A3dApi, IID_IClassFactory,
                                  reinterpret_cast<void **>(&factory)),
                   "DllGetClassObject"))
            break;

        // 2. Create the A3D root and initialize direct-path rendering.
        if (!Check(factory->CreateInstance(NULL, IID_IA3d5,
                                           reinterpret_cast<void **>(&root)),
                   "CreateInstance"))
            break;

        if (!Check(root->InitEx(NULL, A3D_DIRECT_PATH_A3D,
                               A3DRENDERPREFS_DEFAULT, window, A3D_CL_NORMAL),
                   "InitEx"))
            break;

        // This target-specific compatibility call enables rendering in 677.
        // See first-application.md for the implementation reference.
        if (!Check(root->Compat(1000, 1), "Enable rendering"))
            break;

        if (!Check(root->SetOutputGain(0.5f), "SetOutputGain"))
            break;

        // 3. Place a stationary listener at the origin, facing negative Z
        // with positive Y pointing up. Positive X is the listener's right.
        if (!Check(root->QueryInterface(IID_IA3dListener,
                                       reinterpret_cast<void **>(&listener)),
                   "Listener interface"))
            break;

        if (!Check(listener->SetPosition3f(0, 0, 0), "Listener position"))
            break;

        if (!Check(listener->SetOrientation6f(0, 0, -1, 0, 1, 0),
                   "Listener orientation"))
            break;

        if (!Check(listener->SetVelocity3f(0, 0, 0), "Listener velocity"))
            break;

        // 4. Load the WAV into a positional source in front and to the right.
        // Loop it so even a short input plays for the full demonstration.
        if (!Check(root->NewSource(A3DSOURCE_TYPEDEFAULT, &source), "NewSource"))
            break;

        if (!Check(source->LoadFile(argv[2], A3DSOURCE_FORMAT_WAVE), "LoadFile"))
            break;

        if (!Check(source->SetGain(1.0f), "SetGain"))
            break;

        if (!Check(source->SetPosition3f(3, 0, -4), "Source position"))
            break;

        if (!Check(source->Play(A3D_LOOPED), "Play"))
            break;

        // 5. Process window messages and submit scene updates for three seconds.
        // Clear starts a scene submission; Flush sends the current controls.
        // Audio playback runs independently between these updates.
        result = 0;
        DWORD start = GetTickCount();
        bool quit = false;
        while (!quit && GetTickCount() - start < 3000)
        {
            MSG message;
            while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    quit = true;

                TranslateMessage(&message);
                DispatchMessageA(&message);
            }

            if (quit)
                break;

            if (!Check(root->Clear(), "Clear") || !Check(root->Flush(), "Flush"))
            {
                result = 1;
                break;
            }

            // Yield between updates; the mixer has its own scheduling.
            Sleep(16);
        }
    } while (false);

    // 6. Stop playback and release child interfaces before their root.
    // Ordinary reference counting is sufficient for this example.
    if (source)
    {
        if (!Check(source->Stop(), "Stop"))
            result = 1;

        source->Release();
    }

    if (listener)
        listener->Release();

    if (root)
        root->Release();

    if (factory)
        factory->Release();

    // Keep the module loaded until process exit; reference workers/lifetimes
    // are not assumed to permit an immediate FreeLibrary after Release.
    if (IsWindow(window))
        DestroyWindow(window);

    CoUninitialize();

    return result;
}
