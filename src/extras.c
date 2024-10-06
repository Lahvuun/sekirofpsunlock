#include "fps.h"

#include "signals.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <time.h>

static struct ignorable_byte camera_reset[] = {
    // C6 86 ?? ?? 00 00 ?? F3 0F 10 8E ?? ?? 00 00
    { .is_ignored = false, .value = 0xc6 },
    { .is_ignored = false, .value = 0x86 },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0x00 },
    { .is_ignored = false, .value = 0x00 },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0xf3 },
    { .is_ignored = false, .value = 0x0f },
    { .is_ignored = false, .value = 0x10 },
    { .is_ignored = false, .value = 0x8e },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0x00 },
    { .is_ignored = false, .value = 0x00 },
};

static struct ignorable_byte autoloot[] = {
    // C6 85 ?? ?? ?? ?? ?? B0 01 EB ?? C6 85 ?? ?? ?? ?? ?? 32 C0
    { .is_ignored = false, .value = 0xc6 },
    { .is_ignored = false, .value = 0x85 },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0xb0 },
    { .is_ignored = false, .value = 0x01 },
    { .is_ignored = false, .value = 0xeb },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0xc6 },
    { .is_ignored = false, .value = 0x85 },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = true },
    { .is_ignored = false, .value = 0x32 },
    { .is_ignored = false, .value = 0xc0 },
};

static bool patch_camera_reset_with_section(struct context *context, uint8_t *section_bytes, size_t section_bytes_size, size_t section_position)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	size_t pattern_camera_reset_index = 0;
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

		if (find_pattern(camera_reset, sizeof(camera_reset) / sizeof(struct ignorable_byte), context->f, section_bytes, section_bytes_size, section_position, &pattern_camera_reset_index)) {
			stop_and_wait(context);
			break;
		}

		current_time = time(NULL);
		if ((time_t)-1 == current_time) {
			perror("time() failed");
			return false;
		}
		if ((current_time - start_time) > context->timeout) {
			fprintf(stderr, "timeout reached while looking for camera reset pattern\n");
			return false;
		}
	}

	size_t camera_reset_value_index = pattern_camera_reset_index + 6;
    uint8_t camera_reset_enabled = 0x00; // 0x00 = disabled, 0x01 = enabled
	if (!seek_and_write_bytes(&camera_reset_enabled, sizeof(camera_reset_enabled), section_position + camera_reset_value_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

bool patch_camera_reset(struct context *context)
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

	bool success = patch_camera_reset_with_section(context, section_buffer, section_size, section_position);

	free(section_buffer);

	return success;
}

static bool patch_autoloot_with_section(struct context *context, uint8_t *section_bytes, size_t section_bytes_size, size_t section_position)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	size_t pattern_autoloot_index = 0;
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

		if (find_pattern(autoloot, sizeof(autoloot) / sizeof(struct ignorable_byte), context->f, section_bytes, section_bytes_size, section_position, &pattern_autoloot_index)) {
			stop_and_wait(context);
			break;
		}

		current_time = time(NULL);
		if ((time_t)-1 == current_time) {
			perror("time() failed");
			return false;
		}
		if ((current_time - start_time) > context->timeout) {
			fprintf(stderr, "timeout reached while looking for autoloot pattern\n");
			return false;
		}
	}

	size_t autoloot_value_index = pattern_autoloot_index + 18;
    uint8_t autoloot_enabled[2] = { 0xb0, 0x01 }; // b0 01 = enabled, 32 c0 = disabled
	if (!seek_and_write_bytes((uint8_t *)&autoloot_enabled, sizeof(autoloot_enabled), section_position + autoloot_value_index, context->f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

bool patch_autoloot(struct context *context)
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

	bool success = patch_autoloot_with_section(context, section_buffer, section_size, section_position);

	free(section_buffer);

	return success;
}
