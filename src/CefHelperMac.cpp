#include "SonoCefApp.h"

#include "include/wrapper/cef_library_loader.h"

// macOS CEF uses separate Helper app bundles for renderer/GPU/plugin
// subprocesses. This binary loads the framework at runtime and runs the
// requested subprocess with the same sono renderer bridge as the main app.
int main(int argc, char *argv[])
{
    CefScopedLibraryLoader libraryLoader;
    if (!libraryLoader.LoadInHelper())
        return 1;

    CefMainArgs args(argc, argv);
    CefRefPtr<SonoCefApp> app = new SonoCefApp();
    return CefExecuteProcess(args, app.get(), nullptr);
}
