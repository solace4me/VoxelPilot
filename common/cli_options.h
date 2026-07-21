#ifndef VOXELPILOT_CLI_OPTIONS_H
#define VOXELPILOT_CLI_OPTIONS_H

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int run_snapshot;
    int run_review_bundle;
} VoxelPilotCliExportPlan;

static inline int voxelpilot_cli_path_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

static inline int voxelpilot_cli_mkdir_one(const char *folder)
{
#ifdef _WIN32
    if (_mkdir(folder) == 0) return 1;
#else
    if (mkdir(folder, 0775) == 0) return 1;
#endif
    return errno == EEXIST;
}

static inline int voxelpilot_cli_prepare_export_folder(
    const char *folder,
    char       *error,
    size_t      error_size)
{
    char path[1024];
    size_t length;
    size_t i;

    if (error && error_size > 0) {
        error[0] = '\0';
    }
    if (!folder || !folder[0]) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Export folder is empty.");
        }
        return 0;
    }

    length = strlen(folder);
    if (length >= sizeof(path)) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Export folder path is too long.");
        }
        return 0;
    }

    memcpy(path, folder, length + 1);
    while (length > 0 && voxelpilot_cli_path_separator(path[length - 1])) {
        path[--length] = '\0';
    }
    if (length == 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Export folder is empty.");
        }
        return 0;
    }

    for (i = 0; i < length; ++i) {
        if (!voxelpilot_cli_path_separator(path[i])) {
            continue;
        }
        if (i == 0 || (i == 2 && path[1] == ':')) {
            continue;
        }
        path[i] = '\0';
        if (path[0] && !voxelpilot_cli_mkdir_one(path)) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "Could not create export folder: %s", path);
            }
            path[i] = folder[i];
            return 0;
        }
        path[i] = folder[i];
    }

    if (!voxelpilot_cli_mkdir_one(path)) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Could not create export folder: %s", path);
        }
        return 0;
    }
    return 1;
}

static inline int voxelpilot_cli_make_export_plan(
    const char *snapshot_path,
    const char *review_bundle_folder,
    VoxelPilotCliExportPlan *plan)
{
    if (!plan) {
        return 0;
    }

    plan->run_snapshot = snapshot_path && snapshot_path[0] ? 1 : 0;
    plan->run_review_bundle = review_bundle_folder && review_bundle_folder[0] ? 1 : 0;
    return plan->run_snapshot || plan->run_review_bundle ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_CLI_OPTIONS_H */
