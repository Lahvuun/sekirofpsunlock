#define _POSIX_C_SOURCE 1

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include "common.h"

struct extracted_section {
	uint8_t *bytes;
	size_t bytes_size;
};

static char string_buffer[STRING_BUFFER_SIZE] = "";

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

static bool wait_for_data_change_with_section_section(pid_t pid, FILE *f, struct extracted_section *section, struct extracted_section *section_new, size_t position)
{
	assert(section->bytes_size == section_new->bytes_size);

	time_t start_time_seconds = time(NULL);
	if ((time_t)-1 == start_time_seconds) {
		perror("time() failed");
		return false;
	}

	if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
		perror("ptrace(PTRACE_CONT, ...) failed\n");
		return false;
	}

	time_t current_time_seconds = time(NULL);
	if ((time_t)-1 == current_time_seconds) {
		perror("time() failed\n");
		return false;
	}

	size_t i = 0;
	while (current_time_seconds - start_time_seconds < 60) {
		fprintf(stderr, "iteration %zu\n", i);
		++i;

		if (!seek_and_read_bytes(section_new->bytes, section_new->bytes_size, position, f)) {
			fprintf(stderr, "seek_and_read_bytes() failed\n");
			return false;
		}

		for (size_t i = 0; i < section->bytes_size; ++i) {
			if (section->bytes[i] != section_new->bytes[i]) {
				return true;
			}
		}

		current_time_seconds = time(NULL);
		if ((time_t)-1 == current_time_seconds) {
			perror("time() failed\n");
			return false;
		}
	}

	fprintf(stderr, "data hasn't changed in 60 seconds\n");

	return false;
}

static bool wait_for_data_change_with_section(pid_t pid, FILE *f, struct extracted_section *section, size_t position)
{
	uint8_t *bytes = calloc(section->bytes_size, sizeof(uint8_t));
	if (!bytes) {
		fprintf(stderr, "calloc() failed\n");
		return false;
	}

	memcpy(bytes, section->bytes, section->bytes_size);

	struct extracted_section section_new = {
		.bytes = bytes,
		.bytes_size = section->bytes_size,
	};

	bool success = wait_for_data_change_with_section_section(pid, f, section, &section_new, position);

	free(bytes);

	return success;
}

static bool wait_for_data_change(pid_t pid, FILE *f)
{
	uint8_t *data_current = NULL;
	size_t data_current_size = 0;
	size_t position = 0;
	if (!extract_section(".data", f, &data_current, &data_current_size, &position)) {
		fprintf(stderr, "extract_section(\".data\", ...) failed\n");
		return false;
	}

	struct extracted_section section = {
		.bytes = data_current,
		.bytes_size = data_current_size,
	};

	bool success = wait_for_data_change_with_section(pid, f, &section, position);

	free(data_current);

	return success;
}

static bool patch_stopped_process_with_file(pid_t pid, struct game_patch *game_patch, FILE *f)
{
	if (!wait_for_data_change(pid, f)) {
		fprintf(stderr, "wait_for_data_change() failed\n");
		return false;
	}

	if (kill(pid, SIGSTOP) == -1) {
		perror("kill() failed\n");
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

	struct game_patch_context context = {
		.f = f,
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

	bool success = patch_stopped_process_with_file(pid, game_patch, f);

	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return false;
	}

	return success;
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

