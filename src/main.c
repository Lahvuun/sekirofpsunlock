#define _XOPEN_SOURCE 500

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define COMMAND_FPS "set-fps"
#define COMMAND_RESOLUTION "set-resolution"
#define IMAGE_BASE 0x140000000

static sig_atomic_t got_sigchld = 0;

void handle_sigchld(int signum)
{
	switch (signum) {
	case SIGCHLD:
		got_sigchld += 1;
	}
}

struct ignorable_byte {
	bool is_ignored;
	uint8_t value;
};

enum find_sekiro_result {
	FOUND,
	NOT_FOUND,
	ERROR,
};

struct dos_header {
	uint16_t magic;
	uint8_t ignored[58];
	int32_t coff_header_offset;
};

struct coff_header {
	uint32_t signature;
	uint16_t machine;
	uint16_t number_of_sections;
	uint32_t time_date_stamp;
	uint32_t pointer_to_symbol_table;
	uint32_t number_of_symbols;
	uint16_t size_of_optional_header;
	uint16_t characteristics;
};

struct coff_optional_header {
	uint16_t magic;
	uint8_t ignored[104];
	uint16_t number_of_rva_and_sizes;
};

struct section_header {
	char name[8];
	uint32_t virtual_size;
	uint32_t virtual_address;
	uint8_t ignored[24];
};

static bool string_to_uintmax(const char *s, int base, uintmax_t *value_out)
{
	int errno_stored = errno;
	errno = 0;
	char *endptr = NULL;
	*value_out = strtoumax(s, &endptr, base);
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

static bool string_to_uint32(const char *s, int base, uint32_t *value_out)
{
	uintmax_t large = 0;
	if (!string_to_uintmax(s, base, &large)) {
		fprintf(stderr, "string_to_uintmax() failed\n");
		return false;
	}

	if (large > UINT32_MAX) {
		fprintf(stderr, "value does not fit into uint32_t\n");
		return false;
	}

	*value_out = large;

	return true;
}

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

static bool main_fps(FILE *f, int argc, char *argv[]);

static bool main_resolution(FILE *f, int argc, char *argv[]);

static bool handle_arguments(FILE *f, char **arguments, int arguments_size)
{
	static_assert(sizeof(ptrdiff_t) >= sizeof(int), "ptrdiff_t must fit int");

	while (arguments_size > 0) {
		if (!strncmp(*arguments, COMMAND_FPS, strlen(COMMAND_FPS))) {
			if (!main_fps(f, arguments_size - 1, arguments + 1)) {
				fprintf(stderr, "main_fps() failed\n");
				return false;
			}

			arguments += 2;
			arguments_size -= 2;
		} else if (!strncmp(*arguments, COMMAND_RESOLUTION, strlen(COMMAND_RESOLUTION))) {
			if (!main_resolution(f, arguments_size - 1, arguments + 1)) {
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

static bool size_t_to_long(size_t value, long *value_out)
{
	if (value > LONG_MAX) {
		fprintf(stderr, "size_t value is larger than LONG_MAX\n");
		return false;
	}

	*value_out = value;

	return true;
}

static bool seek_and_read_bytes(uint8_t *destination, size_t destination_length, size_t position, FILE *f)
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

static bool seek_and_write_bytes(uint8_t *source, size_t source_length, size_t position, FILE *f) {
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

static bool extract_section(char *name, FILE *f, uint8_t **section_bytes_out, size_t *section_bytes_size_out, size_t *section_position_out)
{
	assert(strlen(name) < 9);

	struct dos_header dos_header = { 0 };
	if (!seek_and_read_bytes((uint8_t *)&dos_header, sizeof(dos_header), IMAGE_BASE, f)) {
		fprintf(stderr, "failed to read dos header\n");
		return false;
	}

	if (dos_header.magic != 0x5a4d) {
		fprintf(stderr, "dos magic does not match\n");
		return false;
	}

	struct coff_header coff_header = { 0 };
	if (!seek_and_read_bytes((uint8_t *)&coff_header, sizeof(coff_header),
				 IMAGE_BASE + dos_header.coff_header_offset, f)) {
		fprintf(stderr, "failed to read coff header\n");
		return false;
	}

	if (coff_header.signature != 0x4550) {
		fprintf(stderr, "pe signature does not match\n");
		return false;
	}

	struct coff_optional_header coff_optional_header = { 0 };
	if (!seek_and_read_bytes((uint8_t *)&coff_optional_header, sizeof(coff_optional_header),
				 IMAGE_BASE + dos_header.coff_header_offset + sizeof(coff_header), f)) {
		fprintf(stderr, "failed to read optional header\n");
		return false;
	}

	if (coff_optional_header.magic != 0x20b) {
		fprintf(stderr, "pe32+ magic does not match\n");
		return false;
	}

	struct section_header section_header = { 0 };
	for (uint16_t i = 0; i < coff_header.number_of_sections; ++i) {
		if (!seek_and_read_bytes((uint8_t *)&section_header, sizeof(section_header),
					 IMAGE_BASE + dos_header.coff_header_offset + sizeof(coff_header) +
						 coff_header.size_of_optional_header + i * sizeof(section_header),
					 f)) {
			fprintf(stderr, "failed to read section header\n");
			return false;
		}

		if (!strncmp(name, section_header.name, sizeof(section_header.name))) {
			*section_bytes_out = calloc(section_header.virtual_size, sizeof(uint8_t));
			if (!*section_bytes_out) {
				fprintf(stderr, "calloc() failed\n");
				return false;
			}

			if (!seek_and_read_bytes(*section_bytes_out, section_header.virtual_size, IMAGE_BASE + section_header.virtual_address, f)) {
				fprintf(stderr, "failed to read section contents\n");
				free(*section_bytes_out);
				return false;
			}
			*section_bytes_size_out = section_header.virtual_size;
			*section_position_out = IMAGE_BASE + section_header.virtual_address;

			return true;
		}
	}
	fprintf(stderr, "couldn't find section with name %s\n", name);

	return false;
}

#include <unistd.h>

static bool wait_for_data_change_with_data_and_buffer(uint8_t *buffer, uint8_t *data, size_t data_size, size_t data_position, FILE *f, pid_t pid, time_t timeout)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	if (got_sigchld > 0) {
		got_sigchld = 0;
		if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
			perror("ptrace(PTRACE_CONT, ...) failed");
			return false;
		}
	}

	time_t current_time = start_time;
	do {
		if (got_sigchld > 0) {
			got_sigchld = 0;
			if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
				perror("ptrace(PTRACE_CONT, ...) failed");
				return false;
			}
		}

		if (!seek_and_read_bytes(buffer, data_size, data_position, f)) {
			fprintf(stderr, "seek_and_read_bytes() failed\n");
			return false;
		}

		for (size_t i = 0; i < data_size; ++i) {
			if (data[i] != buffer[i]) {
				if (kill(pid, SIGSTOP) == -1) {
					perror("kill() failed");
					return false;
				}

				return true;
			}
		}

		current_time = time(NULL);
		if ((time_t)-1 == current_time) {
			perror("time() failed");
			return false;
		}
	} while (current_time - start_time < timeout);
	fprintf(stderr, "timeout reached while waiting for data section to change\n");

	return false;
}

static bool wait_for_data_change_with_data(uint8_t *data, size_t data_size, size_t data_position, FILE *f, pid_t pid, time_t timeout)
{
	uint8_t *buffer = calloc(data_size, sizeof(uint8_t));
	if (!buffer) {
		fprintf(stderr, "calloc() failed\n");
		return false;
	}

	bool success = wait_for_data_change_with_data_and_buffer(buffer, data, data_size, data_position, f, pid, timeout);

	free(buffer);

	return success;
}

static bool wait_for_data_change(FILE *f, pid_t pid, time_t timeout)
{
	uint8_t *data = NULL;
	size_t data_size = 0;
	size_t data_position = 0;
	if (!extract_section(".data", f, &data, &data_size, &data_position)) {
		fprintf(stderr, "extract_section(\".data\", ...) failed\n");
		return false;
	}

	bool success = wait_for_data_change_with_data(data, data_size, data_position, f, pid, timeout);

	free(data);

	return success;
}

static bool patch_attached_process_with_file(FILE *f, pid_t pid, time_t timeout, char **arguments, int arguments_size)
{
	if (!wait_for_data_change(f, pid, timeout)) {
		fprintf(stderr, "wait_for_data_change() failed\n");
		return false;
	}

	if (!handle_arguments(f, arguments, arguments_size)) {
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

	bool success = patch_attached_process_with_file(f, pid, timeout, arguments, arguments_size);

	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return false;
	}

	return success;
}

static enum find_sekiro_result is_process_sekiro(FILE *f, char *buffer, size_t buffer_size)
{
	assert(buffer_size <= INT_MAX);

	if (!fgets(buffer, buffer_size, f)) {
		if (ferror(f)) {
			fprintf(stderr, "fgets() failed\n");
			return ERROR;
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

static bool find_sekiro(time_t timeout, pid_t *pid_out)
{
	time_t start_time = time(NULL);
	if ((time_t)-1 == start_time) {
		perror("time() failed");
		return false;
	}

	char buffer[64] = "";
	time_t current_time = start_time;
	do {
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

	int wstatus = 0;
	while (!WIFSTOPPED(wstatus)) {
		pid_t pid_waited = waitpid(pid, &wstatus, 0);
		if (pid_waited != pid) {
			perror("waitpid() failed");
			return false;
		}
	}

	bool success = patch_attached_process(pid, timeout, arguments, arguments_size);

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
		perror("ptrace(PTRACE_DETACH, ...)");
		return false;
	}

	return success;
}

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

static bool find_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, const uint8_t *bytes,
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

static bool patch_game_fps(FILE *f, float fps)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".text", f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success = patch_game_fps_with_section(f, fps, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool main_fps(FILE *f, int argc, char *argv[])
{
	if (argc < 1) {
		fprintf(stderr, "need at least 1 argument to patch fps\n");
		return false;
	}

	float fps = 0.0f;
	if (!string_to_float(argv[0], &fps)) {
		fprintf(stderr, "string_to_float() failed\n");
		return false;
	}

	if (fps < 30 || fps > 300) {
		fprintf(stderr, "fps needs to be at least 30 and at most 300\n");
		return false;
	}

	if (!patch_game_fps(f, fps)) {
		fprintf(stderr, "patch_game_fps() failed\n");
		return false;
	}

	return true;
}

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

static bool patch_resolution_default_with_section(FILE *f, uint32_t screen_width, uint32_t game_width, uint32_t game_height, uint8_t *section_bytes,
						  size_t section_bytes_size, size_t section_positon)
{
	size_t pattern_resolution_default_index = 0;
	if (screen_width < 1920) {
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

	if (!seek_and_write_bytes((uint8_t *)&game_width,
				  sizeof(game_width),
				  section_positon + pattern_resolution_default_index, f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	if (!seek_and_write_bytes((uint8_t *)&game_height,
				  sizeof(game_height),
				  section_positon + pattern_resolution_default_index + 4, f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_default(FILE *f, uint32_t screen_width, uint32_t game_width, uint32_t game_height)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".data", f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success =
		patch_resolution_default_with_section(f, screen_width, game_width, game_height, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool patch_resolution_scaling_fix_with_section(FILE *f, uint8_t *section_bytes,
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
				 section_position + pattern_resolution_scaling_fix_index, f)) {
		fprintf(stderr, "seek_and_write_bytes() failed\n");
		return false;
	}

	return true;
}

static bool patch_resolution_scaling_fix(FILE *f)
{
	uint8_t *section_bytes = NULL;
	size_t section_bytes_size = 0;
	size_t section_position = 0;
	if (!extract_section(".text", f, &section_bytes, &section_bytes_size, &section_position)) {
		fprintf(stderr, "extract_section() failed\n");
		return false;
	}

	bool success =
		patch_resolution_scaling_fix_with_section(f, section_bytes, section_bytes_size, section_position);

	free(section_bytes);

	return success;
}

static bool patch_resolution(FILE *f, uint32_t screen_width, uint32_t game_width, uint32_t game_height)
{
	if (!patch_resolution_default(f, screen_width, game_width, game_height)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	if (!patch_resolution_scaling_fix(f)) {
		fprintf(stderr, "patch_resolution_default() failed\n");
		return false;
	}

	return true;
}

static bool main_resolution(FILE *f, int argc, char *argv[])
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

	if (!patch_resolution(f, screen_width, game_width, game_height)) {
		fprintf(stderr, "patch_resolution() failed\n");
		return false;
	}

	return true;
}

static bool set_sigchld_handler(void)
{
	struct sigaction act = { 0 };
	act.sa_handler = handle_sigchld;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		perror("sigaction() failed");
		return false;
	}

	return true;
}

int main(int argc, char *argv[])
{
	if (!set_sigchld_handler()) {
		fprintf(stderr, "set_sigchld_handler() failed\n");
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
