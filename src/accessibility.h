#pragma once
// clang-format off
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
// clang-format on

namespace perf
{
// Read-only UIA value: includes metric labels, units, and every application column.
// Values are queried, not announced on each sample.
class ValueProvider final : public IRawElementProviderSimple, public IValueProvider
{
    LONG refs_ = 1;
    HWND window_;

  public:
    explicit ValueProvider(HWND w) : window_(w) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override
    {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple))
            *p = static_cast<IRawElementProviderSimple *>(this);
        else if (id == __uuidof(IValueProvider))
            *p = static_cast<IValueProvider *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&refs_);
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *p) override
    {
        if (!p)
            return E_POINTER;
        *p = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown **p) override
    {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (id == UIA_ValuePatternId)
        {
            *p = static_cast<IValueProvider *>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *p) override
    {
        if (!p)
            return E_POINTER;
        VariantInit(p);
        if (id == UIA_NamePropertyId)
        {
            p->vt = VT_BSTR;
            p->bstrVal = SysAllocString(L"Performance monitor");
        }
        else if (id == UIA_ControlTypePropertyId)
        {
            p->vt = VT_I4;
            p->lVal = UIA_PaneControlTypeId;
        }
        else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                 id == UIA_IsEnabledPropertyId)
        {
            p->vt = VT_BOOL;
            p->boolVal = VARIANT_TRUE;
        }
        else if (id == UIA_IsKeyboardFocusablePropertyId)
        {
            p->vt = VT_BOOL;
            p->boolVal = VARIANT_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **p) override
    {
        return UiaHostProviderFromHwnd(window_, p);
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override
    {
        return UIA_E_NOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR *p) override
    {
        if (!p)
            return E_POINTER;
        if (!IsWindow(window_))
            return UIA_E_ELEMENTNOTAVAILABLE;
        wchar_t text[4096]{};
        GetWindowTextW(window_, text, 4096);
        *p = SysAllocString(text);
        return *p ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *p) override
    {
        if (!p)
            return E_POINTER;
        *p = TRUE;
        return S_OK;
    }
};
} // namespace perf
