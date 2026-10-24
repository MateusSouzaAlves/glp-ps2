# Derived decoder only; preserve clean pinned source and licensing. Never ignore short reads.
set(chd_source "${PLAY_SOURCE}/deps/libchdr/src/libchdr_chd.c")
file(SHA256 "${chd_source}" source_hash)
string(TOUPPER "${source_hash}" source_hash)
if(NOT source_hash STREQUAL "7937ED77C4943F1F401EF35E912E09D3A92330E5A2BFCB8BDE92A2B382F3DF7A")
    message(FATAL_ERROR "Pinned libchdr reader changed; review short-read behavior")
endif()
file(READ "${chd_source}" chd_content)
string(REPLACE "\r\n" "\n" chd_content "${chd_content}")
set(before [=[
				core_fseek(chd->file, blockoffs, SEEK_SET);
				int result = core_fread(chd->file, dest, chd->header.hunkbytes);
]=])
string(FIND "${chd_content}" "${before}" at)
if(at LESS 0)
    message(FATAL_ERROR "Missing NONE hunk-read error patch context")
endif()
string(LENGTH "${before}" length)
math(EXPR after "${at} + ${length}")
string(SUBSTRING "${chd_content}" ${after} -1 tail)
string(FIND "${tail}" "${before}" duplicate)
if(NOT duplicate LESS 0)
    message(FATAL_ERROR "Ambiguous NONE hunk-read patch context")
endif()
string(REPLACE "${before}" [=[
                if(core_fseek(chd->file, blockoffs, SEEK_SET) != 0)
                    return CHDERR_READ_ERROR;
                if(core_fread(chd->file, dest, chd->header.hunkbytes) != chd->header.hunkbytes)
                    return CHDERR_READ_ERROR;
]=] chd_content "${chd_content}")
# Extend the exact-source gate to every map read and data seek used by v5 codecs.
function(replace_reader_once before after)
    string(FIND "${chd_content}" "${before}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing bounded v5 reader patch context")
    endif()
    string(LENGTH "${before}" length)
    math(EXPR tail_at "${at} + ${length}")
    string(SUBSTRING "${chd_content}" ${tail_at} -1 tail)
    string(FIND "${tail}" "${before}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "Ambiguous bounded v5 reader patch context")
    endif()
    string(REPLACE "${before}" "${after}" updated "${chd_content}")
    set(chd_content "${updated}" PARENT_SCOPE)
endfunction()
replace_reader_once([=[
		core_fseek(chd->file, header->mapoffset, SEEK_SET);
		result = core_fread(chd->file, header->rawmap, rawmapsize);
		return CHDERR_NONE;
]=] [=[
        if(core_fseek(chd->file, header->mapoffset, SEEK_SET) != 0 ||
           core_fread(chd->file, header->rawmap, rawmapsize) != rawmapsize)
            return CHDERR_READ_ERROR;
        return CHDERR_NONE;
]=])
replace_reader_once([=[
	core_fseek(chd->file, header->mapoffset, SEEK_SET);
	result = core_fread(chd->file, rawbuf, sizeof(rawbuf));
]=] [=[
    if(core_fseek(chd->file, header->mapoffset, SEEK_SET) != 0 ||
       core_fread(chd->file, rawbuf, sizeof(rawbuf)) != sizeof(rawbuf))
        return CHDERR_READ_ERROR;
]=])
replace_reader_once([=[
	core_fseek(chd->file, header->mapoffset + 16, SEEK_SET);
	result = core_fread(chd->file, compressed_ptr, mapbytes);
]=] [=[
    if(core_fseek(chd->file, header->mapoffset + 16, SEEK_SET) != 0 ||
       core_fread(chd->file, compressed_ptr, mapbytes) != mapbytes)
    {
        free(compressed_ptr);
        return CHDERR_READ_ERROR;
    }
]=])
replace_reader_once([=[
	/* free memory */
	free(compressed_ptr);
	free(bitbuf);
	delete_huffman_decoder(decoder);

	/* verify the final CRC */
]=] [=[
    /* Even a matching CRC cannot authorize a map decoded past the available bits. */
    int overflow = bitstream_overflow(bitbuf);
    free(compressed_ptr);
    free(bitbuf);
    delete_huffman_decoder(decoder);
    if(overflow) return CHDERR_DECOMPRESSION_ERROR;

	/* verify the final CRC */
]=])
replace_reader_once([=[
		core_fseek(chd->file, offset, SEEK_SET);
		bytes = core_fread(chd->file, chd->compressed, size);
]=] [=[
        if(core_fseek(chd->file, offset, SEEK_SET) != 0) return NULL;
        bytes = core_fread(chd->file, chd->compressed, size);
]=])
replace_reader_once([=[
		core_fseek(chd->file, offset, SEEK_SET);
		bytes = core_fread(chd->file, dest, size);
]=] [=[
        if(core_fseek(chd->file, offset, SEEK_SET) != 0) return CHDERR_READ_ERROR;
        bytes = core_fread(chd->file, dest, size);
]=])
set(derived_chd "${CMAKE_BINARY_DIR}/derived/libchdr_chd.c")
file(WRITE "${derived_chd}" "${chd_content}")
get_target_property(chd_sources chdr-static SOURCES)
list(LENGTH chd_sources old_count)
list(REMOVE_ITEM chd_sources "src/libchdr_chd.c" "${chd_source}")
list(LENGTH chd_sources new_count)
math(EXPR removed "${old_count} - ${new_count}")
if(NOT removed EQUAL 1)
    message(FATAL_ERROR "Expected exactly one original decoder compilation unit")
endif()
list(APPEND chd_sources "${derived_chd}")
set_property(TARGET chdr-static PROPERTY SOURCES "${chd_sources}")
target_include_directories(chdr-static PRIVATE "${PLAY_SOURCE}/deps/libchdr/src")
