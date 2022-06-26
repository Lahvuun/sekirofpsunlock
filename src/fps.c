#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "pe.h"

struct game_patch_data {
	float fps;
};

static struct ignorable_byte pattern_framelock_fuzzy[] = {
	{ .is_ignored = false, .value = 0xc7 },
	{ .is_ignored = false, .value = 0x43 },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x4c },
	{ .is_ignored = false, .value = 0x89 },
	{ .is_ignored = false, .value = 0xab },
};

static struct ignorable_byte pattern_framelock_speed_fix[] = {
	{ .is_ignored = false, .value = 0xf3 },
	{ .is_ignored = false, .value = 0x0f },
	{ .is_ignored = false, .value = 0x58 },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x0f },
	{ .is_ignored = false, .value = 0xc6 },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x00 },
	{ .is_ignored = false, .value = 0x0f },
	{ .is_ignored = false, .value = 0x51 },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0xf3 },
	{ .is_ignored = false, .value = 0x0f },
	{ .is_ignored = false, .value = 0x59 },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x0f },
	{ .is_ignored = false, .value = 0x2f },
};

static float patch_framelock_speed_fix_matrix[] = {
	15.0f,	16.0f,	  16.6667f, 18.0f,    18.6875f, 18.8516f, 20.0f,
	24.0f,	25.0f,	  27.5f,    30.0f,    32.0f,	38.5f,	  40.0f,
	48.0f,	49.5f,	  50.0f,    57.2958f, 60.0f,	64.0f,	  66.75f,
	67.0f,	78.8438f, 80.0f,    84.0f,    90.0f,	93.8f,	  100.0f,
	120.0f, 127.0f,	  128.0f,   130.0f,   140.0f,	144.0f,	  150.0f,
};

static bool string_to_float(char *s, float *f_out)
{
	int errno_stored = errno;
	errno = 0;
	char *endptr = NULL;
	float f = strtof(s, &endptr);
	if (errno) {
		perror("strtof() failed");
		return false;
	}
	if (endptr == s) {
		fprintf(stderr, "strtof() failed, no digits were read.\n");
		return false;
	}
	errno = errno_stored;

	*f_out = f;

	return true;
}

static float find_speed_fix_for_refresh_rate(float frame_limit) {
	float ideal_speed_fix = frame_limit / 2.0f;
	float closest_speed_fix = 30.0f;
	for (size_t i = 0; i < sizeof(patch_framelock_speed_fix_matrix) / sizeof(float); ++i) {
		float speed_fix = patch_framelock_speed_fix_matrix[i];
		if (fabsf(ideal_speed_fix - speed_fix) < fabsf(ideal_speed_fix - closest_speed_fix)) {
			closest_speed_fix = speed_fix;
		}
	}

	return closest_speed_fix;
}

static bool patch_framelock(FILE *f, float fps, uint8_t *section_bytes, size_t section_bytes_size, size_t section_position)
{
	size_t pattern_framelock_index = 0;
	if (!find_pattern(pattern_framelock_fuzzy, sizeof(pattern_framelock_fuzzy) / sizeof(struct ignorable_byte), section_bytes, section_bytes_size, &pattern_framelock_index)) {
		fprintf(stderr, "find_pattern(pattern_framelock_fuzzy, ...) failed\n");
		return false;
	}

	size_t framelock_value_index = pattern_framelock_index + 3;
	static_assert(sizeof(fps) == 4, "the game expects fps to be 4 bytes long");
	float delta_time = 1000.0f / fps / 1000.0f;
	if (!seek_and_write_bytes((uint8_t *)&delta_time, sizeof(fps), section_position + framelock_value_index, f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_framelock_speed_fix(FILE *f, float fps, uint8_t *section_bytes, size_t section_bytes_size, size_t section_position)
{
	size_t pattern_framelock_speed_fix_index = 0;
	if (!find_pattern(pattern_framelock_speed_fix, sizeof(pattern_framelock_speed_fix) / sizeof(struct ignorable_byte), section_bytes, section_bytes_size, &pattern_framelock_speed_fix_index)) {
		fprintf(stderr, "find_pattern(pattern_framelock_speed_fix_position, ...) failed\n");
		return false;
	}

	size_t framelock_speed_fix_offset_index = pattern_framelock_speed_fix_index + 15;
	uint32_t framelock_speed_fix_offset = *(uint32_t *)(section_bytes + framelock_speed_fix_offset_index);
	size_t framelock_speed_fix_position = section_position + framelock_speed_fix_offset_index + 4 + framelock_speed_fix_offset;
	float framelock_speed_fix_value = find_speed_fix_for_refresh_rate(fps);
	static_assert(sizeof(framelock_speed_fix_value) == 4, "the game expects framelock_speed_fix_value to be 4 bytes long");
	if (!seek_and_write_bytes((uint8_t *)&framelock_speed_fix_value, sizeof(framelock_speed_fix_value), framelock_speed_fix_position, f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}
static bool patch_game_fps_with_section(FILE *f, float fps, uint8_t *section_bytes, size_t section_bytes_size, size_t section_position)
{
	if (!patch_framelock(f, fps, section_bytes, section_bytes_size, section_position)) {
		fprintf(stderr, "patch_framelock() failed\n");
		return false;
	}

	if (!patch_framelock_speed_fix(f, fps, section_bytes, section_bytes_size, section_position)) {
		fprintf(stderr, "patch_framelock_speed_fix() failed\n");
		return false;
	}

	return true;
}

static bool patch_game_fps(struct game_patch_context *context)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".text", context->f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success = patch_game_fps_with_section(context->f, context->game_patch_data->fps, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool set_fps(pid_t pid, float fps)
{
	struct game_patch_data game_patch_data = {
		.fps = fps,
	};
	struct game_patch game_patch = {
		.game_patch_function = patch_game_fps,
		.game_patch_data = &game_patch_data,
	};
	if (!patch(pid, &game_patch)) {
		fprintf(stderr, "patch() failed\n");
		return false;
	}

	return true;
}

int main(int argc, char *argv[])
{
	if (argc < 1) {
		fprintf(stderr, "argc must be at least 1\n");
		return EXIT_FAILURE;
	}

	if (argc != 3) {
		fprintf(stderr, "usage: %s <pid> <fps>\n", argv[0]);
		return EXIT_FAILURE;
	}

	pid_t pid = 0;
	if (!string_to_pid_t(argv[1], 10, &pid)) {
		fprintf(stderr, "string_to_pid_t() failed\n");
		return EXIT_FAILURE;
	}

	float fps = 0.0f;
	if (!string_to_float(argv[2], &fps)) {
		fprintf(stderr, "string_to_float() failed\n");
		return EXIT_FAILURE;
	}

	if (fps < 30 || fps > 300) {
		fprintf(stderr, "fps needs to be at least 30 and at most 300\n");
		return EXIT_FAILURE;
	}

	if (!set_fps(pid, fps)) {
		fprintf(stderr, "set_fps() failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
