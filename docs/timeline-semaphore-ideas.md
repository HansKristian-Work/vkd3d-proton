# Proposal for improved compatibility with ID3D12Fence in userspace

This is still heavy WIP.

## Concept of materialization

In DRM timeline semaphores, the timeline monotonically flows forward as Vulkan expects.
A given timeline value starts off as not materialized,
once a queue signal operation has been submitted, the timeline can become materialized,
which means that the timeline value will be signaled in finite time (or device lost).
At the point of materialization, we can use binary semaphores.

Vulkan timeline semaphores do not give us the means to know when materialization happens,
i.e. when a timeline value will be signaled in finite time.
For non-shared timeline semaphores, this problem can be solved by tracking submissions, but
for shared timelines, this is practically impossible.

While Vulkan supports wait-before-signal, vkd3d-proton cannot make use of this feature for various reasons,
which include, but not limited to:

- Potential deadlocks if multiple ID3D12CommandQueue are aliased to a single VkQueue.
- WSI requires binary semaphores, which requires that all waits have materialized before
  we can signal binary semaphores.
- Break interop with other APIs through our private interfaces since we need to be able to drain the queue
  of work in a reliable way. We also cannot control if clients use binary semaphores on an interop queue.
- Wait cancelling behavior. It's apparently well-defined to destroy a ID3D12Fence while a queue is waiting on it.
  Releasing all public references on a fence is equivalent to unblock all waiters of the fence, by signaling
  a value of UINT64_MAX immediately.
  Signaling such a fence with vkSignalSemaphore triggers UB in some cases if there are other pending signals
  on the semaphore.

For these reasons (among others),
we are currently forced to block on CPU with vkWaitSemaphores() when implementing
ID3D12CommandQueue::Wait() on a shared fence.
This is terrible for GPU performance.

## Exposing materialization in Vulkan

A straight forward improvement here is to expose materialization as a concept in Vulkan:

- vkWaitSemaphores gets a new flag `VK_SEMAPHORE_WAIT_MATERIALIZATION_BIT`.
  This means the wait blocks until the timeline value is materialized,
  and therefore the point where a binary semaphore can safely be signaled after waiting on the value.
  If the hypothetical extension is supported, non-external TIMELINE must be supported.
  External semaphores get `VK_EXTERNAL_SEMAPHORE_FEATURE_WAIT_MATERIALIZATION_BIT`.

Additional interfaces allow the materialization as direct DRM timeline semaphore features:

`vkGetSemaphoreFdKHR` gets a pNext:

```
struct VkSemaphoreGetTimelineMaterializationInfoFROG {
    ...
    uint64_t timeline;
};
```

`handleType` must be `SYNC_FD`.
When this is called, a sync-fd representing the signal of timeline is exported rather than the timeline itself.
The timeline value must be materialized.
This interface only makes sense in Linux (or platforms that support DRM) as far as I'm aware. In Win32 via Proton, we can add a new interface:

```
struct VkAcquireSemaphoreTimelineMaterializationInfoFROG {
    ...
    VkSemaphore timelineSemaphore;
    uint64_t timelineValue;
    VkSemaphore binarySemaphore;
};

VkResult vkAcquireSemaphoreTimelineMaterializationFROG(
    VkDevice device, const VkAcquireSemaphoreTimelineMaterializationInfoFROG *pInfo);
```

This is a more abstract form of the `SYNC_FD` export which can be supported on any platform.
This works as-if a temporary payload of unknown handle type is imported
to `binarySemaphore`, basically exactly the same model as `vkAcquireNextImageKHR`
where the signal semaphore imports a temporary payload of unknown type.

The implementation in Wine would be expected to be:

- Export to `SYNC_FD`.
- Import the `SYNC_FD` payload with `TEMPORARY` semantics into `binarySemaphore`.

Native Win32 drivers are not expected to implement this extension, unless this feature is somehow
implementable on Windows, but I sort of doubt it ...

In vkd3d-proton we could get performant shared timeline semaphore waits this way:

```
vkWaitSemaphores(.flag = WAIT_MATERIALIZATION, .timeline = shared, .value = value)
vkAcquireSemaphoreTimelineMaterializationFROG(.timeline = shared, .value = value,
    .binary = dummyBinary);
vkQueueSubmit(.waitSemaphores = [ dummyBinary ]);
```

## Abstracting away rewinds

Fences in D3D12 can rewind. This is easily one of the worst aspects of D3D12.
It's a fundamentally broken model that allows needlessly racy behavior on the GPU,
and there are high risks of random deadlocks if a fence is randomly rewound
and there is no pending signal that will signal a higher value.

The concept of materialization is now completely broken and the proposed APIs above
cannot meaningfully work. Waiting for materialization is useless, because the moment
the function returns, the fence might have rewound, and we're no longer in a materialized state.

The way the rewindable fences work from an API point of view is quite tricky, and functions more like a ticket system:

ID3D12Fence::SetEventOnCompletion() atomically:

 - Checks the current value of timeline. If current value is greater-or-equal, the event is instantly signaled.
 - Otherwise, the pending signal is registered in the ID3D12Fence (or for a shared fence, it must be registered in the kernel or similar).
 - For shared fences, any process could satisfy the event signal.

At any point, if the fence reaches the target value, the event is immediately signaled and removed from the list of pending signals.
It allows to "pulse" the event, like `ID3D12Fence::SetEventOnCompletion(event, 10)` followed by
`ID3D12Fence::Signal(10);` then `ID3D12Fence::Signal(9);` or equivalent `ID3D12CommandQueue::Signal()`s.
The event will be signaled, but if you query the fence, it's as if the event should never have been signaled in the first place.
Great design ...

For non-shared fences, this is implementable, but for shared fences,
this gets comically complicated without deep kernel support for this mis-feature.
The obvious solution is to throw a wait thread under the bus, and call
`vkWaitSemaphores()` and `SetEvent()` once that wait is satisfied. However, this is subtly broken:

- In the window from application thread calling `SetEventOnCompletion()` to the worker thread
  entering `vkWaitSemaphores()`, a pulse could have occured, which *should* have unblocked the event,
  but we missed it. Potential deadlock.
- If you try to be clever and block `SetEventOnCompletion()` until the waiter thread reaches `vkWaitSemaphores()`,
  it's still broken. It's not possible to atomically notify the application thread *and* enter `vkWaitSemaphores()` sleep.
  Any moment you're not sleeping to notify the application, the event might have pulsed, and you missed the wakeup.
  Hello, `PulseEvent()`, we missed you.

Similarly, `ID3D12CommandQueue::Wait(ID3D12Fence *fence, uint64_t value)` works like a ticketing system.
Once this function returns, the wait operation is registered in the kernel or similar.
If at any point the fence reaches the value, the wait is considered satisfied and the GPU queue is unblocked.
This unblocking behavior happens even if the command queue is currently blocked on other work.
We have tests proving this behavior:

```
1: fence1->Signal(0);
2: fence2->Signal(0);
3: queue->Wait(fence1, 1); // GPU queue is blocked. wait-before-signal behavior.
4: queue->Wait(fence2, 1); // This wait is also blocked.
5: queue->ExecuteSomething();

fence2->Signal(1); // The second wait is now considered unblocked.
fence2->Signal(0); // Pulse the event back again.

fence1->Signal(1); // This unblocks the wait in 3:

The wait in 4: has already been satisfied, before the command queue had a chance to
process the command. 5: will be executed in finite time now.
```

Internally in vkd3d-proton we have a ticketing system where we register a wait
in `ID3D12CommandQueue::Wait()`. When the submission thread gets around to process the wait,
we wait for either the ticket to have been satisfied by a pulsed event, or a materializing submit
has been made.

For shared fences however, all of this becomes almost impossible to deal with.
We have the same "missed pulsing event" problem since the submission thread
is the thread that has to either call `vkQueueSubmit()` or `vkWaitSemaphores()`.
There is no reasonable way to avoid a submission thread for performance and correctness reasons.

Instead, we introduce a different kind of timeline semaphore,
where we can enforce sane state flow for materialization through some cooperative mechanism.
There's five different components here that could do work, from most desirable to least desirable:

- vkd3d-proton / DXVK level
- winevulkan in-process
- wineserver cross-process
- Vulkan driver
- Kernel

Ideally, we can stay in the first two as much as possible.
Doing work in the third and fourth layers may be required,
but we should never rely on the kernel accomodating us.
Ideally not even the fourth level either.

The challenge will be to make it work well with multiple processes in userspace without making
the Wine implementation unnecessarily expensive.

... TBD

## Improving the situation for ID3D12Fence::SetEventOnCompletion()

Vulkan has no mechanism to signal an operating system handle on completion.
For most applications, this is not an issue since having pollable OS handles for GPU completion is rather niche,
but vkd3d-proton must signal event HANDLEs somehow.

... TBD