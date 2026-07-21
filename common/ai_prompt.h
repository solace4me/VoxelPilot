#ifndef VOXELPILOT_AI_PROMPT_H
#define VOXELPILOT_AI_PROMPT_H

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOXELPILOT_PROMPT_STATUS_MAX 256

#define VOXELPILOT_RENDER_QUALITY_INTERACTIVE 0
#define VOXELPILOT_RENDER_QUALITY_BALANCED    1
#define VOXELPILOT_RENDER_QUALITY_QUALITY     2

typedef struct {
    int   recognized;
    int   set_transfer;
    int   set_min_intensity;
    int   reset_view;
    int   auto_enhance;
    int   set_quality;
    int   quality_mode;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    float min_intensity;
    char  status[VOXELPILOT_PROMPT_STATUS_MAX];
} VoxelPilotPromptResult;

static inline float voxelpilot_ai_clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static inline void voxelpilot_ai_copy_lower(
    const char *src,
    char       *dst,
    size_t      dst_size)
{
    size_t i;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < dst_size && src[i]; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static inline int voxelpilot_ai_extract_first_number(
    const char *text,
    float      *out_value)
{
    const char *cursor;
    char *end_ptr;
    double value;

    if (!text || !out_value) {
        return 0;
    }

    cursor = text;
    while (*cursor) {
        int is_digit = (*cursor >= '0' && *cursor <= '9');
        int is_signed_number =
            *cursor == '-' &&
            ((*(cursor + 1) >= '0' && *(cursor + 1) <= '9') ||
             *(cursor + 1) == '.');
        if (is_digit || *cursor == '.' || is_signed_number) {
            value = strtod(cursor, &end_ptr);
            if (end_ptr != cursor) {
                *out_value = (float)value;
                return 1;
            }
        }
        ++cursor;
    }

    return 0;
}

static inline int voxelpilot_ai_text_contains_any(
    const char *text,
    const char *const *terms,
    int         term_count)
{
    int term_index;

    if (!text || !terms || term_count < 1) {
        return 0;
    }

    for (term_index = 0; term_index < term_count; ++term_index) {
        if (terms[term_index] && strstr(text, terms[term_index])) {
            return 1;
        }
    }

    return 0;
}

static inline void voxelpilot_init_prompt_result(
    VoxelPilotPromptResult *result)
{
    if (!result) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    result->tf_center = 0.45f;
    result->tf_width = 0.35f;
    result->tf_opacity_scale = 1.8f;
    result->min_intensity = 0.0f;
    snprintf(result->status, sizeof(result->status), "No prompt action recognized.");
}

static inline int voxelpilot_parse_prompt_action(
    const char              *prompt,
    VoxelPilotPromptResult *result)
{
    static const char *const visibility_terms[] = {
        "auto enhance", "auto-enhance", "make visible", "more visible",
        "easier to see", "bring out", "brighter", "brighten", "too dark",
        "very dark", "enhance contrast", "more contrast", "clearer", "sharper view",
        "show better", "not showing", "hard to see", "improve visibility"
    };
    static const char *const reset_terms[] = {
        "reset", "full volume", "show all", "restore", "start over"
    };
    static const char *const low_cut_terms[] = {
        "below", "under", "less than", "lower than", "threshold", "cutoff"
    };
    static const char *const high_cut_terms[] = {
        "above", "over", "greater than", "higher than", "at least"
    };
    static const char *const hide_terms[] = {
        "hide", "remove", "exclude", "drop", "suppress", "mask out"
    };
    static const char *const show_terms[] = {
        "show", "keep", "only", "isolate", "segment", "reveal", "focus"
    };
    static const char *const high_density_terms[] = {
        "bone", "cortical", "dense", "high-density", "high density", "shell", "hard tissue"
    };
    static const char *const mid_density_terms[] = {
        "soft tissue", "medium density", "mid density", "muscle", "organ", "tissue"
    };
    static const char *const low_density_terms[] = {
        "low density", "air", "background", "dark material", "low intensity"
    };
    static const char *const quality_terms[] = {
        "quality", "high fidelity", "best detail", "maximum detail", "high detail", "gpu quality"
    };
    static const char *const faster_terms[] = {
        "fast", "faster", "interactive", "responsive", "smooth", "speed"
    };
    char text[512];
    float value = 0.0f;
    int has_number;
    int wants_hide;
    int wants_show;
    int mentions_low_cut;
    int mentions_high_cut;

    if (!result) {
        return 0;
    }

    voxelpilot_init_prompt_result(result);
    voxelpilot_ai_copy_lower(prompt, text, sizeof(text));

    if (!text[0]) {
        return 0;
    }

    has_number = voxelpilot_ai_extract_first_number(text, &value);
    wants_hide = voxelpilot_ai_text_contains_any(text, hide_terms, (int)(sizeof(hide_terms) / sizeof(hide_terms[0])));
    wants_show = voxelpilot_ai_text_contains_any(text, show_terms, (int)(sizeof(show_terms) / sizeof(show_terms[0])));
    mentions_low_cut = voxelpilot_ai_text_contains_any(text, low_cut_terms, (int)(sizeof(low_cut_terms) / sizeof(low_cut_terms[0])));
    mentions_high_cut = voxelpilot_ai_text_contains_any(text, high_cut_terms, (int)(sizeof(high_cut_terms) / sizeof(high_cut_terms[0])));

    if (voxelpilot_ai_text_contains_any(text, quality_terms, (int)(sizeof(quality_terms) / sizeof(quality_terms[0])))) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_QUALITY;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the high-quality GPU rendering preset.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, faster_terms, (int)(sizeof(faster_terms) / sizeof(faster_terms[0])))) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_INTERACTIVE;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the interactive GPU rendering preset.");
        return 1;
    }

    if (strstr(text, "balanced")) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the balanced GPU rendering preset.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, reset_terms, (int)(sizeof(reset_terms) / sizeof(reset_terms[0])))) {
        result->recognized = 1;
        result->reset_view = 1;
        result->set_transfer = 1;
        result->auto_enhance = 1;
        result->tf_center = 0.45f;
        result->tf_width = 0.35f;
        result->tf_opacity_scale = 1.8f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt reset clipping and requested histogram-based view enhancement.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, visibility_terms, (int)(sizeof(visibility_terms) / sizeof(visibility_terms[0])))) {
        result->recognized = 1;
        result->auto_enhance = 1;
        snprintf(result->status, sizeof(result->status),
                 "Prompt requested automatic visibility enhancement from the active histogram.");
        return 1;
    }

    if ((wants_hide || wants_show || mentions_low_cut || mentions_high_cut || strstr(text, "intensity")) &&
        has_number) {
        value = voxelpilot_ai_clampf(value, 0.0f, 1.0f);
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = value;
        result->tf_width = voxelpilot_ai_clampf(1.0f - value, 0.05f, 1.0f);
        result->tf_center = voxelpilot_ai_clampf(value + result->tf_width * 0.5f, 0.0f, 1.0f);
        result->tf_opacity_scale = wants_hide || mentions_low_cut || mentions_high_cut ? 1.55f : 1.35f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a flexible intensity cutoff at %.3f.", value);
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, high_density_terms, (int)(sizeof(high_density_terms) / sizeof(high_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.62f;
        result->tf_center = 0.78f;
        result->tf_width = 0.24f;
        result->tf_opacity_scale = 1.85f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a high-density bone/shell material intent.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, mid_density_terms, (int)(sizeof(mid_density_terms) / sizeof(mid_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.18f;
        result->tf_center = 0.45f;
        result->tf_width = 0.42f;
        result->tf_opacity_scale = 1.35f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a mid-density tissue material intent.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, low_density_terms, (int)(sizeof(low_density_terms) / sizeof(low_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.02f;
        result->tf_center = 0.20f;
        result->tf_width = 0.32f;
        result->tf_opacity_scale = 1.45f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a low-density material intent.");
        return 1;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_AI_PROMPT_H */
