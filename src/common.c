#define _POSIX_C_SOURCE 1

#include "common.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define IMAGE_BASE 0x140000000

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

static bool size_t_to_long(size_t value, long *value_out)
{
	if (value > LONG_MAX) {
		fprintf(stderr, "size_t value is larger than LONG_MAX\n");
		return false;
	}

	*value_out = value;

	return true;
}

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

bool string_to_uint32(const char *s, int base, uint32_t *value_out)
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

bool find_section_info(const char *name, FILE *f, size_t *position_out, size_t *size_out)
{
	size_t name_length = strlen(name);
	assert(name_length < 9);

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

		if (!strncmp(name, section_header.name, name_length)) {
			*size_out = section_header.virtual_size;
			*position_out = IMAGE_BASE + section_header.virtual_address;

			return true;
		}
	}
	fprintf(stderr, "couldn't find section with name %s\n", name);

	return false;
}

bool find_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, FILE *f, uint8_t *buffer, size_t buffer_size, size_t section_position, size_t *index_out)
{
	if (!seek_and_read_bytes(buffer, buffer_size, section_position, f)) {
		fprintf(stderr, "seek_and_read_bytes() failed\n");
		return false;
	}

	for (size_t i = 0; i + pattern_bytes_length < buffer_size; ++i) {
		if (same_pattern(pattern_bytes, pattern_bytes_length, buffer + i, buffer_size - i)) {
			*index_out = i;
			return true;
		}
	}

	return false;
}

bool stop_and_wait(struct context *context)
{
	if (kill(context->pid, SIGSTOP) == -1) {
		perror("kill() failed");
		return false;
	}

	static_assert(!WIFSTOPPED(0), "WIFSTOPPED triggered on 0");
	int wstatus = 0;
	while (!WIFSTOPPED(wstatus)) {
		pid_t pid_waited = waitpid(context->pid, &wstatus, 0);
		if (pid_waited != context->pid) {
			perror("waitpid() failed");
			return false;
		}
	}

	return true;
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
