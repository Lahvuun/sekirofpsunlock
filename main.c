#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include <unistd.h>

#define TEXT_OFFSET 0x140001000
#define TEXT_OFFSET_STR "140001000"

#define log_failure(format) (fprintf(stderr, "%s:%d: "format"() failed\n", __func__, __LINE__))

struct pattern_byte {
	uint8_t value;
	bool ignore;
};

struct pattern_byte framelock[] = {
	{ .value = 0xc7, .ignore = false },
	{ .value = 0x43, .ignore = false },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x4c, .ignore = false },
	{ .value = 0x89, .ignore = false },
	{ .value = 0xab, .ignore = false },
};

struct pattern_byte framelock_speed_fix[] = {
	{ .value = 0xf3, .ignore = false },
	{ .value = 0x0f, .ignore = false },
	{ .value = 0x58, .ignore = false },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x0f, .ignore = false },
	{ .value = 0xc6, .ignore = false },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = false },
	{ .value = 0x0f, .ignore = false },
	{ .value = 0x51, .ignore = false },
	{ .value = 0x00, .ignore = true },
	{ .value = 0xf3, .ignore = false },
	{ .value = 0x0f, .ignore = false },
	{ .value = 0x59, .ignore = false },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x00, .ignore = true },
	{ .value = 0x0f, .ignore = false },
	{ .value = 0x2f, .ignore = false },
};

float patch_framelock_speed_fix_matrix[] = {
            15.0f,
            16.0f,
            16.6667f,
            18.0f,
            18.6875f,
            18.8516f,
            20.0f,
            24.0f,
            25.0f,
            27.5f,
            30.0f,
            32.0f,
            38.5f,
            40.0f,
            48.0f,
            49.5f,
            50.0f,
            57.2958f,
            60.0f,
            64.0f,
            66.75f,
            67.0f,
            78.8438f,
            80.0f,
            84.0f,
            90.0f,
            93.8f,
            100.0f,
            120.0f,
            127.0f,
            128.0f,
            130.0f,
            140.0f,
            144.0f,
            150.0f,
};

static bool long_to_size_t(long value, size_t *out_value)
{
	static_assert(SIZE_MAX >= LONG_MAX, "size_t can't hold the value of long");

	if (value < 0) {
		return false;
	}
	*out_value = value;

	return true;
}

static float find_speed_fix_for_refresh_rate(long frame_limit) {
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

static uint32_t uint32_from_float(float n)
{
	union {
		float f;
		uint32_t u;
	} f2u = { .f = n };

	return f2u.u;
}

static size_t read_size_of_text_from_file(FILE *f) {
	char *line = NULL;
	size_t line_length = 0;
	char *str_r = NULL;
	char *end_str = NULL;
	char *start_str = NULL;
	size_t start = 0;
	size_t end = 0;
	while (!end) {
		if (getline(&line, &line_length, f) < 0) {
			if (errno) {
				perror("getline() failed");
			} else {
				fprintf(stderr, "getline() reached EOF\n");
			}
			break;
		}

		start_str = strtok_r(line, "-", &str_r);
		if (!strcmp(start_str, TEXT_OFFSET_STR)) {
			fprintf(stderr, "found .text start: %s-%s", line, str_r);
			end_str = strtok_r(NULL, "-", &str_r);
			sscanf(start_str, "%zx", &start);
			sscanf(end_str, "%zx", &end);
			break;
		}
	}

	free(line);
	return end - start;
}

static FILE *fopen_maps(long pid)
{
	size_t path_length = 0;
	long path_max = pathconf("/proc", _PC_PATH_MAX);
	if (-1 == path_max) {
		if (errno) {
			perror("pathconf() failed");
		} else {
			fprintf(stderr, "max path length is indeterminable\n");
		}
		return 0;
	}
	if (!long_to_size_t(path_max, &path_length)) {
		fprintf(stderr, "long_to_size_t() failed\n");
		return 0;
	}

	char *s = calloc(path_length, sizeof(char));
	if (!s) {
		log_failure("calloc");
		return 0;
	}

	FILE *f = NULL;
	if (sprintf(s, "/proc/%ld/maps", pid) < 0) {
		log_failure("sprintf");
		goto cleanup;
	}

	f = fopen(s, "r");
	if (!f) {
		log_failure("fopen");
	}

cleanup:
	free(s);
	return f;
}

static size_t read_size_of_text(long pid)
{
	FILE *f = fopen_maps(pid);
	if (!f) {
		log_failure("fopen_maps");
		return 0;
	}
	size_t size = read_size_of_text_from_file(f);
	if (fclose(f) == EOF) {
		perror("fclose() failed");
		return 0;
	}

	return size;
}

size_t find_pattern(int fd, const struct pattern_byte *const pattern_bytes, size_t pattern_length, size_t section_size)
{
	uint8_t *bytes = calloc(section_size, sizeof(uint8_t));
	if (!bytes) {
		return 0;
	}

	pread(fd, bytes, section_size, TEXT_OFFSET);
	size_t location = 0;
	for (size_t i = 0; i < section_size; ++i) {
		for (size_t j = 0; j < pattern_length; ++j) {
			if (pattern_bytes[j].ignore) {
				continue;
			}

			if (pattern_bytes[j].value != bytes[i + j]) {
				break;
			}

			if ((pattern_length - j) == 1) {
				location = i;
				goto cleanup;
			}
		}
	}

cleanup:
	free(bytes);
	return location;
}

static uintptr_t read_speed_fix_pointer_address(int fd, uintptr_t speed_fix_location) {
	uint32_t offset = 0;
	pread(fd, &offset, sizeof(offset), speed_fix_location);
	return speed_fix_location + 8 + offset;
}

static void write_float_mem(int fd, off_t addr, float value)
{
	pwrite(fd, &value, sizeof(value), addr);
}

static bool strtol_safer(const char *restrict nptr, long *out_value)
{
	bool success = false;
	const int err = errno;
	errno = 0;

	char *endptr = NULL;
	*out_value = strtol(nptr, &endptr, 10);
	if (errno) {
		perror("failed to strtol()");
	} else if (nptr == endptr) {
		fprintf(stderr, "failed to strtol(), no digits\n");
	} else {
		success = true;
	}

	errno = err;
	return success;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: sekirofpsunlock [PID] [FPS]\n");
		return EXIT_FAILURE;
	}

	long pid = 0;
	if (!strtol_safer(argv[1], &pid)) {
		fprintf(stderr, "failed to strtol_safer() the PID\n");
		return EXIT_FAILURE;
	}

	long fps = 0;
	if (!strtol_safer(argv[2], &fps)) {
		fprintf(stderr, "failed to strtol_safer() the FPS\n");
		return EXIT_FAILURE;
	}

	if (fprintf(stderr, "PID is %ld, FPS is %ld\n", pid, fps) < 0) {
		return EXIT_FAILURE;
	}

	if (fps < 1) {
		if (fprintf(stderr, "FPS is < 1, will be set to 60\n") < 0) {
			return EXIT_FAILURE;
		}
		fps = 60;
	} else if (fps > 1 && fps < 30) {
		if (fprintf(stderr, "FPS is < 30, will be set to 30\n") < 0) {
			return EXIT_FAILURE;
		}
		fps = 30;
	} else if (fps > 300) {
		if (fprintf(stderr, "FPS is > 300, will be set to 300\n") < 0) {
			return EXIT_FAILURE;
		}
		fps = 300;
	}

	float delta_time = (1000.0f / fps) / 1000.0f;
	float speed_fix = find_speed_fix_for_refresh_rate(fps);
	if (fprintf(stderr, "deltatime hex: %#" PRIx32 "\n", uint32_from_float(delta_time)) < 0) {
		return EXIT_FAILURE;
	}
	if (fprintf(stderr, "speed hex: %#" PRIx32 "\n", uint32_from_float(speed_fix)) < 0) {
		return EXIT_FAILURE;
	}
	size_t text_size = read_size_of_text(pid);
	if (fprintf(stderr, "size of .text map is %#zx\n", text_size) < 0) {
		return EXIT_FAILURE;
	}

	char *path = calloc(PATH_MAX, sizeof(char));
	if (!path) {
		fprintf(stderr, "%s %d: calloc() failed\n", __func__, __LINE__);
		return EXIT_FAILURE;
	}
	snprintf(path, PATH_MAX, "/proc/%ld/mem", pid);
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open(\"%s\", O_RDWR) failed, errno = %d\n", path, errno);
		perror("error");
		return EXIT_FAILURE;
	}
	free(path);
	uintptr_t framelock_location = TEXT_OFFSET + (uintptr_t)find_pattern(fd, framelock, sizeof(framelock) / sizeof(struct pattern_byte), text_size);
	if (framelock_location == TEXT_OFFSET) {
		fprintf(stderr, "framelock_location find_pattern failed\n");
		return EXIT_FAILURE;
	}
	fprintf(stderr, "framelock location is %#" PRIxPTR "\n", framelock_location);
	uintptr_t speed_fix_location = 11 + TEXT_OFFSET + (uintptr_t)find_pattern(fd, framelock_speed_fix, sizeof(framelock_speed_fix) / sizeof(struct pattern_byte), text_size);
	if (speed_fix_location == 11 + TEXT_OFFSET) {
		fprintf(stderr, "speed_fix_location find_pattern failed\n");
		return EXIT_FAILURE;
	}
	fprintf(stderr, "framelock speed fix location is %#" PRIxPTR "\n", speed_fix_location);
	speed_fix_location = read_speed_fix_pointer_address(fd, speed_fix_location);
	fprintf(stderr, "framelock speed fix pointer address is %#" PRIxPTR "\n", speed_fix_location);
	write_float_mem(fd, framelock_location + 3, delta_time);
	write_float_mem(fd, speed_fix_location, speed_fix);
	close(fd);

	return EXIT_SUCCESS;
}
