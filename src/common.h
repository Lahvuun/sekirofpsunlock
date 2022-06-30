#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

struct ignorable_byte {
	bool is_ignored;
	uint8_t value;
};

struct context {
	FILE *f;
	pid_t pid;
	time_t timeout;
};

bool string_to_uint32(const char *s, int base, uint32_t *value_out);
bool find_section_info(const char *name, FILE *f, size_t *position_out, size_t *size_out);
bool find_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, FILE *f, uint8_t *buffer, size_t buffer_size, size_t section_position, size_t *index_out);
bool stop_and_wait(struct context *context);
bool seek_and_read_bytes(uint8_t *destination, size_t destination_length, size_t position, FILE *f);
bool seek_and_write_bytes(uint8_t *source, size_t source_length, size_t position, FILE *f);
