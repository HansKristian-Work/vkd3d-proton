/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define VK_USE_PLATFORM_XCB_KHR
#include <vkd3d.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

struct demo
{
    xcb_connection_t *connection;
    xcb_atom_t wm_protocols_atom;
    xcb_atom_t wm_delete_window_atom;
    xcb_key_symbols_t *xcb_keysyms;

    struct demo_window **windows;
    size_t windows_size;
    size_t window_count;
};

struct demo_window
{
    xcb_window_t window;
    struct demo *demo;

    void *user_data;
    void (*expose_func)(struct demo_window *window, void *user_data);
    void (*key_press_func)(struct demo_window *window, demo_key key, void *user_data);
};

struct demo_swapchain
{
    VkSurfaceKHR vk_surface;
    VkSwapchainKHR vk_swapchain;
    VkFence vk_fence;

    VkInstance vk_instance;
    VkDevice vk_device;
    VkQueue vk_queue;

    uint32_t current_buffer;
    unsigned int buffer_count;
    ID3D12Resource *buffers[1];
};

static inline xcb_atom_t demo_get_atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply;
    xcb_atom_t atom = XCB_NONE;

    cookie = xcb_intern_atom(c, 0, strlen(name), name);
    if ((reply = xcb_intern_atom_reply(c, cookie, NULL)))
    {
        atom = reply->atom;
        free(reply);
    }

    return atom;
}

static inline bool demo_add_window(struct demo *demo, struct demo_window *window)
{
    if (demo->window_count == demo->windows_size)
    {
        size_t new_capacity;
        void *new_elements;

        new_capacity = max(demo->windows_size * 2, 4);
        if (!(new_elements = realloc(demo->windows, new_capacity * sizeof(*demo->windows))))
            return false;
        demo->windows = new_elements;
        demo->windows_size = new_capacity;
    }

    demo->windows[demo->window_count++] = window;

    return true;
}

static inline void demo_remove_window(struct demo *demo, const struct demo_window *window)
{
    size_t i;

    for (i = 0; i < demo->window_count; ++i)
    {
        if (demo->windows[i] != window)
            continue;

        --demo->window_count;
        memmove(&demo->windows[i], &demo->windows[i + 1], (demo->window_count - i) * sizeof(*demo->windows));
        break;
    }
}

static inline struct demo_window *demo_find_window(struct demo *demo, xcb_window_t window)
{
    size_t i;

    for (i = 0; i < demo->window_count; ++i)
    {
        if (demo->windows[i]->window == window)
            return demo->windows[i];
    }

    return NULL;
}

static inline struct demo_window *demo_window_create(struct demo *demo, const char *title,
        unsigned int width, unsigned int height, void *user_data)
{
    static const uint32_t window_events = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;

    struct demo_window *window;
    xcb_size_hints_t hints;
    xcb_screen_t *screen;

    if (!(window = malloc(sizeof(*window))))
        return NULL;

    if (!demo_add_window(demo, window))
    {
        free(window);
        return NULL;
    }

    window->window = xcb_generate_id(demo->connection);
    window->demo = demo;
    window->user_data = user_data;
    window->expose_func = NULL;
    window->key_press_func = NULL;
    screen = xcb_setup_roots_iterator(xcb_get_setup(demo->connection)).data;
    xcb_create_window(demo->connection, XCB_COPY_FROM_PARENT, window->window, screen->root, 0, 0,
            width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            XCB_CW_EVENT_MASK, &window_events);
    xcb_change_property(demo->connection, XCB_PROP_MODE_REPLACE, window->window, XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING, 8, strlen(title), title);
    xcb_change_property(demo->connection, XCB_PROP_MODE_REPLACE, window->window, demo->wm_protocols_atom,
            XCB_ATOM_ATOM, 32, 1, &demo->wm_delete_window_atom);
    hints.flags = XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
    hints.min_width = width;
    hints.min_height = height;
    hints.max_width = width;
    hints.max_height = height;
    xcb_change_property(demo->connection, XCB_PROP_MODE_REPLACE, window->window, XCB_ATOM_WM_NORMAL_HINTS,
            XCB_ATOM_WM_SIZE_HINTS, 32, sizeof(hints) >> 2, &hints);

    xcb_map_window(demo->connection, window->window);

    xcb_flush(demo->connection);

    return window;
}

static inline void demo_window_destroy(struct demo_window *window)
{
    xcb_destroy_window(window->demo->connection, window->window);
    xcb_flush(window->demo->connection);
    demo_remove_window(window->demo, window);
    free(window);
}

static inline void demo_window_set_key_press_func(struct demo_window *window,
        void (*key_press_func)(struct demo_window *window, demo_key key, void *user_data))
{
    window->key_press_func = key_press_func;
}

static inline void demo_window_set_expose_func(struct demo_window *window,
        void (*expose_func)(struct demo_window *window, void *user_data))
{
    window->expose_func = expose_func;
}

static inline void demo_process_events(struct demo *demo)
{
    const struct xcb_client_message_event_t *client_message;
    struct xcb_key_press_event_t *key_press;
    xcb_generic_event_t *event;
    struct demo_window *window;
    xcb_keysym_t sym;

    xcb_flush(demo->connection);

    while (demo->window_count && (event = xcb_wait_for_event(demo->connection)))
    {
        switch (XCB_EVENT_RESPONSE_TYPE(event))
        {
            case XCB_EXPOSE:
                if ((window = demo_find_window(demo, ((struct xcb_expose_event_t *)event)->window))
                        && window->expose_func)
                    window->expose_func(window, window->user_data);
                break;

            case XCB_KEY_PRESS:
                key_press = (struct xcb_key_press_event_t *)event;
                if (!(window = demo_find_window(demo, key_press->event)) || !window->key_press_func)
                    break;
                sym = xcb_key_press_lookup_keysym(demo->xcb_keysyms, key_press, 0);
                window->key_press_func(window, sym, window->user_data);
                break;

            case XCB_CLIENT_MESSAGE:
                client_message = (xcb_client_message_event_t *)event;
                if (client_message->type == demo->wm_protocols_atom
                        && client_message->data.data32[0] == demo->wm_delete_window_atom
                        && (window = demo_find_window(demo, client_message->window)))
                    demo_window_destroy(window);
                break;
        }

        free(event);
    }
}

static inline bool demo_init(struct demo *demo)
{
    if (!(demo->connection = xcb_connect(NULL, NULL)))
        return false;
    if (xcb_connection_has_error(demo->connection) > 0)
        goto fail;
    if ((demo->wm_delete_window_atom = demo_get_atom(demo->connection, "WM_DELETE_WINDOW")) == XCB_NONE)
        goto fail;
    if ((demo->wm_protocols_atom = demo_get_atom(demo->connection, "WM_PROTOCOLS")) == XCB_NONE)
        goto fail;
    if (!(demo->xcb_keysyms = xcb_key_symbols_alloc(demo->connection)))
        goto fail;

    demo->windows = NULL;
    demo->windows_size = 0;
    demo->window_count = 0;

    return true;

fail:
    xcb_disconnect(demo->connection);
    return false;
}

static inline void demo_cleanup(struct demo *demo)
{
    free(demo->windows);
    xcb_key_symbols_free(demo->xcb_keysyms);
    xcb_disconnect(demo->connection);
}

static inline DXGI_FORMAT demo_get_srgb_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

static inline struct demo_swapchain *demo_swapchain_create(ID3D12CommandQueue *command_queue,
        struct demo_window *window, const struct demo_swapchain_desc *desc)
{
    struct VkSwapchainCreateInfoKHR vk_swapchain_desc;
    struct VkXcbSurfaceCreateInfoKHR surface_desc;
    VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
    uint32_t format_count, queue_family_index;
    VkSurfaceCapabilitiesKHR surface_caps;
    VkPhysicalDevice vk_physical_device;
    D3D12_RESOURCE_DESC resource_desc;
    VkFence vk_fence = VK_NULL_HANDLE;
    struct demo_swapchain *swapchain;
    unsigned int image_count, i, j;
    VkFenceCreateInfo fence_desc;
    VkSurfaceFormatKHR *formats;
    ID3D12Device *d3d12_device;
    VkSurfaceKHR vk_surface;
    VkInstance vk_instance;
    VkBool32 supported;
    VkDevice vk_device;
    VkImage *vk_images;
    VkFormat format;

    if ((format = vkd3d_get_vk_format(demo_get_srgb_format(desc->format))) == VK_FORMAT_UNDEFINED)
        return NULL;

    if (FAILED(ID3D12CommandQueue_GetDevice(command_queue, &IID_ID3D12Device, (void **)&d3d12_device)))
        return NULL;

    vk_instance = vkd3d_get_vk_instance(d3d12_device);
    vk_physical_device = vkd3d_get_vk_physical_device(d3d12_device);
    vk_device = vkd3d_get_vk_device(d3d12_device);

    surface_desc.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surface_desc.pNext = NULL;
    surface_desc.flags = 0;
    surface_desc.connection = window->demo->connection;
    surface_desc.window = window->window;
    if (vkCreateXcbSurfaceKHR(vk_instance, &surface_desc, NULL, &vk_surface) < 0)
    {
        ID3D12Device_Release(d3d12_device);
        return NULL;
    }

    queue_family_index = vkd3d_get_vk_queue_family_index(command_queue);
    if (vkGetPhysicalDeviceSurfaceSupportKHR(vk_physical_device,
            queue_family_index, vk_surface, &supported) < 0 || !supported)
        goto fail;

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface, &surface_caps) < 0)
        goto fail;

    if ((surface_caps.maxImageCount && desc->buffer_count > surface_caps.maxImageCount)
            || desc->buffer_count < surface_caps.minImageCount
            || desc->width > surface_caps.maxImageExtent.width || desc->width < surface_caps.minImageExtent.width
            || desc->height > surface_caps.maxImageExtent.height || desc->height < surface_caps.minImageExtent.height
            || !(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        goto fail;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, NULL) < 0
            || !format_count || !(formats = calloc(format_count, sizeof(*formats))))
        goto fail;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, formats) < 0)
    {
        free(formats);
        goto fail;
    }

    if (format_count != 1 || formats->format != VK_FORMAT_UNDEFINED
            || formats->colorSpace != VK_COLORSPACE_SRGB_NONLINEAR_KHR)
    {
        for (i = 0; i < format_count; ++i)
        {
            if (formats[i].format == format && formats[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
                break;
        }

        if (i == format_count)
        {
            free(formats);
            goto fail;
        }
    }

    free(formats);
    formats = NULL;

    vk_swapchain_desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    vk_swapchain_desc.pNext = NULL;
    vk_swapchain_desc.flags = 0;
    vk_swapchain_desc.surface = vk_surface;
    vk_swapchain_desc.minImageCount = desc->buffer_count;
    vk_swapchain_desc.imageFormat = format;
    vk_swapchain_desc.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    vk_swapchain_desc.imageExtent.width = desc->width;
    vk_swapchain_desc.imageExtent.height = desc->height;
    vk_swapchain_desc.imageArrayLayers = 1;
    vk_swapchain_desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk_swapchain_desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_swapchain_desc.queueFamilyIndexCount = 0;
    vk_swapchain_desc.pQueueFamilyIndices = NULL;
    vk_swapchain_desc.preTransform = surface_caps.currentTransform;
    vk_swapchain_desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vk_swapchain_desc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    vk_swapchain_desc.clipped = VK_TRUE;
    vk_swapchain_desc.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(vk_device, &vk_swapchain_desc, NULL, &vk_swapchain) < 0)
        goto fail;

    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = NULL;
    fence_desc.flags = 0;
    if (vkCreateFence(vk_device, &fence_desc, NULL, &vk_fence) < 0)
        goto fail;

    if (vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, NULL) < 0
            || !(vk_images = calloc(image_count, sizeof(*vk_images))))
        goto fail;

    if (vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, vk_images) < 0)
    {
        free(vk_images);
        goto fail;
    }

    if (!(swapchain = malloc(offsetof(struct demo_swapchain, buffers[image_count]))))
    {
        free(vk_images);
        goto fail;
    }
    swapchain->vk_surface = vk_surface;
    swapchain->vk_swapchain = vk_swapchain;
    swapchain->vk_fence = vk_fence;
    swapchain->vk_instance = vk_instance;
    swapchain->vk_device = vk_device;
    swapchain->vk_queue = vkd3d_get_vk_queue(command_queue);

    vkAcquireNextImageKHR(vk_device, vk_swapchain, UINT64_MAX,
            VK_NULL_HANDLE, vk_fence, &swapchain->current_buffer);
    vkWaitForFences(vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vk_device, 1, &vk_fence);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = desc->width;
    resource_desc.Height = desc->height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = desc->format;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (i = 0; i < image_count; ++i)
    {
        if (FAILED(vkd3d_create_image_resource(d3d12_device, &resource_desc, vk_images[i],
                VKD3D_RESOURCE_INITIAL_STATE_TRANSITION | VKD3D_RESOURCE_SWAPCHAIN_IMAGE,
                &swapchain->buffers[i])))
        {
            for (j = 0; j < i; ++j)
            {
                ID3D12Resource_Release(swapchain->buffers[j]);
            }
            free(swapchain);
            free(vk_images);
            goto fail;
        }
    }
    swapchain->buffer_count = image_count;
    free(vk_images);
    ID3D12Device_Release(d3d12_device);

    return swapchain;

fail:
    if (vk_fence != VK_NULL_HANDLE)
        vkDestroyFence(vk_device, vk_fence, NULL);
    if (vk_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(vk_device, vk_swapchain, NULL);
    vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
    ID3D12Device_Release(d3d12_device);
    return NULL;
}

static inline unsigned int demo_swapchain_get_current_back_buffer_index(struct demo_swapchain *swapchain)
{
    return swapchain->current_buffer;
}

static inline ID3D12Resource *demo_swapchain_get_back_buffer(struct demo_swapchain *swapchain, unsigned int index)
{
    ID3D12Resource *resource = NULL;

    if (index < swapchain->buffer_count && (resource = swapchain->buffers[index]))
        ID3D12Resource_AddRef(resource);

    return resource;
}

static inline void demo_swapchain_present(struct demo_swapchain *swapchain)
{
    VkPresentInfoKHR present_desc;

    present_desc.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_desc.pNext = NULL;
    present_desc.waitSemaphoreCount = 0;
    present_desc.pWaitSemaphores = NULL;
    present_desc.swapchainCount = 1;
    present_desc.pSwapchains = &swapchain->vk_swapchain;
    present_desc.pImageIndices = &swapchain->current_buffer;
    present_desc.pResults = NULL;

    vkQueuePresentKHR(swapchain->vk_queue, &present_desc);

    vkAcquireNextImageKHR(swapchain->vk_device, swapchain->vk_swapchain, UINT64_MAX,
            VK_NULL_HANDLE, swapchain->vk_fence, &swapchain->current_buffer);
    vkWaitForFences(swapchain->vk_device, 1, &swapchain->vk_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(swapchain->vk_device, 1, &swapchain->vk_fence);
}

static inline void demo_swapchain_destroy(struct demo_swapchain *swapchain)
{
    unsigned int i;

    for (i = 0; i < swapchain->buffer_count; ++i)
    {
        ID3D12Resource_Release(swapchain->buffers[i]);
    }
    vkDestroyFence(swapchain->vk_device, swapchain->vk_fence, NULL);
    vkDestroySwapchainKHR(swapchain->vk_device, swapchain->vk_swapchain, NULL);
    vkDestroySurfaceKHR(swapchain->vk_instance, swapchain->vk_surface, NULL);
    free(swapchain);
}

static inline HANDLE demo_create_event(void)
{
    return vkd3d_create_event();
}

static inline unsigned int demo_wait_event(HANDLE event, unsigned int ms)
{
    return vkd3d_wait_event(event, ms);
}

static inline void demo_destroy_event(HANDLE event)
{
    vkd3d_destroy_event(event);
}

static inline HRESULT demo_create_root_signature(ID3D12Device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, ID3D12RootSignature **signature)
{
    return ID3D12Device_CreateRootSignature(device, 0, desc, ~(SIZE_T)0,
            &IID_ID3D12RootSignature, (void **)signature);
}

static inline bool demo_load_shader(struct demo *demo, const wchar_t *hlsl_name, const char *entry_point,
        const char *profile, const char *spv_name, D3D12_SHADER_BYTECODE *shader)
{
    size_t data_size;
    struct stat st;
    ssize_t res;
    void *data;
    int fd;

    if ((fd = open(spv_name, O_RDONLY)) == -1)
        return false;

    if (fstat(fd, &st) == -1)
        goto fail;

    data_size = st.st_size;
    if (!(data = malloc(data_size)))
        goto fail;

    res = read(fd, data, data_size);
    close(fd);
    if (res != data_size)
    {
        free(data);
        return false;
    }

    shader->pShaderBytecode = data;
    shader->BytecodeLength = data_size;

    return true;

fail:
    close(fd);
    return false;
}
