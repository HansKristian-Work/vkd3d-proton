# Proposal for improved compatibility with ID3D12Fence in userspace

This is still heavy WIP.

## Concept of materialization

In DRM timeline semaphores, the timeline monotonically flows forward as Vulkan expects.
A given timeline value starts off as not materialized.
Once a queue signal operation has been submitted, the timeline can become materialized,
which means that the timeline value will be signaled in finite time (or device lost).
At the point of materialization, we can use binary semaphores.

Vulkan timeline semaphores do not give us the means to know when materialization happens,
i.e. when a timeline value will be signaled in finite time.
For non-shared timeline semaphores, this problem can be solved by tracking submissions, but
for shared timelines, this is practically impossible.

While Vulkan supports wait-before-signal, vkd3d-proton cannot make use of this feature for various reasons,
which include, but not limited to:

- Potential deadlocks if multiple ID3D12CommandQueues are aliased to a single VkQueue.
- WSI requires binary semaphores, which requires that all waits have materialized before
  we can signal binary semaphores.
- Break interop with other APIs through our private interfaces since we need to be able to drain the queue
  of work in a reliable way. We also cannot control if clients use binary semaphores on an interop queue.
- Wait cancelling behavior. It's apparently well-defined to destroy a ID3D12Fence while a queue is waiting on it.
  Releasing all public references on a fence is equivalent to unblock all waiters of the fence, by signaling
  a value of UINT64_MAX immediately.
  Signaling such a fence with vkSignalSemaphore triggers UB in some cases if there are other pending signals
  on the semaphore. This is a rather awkward edge case though ...

For these reasons (among others),
we are currently forced to block on CPU with vkWaitSemaphores() when implementing
ID3D12CommandQueue::Wait() on a shared fence. This is terrible for GPU performance.

## Exposing materialization in Vulkan

A straight forward improvement here is to expose materialization as a concept in Vulkan:

- vkWaitSemaphores gets a new flag `VK_SEMAPHORE_WAIT_MATERIALIZATION_BIT`.
  This means the wait blocks until the timeline value is materialized,
  and therefore the point where a binary semaphore can safely be signaled after waiting on the value.
  If the hypothetical extension is supported, non-external TIMELINE must be supported.
  External semaphores get `VK_EXTERNAL_SEMAPHORE_FEATURE_WAIT_MATERIALIZATION_BIT` or something like that.

Additional interfaces allow the materialization as direct DRM timeline semaphore features:

`vkGetSemaphoreFdKHR` gets a pNext:

```
struct VkSemaphoreGetTimelineMaterializationInfoWINE {
    ...
    uint64_t timeline;
};
```

`handleType` must be `SYNC_FD`.
This can be called on non-external timeline semaphore, and optionally supported for external handle types.

When this is called, a sync-fd representing the signal of timeline is exported rather than the timeline itself.
The timeline value must be materialized before a sync-fd can be exported.
This interface only makes sense in Linux (or platforms that support DRM) as far as I'm aware.
In Win32 via Proton, we can add a new interface:

```
struct VkAcquireSemaphoreMaterializationInfoWINE {
    ...
    VkSemaphore semaphore;
    uint64_t value;
    VkSemaphore binarySemaphore;
};

VkResult vkAcquireSemaphoreMaterializationWINE(
    VkDevice device, const VkAcquireSemaphoreTimelineMaterializationInfoWINE *pInfo);
```

This is a more abstract form of the `SYNC_FD` export which can be supported on any platform.
This works as-if a temporary payload of unknown handle type is imported
to `binarySemaphore`, essentially the same model as `vkAcquireNextImageKHR`
where the signal semaphore imports a temporary payload of unknown type.

The implementation in Wine would be expected to be:

- Export to `SYNC_FD`.
- Import the `SYNC_FD` payload with `TEMPORARY` semantics into `binarySemaphore`.

Native Win32 drivers are not expected to implement this extension, unless this feature is somehow
implementable on Windows, but I doubt it ...

In vkd3d-proton we could get performant shared timeline semaphore waits this way:

```
vkWaitSemaphores(.flag = WAIT_MATERIALIZATION, .timeline = shared, .value = value)
vkAcquireSemaphoreTimelineMaterializationWINE(.timeline = shared, .value = value, .binary = dummyBinary);
vkQueueSubmit(.waitSemaphores = [ dummyBinary ]);
```

Technically, a special Vulkan API isn't really needed. If VkSemaphores are exposed as KMT handles
or something, a private API for this can be designed too which works on KMT handles instead of VkSemaphore objects.

## Improving the situation for ID3D12Fence::SetEventOnCompletion()

Vulkan has no mechanism to signal an operating system handle on completion.
For most applications, this is not an issue since having pollable OS handles for GPU completion is rather niche,
but vkd3d-proton must signal event HANDLEs somehow, since every game relies on that to work well.

We currently solve this with one worker thread per ID3D12CommandQueue, whose only job it is to wait on
timeline semaphores and turn that into fence signaling and other callback work somehow.
One worker thread per queue is required for async queues. Once we block on a wait on `vkWaitSemaphores()`
we cannot add new waiters to that thread.
This completely breaks async queue signals if we're blocked on a long-running graphics queue submission.
An `epoll` thread would support this use case, but we cannot access that from Win32 APIs.

For shared fences, only Wine would be able to correctly handle event signaling.
For non-shared fences, having interfaces for this is a theoretical win, since throwing 1 posix thread under the bus
is likely more efficient than throwing N Win32 threads under the bus.

### The DRM timeline fence export method

To turn timelines into OS-pollable events, we can export a sync_fd for a given timeline.
This FD can then be epoll'd.
Another important point is that the binary payload of sync_fd can be copied,
so we can wait on the same fence multiple times. Normal Vulkan binary semaphores don't allow that which
is a massive ballache.

```
struct VkTimelineEventSignalWINE
{
    // ...
    // flag for any/all
    const VkSemaphore *sem;
    const uint64_t *values;
    uint32_t count;
    HANDLE event; // can be NULL. Translates into blocking call instead.
};
vkSetEventOnTimelineCompletionWINE(VkDevice device, const VkTimelineEventSignalWINE *pSignalInfo);
```

Technically, a special Vulkan API isn't really needed. If VkSemaphores are exposed as KMT handles
or something, a private API for this can be made too which works on KMT handles instead of VkSemaphore objects.

Either winevulkan per-process or wineserver could have one grand epoll thread whose only job it is
to wait for sync_fds and forward that to ntsync `SET_EVENT` or similar.

### The signal ordering problem

When there are multiple waiting threads, we can end up in a subtle signal ordering bug.
D3D12 signal ordering requires that if fence A is signaled before B,
and fence A signal is associated with event A, and signal B is associated with event B,
then event B must signal after event A.

```
queueA->Signal(fenceA, 1);
fenceA->SetEventOnCompletion(1, eventA);

queueB->Wait(fenceA, 1);
queueB->Signal(fenceB, 1);
fenceB->SetEventOnCompletion(1, eventB);
```

In this scenario, fenceB is guaranteed to signal after fenceA, and the event signal ordering must be observed.
With our two waiting threads that call `SetEvent()`,
there is a risk that the fence worker thread in queueB overtakes the worker in queueA.
Our fix for that is to enqueue a "signal ordering wait" in queueB. If wait on a fence, we
require that the fence signal is fully resolved on CPU side before we allow subsequent signals to be processed.

Again, with shared fences, these rules are more or less impossible to implement. The signaling of events
must be coordinated by a single process somehow.

### GetValue() and event HANDLE signal ordering

If the HANDLE has been signaled, the update must be observable through GetCurrentValue() as well.

### Signal ordering for shared vs non-shared?

To be extremely correct, we would need to observe signal order across shared and non-shared fences.
If this must be done, the only reasonable way to implement this is to make "everything" shared
where wineserver is the single arbitrator of signal ordering. This is likely overkill.

An acceptable compromise is likely to separate the two worlds.
Resolve non-shared sync-fd waits in winevulkan process, resolve shared fences in wineserver.

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
It allows "pulsing" the event, like `ID3D12Fence::SetEventOnCompletion(event, 10)` followed by
`ID3D12Fence::Signal(10);` then `ID3D12Fence::Signal(9);` or equivalent `ID3D12CommandQueue::Signal()`s.
The event will be signaled, but if you query the fence, it's as if the event should never have been signaled in the first place.
Great design ...

For non-shared fences, this is implementable, but for shared fences,
this gets comically complicated without deep kernel support for this mis-feature.
The obvious solution is to throw a wait thread under the bus, and call
`vkWaitSemaphores()` and `SetEvent()` once that wait is satisfied. However, this is subtly broken:

- In the window from application thread calling `SetEventOnCompletion()` to the worker thread
  entering `vkWaitSemaphores()`, a pulse could have occurred, which *should* have unblocked the event,
  but we missed it. Potential deadlock.
- If you try to be clever and block `SetEventOnCompletion()` until the waiter thread reaches `vkWaitSemaphores()`,
  it's still broken. It's not possible to atomically notify the application thread *and* enter `vkWaitSemaphores()` sleep.
  Any moment you're not sleeping to notify the application, the event might have pulsed, and you missed the wakeup.
  Hello, `PulseEvent()`, we missed you.

Similarly, `ID3D12CommandQueue::Wait(ID3D12Fence *fence, uint64_t value)` works like a ticketing system.
Once this function returns, the wait operation is registered in the kernel or similar.
If at any point the fence reaches the value, the wait is considered satisfied and the GPU queue is unblocked.
This unblocking behavior happens **even if the command queue is currently blocked on other work**.
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
the Wine implementation unnecessarily expensive and difficult to maintain.

### The bundle of fences method

The obvious idea for a shared timeline is to express it as a single shared timeline semaphore
and coordinate that semaphore across processes. Proton implementations have done this in the past.

This approach does not work well in practice, as it is not possible to implement
out of order signaling behavior when multiple queues are used.

For example, this kind of code is valid D3D12:

```
queueA->Execute(ALOT);
queueA->Signal(fence, 1);

queueB->Execute(ALITTLE);
queueB->Signal(fence, 1);
```

This is a racing signal which is mildly nonsense, but is allowed in D3D12 and has well-defined semantics on
all possible paths. Once fence = 1 is signaled, a waiter knows that at least one of the queues are complete.
A shared timeline implementation would need to
signal `fence` with a monotonically increasing `++timelineValue` on the first queue submit,
and set up some kind of tracking that reaching `timelineValue` signal means signaling value to 2.

The second submission will have to set up a signal to `++timelineValue`, however, we must insert a wait for `timelineValue - 1`.
We are not allowed to signal timelines backwards, which means that `queueB` must get a false dependency on `queueA`.
This is broken from a performance PoV and is not how Windows behaves.

For non-shared fences we currently solve this where every `VkQueue` is assigned its own timeline semaphore.
This approach would be impossible for shared fences.

Overall, trying to ram `ID3D12Fence` into the Vulkan timeline semaphore model is just wrong.
I'll just invent some random new name instead. Technically, we don't **need** a Vulkan API here, it could
easily be a KMT private API thing as well. However, it's easier to straw-man the design in the language of Vulkan for me.

### `VK_WINE_edge_triggered_semaphore`

Add a new semaphore type `VK_SEMAPHORE_TYPE_EDGE_TRIGGERED_WINE`.
This new type of semaphore cannot be used directly in `vkQueueSubmit` or `vkWaitSemaphores`.
The basic idea is that we will have separate APIs that interact with the object to convert these into "normal" synchronization primitives.
The value associated with the volatile semaphore behaves as D3D12 expects. The value can rewind, and it can race.
It's just an arbitrary 64-bit value that updates atomically.

`vkGetSemaphoreValue()` and `vkSignalSemaphore()` works as one would expect and immediately modifies the value.
If the value increases as a result of this, events may be triggered.

### Handling pulsed signal

As discussed earlier, waiting for a value to complete is very painful, since once we start waiting for a value,
the wait must never be interrupted, or we risk losing wakeups (i.e. the infamous `PulseEvent` problem).
To fix this, add an API to register a wait. This turns a pulse into a binary event that makes sense.

```
VkResult vkRegisterSemaphoreEdgeWINE(VkDevice device, VkSemaphore edgeSemaphore, uint64_t value, uint64_t *pOpaqueHandle);
void vkUnregisterSemaphoreEdgeWINE(VkDevice device, VkSemaphore edgeSemaphore, uint64_t opaqueHandle);
```

pOpaqueHandle of 0 means that the value is already reached, and the wait is immediately satisfied.
Otherwise, it's just an opaque value that is passed around. pOpaqueHandle goes through this sequence of states:

- Idle
- Materialized
- Signaled

Once we have an opaque handle, we can interact with it:

`vkWaitSemaphores` can take `EDGE_TRIGGERED_WINE` handles.
The value to wait for is `opaqueHandle` value.
For sanity, it's not possible to mix and match here. If one semaphore is `EDGE_TRIGGERED`, all must be.
Both wait for completion and wait for materialization is possible here.

To wait for volatile semaphore signal on GPU:

```
vkAcquireSemaphoreMaterializationWINE(semaphore = volatileSem, value = opaqueHandle, binarySemaphore);
```

Then pass that binary semaphore to `vkQueueSubmit`.
We only need to block in the submission thread until there is a pending signal, which is a massive win
over the current implementation which blocks until the semaphore is fully signaled.
In vkd3d-proton, we could trivially reuse the binary submission semaphore and just temporary import the payload
there, so implementation complexity is trivial.

To signal a volatile semaphore:

```
ID3D12CommandQueue::Signal(fence, fenceValue) {
    vkQueueSubmit(signal = perQueueTimeline, value = ++perQueueTimelineValue);
    vkRegisterSemaphoreSignalWINE(fence->volatileSem, fenceValue, perQueueTimeline, perQueueTimelineValue);
}
```

With this implementation, Wine could use `VkSemaphoreGetTimelineMaterializationInfoWINE` on `vkGetSemaphoreFdKHR`,
extract the associated `SYNC_FD` for `perQueueTimeline` and register that.

NOTE: There is one weakness with this model that we cannot overcome.
When we materialize a wait, we lock ourselves in to waiting on a specific binary semaphore.
If multiple queues are able to signal value, we have no easy way to unblock a GPU queue at the earliest possible time.
I'm not aware of any case where this is important, but it is a theoretical weakness at least.

## Implementation ideas

The shared fence can be expressed as a trivial shared memfd that holds opaque handle value, and current value.
`ID3D12Fence::GetCurrentValue()` is spammed like no tomorrow, and it's valuable to ensure it can be implemented with a trivial
atomic uint64_t load from memory.

This is only valid if the fence value and associated events are signaled atomically in the same place though.

TBD. I'll try to prototype a PoC for this style of implementation.
