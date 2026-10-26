#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "semantic_discovery.h"

static bool parse_int(const char *text, int *out_value) {
    char *end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (!text || !out_value || !end || *end != '\0' || value < 1 || value > 8192) return false;
    *out_value = (int)value;
    return true;
}

static bool parse_float(const char *text, float *out_value) {
    char *end = nullptr;
    double value = std::strtod(text, &end);
    if (!text || !out_value || !end || *end != '\0') return false;
    *out_value = (float)value;
    return true;
}

static bool parse_size(const char *text, size_t *out_value) {
    char *end = nullptr;
    unsigned long long value = std::strtoull(text, &end, 10);
    if (!text || !out_value || !end || *end != '\0' || value == 0ULL) return false;
    *out_value = (size_t)value;
    return true;
}

static void json_escape(std::ostream &out, const char *text) {
    if (!text) return;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        unsigned char ch = *cursor;
        if (ch == '\\' || ch == '"') out << '\\' << (char)ch;
        else if (ch == '\n') out << "\\n";
        else if (ch == '\r') out << "\\r";
        else if (ch == '\t') out << "\\t";
        else out << (char)ch;
    }
}

static const char *relation_name(VoxelPilotRegionRelationType type) {
    switch (type) {
    case VOXELPILOT_REGION_RELATION_ADJACENT: return "adjacent";
    case VOXELPILOT_REGION_RELATION_CONTAINS: return "contains";
    case VOXELPILOT_REGION_RELATION_INSIDE: return "inside";
    case VOXELPILOT_REGION_RELATION_ISOLATED: return "isolated";
    default: return "unknown";
    }
}

static int write_report(
    const std::string &path,
    const std::string &volume_path,
    int width,
    int height,
    int depth,
    float sensitivity,
    size_t min_region_voxels,
    const std::vector<float> &histogram,
    const std::vector<unsigned char> &label_mask,
    const VoxelPilotSemanticRegion *regions,
    int region_count,
    const VoxelPilotRegionRelation *relations,
    int relation_count) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "could not write report: " << path << "\n";
        return 1;
    }
    size_t labeled_voxels = 0;
    for (unsigned char label : label_mask) if (label != 0) ++labeled_voxels;
    int peak_bin = (int)std::distance(histogram.begin(), std::max_element(histogram.begin(), histogram.end()));

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"volume\": \""; json_escape(out, volume_path.c_str()); out << "\",\n";
    out << "  \"dimensions\": [" << width << ", " << height << ", " << depth << "],\n";
    out << "  \"sensitivity\": " << sensitivity << ",\n";
    out << "  \"min_region_voxels\": " << min_region_voxels << ",\n";
    out << "  \"region_count\": " << region_count << ",\n";
    out << "  \"relation_count\": " << relation_count << ",\n";
    out << "  \"labeled_voxels\": " << labeled_voxels << ",\n";
    out << "  \"labeled_ratio\": " << (label_mask.empty() ? 0.0 : (double)labeled_voxels / (double)label_mask.size()) << ",\n";
    out << "  \"histogram_peak_bin\": " << peak_bin << ",\n";
    out << "  \"regions\": [\n";
    for (int i = 0; i < region_count; ++i) {
        const VoxelPilotSemanticRegion &region = regions[i];
        out << "    {\n";
        out << "      \"label_id\": " << (int)region.label_id << ",\n";
        out << "      \"rank\": " << region.importance_rank << ",\n";
        out << "      \"name\": \""; json_escape(out, region.name); out << "\",\n";
        out << "      \"archetype\": \""; json_escape(out, region.archetype); out << "\",\n";
        out << "      \"category\": \""; json_escape(out, region.category); out << "\",\n";
        out << "      \"elongation\": " << region.elongation << ",\n";
        out << "      \"voxel_count\": " << region.voxel_count << ",\n";
        out << "      \"mean_intensity\": " << region.mean_intensity << ",\n";
        out << "      \"confidence\": " << region.confidence << ",\n";
        out << "      \"stability\": " << region.stability_score << ",\n";
        out << "      \"importance\": " << region.importance_score << ",\n";
        out << "      \"centroid\": [" << region.centroid[0] << ", " << region.centroid[1] << ", " << region.centroid[2] << "],\n";
        out << "      \"bbox_min\": [" << region.bbox_min[0] << ", " << region.bbox_min[1] << ", " << region.bbox_min[2] << "],\n";
        out << "      \"bbox_max\": [" << region.bbox_max[0] << ", " << region.bbox_max[1] << ", " << region.bbox_max[2] << "]\n";
        out << "    }" << (i + 1 < region_count ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"relations\": [\n";
    for (int i = 0; i < relation_count; ++i) {
        const VoxelPilotRegionRelation &relation = relations[i];
        out << "    {\"region_a\": " << (int)relation.region_a
            << ", \"region_b\": " << (int)relation.region_b
            << ", \"type\": \"" << relation_name(relation.type)
            << "\", \"confidence\": " << relation.confidence << "}";
        out << (i + 1 < relation_count ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return 0;
}

int main(int argc, char **argv) {
    std::string volume_path;
    std::string type = "uint8";
    std::string report_path = "build/asd_report.json";
    int width = 0, height = 0, depth = 0;
    int histogram_bins = 128;
    int min_regions = 1;
    float sensitivity = 0.55f;
    size_t min_region_voxels = 128;
    float spacing[3] = { 1.0f, 1.0f, 1.0f };

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--volume") == 0 && i + 1 < argc) volume_path = argv[++i];
        else if (std::strcmp(arg, "--width") == 0 && i + 1 < argc && parse_int(argv[++i], &width)) {}
        else if (std::strcmp(arg, "--height") == 0 && i + 1 < argc && parse_int(argv[++i], &height)) {}
        else if (std::strcmp(arg, "--depth") == 0 && i + 1 < argc && parse_int(argv[++i], &depth)) {}
        else if (std::strcmp(arg, "--type") == 0 && i + 1 < argc) type = argv[++i];
        else if (std::strcmp(arg, "--sensitivity") == 0 && i + 1 < argc && parse_float(argv[++i], &sensitivity)) {}
        else if (std::strcmp(arg, "--min-region-voxels") == 0 && i + 1 < argc && parse_size(argv[++i], &min_region_voxels)) {}
        else if (std::strcmp(arg, "--min-regions") == 0 && i + 1 < argc && parse_int(argv[++i], &min_regions)) {}
        else if (std::strcmp(arg, "--report") == 0 && i + 1 < argc) report_path = argv[++i];
        else if (std::strcmp(arg, "--spacing") == 0 && i + 3 < argc &&
                 parse_float(argv[i + 1], &spacing[0]) && parse_float(argv[i + 2], &spacing[1]) &&
                 parse_float(argv[i + 3], &spacing[2])) { i += 3; }
        else {
            std::cerr << "Usage: " << argv[0] << " --volume file.raw --width W --height H --depth D --type uint8|uint16|float32 [--spacing SX SY SZ] [--sensitivity S] [--min-region-voxels N] [--min-regions N] [--report file.json]\n";
            return 2;
        }
    }
    if (volume_path.empty() || width < 1 || height < 1 || depth < 1) {
        std::cerr << "missing required volume or dimensions\n";
        return 2;
    }

    size_t voxel_count = (size_t)width * (size_t)height * (size_t)depth;
    size_t bytes_per_voxel = type == "uint8" ? 1 : (type == "uint16" ? 2 : (type == "float32" ? 4 : 0));
    if (bytes_per_voxel == 0) {
        std::cerr << "unsupported type: " << type << "\n";
        return 2;
    }

    std::ifstream input(volume_path, std::ios::binary);
    if (!input) {
        std::cerr << "could not open volume: " << volume_path << "\n";
        return 1;
    }
    std::vector<unsigned char> raw(voxel_count * bytes_per_voxel);
    input.read((char *)raw.data(), (std::streamsize)raw.size());
    if (input.gcount() != (std::streamsize)raw.size()) {
        std::cerr << "volume file size is smaller than expected\n";
        return 1;
    }

    std::vector<float> volume(voxel_count);
    std::vector<float> histogram((size_t)histogram_bins, 0.0f);
    /* uint16 is scaled by the data's own range, exactly as the app's
       importer does (common/volume_import.cpp) - so this tool measures
       what the app actually discovers. */
    uint16_t u16_lo = 65535, u16_hi = 0;
    if (type == "uint16") {
        for (size_t i = 0; i < voxel_count; ++i) {
            uint16_t sample = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
            if (sample < u16_lo) u16_lo = sample;
            if (sample > u16_hi) u16_hi = sample;
        }
    }
    for (size_t i = 0; i < voxel_count; ++i) {
        float value = 0.0f;
        if (type == "uint8") value = (float)raw[i] / 255.0f;
        else if (type == "uint16") {
            uint16_t sample = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
            value = u16_hi > u16_lo ? (float)(sample - u16_lo) / (float)(u16_hi - u16_lo) : 0.0f;
        } else {
            std::memcpy(&value, &raw[i * 4], sizeof(float));
            if (value < 0.0f) value = 0.0f;
            if (value > 1.0f) value = 1.0f;
        }
        volume[i] = value;
        int bin = (int)(value * (float)(histogram_bins - 1));
        if (bin < 0) bin = 0;
        if (bin >= histogram_bins) bin = histogram_bins - 1;
        histogram[(size_t)bin] += 1.0f;
    }

    std::vector<unsigned char> label_mask(voxel_count, 0);
    VoxelPilotSemanticRegion regions[VOXELPILOT_ASD_MAX_REGIONS];
    VoxelPilotRegionRelation relations[VOXELPILOT_ASD_MAX_RELATIONS];
    int region_count = 0;
    int relation_count = 0;

    /* Same call the app makes: adaptive material classes, real spacing. */
    int ok = voxelpilot_discover_semantic_regions_ex(volume.data(), width, height, depth, spacing, 1, VOXELPILOT_ASD_MAX_REGIONS, sensitivity, min_region_voxels, label_mask.data(), regions, &region_count);
    if (!ok || region_count < min_regions) {
        std::cerr << "ASD verification failed: discovered " << region_count << " regions, expected at least " << min_regions << "\n";
        return 1;
    }
    voxelpilot_compute_region_relationships(regions, region_count, relations, VOXELPILOT_ASD_MAX_RELATIONS, &relation_count);
    if (write_report(report_path, volume_path, width, height, depth, sensitivity, min_region_voxels, histogram, label_mask, regions, region_count, relations, relation_count) != 0) return 1;

    std::cout << "ASD verification passed: " << region_count << " regions, " << relation_count << " relations";
    if (region_count > 0) std::cout << ", top=\"" << regions[0].name << "\" archetype=" << regions[0].archetype << " voxels=" << regions[0].voxel_count;
    std::cout << "\n";
    return 0;
}
