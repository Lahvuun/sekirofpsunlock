#include "resolution.h"

#include "common.h"
#include "signals.h"

#include <stdlib.h>
#include <sys/ptrace.h>
#include <time.h>

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

static bool patch_resolution_default_with_section(struct context *context, uint32_t screen_width, uint32_t game_width, uint32_t game_height, uint8_t *section_bytes,
						  size_t section_bytes_size, size_t section_position)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	size_t pattern_resolution_default_index = 0;
	time_t current_time = start_time;
	while (true) {
		if (got_sigterm) {
			fprintf(stderr, "got SIGTERM, exiting\n");
			return false;
		}

		if (got_sigchld) {
			if (ptrace(PTRACE_CONT, context->pid, NULL, NULL) == -1) {
				perror("ptrace(PTRACE_CONT) failed");
				return false;
			}
			got_sigchld = 0;
		}

		if (screen_width < 1920) {
			if (find_pattern(pattern_resolution_default_720,
					sizeof(pattern_resolution_default_720) / sizeof(struct ignorable_byte), context->f, section_bytes,
					section_bytes_size, section_position, &pattern_resolution_default_index)) {
				stop_and_wait(context);
				break;
			}
		} else {
			if (find_pattern(pattern_resolution_default,
					sizeof(pattern_resolution_default) / sizeof(struct ignorable_byte), context->f, section_bytes,
					section_bytes_size, section_position, &pattern_resolution_default_index)) {
				stop_and_wait(context);
				break;
			}
		}

		current_time = time(NULL);
		if ((time_t)-1 == current_time) {
			perror("time() failed");
			return false;
		}
		if ((current_time - start_time) > context->timeout) {
			fprintf(stderr, "timeout reached while looking for speed fix pattern\n");
			return false;
		}
	}

	if (!seek_and_write_bytes((uint8_t *)&game_width,
				  sizeof(game_width),
				  section_position + pattern_resolution_default_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	if (!seek_and_write_bytes((uint8_t *)&game_height,
				  sizeof(game_height),
				  section_position + pattern_resolution_default_index + 4, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_default(struct context *context, uint32_t screen_width, uint32_t game_width, uint32_t game_height)
{
	size_t section_position;
	size_t section_size = 0;
	if (!find_section_info(".data", context->f, &section_position, &section_size)) {
		fprintf(stderr, "find_section_info(\".data\", ...) failed\n");
		return false;
	}

	uint8_t *section_buffer = calloc(section_size, sizeof(uint8_t));
	if (!section_buffer) {
		fprintf(stderr, "calloc() failed\n");
		return false;
	}

	bool success =
		patch_resolution_default_with_section(context, screen_width, game_width, game_height, section_buffer, section_size, section_position);

	free(section_buffer);

	return success;
}

static bool patch_resolution_scaling_fix_with_section(struct context *context, uint8_t *section_bytes,
						      size_t section_bytes_size, size_t section_position)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	size_t pattern_resolution_scaling_fix_index = 0;
	time_t current_time = start_time;
	while (true) {
		if (got_sigterm) {
			fprintf(stderr, "got SIGTERM, exiting\n");
			return false;
		}

		if (got_sigchld) {
			if (ptrace(PTRACE_CONT, context->pid, NULL, NULL) == -1) {
				perror("ptrace(PTRACE_CONT) failed");
				return false;
			}
			got_sigchld = 0;
		}

		if (find_pattern(pattern_resolution_scaling_fix,
				sizeof(pattern_resolution_scaling_fix) / sizeof(struct ignorable_byte), context->f, section_bytes,
				section_bytes_size, section_position, &pattern_resolution_scaling_fix_index)) {
			stop_and_wait(context);
			break;
		}

		current_time = time(NULL);
		if ((time_t)-1 == current_time) {
			perror("time() failed");
			return false;
		}
		if ((current_time - start_time) > context->timeout) {
			fprintf(stderr, "timeout reached while looking for speed fix pattern\n");
			return false;
		}
	}

	uint8_t nop_jmp[] = { 0x90, 0x90, 0xeb };
	if (!seek_and_write_bytes(nop_jmp, sizeof(nop_jmp) / sizeof(uint8_t),
				 section_position + pattern_resolution_scaling_fix_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_scaling_fix(struct context *context)
{
	size_t section_position;
	size_t section_size = 0;
	if (!find_section_info(".text", context->f, &section_position, &section_size)) {
		fprintf(stderr, "find_section_info(\".text\", ...) failed\n");
		return false;
	}

	uint8_t *section_buffer = calloc(section_size, sizeof(uint8_t));
	if (!section_buffer) {
		fprintf(stderr, "calloc() failed\n");
		return false;
	}

	bool success =
		patch_resolution_scaling_fix_with_section(context, section_buffer, section_size, section_position);

	free(section_buffer);

	return success;
}

static bool patch_resolution(struct context *context, uint32_t screen_width, uint32_t game_width, uint32_t game_height)
{
	if (!patch_resolution_default(context, screen_width, game_width, game_height)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	if (!patch_resolution_scaling_fix(context)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	return true;
}

bool main_resolution(struct context *context, int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "argc must be at least 3\n");
		return false;
	}

	uint32_t screen_width = 0;
	if (!string_to_uint32(argv[0], 10, &screen_width)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return false;
	}

	uint32_t game_width = 0;
	if (!string_to_uint32(argv[1], 10, &game_width)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return false;
	}

	uint32_t game_height = 0;
	if (!string_to_uint32(argv[2], 10, &game_height)) {
		fprintf(stderr, "string_to_uint32_t() failed\n");
		return false;
	}

	if (!patch_resolution(context, screen_width, game_width, game_height)) {
		fprintf(stderr, "patch_resolution() failed\n");
		return false;
	}

	return true;
}
