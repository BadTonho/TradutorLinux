#include <windows.h>
#include <oleauto.h>

int main(void) {
    const wchar_t text[] = L"TradutorLinux";
    BSTR bstr = SysAllocString(text);
    if (bstr == NULL) {
        ExitProcess(1);
    }
    UINT len = SysStringLen(bstr);
    if (len != 13) {
        SysFreeString(bstr);
        ExitProcess(2);
    }

    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BSTR;
    var.bstrVal = bstr;
    VariantClear(&var);

    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle == NULL || stdout_handle == INVALID_HANDLE_VALUE) {
        ExitProcess(3);
    }

    const char output[] = "oleautbstr\n";
    DWORD written = 0;
    if (!WriteFile(stdout_handle, output, (DWORD)(sizeof(output) - 1), &written, NULL)) {
        ExitProcess(4);
    }

    ExitProcess(0);
    return 0;
}

