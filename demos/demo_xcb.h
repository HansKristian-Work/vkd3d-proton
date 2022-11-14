/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define VK_USE_PLATFORM_XCB_KHR
#include <vkd3d.h>
#include <vkd3d_utils.h>
#include <vkd3d_sonames.h>
#include <vkd3d_swapchain_factory.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <dlfcn.h>

struct demo
{
    xcb_connection_t *connection;
    xcb_atom_t wm_protocols_atom;
    xcb_atom_t wm_delete_window_atom;
    xcb_key_symbols_t *xcb_keysyms;
    int screen;

    struct demo_window **windows;
    size_t windows_size;
    size_t window_count;

    void *user_data;
    void (*idle_func)(struct demo *demo, void *user_data);
    bool destroy_request;
};

struct demo_window
{
    xcb_window_t window;
    struct demo *demo;

    void *user_data;
    void (*expose_func)(struct demo_window *window, void *user_data);
    void (*key_press_func)(struct demo_window *window, demo_key key, void *user_data);
};

struct xcb_surface_factory
{
    uint32_t refcount;
    xcb_connection_t *connection;
    xcb_window_t window;
    PFN_vkCreateXcbSurfaceKHR create_surface;
    IDXGIVkSurfaceFactory IDXGIVkSurfaceFactory_iface;
};

struct demo_swapchain
{
    IDXGIVkSwapChain *swapchain;
    IDXGIVkSurfaceFactory *surface_factory;
};

static HRESULT STDMETHODCALLTYPE xcb_surface_factory_QueryInterface(IDXGIVkSurfaceFactory *iface, REFIID riid, void **object)
{
    struct xcb_surface_factory *factory = CONTAINING_RECORD(iface, struct xcb_surface_factory, IDXGIVkSurfaceFactory_iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIVkSurfaceFactory))
    {
        factory->refcount++;
        *object = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE xcb_surface_factory_AddRef(IDXGIVkSurfaceFactory *iface)
{
    struct xcb_surface_factory *factory = CONTAINING_RECORD(iface, struct xcb_surface_factory, IDXGIVkSurfaceFactory_iface);
    return ++factory->refcount;
}

static ULONG STDMETHODCALLTYPE xcb_surface_factory_Release(IDXGIVkSurfaceFactory *iface)
{
    struct xcb_surface_factory *factory = CONTAINING_RECORD(iface, struct xcb_surface_factory, IDXGIVkSurfaceFactory_iface);
    uint32_t refcount;

    refcount = --factory->refcount;
    if (!refcount)
        free(factory);
    return refcount;
}

static VkResult STDMETHODCALLTYPE xcb_surface_factory_CreateSurface(IDXGIVkSurfaceFactory *iface,
        VkInstance vk_instance, VkPhysicalDevice vk_physical_device, VkSurfaceKHR *vk_surface)
{
    struct xcb_surface_factory *factory = CONTAINING_RECORD(iface, struct xcb_surface_factory, IDXGIVkSurfaceFactory_iface);
    VkXcbSurfaceCreateInfoKHR create_info;

    create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = NULL;
    create_info.flags = 0;
    create_info.window = factory->window;
    create_info.connection = factory->connection;
    return factory->create_surface(vk_instance, &create_info, NULL, vk_surface);
}

static CONST_VTBL struct IDXGIVkSurfaceFactoryVtbl xcb_surface_factory_vtbl =
{
    xcb_surface_factory_QueryInterface,
    xcb_surface_factory_AddRef,
    xcb_surface_factory_Release,
    xcb_surface_factory_CreateSurface,
};

static HRESULT xcb_surface_factory_create(xcb_connection_t *connection, xcb_window_t window,
        IDXGIVkSurfaceFactory **out_factory)
{
    PFN_vkCreateXcbSurfaceKHR create_surface;
    struct xcb_surface_factory *factory;
    void *libvulkan;

    libvulkan = dlopen(SONAME_LIBVULKAN, RTLD_LAZY);
    if (!libvulkan)
        return E_NOTIMPL;

    create_surface = (PFN_vkCreateXcbSurfaceKHR)dlsym(libvulkan, "vkCreateXcbSurfaceKHR");
    if (!create_surface)
        return E_NOTIMPL;

    factory = calloc(1, sizeof(*factory));
    factory->IDXGIVkSurfaceFactory_iface.lpVtbl = &xcb_surface_factory_vtbl;
    factory->connection = connection;
    factory->window = window;
    factory->create_surface = create_surface;
    factory->refcount = 1;
    *out_factory = &factory->IDXGIVkSurfaceFactory_iface;
    return S_OK;
}

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

static inline xcb_screen_t *demo_get_screen(xcb_connection_t *c, int idx)
{
    xcb_screen_iterator_t iter;

    iter = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (; iter.rem; xcb_screen_next(&iter), --idx)
    {
        if (!idx)
            return iter.data;
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

    if (!(screen = demo_get_screen(demo->connection, demo->screen)))
        return NULL;

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

static inline void demo_swapchain_destroy(struct demo_swapchain *swapchain)
{
    IDXGIVkSwapChain_Release(swapchain->swapchain);
    IDXGIVkSurfaceFactory_Release(swapchain->surface_factory);
    free(swapchain);
}

static inline void demo_window_destroy(struct demo_window *window)
{
    xcb_destroy_window(window->demo->connection, window->window);
    xcb_flush(window->demo->connection);
    demo_remove_window(window->demo, window);
    free(window);
}

static inline void demo_window_destroy_defer(struct demo_window *window)
{
    window->demo->destroy_request = true;
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

    while (demo->window_count && !demo->destroy_request)
    {
        if (!demo->idle_func)
        {
            if (!(event = xcb_wait_for_event(demo->connection)))
                break;
        }
        else if (!(event = xcb_poll_for_event(demo->connection)))
        {
            demo->idle_func(demo, demo->user_data);
            continue;
        }

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
                    demo_window_destroy_defer(window);
                break;
        }

        free(event);
    }
}

static inline bool demo_init(struct demo *demo, void *user_data)
{
    if (!(demo->connection = xcb_connect(NULL, &demo->screen)))
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
    demo->user_data = user_data;
    demo->idle_func = NULL;

    return true;

fail:
    xcb_disconnect(demo->connection);
    return false;
}

static inline void demo_cleanup(struct demo *demo)
{
    while (demo->window_count)
        demo_window_destroy(demo->windows[demo->window_count - 1]);
    free(demo->windows);
    xcb_key_symbols_free(demo->xcb_keysyms);
    xcb_disconnect(demo->connection);
}

static inline void demo_set_idle_func(struct demo *demo,
        void (*idle_func)(struct demo *demo, void *user_data))
{
    demo->idle_func = idle_func;
}

static inline struct demo_swapchain *demo_swapchain_create(ID3D12CommandQueue *command_queue,
        struct demo_window *window, const struct demo_swapchain_desc *desc)
{
    IDXGIVkSwapChainFactory *factory = NULL;
    struct demo_swapchain *swapchain;
    DXGI_SWAP_CHAIN_DESC1 swap_desc;

    swapchain = calloc(1, sizeof(*swapchain));

    if (FAILED(ID3D12CommandQueue_QueryInterface(command_queue, &IID_IDXGIVkSwapChainFactory, (void**)&factory)))
        goto fail;
    if (FAILED(xcb_surface_factory_create(window->demo->connection, window->window, &swapchain->surface_factory)))
        goto fail;

    memset(&swap_desc, 0, sizeof(swap_desc));
    swap_desc.Format = desc->format;
    swap_desc.Width = desc->width;
    swap_desc.Height = desc->height;
    swap_desc.BufferCount = desc->buffer_count;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(IDXGIVkSwapChainFactory_CreateSwapChain(factory, swapchain->surface_factory, &swap_desc, &swapchain->swapchain)))
        goto fail;

    if (factory)
        IDXGIVkSwapChainFactory_Release(factory);
    return swapchain;

fail:
    if (factory)
        IDXGIVkSwapChainFactory_Release(factory);
    if (swapchain && swapchain->surface_factory)
        IDXGIVkSurfaceFactory_Release(swapchain->surface_factory);
    free(swapchain);
    return NULL;
}

static inline unsigned int demo_swapchain_get_current_back_buffer_index(struct demo_swapchain *swapchain)
{
    return IDXGIVkSwapChain_GetImageIndex(swapchain->swapchain);
}

static inline ID3D12Resource *demo_swapchain_get_back_buffer(struct demo_swapchain *swapchain, unsigned int index)
{
    ID3D12Resource *resource;
    if (FAILED(IDXGIVkSwapChain_GetImage(swapchain->swapchain, index, &IID_ID3D12Resource, (void**)&resource)))
        resource = NULL;
    return resource;
}

static inline void demo_swapchain_present(struct demo_swapchain *swapchain)
{
    IDXGIVkSwapChain_Present(swapchain->swapchain, 1, 0, NULL);
}

static inline HANDLE demo_create_event(void)
{
    return vkd3d_create_eventfd();
}

static inline unsigned int demo_wait_event(HANDLE event, unsigned int ms)
{
    return vkd3d_wait_eventfd(event, ms);
}

static inline void demo_destroy_event(HANDLE event)
{
    vkd3d_destroy_eventfd(event);
}
