/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#define INITGUID
#include "vkd3d_private.h"

HRESULT WINAPI vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID riid, void **device)
{
    struct d3d12_device *object;
    HRESULT hr;

    TRACE("create_info %p, riid %s, device %p.\n", create_info, debugstr_guid(riid), device);

    if (create_info->minimum_feature_level < D3D_FEATURE_LEVEL_11_0
            || !is_valid_feature_level(create_info->minimum_feature_level))
    {
        WARN("Invalid feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (!check_feature_level_support(create_info->minimum_feature_level))
    {
        FIXME("Unsupported feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_device_create(create_info, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12Device_iface, &IID_ID3D12Device,
            riid, device);
}

HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        REFIID riid, void **device)
{
    struct vkd3d_device_create_info create_info;

    TRACE("adapter %p, minimum_feature_level %#x, riid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(riid), device);

    if (adapter)
        FIXME("Ignoring adapter %p.\n", adapter);

    create_info.minimum_feature_level = minimum_feature_level;
    create_info.signal_event_pfn = VKD3DSignalEvent;

    return vkd3d_create_device(&create_info, riid, device);
}

/* Events */
HANDLE WINAPI VKD3DCreateEvent(void)
{
    struct vkd3d_event *event;
    int rc;

    TRACE(".\n");

    if (!(event = vkd3d_malloc(sizeof(*event))))
        return NULL;

    if ((rc = pthread_mutex_init(&event->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        vkd3d_free(event);
        return NULL;
    }
    if ((rc = pthread_cond_init(&event->cond, NULL)))
    {
        ERR("Failed to initialize condition variable, error %d.\n", rc);
        pthread_mutex_destroy(&event->mutex);
        vkd3d_free(event);
        return NULL;
    }

    event->is_signaled = FALSE;

    TRACE("Created event %p.\n", event);

    return event;
}

unsigned int WINAPI VKD3DWaitEvent(HANDLE event, unsigned int milliseconds)
{
    struct vkd3d_event *impl = event;
    int rc;

    TRACE("event %p, milliseconds %u.\n", event, milliseconds);

    if ((rc = pthread_mutex_lock(&impl->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return WAIT_FAILED;
    }

    if (impl->is_signaled || !milliseconds)
    {
        BOOL is_signaled = impl->is_signaled;
        impl->is_signaled = FALSE;
        pthread_mutex_unlock(&impl->mutex);
        return is_signaled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }

    if (milliseconds == INFINITE)
    {
        do
        {
            if ((rc = pthread_cond_wait(&impl->cond, &impl->mutex)))
            {
                ERR("Failed to wait on condition variable, error %d.\n", rc);
                pthread_mutex_unlock(&impl->mutex);
                return WAIT_FAILED;
            }
        } while (!impl->is_signaled);

        impl->is_signaled = FALSE;
        pthread_mutex_unlock(&impl->mutex);
        return WAIT_OBJECT_0;
    }

    pthread_mutex_unlock(&impl->mutex);
    FIXME("Timed wait not implemented yet.\n");
    return WAIT_FAILED;
}

BOOL WINAPI VKD3DSignalEvent(HANDLE event)
{
    struct vkd3d_event *impl = event;
    int rc;

    TRACE("event %p.\n", event);

    if ((rc = pthread_mutex_lock(&impl->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return FALSE;
    }
    impl->is_signaled = TRUE;
    pthread_cond_signal(&impl->cond);
    pthread_mutex_unlock(&impl->mutex);

    return TRUE;
}

void WINAPI VKD3DDestroyEvent(HANDLE event)
{
    struct vkd3d_event *impl = event;
    int rc;

    TRACE("event %p.\n", event);

    if ((rc = pthread_mutex_destroy(&impl->mutex)))
        ERR("Failed to destroy mutex, error %d.\n", rc);
    if ((rc = pthread_cond_destroy(&impl->cond)))
        ERR("Failed to destroy condition variable, error %d.\n", rc);
    vkd3d_free(impl);
}
