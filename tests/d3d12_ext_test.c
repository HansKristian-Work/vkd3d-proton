#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#ifdef _MSC_VER
/* Used for M_PI */
#define _USE_MATH_DEFINES
#endif

#include "d3d12_crosstest.h"
#include "d3d12_test_helper.h"
#include "vkd3d_command_list_vkd3d_ext.h"
#include "vkd3d_device_vkd3d_ext.h"

static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER pfn_D3D12CreateVersionedRootSignatureDeserializer;
static PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE pfn_D3D12SerializeVersionedRootSignature;
PFN_D3D12_CREATE_DEVICE pfn_D3D12CreateDevice;
PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES pfn_D3D12EnableExperimentalFeatures;
PFN_D3D12_GET_DEBUG_INTERFACE pfn_D3D12GetDebugInterface;

/* It will test ID3D12DeviceExt by querying from ID3D12Device. Also newly added API. */
static void test_create_device_ext(void)
{
    VkPhysicalDevice physical_device = 0;
    ID3D12DeviceExt *ext_device;
    VkInstance instance = 0;
    VkDevice vk_device = 0;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }
      
    check_interface(device, &IID_ID3D12Device, true);
    check_interface(device, &IID_ID3D12DeviceExt, true);

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12DeviceExt, (void**)&ext_device);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12DeviceExt_GetVulkanHandles(ext_device, &instance, &physical_device, &vk_device);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(instance != 0 && physical_device != 0 && vk_device != 0, "Got invalid vulkan handles %#x, %#x, %#x \n", instance, physical_device, vk_device); 

    refcount = ID3D12DeviceExt_Release(ext_device);
    ok(refcount == 1, "Got unexpected refcount %#x.\n", refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

/* It will test ID3D12GraphicsCommandListExt by querying it from ID3D12CommandList and also calling newly added API. */
static void test_create_command_list_ext(void)
{
    ID3D12GraphicsCommandListExt *ext_command_list;
    ID3D12CommandAllocator *command_allocator;
    VkCommandBuffer vk_command_buffer = 0;
    ID3D12CommandList *command_list;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;
    
    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }
    
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            NULL, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);
    
    check_interface(command_list, &IID_ID3D12GraphicsCommandListExt, true);
    ID3D12CommandList_QueryInterface(command_list, &IID_ID3D12GraphicsCommandListExt, (void **)&ext_command_list);

    refcount = get_refcount(command_list);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    hr = ID3D12GraphicsCommandListExt_GetVulkanHandle(ext_command_list, &vk_command_buffer);
    ok(SUCCEEDED(hr), "Failed to get device from command list, hr %#x.\n", hr);
    ok(vk_command_buffer != 0, "Got invalid vulkan handle %#x \n", vk_command_buffer); 

    refcount = ID3D12GraphicsCommandListExt_Release(ext_command_list);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = ID3D12CommandList_Release(command_list);
    ok(refcount == 0, "Got unexpected refcount %u.\n", (unsigned int)refcount);
}

START_TEST(vkd3d_ext)
{
    pfn_D3D12CreateDevice = get_d3d12_pfn(D3D12CreateDevice);
    pfn_D3D12EnableExperimentalFeatures = get_d3d12_pfn(D3D12EnableExperimentalFeatures);
    pfn_D3D12GetDebugInterface = get_d3d12_pfn(D3D12GetDebugInterface);

    parse_args(argc, argv);
    enable_d3d12_debug_layer(argc, argv);
    init_adapter_info();

    pfn_D3D12CreateVersionedRootSignatureDeserializer = get_d3d12_pfn(D3D12CreateVersionedRootSignatureDeserializer);
    pfn_D3D12SerializeVersionedRootSignature = get_d3d12_pfn(D3D12SerializeVersionedRootSignature);
    /* Test newly added interface: ID3D12DeviceExt. */
    run_test(test_create_device_ext);
    /* Test newly added interface: ID3D12GraphicsCommandListExt. */
    run_test(test_create_command_list_ext);
}

