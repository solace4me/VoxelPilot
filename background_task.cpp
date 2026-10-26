/* background_task.cpp - a minimal worker thread on the Windows API.

   Used for region discovery on large scans (standalone_main.cpp), which
   otherwise froze the UI for seconds. Deliberately not std::thread: the
   C++ standard library records a runtime-library setting (/MT) that
   conflicts with the CUDA objects (/MD) at link time, while the Windows
   thread API carries no such dependency. Kept in its own file so
   <windows.h> does not leak into the GL / ImGui translation units. */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "background_task.h"

typedef struct {
    void (*fn)(void *);
    void *arg;
} VoxelPilotTaskStart;

static DWORD WINAPI voxelpilot_task_entry(LPVOID param)
{
    VoxelPilotTaskStart start = *(VoxelPilotTaskStart *)param;
    HeapFree(GetProcessHeap(), 0, param);
    start.fn(start.arg);
    return 0;
}

void *voxelpilot_task_start(void (*fn)(void *), void *arg)
{
    VoxelPilotTaskStart *start;
    HANDLE thread;

    if (!fn) return 0;
    start = (VoxelPilotTaskStart *)HeapAlloc(GetProcessHeap(), 0, sizeof(*start));
    if (!start) return 0;
    start->fn = fn;
    start->arg = arg;
    thread = CreateThread(NULL, 0, voxelpilot_task_entry, start, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, start);
        return 0;
    }
    return (void *)thread;
}

int voxelpilot_task_is_done(void *task)
{
    return task && WaitForSingleObject((HANDLE)task, 0) == WAIT_OBJECT_0;
}

void voxelpilot_task_join(void *task)
{
    if (!task) return;
    WaitForSingleObject((HANDLE)task, INFINITE);
    CloseHandle((HANDLE)task);
}
