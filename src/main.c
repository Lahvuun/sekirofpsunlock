#define _POSIX_SOURCE 1

#include "common.h"
#include "signals.h"
#include "sekiro.h"
#include "fps.h"
#include "resolution.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define COMMAND_FPS "set-fps"
#define COMMAND_RESOLUTION "set-resolution"

static time_t uint32_to_time(uint32_t value)
{
	static_assert(sizeof(time_t) > sizeof(uint32_t), "time_t must fit uint32_t");

	return value;
}

static bool string_to_time(const char *s, int base, time_t *value_out)
{
	uint32_t value_uint32 = 0;
	if (!string_to_uint32(s, base, &value_uint32)) {
		fprintf(stderr, "string_to_uint32() failed\n");
		return false;
	}

	*value_out = uint32_to_time(value_uint32);

	return true;
}

static bool handle_arguments(struct context *context, char **arguments, int arguments_size)
{
	static_assert(sizeof(ptrdiff_t) >= sizeof(int), "ptrdiff_t must fit int");

	while (arguments_size > 0) {
		if (!strncmp(*arguments, COMMAND_FPS, strlen(COMMAND_FPS))) {
			if (!main_fps(context, arguments_size - 1, arguments + 1)) {
				fprintf(stderr, "main_fps() failed\n");
				return false;
			}

			arguments += 2;
			arguments_size -= 2;
		} else if (!strncmp(*arguments, COMMAND_RESOLUTION, strlen(COMMAND_RESOLUTION))) {
			if (!main_resolution(context, arguments_size - 1, arguments + 1)) {
				fprintf(stderr, "main_resolution() failed\n");
				return false;
			}

			arguments += 4;
			arguments_size -= 4;
		} else {
			fprintf(stderr, "unknown command: %s\n", *arguments);

			return false;
		}
	}

	return true;
}

static bool patch_attached_process_with_file(struct context *context, char **arguments, int arguments_size)
{
	if (!handle_arguments(context, arguments, arguments_size)) {
		fprintf(stderr, "handle_arguments() failed\n");
		return false;
	}

	return true;
}

static bool patch_attached_process(pid_t pid, time_t timeout, char **arguments, int arguments_size)
{
	char path[64] = "";
	long pid_long = pid;
	int written = snprintf(path, 64, "/proc/%ld/mem", pid_long);
	if (written < 0) {
		fprintf(stderr, "snprintf() failed\n");
		return false;
	}
	if (written > 64) {
		fprintf(stderr, "path did not fit the buffer\n");
		return false;
	}

	FILE *f = fopen(path, "r+");
	if (!f) {
		perror("fopen() failed");
		return false;
	}

	struct context context = {
		.f = f,
		.pid = pid,
		.timeout = timeout,
	};

	bool success = patch_attached_process_with_file(&context, arguments, arguments_size);

	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return false;
	}

	return success;
}

static bool patch(time_t timeout, char **arguments, int arguments_size)
{
	pid_t pid = 0;
	if (!find_sekiro(timeout, &pid)) {
		fprintf(stderr, "find_sekiro() failed\n");
		return false;
	}

	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
		perror("ptrace(PTRACE_ATTACH, ...)");
		return false;
	}

	bool success = patch_attached_process(pid, timeout, arguments, arguments_size);

	if (kill(pid, SIGCONT) == -1) {
		perror("kill() failed");
		success = false;
	}

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
		perror("ptrace(PTRACE_DETACH, ...)");
		return false;
	}

	return success;
}

int main(int argc, char *argv[])
{
	if (!set_sigchld_handler()) {
		fprintf(stderr, "set_sigchld_handler() failed\n");
		return EXIT_FAILURE;
	}

	if (!set_sigterm_handler()) {
		fprintf(stderr, "set_sigterm_handler() failed\n");
		return EXIT_FAILURE;
	}

	if (argc < 1) {
		fprintf(stderr, "need at least one argument\n");
		return EXIT_FAILURE;
	}

	if (argc < 3) {
		fprintf(stderr, "usage: %s <timeout-seconds> <argument> {<argument>}\n", argv[0]);
		return EXIT_FAILURE;
	}

	time_t timeout = 0;
	if (!string_to_time(argv[1], 10, &timeout)) {
		fprintf(stderr, "could not parse timeout\n");
		return EXIT_FAILURE;
	}

	if (!patch(timeout, argv + 2, argc - 2)) {
		fprintf(stderr, "patch() failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
