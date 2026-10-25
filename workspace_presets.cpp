/* Workspace preset save/load and CLI argument-parsing helpers.
   Extracted from standalone_main.cu as part of the Tier 3c module split
   (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   parse_cli_int / parse_cli_float / parse_cli_data_type are used only
   from main()'s argv parsing in standalone_main.cu. save_workspace_preset
   / load_workspace_preset are used from the workspace-preset UI controls
   in standalone_main.cu. load_workspace_preset calls back into
   clamp_cinematic_settings() and sync_resolution_selection(), which stay
   defined in standalone_main.cu (declared in standalone_app.h). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "standalone_app.h"

int parse_cli_int(const char *text, int *out_value)
{
    char *end = NULL;
    long value;

    if (!text || !out_value) {
        return -1;
    }

    value = strtol(text, &end, 10);
    if (end == text || (end && *end != '\0')) {
        return -1;
    }

    if (value < 1 || value > 4096) {
        return -1;
    }

    *out_value = (int)value;
    return 0;
}

int parse_cli_float(const char *text, float min_value, float max_value, float *out_value)
{
    char *end = NULL;
    double value;

    if (!text || !out_value) {
        return -1;
    }

    value = strtod(text, &end);
    if (end == text || (end && *end != '\0')) {
        return -1;
    }

    if (value < (double)min_value || value > (double)max_value) {
        return -1;
    }

    *out_value = (float)value;
    return 0;
}

int parse_cli_data_type(const char *text, int *out_data_type)
{
    VolumeDataType data_type;

    if (!text || !out_data_type) {
        return -1;
    }

    if (volume_import_parse_data_type(text, &data_type) != 0) {
        return -1;
    }

    *out_data_type = (int)data_type;
    return 0;
}

int save_workspace_preset(StandaloneApp *app, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "render_w %d\n", app->render_w);
    fprintf(fp, "render_h %d\n", app->render_h);
    fprintf(fp, "render_match_window %d\n", app->render_match_window);
    fprintf(fp, "render_quality_mode %d\n", app->render_quality_mode);
    fprintf(fp, "ribbon_expanded %d\n", app->ribbon_expanded);
    fprintf(fp, "ribbon_active_tab %d\n", app->ribbon_active_tab);
    fprintf(fp, "controls_visible %d\n", app->controls_visible);
    fprintf(fp, "insights_visible %d\n", app->insights_visible);
    fprintf(fp, "scene_visible %d\n", app->scene_visible);
    fprintf(fp, "step_size %.9f\n", app->step_size);
    fprintf(fp, "threshold %.9f\n", app->threshold);
    fprintf(fp, "skip_mult %.9f\n", app->skip_mult);
    fprintf(fp, "fov_y_deg %.9f\n", app->fov_y_deg);
    fprintf(fp, "tf_center %.9f\n", app->tf_center);
    fprintf(fp, "tf_width %.9f\n", app->tf_width);
    fprintf(fp, "tf_opacity_scale %.9f\n", app->tf_opacity_scale);
    fprintf(fp, "cinematic_enabled %d\n", app->cinematic_enabled);
    fprintf(fp, "cinematic_rim_strength %.9f\n", app->cinematic_rim_strength);
    fprintf(fp, "cinematic_glow_strength %.9f\n", app->cinematic_glow_strength);
    fprintf(fp, "cinematic_contrast %.9f\n", app->cinematic_contrast);
    fprintf(fp, "tf_palette %d\n", app->tf_palette);
    fprintf(fp, "tf_invert %d\n", app->tf_invert);
    fprintf(fp, "measurement_visible %d\n", app->measurement_visible);
    fprintf(fp, "measurement_target %d\n", app->measurement_target);
    fprintf(fp, "cam_pos %.9f %.9f %.9f\n", app->cam_pos[0], app->cam_pos[1], app->cam_pos[2]);
    fprintf(fp, "cam_dir %.9f %.9f %.9f\n", app->cam_dir[0], app->cam_dir[1], app->cam_dir[2]);
    fprintf(fp, "cam_up %.9f %.9f %.9f\n", app->cam_up[0], app->cam_up[1], app->cam_up[2]);
    fprintf(fp, "light_pos %.9f %.9f %.9f\n", app->light_pos[0], app->light_pos[1], app->light_pos[2]);
    fprintf(fp, "clip_min %.9f %.9f %.9f\n", app->clip_min[0], app->clip_min[1], app->clip_min[2]);
    fprintf(fp, "clip_max %.9f %.9f %.9f\n", app->clip_max[0], app->clip_max[1], app->clip_max[2]);
    fprintf(fp, "slice %.9f %.9f %.9f\n", app->slice_x, app->slice_y, app->slice_z);
    fprintf(fp, "measure_a %.9f %.9f %.9f\n", app->measure_a[0], app->measure_a[1], app->measure_a[2]);
    fprintf(fp, "measure_b %.9f %.9f %.9f\n", app->measure_b[0], app->measure_b[1], app->measure_b[2]);
    fprintf(fp, "voxel_spacing %.9f %.9f %.9f\n", app->voxel_spacing[0], app->voxel_spacing[1], app->voxel_spacing[2]);
    fprintf(fp, "orbit_target %.9f %.9f %.9f\n", app->orbit_target[0], app->orbit_target[1], app->orbit_target[2]);
    fprintf(fp, "orbit %.9f %.9f %.9f\n", app->orbit_yaw, app->orbit_pitch, app->orbit_radius);
    fclose(fp);
    return 0;
}

int load_workspace_preset(StandaloneApp *app, const char *path)
{
    FILE *fp = fopen(path, "r");
    char key[64];

    if (!fp) {
        return -1;
    }

    while (fscanf(fp, "%63s", key) == 1) {
        if (strcmp(key, "render_w") == 0) fscanf(fp, "%d", &app->render_w);
        else if (strcmp(key, "render_h") == 0) fscanf(fp, "%d", &app->render_h);
        else if (strcmp(key, "render_match_window") == 0) fscanf(fp, "%d", &app->render_match_window);
        else if (strcmp(key, "render_quality_mode") == 0) fscanf(fp, "%d", &app->render_quality_mode);
        else if (strcmp(key, "ribbon_expanded") == 0) fscanf(fp, "%d", &app->ribbon_expanded);
        else if (strcmp(key, "ribbon_active_tab") == 0) fscanf(fp, "%d", &app->ribbon_active_tab);
        else if (strcmp(key, "controls_visible") == 0) fscanf(fp, "%d", &app->controls_visible);
        else if (strcmp(key, "insights_visible") == 0) fscanf(fp, "%d", &app->insights_visible);
        else if (strcmp(key, "scene_visible") == 0) fscanf(fp, "%d", &app->scene_visible);
        else if (strcmp(key, "step_size") == 0) fscanf(fp, "%f", &app->step_size);
        else if (strcmp(key, "threshold") == 0) fscanf(fp, "%f", &app->threshold);
        else if (strcmp(key, "skip_mult") == 0) fscanf(fp, "%f", &app->skip_mult);
        else if (strcmp(key, "fov_y_deg") == 0) fscanf(fp, "%f", &app->fov_y_deg);
        else if (strcmp(key, "tf_center") == 0) fscanf(fp, "%f", &app->tf_center);
        else if (strcmp(key, "tf_width") == 0) fscanf(fp, "%f", &app->tf_width);
        else if (strcmp(key, "tf_opacity_scale") == 0) fscanf(fp, "%f", &app->tf_opacity_scale);
        else if (strcmp(key, "cinematic_enabled") == 0) fscanf(fp, "%d", &app->cinematic_enabled);
        else if (strcmp(key, "cinematic_rim_strength") == 0) fscanf(fp, "%f", &app->cinematic_rim_strength);
        else if (strcmp(key, "cinematic_glow_strength") == 0) fscanf(fp, "%f", &app->cinematic_glow_strength);
        else if (strcmp(key, "cinematic_contrast") == 0) fscanf(fp, "%f", &app->cinematic_contrast);
        else if (strcmp(key, "tf_palette") == 0) fscanf(fp, "%d", &app->tf_palette);
        else if (strcmp(key, "tf_invert") == 0) fscanf(fp, "%d", &app->tf_invert);
        else if (strcmp(key, "measurement_visible") == 0) fscanf(fp, "%d", &app->measurement_visible);
        else if (strcmp(key, "measurement_target") == 0) fscanf(fp, "%d", &app->measurement_target);
        else if (strcmp(key, "cam_pos") == 0) fscanf(fp, "%f %f %f", &app->cam_pos[0], &app->cam_pos[1], &app->cam_pos[2]);
        else if (strcmp(key, "cam_dir") == 0) fscanf(fp, "%f %f %f", &app->cam_dir[0], &app->cam_dir[1], &app->cam_dir[2]);
        else if (strcmp(key, "cam_up") == 0) fscanf(fp, "%f %f %f", &app->cam_up[0], &app->cam_up[1], &app->cam_up[2]);
        else if (strcmp(key, "light_pos") == 0) fscanf(fp, "%f %f %f", &app->light_pos[0], &app->light_pos[1], &app->light_pos[2]);
        else if (strcmp(key, "clip_min") == 0) fscanf(fp, "%f %f %f", &app->clip_min[0], &app->clip_min[1], &app->clip_min[2]);
        else if (strcmp(key, "clip_max") == 0) fscanf(fp, "%f %f %f", &app->clip_max[0], &app->clip_max[1], &app->clip_max[2]);
        else if (strcmp(key, "slice") == 0) fscanf(fp, "%f %f %f", &app->slice_x, &app->slice_y, &app->slice_z);
        else if (strcmp(key, "measure_a") == 0) fscanf(fp, "%f %f %f", &app->measure_a[0], &app->measure_a[1], &app->measure_a[2]);
        else if (strcmp(key, "measure_b") == 0) fscanf(fp, "%f %f %f", &app->measure_b[0], &app->measure_b[1], &app->measure_b[2]);
        else if (strcmp(key, "voxel_spacing") == 0) fscanf(fp, "%f %f %f", &app->voxel_spacing[0], &app->voxel_spacing[1], &app->voxel_spacing[2]);
        else if (strcmp(key, "orbit_target") == 0) fscanf(fp, "%f %f %f", &app->orbit_target[0], &app->orbit_target[1], &app->orbit_target[2]);
        else if (strcmp(key, "orbit") == 0) fscanf(fp, "%f %f %f", &app->orbit_yaw, &app->orbit_pitch, &app->orbit_radius);
    }

    fclose(fp);
    app->ribbon_expanded = app->ribbon_expanded ? 1 : 0;
    if (app->ribbon_active_tab < 0 || app->ribbon_active_tab >= VOXELPILOT_RIBBON_COUNT) {
        app->ribbon_active_tab = VOXELPILOT_RIBBON_HOME;
    }
    app->controls_visible = app->controls_visible ? 1 : 0;
    app->insights_visible = app->insights_visible ? 1 : 0;
    app->scene_visible = app->scene_visible ? 1 : 0;
    clamp_cinematic_settings(app);
    sync_resolution_selection(app);
    return 0;
}
