#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

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
	uint8_t ignored[20];
};

bool extract_section(char *name, FILE *f, uint8_t **section_bytes_out, size_t *section_bytes_size_out, size_t *section_position_out);
