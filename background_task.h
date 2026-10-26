#ifndef VOXELPILOT_BACKGROUND_TASK_H
#define VOXELPILOT_BACKGROUND_TASK_H

/* A worker thread (background_task.cpp). start returns an opaque handle
   or NULL; is_done never blocks; join waits and releases the handle. */
void *voxelpilot_task_start(void (*fn)(void *), void *arg);
int voxelpilot_task_is_done(void *task);
void voxelpilot_task_join(void *task);

#endif /* VOXELPILOT_BACKGROUND_TASK_H */
