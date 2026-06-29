#include "FileSelectionService.h"
#include "../App/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <comdef.h>
#include <thread>
#include <chrono>

namespace Services
{
    // Helper function for dynamic COM dispatch calls
    static HRESULT InvokeCOM(IDispatch* pDisp, const wchar_t* name, WORD flags, VARIANT* pVar, VARIANTARG* args = nullptr, UINT argCount = 0)
    {
        if (!pDisp) return E_POINTER;
        DISPID dispid;
        OLECHAR* szName = const_cast<OLECHAR*>(name);
        HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &szName, 1, LOCALE_USER_DEFAULT, &dispid);
        if (FAILED(hr)) return hr;

        DISPPARAMS params = { args, nullptr, argCount, 0 };
        return pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, pVar, NULL, NULL);
    }

    static bool IsExplorerOrDesktopWindow(HWND hwnd, std::wstring& outClassName)
    {
        wchar_t className[256];
        if (GetClassNameW(hwnd, className, 256) > 0)
        {
            outClassName = className;
            if (outClassName == L"CabinetWClass" || outClassName == L"ExploreWClass" ||
                outClassName == L"Progman" || outClassName == L"WorkerW")
            {
                return true;
            }
        }
        return false;
    }

    static bool IsPointInWindow(POINT pt, HWND targetRootHwnd, bool isDesktop)
    {
        HWND hwndAtPt = WindowFromPoint(pt);
        if (!hwndAtPt) return false;

        HWND rootAtPt = GetAncestor(hwndAtPt, GA_ROOT);
        if (!rootAtPt) rootAtPt = hwndAtPt;

        if (isDesktop)
        {
            wchar_t className[256] = { 0 };
            if (GetClassNameW(rootAtPt, className, 256) > 0)
            {
                std::wstring cls = className;
                return (cls == L"Progman" || cls == L"WorkerW");
            }
            return false;
        }
        else
        {
            return (rootAtPt == targetRootHwnd);
        }
    }

    std::vector<std::wstring> FileSelectionService::GetSelectedFiles(HWND hwnd, POINT clickPt, POINT popupCenter)
    {
        std::vector<std::wstring> selectedFiles;

        HWND rootHwnd = GetAncestor(hwnd, GA_ROOT);
        if (!rootHwnd) rootHwnd = hwnd;

        std::wstring className;
        if (!IsExplorerOrDesktopWindow(rootHwnd, className))
        {
            return selectedFiles;
        }

        bool isDesktop = (className == L"Progman" || className == L"WorkerW");

        // Verify points and boundaries for selection
        if (isDesktop)
        {
            // Desktop file selection: check if original click point is on the desktop window
            if (!IsPointInWindow(clickPt, rootHwnd, true))
            {
                return selectedFiles;
            }
        }
        else
        {
            // Folder file selection: check if original click and clamped popup center are inside the folder window
            if (!IsPointInWindow(clickPt, rootHwnd, false) || !IsPointInWindow(popupCenter, rootHwnd, false))
            {
                return selectedFiles;
            }
        }

        // Iterate Shell Windows to retrieve the document SelectedItems
        IShellWindows* psw = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, IID_IShellWindows, (void**)&psw);
        if (SUCCEEDED(hr) && psw)
        {
            IDispatch* pdisp = nullptr;

            if (isDesktop)
            {
                VARIANT vLoc = {};
                vLoc.vt = VT_I4;
                vLoc.lVal = 0;

                VARIANT vEmpty = {};
                VariantInit(&vEmpty);

                long lHwnd = 0;
                hr = psw->FindWindowSW(&vLoc, &vEmpty, 8, &lHwnd, 1, &pdisp); // SWC_DESKTOP=8, SWFO_NEEDDISPATCH=1
            }
            else
            {
                long count = 0;
                if (SUCCEEDED(psw->get_Count(&count)))
                {
                    for (long i = 0; i < count; ++i)
                    {
                        VARIANT vIndex = {};
                        vIndex.vt = VT_I4;
                        vIndex.lVal = i;

                        IDispatch* pdispWindow = nullptr;
                        if (SUCCEEDED(psw->Item(vIndex, &pdispWindow)) && pdispWindow)
                        {
                            VARIANT varHwnd = {};
                            if (SUCCEEDED(InvokeCOM(pdispWindow, L"HWND", DISPATCH_PROPERTYGET, &varHwnd)))
                            {
                                HWND w_hwnd = nullptr;
                                if (varHwnd.vt == VT_I4) w_hwnd = (HWND)(LONG_PTR)varHwnd.lVal;
                                else if (varHwnd.vt == VT_I8) w_hwnd = (HWND)(LONG_PTR)varHwnd.llVal;
                                else if (varHwnd.vt == VT_INT) w_hwnd = (HWND)(LONG_PTR)varHwnd.intVal;

                                HWND w_root = GetAncestor(w_hwnd, GA_ROOT);
                                if (!w_root) w_root = w_hwnd;

                                if (w_root == rootHwnd || w_hwnd == rootHwnd)
                                {
                                    pdisp = pdispWindow;
                                    break;
                                }
                            }
                            pdispWindow->Release();
                        }
                    }
                }
            }

            if (pdisp)
            {
                VARIANT varDoc = {};
                if (SUCCEEDED(InvokeCOM(pdisp, L"Document", DISPATCH_PROPERTYGET, &varDoc)) && varDoc.vt == VT_DISPATCH && varDoc.pdispVal)
                {
                    VARIANT varSelected = {};
                    if (SUCCEEDED(InvokeCOM(varDoc.pdispVal, L"SelectedItems", DISPATCH_METHOD | DISPATCH_PROPERTYGET, &varSelected)) && varSelected.vt == VT_DISPATCH && varSelected.pdispVal)
                    {
                        VARIANT varCount = {};
                        if (SUCCEEDED(InvokeCOM(varSelected.pdispVal, L"Count", DISPATCH_PROPERTYGET, &varCount)))
                        {
                            long itemsCount = 0;
                            if (varCount.vt == VT_I4) itemsCount = varCount.lVal;

                            for (long i = 0; i < itemsCount; ++i)
                            {
                                VARIANTARG arg = {};
                                VariantInit(&arg);
                                arg.vt = VT_I4;
                                arg.lVal = i;

                                VARIANT varItem = {};
                                if (SUCCEEDED(InvokeCOM(varSelected.pdispVal, L"Item", DISPATCH_METHOD | DISPATCH_PROPERTYGET, &varItem, &arg, 1)) && varItem.vt == VT_DISPATCH && varItem.pdispVal)
                                {
                                    VARIANT varPath = {};
                                    if (SUCCEEDED(InvokeCOM(varItem.pdispVal, L"Path", DISPATCH_PROPERTYGET, &varPath)) && varPath.vt == VT_BSTR)
                                    {
                                        selectedFiles.push_back(varPath.bstrVal);
                                        SysFreeString(varPath.bstrVal);
                                    }
                                    varItem.pdispVal->Release();
                                }
                            }
                        }
                        varSelected.pdispVal->Release();
                    }
                    varDoc.pdispVal->Release();
                }
                pdisp->Release();
            }
            psw->Release();
        }

        return selectedFiles;
    }

    void FileSelectionService::CaptureSelectedFilesAsync(HWND activeHwnd, POINT clickPt, POINT popupCenter, std::function<void(const SelectionContext&)> callback)
    {
        auto capturedTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

        std::thread([activeHwnd, clickPt, popupCenter, capturedTime, callback]() {
            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            bool needsUninit = (hr == S_OK || hr == S_FALSE);

            std::vector<std::wstring> files = GetSelectedFiles(activeHwnd, clickPt, popupCenter);

            if (needsUninit)
            {
                CoUninitialize();
            }

            SelectionContext ctx;
            ctx.filePaths = std::move(files);
            ctx.sourceHwnd = activeHwnd;
            ctx.capturedTime = capturedTime;
            ctx.isPending = false;

            if (callback)
            {
                callback(ctx);
            }
        }).detach();
    }
}
