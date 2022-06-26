#include <stdlib.h>

#include "common.h"
#include "pe.h"

struct game_patch_data {
	uint32_t screen_width;
	uint32_t game_width;
	uint32_t game_height;
};

static struct ignorable_byte pattern_resolution_default[] = {
	{ .is_ignored = false, .value = 0x80 }, { .is_ignored = false, .value = 0x7 },
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0x38 }, { .is_ignored = false, .value = 0x4 },
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x8 },
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0x80 }, { .is_ignored = false, .value = 0x4 },
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x0 },
};

static struct ignorable_byte pattern_resolution_default_720[] = {
	{ .is_ignored = false, .value = 0x0 },	{ .is_ignored = false, .value = 0x5 },
	{ .is_ignored = false, .value = 0x0 }, { .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0xd0 }, { .is_ignored = false, .value = 0x2 },
	{ .is_ignored = false, .value = 0x0 }, { .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0xa0 }, { .is_ignored = false, .value = 0x5 },
	{ .is_ignored = false, .value = 0x0 }, { .is_ignored = false, .value = 0x0 },
	{ .is_ignored = false, .value = 0x2a }, { .is_ignored = false, .value = 0x3 },
	{ .is_ignored = false, .value = 0x0 }, { .is_ignored = false, .value = 0x0 },
};

static struct ignorable_byte pattern_resolution_scaling_fix[] = {
	{ .is_ignored = false, .value = 0x85 },
	{ .is_ignored = false, .value = 0xc9 },
	{ .is_ignored = false, .value = 0x74 },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x47 },
	{ .is_ignored = false, .value = 0x8b },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x45 },
	{ .is_ignored = true },
	{ .is_ignored = true },
	{ .is_ignored = false, .value = 0x74 },
};

static bool string_to_uint32_t(char *string, uint32_t *value_out)
{
	uintmax_t value_uintmax_t = 0;
	if (!string_to_uintmax_t(string, 10, &value_uintmax_t)) {
		fprintf(stderr, "string_to_uintmax_t() failed\n");
		return false;
	}

	if (value_uintmax_t > UINT32_MAX) {
		fprintf(stderr, "value is larger than UINT32_MAX\n");
		return false;
	}

	*value_out = value_uintmax_t;

	return true;
}

static bool patch_resolution_default_with_section(struct game_patch_context *context, uint8_t *section_bytes,
						  size_t section_bytes_size, size_t section_positon)
{
	size_t pattern_resolution_default_index = 0;
	if (context->game_patch_data->screen_width < 1920) {
		if (!find_pattern(pattern_resolution_default_720,
				  sizeof(pattern_resolution_default_720) / sizeof(struct ignorable_byte), section_bytes,
				  section_bytes_size, &pattern_resolution_default_index)) {
			fprintf(stderr, "find_pattern() failed\n");
			return false;
		}
	} else {
		if (!find_pattern(pattern_resolution_default,
				  sizeof(pattern_resolution_default) / sizeof(struct ignorable_byte), section_bytes,
				  section_bytes_size, &pattern_resolution_default_index)) {
			fprintf(stderr, "find_pattern() failed\n");
			return false;
		}
	}

	if (!seek_and_write_bytes((uint8_t *)&context->game_patch_data->game_width,
				  sizeof(context->game_patch_data->game_width),
				  section_positon + pattern_resolution_default_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	if (!seek_and_write_bytes((uint8_t *)&context->game_patch_data->game_height,
				  sizeof(context->game_patch_data->game_height),
				  section_positon + pattern_resolution_default_index + 4, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_default(struct game_patch_context *context)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".data", context->f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success =
		patch_resolution_default_with_section(context, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool patch_resolution_scaling_fix_with_section(struct game_patch_context *context, uint8_t *section_bytes,
						      size_t section_bytes_size, size_t section_position)
{
	size_t pattern_resolution_scaling_fix_index = 0;
	if (!find_pattern(pattern_resolution_scaling_fix,
			 sizeof(pattern_resolution_scaling_fix) / sizeof(struct ignorable_byte), section_bytes,
			 section_bytes_size, &pattern_resolution_scaling_fix_index)) {
		fprintf(stderr, "find_pattern() failed\n");
		return false;
	}

	uint8_t nop_jmp[] = { 0x90, 0x90, 0xeb };
	if (!seek_and_write_bytes(nop_jmp, sizeof(nop_jmp) / sizeof(uint8_t),
				 section_position + pattern_resolution_scaling_fix_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_scaling_fix(struct game_patch_context *context)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".text", context->f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success =
		patch_resolution_scaling_fix_with_section(context, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool patch_resolution(struct game_patch_context *context)
{
	if (!patch_resolution_default(context)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	if (!patch_resolution_scaling_fix(context)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	return true;
}

static bool set_resolution(pid_t pid, uint32_t screen_width, uint32_t game_width, uint32_t game_height)
{
	struct game_patch_data game_patch_data = {
		.screen_width = screen_width,
		.game_width = game_width,
		.game_height = game_height,
	};
	struct game_patch game_patch = {
		.game_patch_function = patch_resolution,
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

	if (argc != 5) {
		fprintf(stderr, "usage: %s <pid> <screen-width> <game-width> <game-height>\n", argv[0]);
		return EXIT_FAILURE;
	}

	pid_t pid = 0;
	if (!string_to_pid_t(argv[1], 10, &pid)) {
		fprintf(stderr, "string_to_pid_t() failed\n");
		return EXIT_FAILURE;
	}

	uint32_t screen_width = 0;
	if (!string_to_uint32_t(argv[2], &screen_width)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return EXIT_FAILURE;
	}

	uint32_t game_width = 0;
	if (!string_to_uint32_t(argv[3], &game_width)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return EXIT_FAILURE;
	}

	uint32_t game_height = 0;
	if (!string_to_uint32_t(argv[4], &game_height)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return EXIT_FAILURE;
	}

	if (!set_resolution(pid, screen_width, game_width, game_height)) {
		fprintf(stderr, "set_resolution() failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
