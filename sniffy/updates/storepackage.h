#ifndef STOREPACKAGE_H
#define STOREPACKAGE_H

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace StorePackage {

// True when running with an MSIX package identity (Microsoft Store install).
// Store apps are updated by Windows, so the in-app installer must not be used.
inline bool isPackagedApp()
{
#ifdef Q_OS_WIN
    static const bool packaged = []() {
        using GetCurrentPackageFullNameFn = LONG(WINAPI *)(UINT32 *, PWSTR);
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr) {
            return false;
        }

        const auto function = reinterpret_cast<GetCurrentPackageFullNameFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "GetCurrentPackageFullName")));
        if (function == nullptr) {
            return false;
        }

        UINT32 length = 0;
        return function(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    }();
    return packaged;
#else
    return false;
#endif
}

} // namespace StorePackage

#endif // STOREPACKAGE_H
