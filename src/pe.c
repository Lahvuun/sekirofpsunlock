#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pe.h"

#define IMAGE_BASE 0x140000000

bool extract_section(char *name, FILE *f, uint8_t **section_bytes_out, size_t *section_bytes_size_out, size_t *section_position_out)
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
