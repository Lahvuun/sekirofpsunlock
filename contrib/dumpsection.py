#!/usr/bin/env python3

import collections
import os
import struct
import sys
import typing

IMAGE_BASE = 0x140000000
COFF_HEADER_STRUCT = struct.Struct("<IHHIIIHH")
SECTION_HEADER_STRUCT = struct.Struct("<8sII24x")

DosHeader = collections.namedtuple("DosHeader", ("magic", "coff_header_offset"))
CoffHeader = collections.namedtuple("CoffHeader", ("signature", "machine", "number_of_sections", "time_date_stamp", "pointer_to_symbol_table", "number_of_symbols", "size_of_optional_header", "characteristics"))
CoffOptionalHeader = collections.namedtuple("CoffOptionalHeader", ("magic", "number_of_rva_and_size"))
SectionHeader = collections.namedtuple("SectionHeader", ("name", "virtual_size", "virtual_address"))

def read_dos_header(f: typing.BinaryIO, offset: int) -> DosHeader:
    dos_header_struct = struct.Struct("<H58xi")
    f.seek(offset, os.SEEK_SET)
    return DosHeader(*dos_header_struct.unpack(f.read(dos_header_struct.size)))

def read_coff_header(f: typing.BinaryIO, offset: int) -> CoffHeader:
    f.seek(offset, os.SEEK_SET)
    return CoffHeader(*COFF_HEADER_STRUCT.unpack(f.read(COFF_HEADER_STRUCT.size)))

def read_coff_optional_header(f: typing.BinaryIO, offset: int) -> CoffOptionalHeader:
    coff_optional_header_struct = struct.Struct("<H104xH")
    f.seek(offset, os.SEEK_SET)
    return CoffOptionalHeader(*coff_optional_header_struct.unpack(f.read(coff_optional_header_struct.size)))

def read_section_header(f: typing.BinaryIO, offset: int) -> SectionHeader:
    f.seek(offset, os.SEEK_SET)
    return SectionHeader(*SECTION_HEADER_STRUCT.unpack(f.read(SECTION_HEADER_STRUCT.size)))

if __name__ == "__main__":
    mem_path = sys.argv[1]
    section_name = sys.argv[2]

    with open(mem_path, "rb") as f:
        dos_header = read_dos_header(f, IMAGE_BASE)
        assert(dos_header.magic == 0x5a4d)

        coff_header = read_coff_header(f, IMAGE_BASE + dos_header.coff_header_offset)
        assert(coff_header.signature == 0x4550)

        coff_optional_header = read_coff_optional_header(f, IMAGE_BASE + dos_header.coff_header_offset + COFF_HEADER_STRUCT.size)
        assert(coff_optional_header.magic == 0x20b)

        for i in range(coff_header.number_of_sections):
            section_header = read_section_header(f, IMAGE_BASE + dos_header.coff_header_offset + COFF_HEADER_STRUCT.size + coff_header.size_of_optional_header + i * SECTION_HEADER_STRUCT.size)
            if section_header.name.decode("utf-8").rstrip("\x00") == section_name:
                f.seek(IMAGE_BASE + section_header.virtual_address, os.SEEK_SET)
                sys.stdout.buffer.write(f.read(section_header.virtual_size))
                break
        else:
            raise ValueError("Could not find section {} in {}".format(section_name, mem_path))
