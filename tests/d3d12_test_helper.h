#ifndef __VK3D12_TEST_HELPER_H__
#define __VK3D12_TEST_HELPER_H__

#define check_interface(a, b, c) check_interface_(__LINE__, (IUnknown *)a, b, c)
static void check_interface_(unsigned int line, IUnknown *iface, REFIID riid, bool supported)
{
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, riid, (void **)&unk);
    ok_(line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

static ULONG get_refcount(void *iface)
{
    IUnknown *unk = iface;
    IUnknown_AddRef(unk);
    return IUnknown_Release(unk);
}

#endif  // __VK3D12_TEST_HELPER_H__

