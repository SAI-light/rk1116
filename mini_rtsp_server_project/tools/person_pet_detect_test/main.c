#include "rockiva_detector.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MODEL_DIR "/root/rockiva_model"
#define DEFAULT_THRESHOLD 60U
#define DEFAULT_TIMEOUT_MS 10000U

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --nv12 FILE --width W --height H [options]\n"
            "\n"
            "Required:\n"
            "      --nv12 FILE           Raw contiguous NV12 file\n"
            "      --width W             Image width\n"
            "      --height H            Image height\n"
            "\n"
            "Options:\n"
            "  -m, --model-dir DIR       RockIVA model directory\n"
            "                            default: %s\n"
            "  -t, --threshold N         PERSON/PET threshold [0,100]\n"
            "                            default: %u\n"
            "      --timeout-ms N        Inference timeout in milliseconds\n"
            "                            default: %u\n"
            "  -h, --help                Show this help\n",
            program,
            DEFAULT_MODEL_DIR,
            DEFAULT_THRESHOLD,
            DEFAULT_TIMEOUT_MS);
}

static int parse_u32(const char *text,
                     unsigned int min_value,
                     unsigned int max_value,
                     unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return -1;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < min_value || parsed > max_value) {
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    const char *nv12_path = NULL;
    const char *model_dir = DEFAULT_MODEL_DIR;
    unsigned int width = 0U;
    unsigned int height = 0U;
    unsigned int threshold = DEFAULT_THRESHOLD;
    unsigned int timeout_ms = DEFAULT_TIMEOUT_MS;
    RockIvaDetectionSummary summary;
    int i;

    memset(&summary, 0, sizeof(summary));

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--nv12") == 0 && i + 1 < argc) {
            nv12_path = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 2U, 65535U, &width) != 0) {
                fprintf(stderr, "Invalid width: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 2U, 65535U, &height) != 0) {
                fprintf(stderr, "Invalid height: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if ((strcmp(argv[i], "-m") == 0 ||
                    strcmp(argv[i], "--model-dir") == 0) &&
                   i + 1 < argc) {
            model_dir = argv[++i];
        } else if ((strcmp(argv[i], "-t") == 0 ||
                    strcmp(argv[i], "--threshold") == 0) &&
                   i + 1 < argc) {
            if (parse_u32(argv[++i], 0U, 100U, &threshold) != 0) {
                fprintf(stderr, "Invalid threshold: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--timeout-ms") == 0 &&
                   i + 1 < argc) {
            if (parse_u32(argv[++i], 1U, 60000U, &timeout_ms) != 0) {
                fprintf(stderr, "Invalid timeout: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (nv12_path == NULL || width == 0U || height == 0U) {
        fprintf(stderr, "--nv12, --width and --height are required\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if ((width & 1U) != 0U || (height & 1U) != 0U) {
        fprintf(stderr, "NV12 width and height must be even\n");
        return EXIT_FAILURE;
    }

    if (rockiva_detect_nv12(model_dir,
                            nv12_path,
                            width,
                            height,
                            (uint8_t)threshold,
                            (uint8_t)threshold,
                            (int)timeout_ms,
                            &summary) != 0) {
        return EXIT_FAILURE;
    }

    printf("\nDetection summary\n");
    printf("  frame_id       : %u\n", summary.frame_id);
    printf("  object_count   : %u\n", summary.object_count);
    printf("  person_count   : %u\n", summary.person_count);
    printf("  pet_count      : %u\n", summary.pet_count);
    printf("  human_detected : %s\n",
           summary.person_count > 0U ? "yes" : "no");
    printf("  pet_detected   : %s\n",
           summary.pet_count > 0U ? "yes" : "no");

    return EXIT_SUCCESS;
}
