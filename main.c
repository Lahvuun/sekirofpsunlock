#define _GNU_SOURCE

#include <stdbool.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <unistd.h>

#define TEXT_OFFSET 0x140001000
#define TEXT_OFFSET_STR "140001000"

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
	char *const line = calloc(LINE_MAX, sizeof(char));
	if (!line) {
		return 0;
	}

	size_t start = 0;
	size_t end = 0;
	while (!end) {
		if (!fgets(line, LINE_MAX, f)) {
			fprintf(stderr, "failed to read line from maps\n");
			break;
		}

		char *end_s = line;
		char *start_s = strsep(&end_s, "-");
		if (!strcmp(start_s, TEXT_OFFSET_STR)) {
			fprintf(stderr, "found .text start: %s-%s", line, end_s);
			end_s = strsep(&end_s, " ");
			sscanf(start_s, "%zx", &start);
			sscanf(end_s, "%zx", &end);
			break;
		}
	}

	free(line);
	return end - start;
}

static size_t read_size_of_text(long pid)
{
	char *s = calloc(PATH_MAX, sizeof(char));
	if (!s) {
		return 0;
	}

	sprintf(s, "/proc/%ld/maps", pid);
	FILE *f = fopen(s, "r");
	free(s);
	if (!f) {
		fprintf(stderr, "failed to open maps\n");
		return 0;
	}

	size_t size = read_size_of_text_from_file(f);

	fclose(f);
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

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: sekirofpsunlock [PID] [FPS]\n");
		return EXIT_FAILURE;
	}

	long pid = strtol(argv[1], NULL, 10);
	if (errno) {
		fprintf(stderr, "failed to strtol() PID\n");
		return EXIT_FAILURE;
	}

	long fps = strtol(argv[2], NULL, 10);
	if (errno) {
		fprintf(stderr, "failed to strtol() FPS\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "PID is %ld, FPS is %ld\n", pid, fps);

	if (fps < 1) {
		fprintf(stderr, "FPS is < 1, will be set to 60\n");
		fps = 60;
	} else if (fps > 1 && fps < 30) {
		fprintf(stderr, "FPS is < 30, will be set to 30\n");
		fps = 30;
	} else if (fps > 300) {
		fprintf(stderr, "FPS is > 300, will be set to 300\n");
		fps = 300;
	}

	float delta_time = (1000.0f / fps) / 1000.0f;
	float speed_fix = find_speed_fix_for_refresh_rate(fps);
	fprintf(stderr, "deltatime hex: %#" PRIx32 "\n", uint32_from_float(delta_time));
	fprintf(stderr, "speed hex: %#" PRIx32 "\n", uint32_from_float(speed_fix));
	size_t text_size = read_size_of_text(pid);
	fprintf(stderr, "size of .text map is %#zx\n", text_size);

	char *path = calloc(PATH_MAX, sizeof(char));
	if (!path) {
		fprintf(stderr, "%s %d: calloc() failed\n", __func__, __LINE__);
		return EXIT_FAILURE;
	}
	snprintf(path, PATH_MAX, "/proc/%ld/mem", pid);
	int fd = open(path, O_RDWR);
	free(path);
	uintptr_t framelock_location = TEXT_OFFSET + (uintptr_t)find_pattern(fd, framelock, sizeof(framelock) / sizeof(struct pattern_byte), text_size);
	fprintf(stderr, "framelock location is %#" PRIxPTR "\n", framelock_location);
	uintptr_t speed_fix_location = 11 + TEXT_OFFSET + (uintptr_t)find_pattern(fd, framelock_speed_fix, sizeof(framelock_speed_fix) / sizeof(struct pattern_byte), text_size);
	fprintf(stderr, "framelock speed fix location is %#" PRIxPTR "\n", speed_fix_location);
	speed_fix_location = read_speed_fix_pointer_address(speed_fix_location, pid);
	fprintf(stderr, "framelock speed fix pointer address is %#" PRIxPTR "\n", speed_fix_location);
	write_float_mem(fd, framelock_location + 3, delta_time);
	write_float_mem(fd, speed_fix_location, speed_fix);
	close(fd);

	return EXIT_SUCCESS;
}
