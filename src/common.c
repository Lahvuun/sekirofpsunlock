#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include "common.h"

#define STRING_BUFFER_SIZE 1024

static char string_buffer[STRING_BUFFER_SIZE] = "";

static bool string_to_size_t(char *s, int base, size_t *n_out)
{
	uintmax_t n_uintmax_t;
	if (!string_to_uintmax_t(s, base, &n_uintmax_t)) {
		fprintf(stderr, "string_to_uintmax_t() failed\n");
		return false;
	}

	if (n_uintmax_t > SIZE_MAX) {
		fprintf(stderr, "value is larger than SIZE_MAX\n");
		return false;
	}

	*n_out = n_uintmax_t;

	return true;
}

static bool fill_proc_path(char *destination, size_t destination_length, long pid, char *rest)
{
	int written = snprintf(destination, destination_length, "/proc/%ld/%s", pid, rest);
	if (written < 0) {
		fprintf(stderr, "snprintf() failed\n");
		return false;
	}

	static_assert(sizeof(size_t) >= sizeof(int), "size_t must fit int");
	size_t written_size_t = written;
	if (written_size_t > destination_length) {
		fprintf(stderr, "snprintf() failed: buffer not large enough to fit the resulting path\n");
		return false;
	}

	return true;
}

static bool find_text_section_size_with_file(FILE *f, size_t *size_out)
{
	const char text_section_start[] = "140001000";
	do {
		if (!fgets(string_buffer, STRING_BUFFER_SIZE, f)) {
			if (feof(f)) {
				fprintf(stderr, "fgets() failed: reached end-of-file before .text could be found\n");
			} else {
				fprintf(stderr, "fgets() failed\n");
			}
			return false;
		}
	} while (strncmp(string_buffer, text_section_start, sizeof(text_section_start) - 1));

	if (!strtok(string_buffer, "-")) {
		fprintf(stderr, "strtok() found no tokens when there should be\n");
		return false;
	}

	char *token = strtok(NULL, " ");
	if (!token) {
		fprintf(stderr, "strtok() found no tokens when there should be\n");
		return false;
	}

	if (!string_to_size_t(token, 16, size_out)) {
		fprintf(stderr, "string_to_size_t() failed\n");
		return false;
	}

	*size_out -= TEXT_OFFSET;

	return true;
}

static bool find_text_section_size(pid_t pid, size_t *size_out)
{
	if (!fill_proc_path(string_buffer, STRING_BUFFER_SIZE, pid, "maps")) {
		fprintf(stderr, "fill_proc_path() failed\n");
		return false;
	}

	FILE *f = fopen(string_buffer, "r");
	if (!f) {
		perror("fopen() failed");
		return false;
	}

	bool success = find_text_section_size_with_file(f, size_out);

	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return false;
	}

	return success;
}

static bool patch_stopped_process_with_file(FILE *f, pid_t pid, struct game_patch *game_patch)
{
	size_t text_size = 0;
	if (!find_text_section_size(pid, &text_size)) {
		fprintf(stderr, "find_text_section_size() failed\n");
		return false;
	}

	struct game_patch_context context = {
		.f = f,
		.text_section_size = text_size,
		.game_patch_data = game_patch->game_patch_data,
	};

	return game_patch->game_patch_function(&context);
}

static bool patch_stopped_process(pid_t pid, struct game_patch *game_patch)
{
	if (!fill_proc_path(string_buffer, STRING_BUFFER_SIZE, pid, "mem")) {
		fprintf(stderr, "fill_proc_path() failed\n");
		return false;
	}

	FILE *f = fopen(string_buffer, "r+");
	if (!f) {
		perror("fopen() failed");
		return false;
	}

	bool success = patch_stopped_process_with_file(f, pid, game_patch);

	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return false;
	}

	return success;
}

static bool same_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, const uint8_t *bytes, const size_t bytes_length)
{
	for (size_t i = 0; i < pattern_bytes_length && i < bytes_length; ++i) {
		const struct ignorable_byte ignorable_byte = pattern_bytes[i];
		if (!ignorable_byte.is_ignored && bytes[i] != ignorable_byte.value) {
			return false;
		}
	}

	return true;
}

static bool string_to_long(char *s, int base, long *n_out)
{
	int errno_stored = errno;
	errno = 0;
	char *endptr = NULL;
	*n_out = strtol(s, &endptr, base);
	if (errno) {
		perror("strtol() failed");
		return false;
	}
	if (endptr == s) {
		fprintf(stderr, "strtol() failed, no digits were read.\n");
		return false;
	}
	errno = errno_stored;

	return true;
}

static bool size_t_to_long(size_t value, long *value_out)
{
	if (value > LONG_MAX) {
		fprintf(stderr, "size_t value is larger than LONG_MAX\n");
		return false;
	}

	*value_out = value;

	return true;
}

bool string_to_uintmax_t(char *s, int base, uintmax_t *n_out)
{
	int errno_stored = errno;
	errno = 0;
	char *endptr = NULL;
	*n_out = strtoumax(s, &endptr, base);
	if (errno) {
		perror("strtoumax() failed");
		return false;
	}
	errno = errno_stored;
	if (endptr == s) {
		fprintf(stderr, "strtoumax() failed, no digits were read\n");
		return false;
	}

	return true;
}

bool string_to_pid_t(char *s, int base, pid_t *pid_out)
{
	long pid_long = 0;
	if (!string_to_long(s, base, &pid_long)) {
		fprintf(stderr, "string_to_long() failed\n");
		return false;
	}

	const long pid_t_max = ~(1 << (sizeof(pid_t) * 8 - 1));
	if (pid_long > pid_t_max) {
		fprintf(stderr, "pid is too big for pid_t\n");
		return false;
	}

	*pid_out = pid_long;

	return true;
}

bool find_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, const uint8_t *bytes,
		  const size_t bytes_length, size_t *position_out)
{
	for (size_t i = 0; i + pattern_bytes_length < bytes_length; ++i) {
		if (same_pattern(pattern_bytes, pattern_bytes_length, bytes + i, bytes_length - i)) {
			*position_out = i;
			return true;
		}
	}

	return false;
}

bool seek_and_read_bytes(uint8_t *destination, size_t destination_length, size_t position, FILE *f)
{
	long position_long = 0;
	if (!size_t_to_long(position, &position_long)) {
		fprintf(stderr, "size_t_to_long() failed\n");
		return false;
	}

	if (fseek(f, position_long, SEEK_SET) == -1) {
		perror("fseek() failed");
		return false;
	}

	size_t read = fread(destination, sizeof(uint8_t), destination_length, f);
	if (read < destination_length) {
		if (feof(f)) {
			fprintf(stderr, "fread() reached end-of-file unexpectedly\n");
		} else {
			fprintf(stderr, "fread() failed\n");
		}
		return false;
	}

	return true;
}

bool seek_and_write_bytes(uint8_t *source, size_t source_length, size_t position, FILE *f) {
	long position_long = 0;
	if (!size_t_to_long(position, &position_long)) {
		fprintf(stderr, "size_t_to_long() failed\n");
		return false;
	}

	if (fseek(f, position_long, SEEK_SET) == -1) {
		perror("fseek() failed");
		return false;
	}

	size_t written = fwrite(source, sizeof(uint8_t), source_length, f);
	if (written < source_length) {
		if (feof(f)) {
			fprintf(stderr, "fwrite() reached end-of-file unexpectedly\n");
		} else {
			fprintf(stderr, "fwrite() failed\n");
		}
		fprintf(stderr, "it's possible that the process is corrupted now, you should restart the game\n");
		return false;
	}

	return true;
}

bool patch(pid_t pid, struct game_patch *game_patch)
{
	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
		fprintf(stderr, "ptrace(PTRACE_ATTACH, ...) failed\n");
		return false;
	}

	int wstatus = 0;
	while (!WIFSTOPPED(wstatus)) {
		pid_t waited_pid = waitpid(pid, &wstatus, 0);
		if (waited_pid != pid) {
			perror("waitpid() failed\n");
			return false;
		}
	}

	bool success = patch_stopped_process(pid, game_patch);

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
		fprintf(stderr, "ptrace(PTRACE_DETACH, ...) failed\n");
		return false;
	}

	return success;
}
