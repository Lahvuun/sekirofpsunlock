#include "sekiro.h"

#include "signals.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum find_sekiro_result {
	FOUND,
	NOT_FOUND,
	ERROR,
};

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

static bool string_to_pid(char *s, int base, pid_t *pid_out)
{
	long pid_long = 0;
	if (!string_to_long(s, base, &pid_long)) {
		fprintf(stderr, "string_to_long() failed\n");
		return false;
	}

	static_assert(sizeof(int32_t) == sizeof(pid_t), "pid_t must take the same amount of bytes as int32_t");
	if (pid_long > INT32_MAX) {
		fprintf(stderr, "pid is too big for pid_t\n");
		return false;
	}

	*pid_out = pid_long;

	return true;
}

static enum find_sekiro_result is_process_sekiro(FILE *f, char *buffer, size_t buffer_size)
{
	// fgets takes int, so make sure the implicit cast provides the correct value.
	assert(buffer_size <= INT_MAX);

	if (!fgets(buffer, buffer_size, f)) {
		// Can happen if the file has been opened but the process is already dead by the time fgets is called.
		// These reads from /proc should never really fail for any other reason, so it's probably fine to treat it as a failure to find the process.
		// Even if it is a real error (in which case something is seriously wrong), the timeout should take care of it.
		if (ferror(f)) {
			return NOT_FOUND;
		}
		if (feof(f)) {
			fprintf(stderr, "got unexpected EOF\n");
			return ERROR;
		}
	}
	if (!strstr(buffer, "sekiro.exe")) {
		return NOT_FOUND;
	}

	return FOUND;
}

static enum find_sekiro_result find_sekiro_in_dir(DIR *dirp, char *buffer, size_t buffer_size, pid_t *pid_out)
{
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(dirp);
		if (!entry) {
			if (errno) {
				perror("readdir() failed");
				return ERROR;
			}

			return NOT_FOUND;
		}

		int written = snprintf(buffer, buffer_size, "/proc/%s/status", entry->d_name);
		if (written < 0) {
			fprintf(stderr, "snprintf() failed\n");
			return ERROR;
		}
		size_t written_size = written;
		if (written_size > buffer_size) {
			fprintf(stderr, "snprintf() did not write the whole string, buffer is too short\n");
			return ERROR;
		}

		FILE *f = fopen(buffer, "r");
		if (!f) {
			if (ENOENT == errno || ENOTDIR == errno) {
				continue;
			}
			perror("fopen() failed");
			return ERROR;
		}

		enum find_sekiro_result result = is_process_sekiro(f, buffer, buffer_size);

		if (fclose(f) == EOF) {
			perror("fclose() failed");
			return ERROR;
		}

		switch (result) {
		case NOT_FOUND:
			break;
		case FOUND:
			if (!string_to_pid(entry->d_name, 10, pid_out)) {
				fprintf(stderr, "string_to_pid() failed\n");
				return ERROR;
			}

			return FOUND;
		case ERROR:
			fprintf(stderr, "is_process_sekiro() failed\n");
			return ERROR;
		default:
			fprintf(stderr, "got unknown is_process_sekiro() result\n");
			return ERROR;
		}
	}
}

bool find_sekiro(time_t timeout, pid_t *pid_out)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	char buffer[64] = "";
	time_t current_time = start_time;
	do {
		if (got_sigterm) {
			fprintf(stderr, "got SIGTERM, exiting\n");
			return false;
		}

		DIR *dirp = opendir("/proc");
		if (!dirp) {
			perror("opendir() failed");
			return false;
		}

		enum find_sekiro_result result = find_sekiro_in_dir(dirp, buffer, sizeof(buffer), pid_out);
		if (closedir(dirp) == -1) {
			perror("closedir() failed");
			return false;
		}

		switch (result) {
		case NOT_FOUND:
			break;
		case FOUND:
			return true;
		case ERROR:
			fprintf(stderr, "find_sekiro_in_dir() failed\n");
			return false;
		default:
			fprintf(stderr, "unknown result of find_sekiro_in_dir()\n");
			return false;
		}

		current_time = time(NULL);
		if ((time_t)-1 == start_time) {
			perror("time() failed");
			return false;
		}
	} while (current_time - start_time < timeout);

	fprintf(stderr, "timeout reached while searching for sekiro.exe\n");

	return false;
}
