/* GL resource setup, shaders, mouse/camera input, slice previews, and
   the GPU/standalone render paths.
   Extracted from standalone_main.cu as part of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   setup_gl_resources, process_mouse_camera_controls,
   sync_render_target_to_framebuffer, update_slice_previews,
   render_frame_standalone, draw_fullscreen_quad,
   update_main_render_hover_pick, and draw_main_render_hover_tooltip
   are called from standalone_main.cu's main loop (and draw_gui, for
   the last two) and are now non-static for that reason.
   render_frame_gpu_snapshot was already non-static (called from
   export_bundles.cpp too); its standalone_app.h declaration moves
   here from the step-1 section.
   compile_shader, upload_gray_texture, tf_preview_value,
   attach_label_overlay_to_render_command, build_current_renderer_input,
   and update_render_performance_average stay static/private - every
   caller moved here with them. update_render_performance_average is
   placed before render_frame_gpu_snapshot/render_frame_standalone in
   this file (even though it was physically later in
   standalone_main.cu) so no internal forward declaration is needed.
*/

#include <stdio.h>
#include <string.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"

#include "common/renderer_api.h"
#include "common/math_utils.h"
#include "common/render_command_builder.h"
#include "common/ui_state_helpers.h"
#include "common/ai_features.h"
#include "common/performance_governor.h"
#include "common/semantic_discovery.h"

#include "standalone_app.h"

static const char *vs_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *fs_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    outColor = vec4(texture(tex, uv).rgb, 1.0);\n"
    "}\n";

/* ============================================================
   Setup GL resources for the standalone viewport
   ============================================================ */
static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "Shader error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

int setup_gl_resources(StandaloneApp *app)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    if (!vs || !fs) return -1;

    app->prog = glCreateProgram();
    glAttachShader(app->prog, vs);
    glAttachShader(app->prog, fs);
    glLinkProgram(app->prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    app->tex_uniform_loc = glGetUniformLocation(app->prog, "tex");

    float quadVerts[] = {
        -1,-1, 0,0,
         1,-1, 1,0,
         1, 1, 1,1,
        -1, 1, 0,1
    };
    unsigned int quadIdx[] = {0,1,2, 2,3,0};

    glGenVertexArrays(1, &app->vao);
    glGenBuffers(1, &app->vbo);
    glGenBuffers(1, &app->ebo);
    glBindVertexArray(app->vao);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4*sizeof(float),
                          (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glGenTextures(1, &app->gl_tex);
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 app->render_w, app->render_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &app->axial_tex);
    glGenTextures(1, &app->coronal_tex);
    glGenTextures(1, &app->sagittal_tex);

    app->tex_w = app->render_w;
    app->tex_h = app->render_h;
    return 0;
}

static void upload_gray_texture(
    GLuint               tex,
    const unsigned char *pixels,
    int                  width,
    int                  height)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_R8,
        width, height,
        0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void process_mouse_camera_controls(StandaloneApp *app)
{
    ImGuiIO *io;
    double mouse_x, mouse_y;
    int left_down;
    int motion_active = 0;

    io = &ImGui::GetIO();
    glfwGetCursorPos(app->window, &mouse_x, &mouse_y);
    left_down = glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT);

    if (!io->WantCaptureMouse && left_down == GLFW_PRESS) {
        if (!app->orbit_drag_active) {
            sync_orbit_from_camera(app);
            app->orbit_drag_active = 1;
        }
        float dx = (float)(mouse_x - app->last_mouse_x);
        float dy = (float)(mouse_y - app->last_mouse_y);

        app->orbit_yaw += dx * 0.01f;
        app->orbit_pitch += dy * 0.01f;
        if (fabsf(dx) > 0.001f || fabsf(dy) > 0.001f) {
            motion_active = 1;
        }
        if (app->orbit_pitch > 1.45f) app->orbit_pitch = 1.45f;
        if (app->orbit_pitch < -1.45f) app->orbit_pitch = -1.45f;
        apply_orbit_camera(app);
    } else {
        app->orbit_drag_active = 0;
    }

    if (!io->WantCaptureMouse && app->pending_scroll != 0.0f) {
        app->orbit_radius *= (1.0f - app->pending_scroll * 0.10f);
        if (app->orbit_radius < 0.25f) app->orbit_radius = 0.25f;
        if (app->orbit_radius > 6.0f) app->orbit_radius = 6.0f;
        apply_orbit_camera(app);
        motion_active = 1;
    }

    if (motion_active) {
        app->camera_motion_active = 1;
    }

    app->pending_scroll = 0.0f;
    app->last_mouse_x = mouse_x;
    app->last_mouse_y = mouse_y;
}

void sync_render_target_to_framebuffer(
    StandaloneApp *app,
    int            framebuffer_width,
    int            framebuffer_height)
{
    if (!voxelpilot_should_sync_render_target(
            app->render_match_window,
            app->render_w,
            app->render_h,
            framebuffer_width,
            framebuffer_height)) {
        return;
    }

    app->render_w = voxelpilot_clamp_render_extent(framebuffer_width);
    app->render_h = voxelpilot_clamp_render_extent(framebuffer_height);
    sync_resolution_selection(app);
}

static unsigned char tf_preview_value(
    const StandaloneApp *app,
    float                value)
{
    VolumeParams params;
    float r, g, b, a;
    float lum;

    memset(&params, 0, sizeof(params));
    params.tf_center = app->tf_center;
    params.tf_width = app->tf_width;
    params.tf_opacity_scale = app->tf_opacity_scale;
    params.tf_palette = app->tf_palette;
    params.tf_invert = app->tf_invert;

    tf_lookup(params, value, &r, &g, &b, &a);
    lum = ((r + g + b) / 3.0f) * fmaxf(a, 0.25f);
    return (unsigned char)(fminf(fmaxf(lum, 0.0f), 1.0f) * 255.0f);
}

void update_slice_previews(StandaloneApp *app)
{
    int x, y, z;
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;
    int axial_z;
    int coronal_y;
    int sagittal_x;

    if (!app->renderer.volume_data || W < 1 || H < 1 || D < 1) {
        return;
    }

    /* Reallocate persistent buffers only when volume dimensions change */
    if (W != app->slice_buf_W ||
        H != app->slice_buf_H ||
        D != app->slice_buf_D) {

        free(app->slice_axial_buf);
        free(app->slice_coronal_buf);
        free(app->slice_sagittal_buf);

        app->slice_axial_buf    = (unsigned char *)malloc((size_t)W * (size_t)H);
        app->slice_coronal_buf  = (unsigned char *)malloc((size_t)W * (size_t)D);
        app->slice_sagittal_buf = (unsigned char *)malloc((size_t)H * (size_t)D);

        if (!app->slice_axial_buf ||
            !app->slice_coronal_buf ||
            !app->slice_sagittal_buf) {
            free(app->slice_axial_buf);
            free(app->slice_coronal_buf);
            free(app->slice_sagittal_buf);
            app->slice_axial_buf    = NULL;
            app->slice_coronal_buf  = NULL;
            app->slice_sagittal_buf = NULL;
            app->slice_buf_W = 0;
            app->slice_buf_H = 0;
            app->slice_buf_D = 0;
            return;
        }

        app->slice_buf_W = W;
        app->slice_buf_H = H;
        app->slice_buf_D = D;
        /* Force recompute on dimension change */
        app->slice_preview_dirty = 1;
    }

    /* Skip CPU recompute when slice positions and dirty flag are unchanged */
    if (!app->slice_preview_dirty &&
        app->slice_x == app->slice_prev_x &&
        app->slice_y == app->slice_prev_y &&
        app->slice_z == app->slice_prev_z) {
        return;
    }
    app->slice_prev_x        = app->slice_x;
    app->slice_prev_y        = app->slice_y;
    app->slice_prev_z        = app->slice_z;
    app->slice_preview_dirty = 0;

    axial_z    = (int)(app->slice_z * (float)(D - 1));
    coronal_y  = (int)(app->slice_y * (float)(H - 1));
    sagittal_x = (int)(app->slice_x * (float)(W - 1));

    for (y = 0; y < H; ++y) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)axial_z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)x;
            app->slice_axial_buf[y * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (x = 0; x < W; ++x) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)coronal_y * (size_t)W
                       + (size_t)x;
            app->slice_coronal_buf[z * W + x] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    for (z = 0; z < D; ++z) {
        for (y = 0; y < H; ++y) {
            size_t idx = (size_t)z * (size_t)W * (size_t)H
                       + (size_t)y * (size_t)W
                       + (size_t)sagittal_x;
            app->slice_sagittal_buf[z * H + y] =
                tf_preview_value(app, app->renderer.volume_data[idx]);
        }
    }

    upload_gray_texture(app->axial_tex,    app->slice_axial_buf,    W, H);
    upload_gray_texture(app->coronal_tex,  app->slice_coronal_buf,  W, D);
    upload_gray_texture(app->sagittal_tex, app->slice_sagittal_buf, H, D);
}

static void update_render_performance_average(StandaloneApp *app)
{
    if (!app) return;
    app->render_ms_average = voxelpilot_update_render_ms_average(
        app->render_ms_average,
        app->last_render_ms,
        app->render_ms_average_ready);
    app->render_ms_average_ready = 1;
}

static void attach_label_overlay_to_render_command(
    StandaloneApp  *app,
    RendererInput  *cmd)
{
    VoxelPilotRenderLabelOverlayConfig labels;
    int i;
    int color_count;

    if (!app || !cmd) return;

    voxelpilot_render_label_overlay_config_defaults(&labels);
    labels.visible = app->label_overlay_visible;
    labels.label_mask = app->label_mask;
    labels.label_mask_width = app->label_mask_W;
    labels.label_mask_height = app->label_mask_H;
    labels.label_mask_depth = app->label_mask_D;
    labels.renderer_width = app->renderer.W;
    labels.renderer_height = app->renderer.H;
    labels.renderer_depth = app->renderer.D;
    labels.label_count = app->annotation_count;
    labels.alpha = app->label_overlay_alpha;
    labels.revision = app->label_mask_revision;

    if (app->label_selected_emphasis_enabled &&
        app->selected_annotation >= 0 &&
        app->selected_annotation < app->annotation_count) {
        labels.selected_enabled = 1;
        labels.selected_label_id = app->annotations[app->selected_annotation].label_id;
        labels.selected_emphasis = app->label_selected_emphasis_strength;
    }

    color_count = MAX_ANNOTATIONS < RENDERER_MAX_LABELS
                      ? MAX_ANNOTATIONS
                      : RENDERER_MAX_LABELS;
    for (i = 0; i < color_count; ++i) {
        labels.colors[i * 3 + 0] = app->annotations[i].color[0];
        labels.colors[i * 3 + 1] = app->annotations[i].color[1];
        labels.colors[i * 3 + 2] = app->annotations[i].color[2];
    }

    voxelpilot_attach_label_overlay_to_renderer_input(&labels, cmd);
}
static int build_current_renderer_input(
    StandaloneApp *app,
    RendererInput *cmd)
{
    VoxelPilotRenderCommandConfig config;

    if (!app || !cmd) return 0;

    refresh_adaptive_quality_for_frame(app);
    voxelpilot_render_command_config_defaults(&config);

    memcpy(config.cam_pos, app->cam_pos, sizeof(config.cam_pos));
    memcpy(config.cam_dir, app->cam_dir, sizeof(config.cam_dir));
    memcpy(config.cam_up, app->cam_up, sizeof(config.cam_up));
    memcpy(config.light_pos, app->light_pos, sizeof(config.light_pos));
    memcpy(config.clip_min, app->clip_min, sizeof(config.clip_min));
    memcpy(config.clip_max, app->clip_max, sizeof(config.clip_max));

    config.fov_y_degrees = app->fov_y_deg;
    config.step_size = app->adaptive_step_size;
    config.threshold = app->threshold;
    config.empty_space_skip_mult = app->adaptive_skip_mult;
    config.tf_center = app->tf_center;
    config.tf_width = app->tf_width;
    config.tf_opacity_scale = app->tf_opacity_scale;
    config.tf_palette = app->tf_palette;
    config.tf_invert = app->tf_invert;
    config.cinematic_enabled = app->cinematic_enabled;
    config.cinematic_rim_strength = app->cinematic_rim_strength;
    config.cinematic_glow_strength = app->cinematic_glow_strength;
    config.cinematic_contrast = app->cinematic_contrast;
    config.render_width = app->render_w;
    config.render_height = app->render_h;

    if (!voxelpilot_build_renderer_input(&config, cmd)) {
        return 0;
    }

    attach_label_overlay_to_render_command(app, cmd);
    return 1;
}
/* ============================================================
   Render one frame to the CUDA output buffer only (no GL window)
   ============================================================ */
void render_frame_gpu_snapshot(StandaloneApp *app)
{
    RendererInput cmd;

    if (!build_current_renderer_input(app, &cmd)) {
        return;
    }

    apply_render_command(&app->renderer, &cmd);
    app->last_render_ms = render_frame_gpu(&app->renderer);
    update_render_performance_average(app);
    app->frame_id++;
}

/* ============================================================
   Render one frame directly (no networking)
   ============================================================ */
void render_frame_standalone(StandaloneApp *app)
{
    RendererInput cmd;

    if (!build_current_renderer_input(app, &cmd)) {
        return;
    }

    /* Apply directly to renderer - NO NETWORK */
    apply_render_command(&app->renderer, &cmd);

    /* Render directly on GPU */
    app->last_render_ms = render_frame_gpu(&app->renderer);
    update_render_performance_average(app);
    app->frame_id++;

    /* Upload pixels directly to OpenGL texture (RGBA) */
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (app->render_w != app->tex_w ||
        app->render_h != app->tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     app->render_w, app->render_h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE,
                     app->renderer.h_out);
        app->tex_w = app->render_w;
        app->tex_h = app->render_h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        app->render_w, app->render_h,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        app->renderer.h_out);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ============================================================
   Draw fullscreen quad
   ============================================================ */
void draw_fullscreen_quad(StandaloneApp *app)
{
    glUseProgram(app->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->gl_tex);
    glUniform1i(app->tex_uniform_loc, 0);
    glBindVertexArray(app->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

void update_main_render_hover_pick(StandaloneApp *app)
{
    ImGuiIO *io = &ImGui::GetIO();
    int fb_w = 0;
    int fb_h = 0;
    double mouse_x = 0.0;
    double mouse_y = 0.0;
    float ndc_x;
    float ndc_y;
    float aspect;
    float tan_fov;
    float pick_threshold;
    float ray_origin[3];
    float ray_direction[3];
    float3 view_dir;
    float3 up;
    float3 right;
    float3 view_up;
    float3 ray;
    unsigned char label_id = 0;
    AnnotationRegion *region = NULL;
    VoxelPilotSemanticRegion *semantic = NULL;

    app->main_hover_active = 0;
    app->main_hover_label_id = 0;

    if (!app->renderer.volume_data ||
        app->renderer.W < 1 ||
        app->renderer.H < 1 ||
        app->renderer.D < 1 ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
        ImGui::IsAnyItemActive() ||
        glfwGetMouseButton(app->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        return;
    }

    glfwGetFramebufferSize(app->window, &fb_w, &fb_h);
    glfwGetCursorPos(app->window, &mouse_x, &mouse_y);
    if (fb_w <= 0 || fb_h <= 0 ||
        mouse_x < 0.0 || mouse_y < 0.0 ||
        mouse_x >= (double)fb_w || mouse_y >= (double)fb_h) {
        return;
    }

    ndc_x = ((float)mouse_x / (float)fb_w) * 2.0f - 1.0f;
    ndc_y = 1.0f - ((float)mouse_y / (float)fb_h) * 2.0f;
    aspect = (float)fb_w / (float)fb_h;
    tan_fov = tanf(0.5f * app->fov_y_deg * 3.14159265f / 180.0f);

    view_dir = f3_normalize(f3_make(
        app->cam_dir[0],
        app->cam_dir[1],
        app->cam_dir[2]));
    up = f3_normalize(f3_make(
        app->cam_up[0],
        app->cam_up[1],
        app->cam_up[2]));
    right = f3_normalize(f3_cross(view_dir, up));
    view_up = f3_cross(right, view_dir);
    ray = f3_normalize(
        f3_add(
            view_dir,
            f3_add(
                f3_scale(right, ndc_x * aspect * tan_fov),
                f3_scale(view_up, ndc_y * tan_fov))));

    ray_origin[0] = app->cam_pos[0];
    ray_origin[1] = app->cam_pos[1];
    ray_origin[2] = app->cam_pos[2];
    ray_direction[0] = ray.x;
    ray_direction[1] = ray.y;
    ray_direction[2] = ray.z;
    pick_threshold = fmaxf(0.03f, app->tf_center - app->tf_width * 0.5f);

    if (!voxelpilot_pick_volume_along_ray(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            ray_origin,
            ray_direction,
            fmaxf(app->step_size * 2.0f, 0.003f),
            pick_threshold,
            app->clip_min,
            app->clip_max,
            &app->main_hover_pick)) {
        snprintf(app->main_hover_description,
                 sizeof(app->main_hover_description),
                 "3D hover: no visible material under cursor.");
        return;
    }

    if (app->label_mask &&
        app->label_mask_W == app->renderer.W &&
        app->label_mask_H == app->renderer.H &&
        app->label_mask_D == app->renderer.D) {
        label_id = app->label_mask[app->main_hover_pick.index];
        region = find_annotation_by_label(app, label_id);
        semantic = find_semantic_region_by_label(app, label_id);
    }

    app->main_hover_active = 1;
    app->main_hover_label_id = label_id;
    if (semantic) {
        const char *note = semantic_relationship_note(app, label_id);
        snprintf(app->main_hover_description,
                 sizeof(app->main_hover_description),
                 "%s | %s | confidence %.0f%% | stability %.0f%% | source ASD-Lite. %s%s%s",
                 semantic->name,
                 semantic->archetype,
                 semantic->confidence * 100.0f,
                 semantic->stability_score * 100.0f,
                 semantic->description,
                 (note && note[0]) ? " " : "",
                 (note && note[0]) ? note : "");
    } else {
        voxelpilot_describe_voxel_context(
            app->main_hover_pick.intensity,
            label_id,
            region ? region->name : "",
            app->main_hover_description,
            sizeof(app->main_hover_description));
    }
}

void draw_main_render_hover_tooltip(StandaloneApp *app)
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    AnnotationRegion *region = NULL;
    VoxelPilotSemanticRegion *semantic = NULL;

    if (!app->main_hover_active) {
        return;
    }

    region = find_annotation_by_label(app, app->main_hover_label_id);
    semantic = find_semantic_region_by_label(app, app->main_hover_label_id);
    ImGui::SetNextWindowPos(ImVec2(mouse.x + 18.0f, mouse.y + 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("3D Hover Pick", NULL, flags);
    ImGui::Text("3D Hover Pick");
    ImGui::Separator();
    ImGui::TextWrapped("%s", app->main_hover_description);
    ImGui::Text("Voxel: %d, %d, %d",
                app->main_hover_pick.x,
                app->main_hover_pick.y,
                app->main_hover_pick.z);
    ImGui::Text("Point: %.3f, %.3f, %.3f",
                app->main_hover_pick.point[0],
                app->main_hover_pick.point[1],
                app->main_hover_pick.point[2]);
    if (semantic) {
        ImGui::Text("ASD rank: %d | importance %.0f%%",
                    semantic->importance_rank,
                    semantic->importance_score * 100.0f);
    }
    if (region) {
        ImGui::Text("Region voxels: %zu", region->voxel_count);
        ImGui::ColorButton("##main_hover_label_color",
                           ImVec4(region->color[0],
                                  region->color[1],
                                  region->color[2],
                                  1.0f),
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(18.0f, 18.0f));
    }
    ImGui::End();
}

