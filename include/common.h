#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define log_failure(str) (fprintf(stderr, "%s:%s at %d\n", str, __FILE__, __LINE__))

struct game_patch_data;

struct game_patch_context {
	FILE *f;
	struct game_patch_data *game_patch_data;
};

typedef bool (*game_patch_function_t)(struct game_patch_context *context);

struct game_patch {
	game_patch_function_t game_patch_function;
	struct game_patch_data *game_patch_data;
};

struct ignorable_byte {
    bool is_ignored;
    uint8_t value;
};

bool string_to_uintmax_t(const char *s, int base, uintmax_t *n_out);
bool string_to_pid_t(char *s, int base, pid_t *pid_out);
bool find_pattern(const struct ignorable_byte *pattern_bytes, const size_t pattern_bytes_length, const uint8_t *bytes,
		  const size_t bytes_length, size_t *position_out);
bool seek_and_write_bytes(uint8_t *source, size_t source_length, size_t position, FILE *f);
bool seek_and_read_bytes(uint8_t *destination, size_t destination_length, size_t position, FILE *f);
bool patch(pid_t pid, struct game_patch *game_patch);
