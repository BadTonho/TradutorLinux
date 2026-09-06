use crate::msix_contract::*;

use std::collections::{HashMap, HashSet};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;

const ZIP_LOCAL_HEADER_MAGIC: u32 = 0x0403_4b50;
const ZIP_CENTRAL_HEADER_MAGIC: u32 = 0x0201_4b50;
const ZIP_EOCD_MAGIC: u32 = 0x0605_4b50;

const ZIP_LOCAL_HEADER_SIZE: usize = 30;
const ZIP_CENTRAL_HEADER_SIZE: usize = 46;
const ZIP_EOCD_SIZE: usize = 22;

const XML_BOM: &[u8] = b"\xef\xbb\xbf";

#[cfg(not(test))]
extern "C" {
    fn tl_msix_inflate_raw(
        input: *const u8,
        input_length: u64,
        output: *mut u8,
        output_length: u64,
    ) -> i32;
}

#[derive(Clone, Copy, Debug)]
struct Failure {
    status: u32,
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
}

fn failure(
    status: u32,
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
) -> Failure {
    Failure {
        status,
        code,
        phase,
        input_offset,
        detail_value,
        message,
    }
}

fn malformed(
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
) -> Failure {
    failure(
        STATUS_MALFORMED,
        code,
        phase,
        input_offset,
        detail_value,
        message,
    )
}

fn truncated(
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
) -> Failure {
    failure(
        STATUS_TRUNCATED,
        code,
        phase,
        input_offset,
        detail_value,
        message,
    )
}

fn unsupported_format(
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
) -> Failure {
    failure(
        STATUS_UNSUPPORTED_FORMAT,
        code,
        phase,
        input_offset,
        detail_value,
        message,
    )
}

fn unsupported_mechanism(
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &'static [u8],
) -> Failure {
    failure(
        STATUS_UNSUPPORTED_MECHANISM,
        code,
        phase,
        input_offset,
        detail_value,
        message,
    )
}

fn internal_failure(message: &'static [u8]) -> Failure {
    failure(
        STATUS_INTERNAL,
        ERROR_INTERNAL,
        PHASE_NONE,
        UNKNOWN_OFFSET,
        0,
        message,
    )
}

fn offset(value: usize) -> u64 {
    u64::try_from(value).unwrap_or(UNKNOWN_OFFSET)
}

fn read_u16(bytes: &[u8], position: usize, code: u32, phase: u32) -> Result<u16, Failure> {
    let end = match position.checked_add(2) {
        Some(end) => end,
        None => {
            return Err(truncated(
                code,
                phase,
                offset(position),
                2,
                b"range ZIP truncado",
            ))
        }
    };
    if end > bytes.len() {
        return Err(truncated(
            code,
            phase,
            offset(position),
            2,
            b"range ZIP truncado",
        ));
    }
    Ok(u16::from_le_bytes([bytes[position], bytes[position + 1]]))
}

fn read_u32(bytes: &[u8], position: usize, code: u32, phase: u32) -> Result<u32, Failure> {
    let end = match position.checked_add(4) {
        Some(end) => end,
        None => {
            return Err(truncated(
                code,
                phase,
                offset(position),
                4,
                b"range ZIP truncado",
            ))
        }
    };
    if end > bytes.len() {
        return Err(truncated(
            code,
            phase,
            offset(position),
            4,
            b"range ZIP truncado",
        ));
    }
    Ok(u32::from_le_bytes([
        bytes[position],
        bytes[position + 1],
        bytes[position + 2],
        bytes[position + 3],
    ]))
}

fn range(
    bytes: &[u8],
    position: usize,
    length: usize,
    code: u32,
    phase: u32,
) -> Result<&[u8], Failure> {
    let end = match position.checked_add(length) {
        Some(end) => end,
        None => {
            return Err(truncated(
                code,
                phase,
                offset(position),
                u64::try_from(length).unwrap_or(u64::MAX),
                b"range ZIP truncado",
            ))
        }
    };
    if end > bytes.len() {
        return Err(truncated(
            code,
            phase,
            offset(position),
            u64::try_from(length).unwrap_or(u64::MAX),
            b"range ZIP truncado",
        ));
    }
    Ok(&bytes[position..end])
}

fn owned(bytes: &[u8]) -> Result<Vec<u8>, Failure> {
    let mut result = Vec::new();
    result
        .try_reserve_exact(bytes.len())
        .map_err(|_| internal_failure(b"falha ao reservar memoria do pacote"))?;
    result.extend_from_slice(bytes);
    Ok(result)
}

struct ZipEntry {
    name: Vec<u8>,
    normalized_name: Vec<u8>,
    compression_method: u16,
    crc32: u32,
    compressed_size: u32,
    uncompressed_size: u32,
    local_header_offset: u32,
    central_offset: usize,
}

struct ZipArchive {
    entries: Vec<ZipEntry>,
}

fn normalize_zip_name(name: &[u8], base_offset: usize) -> Result<(Vec<u8>, bool), Failure> {
    if name.is_empty()
        || name.len() > LIMIT_MAX_ZIP_FILENAME_BYTES as usize
        || name.contains(&0)
        || name[0] == b'/'
        || name[0] == b'\\'
        || (name.len() >= 2
            && name[0].is_ascii_alphabetic()
            && name[1] == b':')
    {
        return Err(malformed(
            ERROR_ZIP_PATH,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(base_offset),
            u64::try_from(name.len()).unwrap_or(u64::MAX),
            b"nome de entrada ZIP inseguro",
        ));
    }

    let directory = name
        .last()
        .is_some_and(|byte| *byte == b'/' || *byte == b'\\');
    let mut normalized = Vec::new();
    normalized
        .try_reserve_exact(name.len())
        .map_err(|_| internal_failure(b"falha ao reservar nome ZIP"))?;

    let mut component_start = 0usize;
    for (index, byte) in name.iter().enumerate() {
        if *byte != b'/' && *byte != b'\\' {
            continue;
        }
        let component = &name[component_start..index];
        if (component.is_empty() || component == b"." || component == b"..")
            && !(directory && index + 1 == name.len() && component.is_empty())
        {
            return Err(malformed(
                ERROR_ZIP_PATH,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(base_offset + component_start),
                0,
                b"nome ZIP contem traversal ou segmento ambiguo",
            ));
        }
        if !normalized.is_empty() {
            normalized.push(b'/');
        }
        normalized.extend_from_slice(component);
        component_start = index + 1;
    }

    let final_component = &name[component_start..];
    if final_component.is_empty() {
        if !directory {
            return Err(malformed(
                ERROR_ZIP_PATH,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(base_offset + component_start),
                0,
                b"nome ZIP termina em separador invalido",
            ));
        }
    } else if final_component == b"." || final_component == b".." {
        return Err(malformed(
            ERROR_ZIP_PATH,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(base_offset + component_start),
            0,
            b"nome ZIP contem traversal",
        ));
    }
    if !normalized.is_empty() && !final_component.is_empty() {
        normalized.push(b'/');
    }
    if !final_component.is_empty() {
        normalized.extend_from_slice(final_component);
    } else if directory && !normalized.ends_with(b"/") {
        normalized.push(b'/');
    }
    if normalized.is_empty() {
        return Err(malformed(
            ERROR_ZIP_PATH,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(base_offset),
            0,
            b"nome de entrada ZIP vazio",
        ));
    }
    Ok((normalized, directory))
}

fn parse_zip(input: &[u8]) -> Result<ZipArchive, Failure> {
    if input.len() > LIMIT_MAX_PACKAGE_BYTES as usize {
        return Err(failure(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            offset(input.len()),
            b"pacote excede o limite de entrada",
        ));
    }
    if input.len() < ZIP_EOCD_SIZE {
        return Err(truncated(
            ERROR_ZIP_EOCD,
            PHASE_ZIP_EOCD,
            offset(input.len()),
            ZIP_EOCD_SIZE as u64,
            b"pacote nao contem um EOCD completo",
        ));
    }

    let tail_start = input
        .len()
        .saturating_sub(ZIP_EOCD_SIZE + 65_535usize);
    let mut eocd_position = None;
    let mut position = input.len() - ZIP_EOCD_SIZE;
    loop {
        if read_u32(input, position, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)? == ZIP_EOCD_MAGIC {
            let comment_length = read_u16(
                input,
                position + 20,
                ERROR_ZIP_EOCD,
                PHASE_ZIP_EOCD,
            )? as usize;
            if position
                .checked_add(ZIP_EOCD_SIZE)
                .and_then(|value| value.checked_add(comment_length))
                .is_some_and(|end| end <= input.len())
            {
                eocd_position = Some(position);
                break;
            }
        }
        if position == tail_start {
            break;
        }
        position -= 1;
    }
    let eocd = eocd_position.ok_or_else(|| {
        truncated(
            ERROR_ZIP_EOCD,
            PHASE_ZIP_EOCD,
            offset(tail_start),
            ZIP_EOCD_SIZE as u64,
            b"EOCD do pacote nao foi encontrado",
        )
    })?;

    let disk_number = read_u16(input, eocd + 4, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;
    let central_disk = read_u16(input, eocd + 6, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;
    let entries_on_disk = read_u16(input, eocd + 8, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;
    let total_entries = read_u16(input, eocd + 10, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;
    let central_size = read_u32(input, eocd + 12, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;
    let central_offset = read_u32(input, eocd + 16, ERROR_ZIP_EOCD, PHASE_ZIP_EOCD)?;

    if total_entries == u16::MAX || central_size == u32::MAX || central_offset == u32::MAX {
        return Err(unsupported_format(
            ERROR_ZIP_EOCD,
            PHASE_ZIP_EOCD,
            offset(eocd),
            u64::from(total_entries),
            b"Zip64 nao e suportado",
        ));
    }
    if disk_number != 0 || central_disk != 0 || entries_on_disk != total_entries {
        return Err(unsupported_format(
            ERROR_ZIP_EOCD,
            PHASE_ZIP_EOCD,
            offset(eocd),
            u64::from(disk_number),
            b"pacote ZIP dividido em multiplos discos",
        ));
    }
    if total_entries == 0 || u64::from(total_entries) > LIMIT_MAX_ZIP_ENTRIES {
        return Err(malformed(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(eocd + 10),
            u64::from(total_entries),
            b"quantidade de entradas ZIP invalida",
        ));
    }

    let central_start = usize::try_from(central_offset).map_err(|_| {
        truncated(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(eocd + 16),
            u64::from(central_offset),
            b"offset da central directory nao cabe no host",
        )
    })?;
    let central_length = usize::try_from(central_size).map_err(|_| {
        truncated(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(eocd + 12),
            u64::from(central_size),
            b"tamanho da central directory nao cabe no host",
        )
    })?;
    let central_end = central_start.checked_add(central_length).ok_or_else(|| {
        truncated(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(central_start),
            u64::from(central_size),
            b"central directory excede o pacote",
        )
    })?;
    if central_start > input.len() || central_end > input.len() || central_end > eocd {
        return Err(truncated(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(central_start),
            u64::from(central_size),
            b"central directory excede o pacote",
        ));
    }

    let mut entries = Vec::new();
    entries
        .try_reserve_exact(total_entries as usize)
        .map_err(|_| internal_failure(b"falha ao reservar entradas ZIP"))?;
    let mut names = HashSet::new();
    let mut total_uncompressed = 0u64;
    let mut cursor = central_start;
    for _ in 0..total_entries {
        if cursor.checked_add(ZIP_CENTRAL_HEADER_SIZE).is_none()
            || cursor + ZIP_CENTRAL_HEADER_SIZE > central_end
        {
            return Err(truncated(
                ERROR_ZIP_CENTRAL_DIRECTORY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor),
                ZIP_CENTRAL_HEADER_SIZE as u64,
                b"registro da central directory truncado",
            ));
        }
        if read_u32(
            input,
            cursor,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )? != ZIP_CENTRAL_HEADER_MAGIC
        {
            return Err(malformed(
                ERROR_ZIP_CENTRAL_DIRECTORY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor),
                0,
                b"assinatura da central directory invalida",
            ));
        }
        let version_made_by = read_u16(
            input,
            cursor + 4,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let flags = read_u16(
            input,
            cursor + 8,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let compression_method = read_u16(
            input,
            cursor + 10,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let crc32 = read_u32(
            input,
            cursor + 16,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let compressed_size = read_u32(
            input,
            cursor + 20,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let uncompressed_size = read_u32(
            input,
            cursor + 24,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let filename_length = read_u16(
            input,
            cursor + 28,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )? as usize;
        let extra_length = read_u16(
            input,
            cursor + 30,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )? as usize;
        let comment_length = read_u16(
            input,
            cursor + 32,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )? as usize;
        let disk_start = read_u16(
            input,
            cursor + 34,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let external_attributes = read_u32(
            input,
            cursor + 38,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;
        let local_header_offset = read_u32(
            input,
            cursor + 42,
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
        )?;

        if filename_length == 0 || filename_length > LIMIT_MAX_ZIP_FILENAME_BYTES as usize {
            return Err(malformed(
                ERROR_ZIP_ENTRY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 28),
                u64::try_from(filename_length).unwrap_or(u64::MAX),
                b"nome de entrada ZIP invalido",
            ));
        }
        if compressed_size == u32::MAX || uncompressed_size == u32::MAX {
            return Err(unsupported_format(
                ERROR_ZIP_ENTRY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 20),
                u64::from(compressed_size),
                b"entrada ZIP usa Zip64",
            ));
        }
        if disk_start != 0 || (flags & 1) != 0 {
            return Err(unsupported_mechanism(
                ERROR_ZIP_COMPRESSION,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 8),
                u64::from(flags),
                b"entrada ZIP criptografada ou multipartes",
            ));
        }
        if compression_method != 0 && compression_method != 8 {
            return Err(unsupported_mechanism(
                ERROR_ZIP_COMPRESSION,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 10),
                u64::from(compression_method),
                b"metodo de compressao ZIP nao suportado",
            ));
        }
        if u64::from(compressed_size) > LIMIT_MAX_PACKAGE_UNCOMPRESSED_BYTES
            || u64::from(uncompressed_size) > LIMIT_MAX_ENTRY_UNCOMPRESSED_BYTES
            || total_uncompressed
                .checked_add(u64::from(uncompressed_size))
                .is_none_or(|value| value > LIMIT_MAX_PACKAGE_UNCOMPRESSED_BYTES)
        {
            return Err(malformed(
                ERROR_ZIP_ENTRY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 20),
                u64::from(uncompressed_size),
                b"tamanho descompactado do pacote excede o limite",
            ));
        }
        let unix_mode = external_attributes >> 16;
        if (version_made_by >> 8) == 3 && (unix_mode & 0o170000) == 0o120000 {
            return Err(malformed(
                ERROR_ZIP_LINK,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor + 38),
                u64::from(unix_mode),
                b"links simbolicos nao sao permitidos no pacote",
            ));
        }

        let metadata_length = filename_length
            .checked_add(extra_length)
            .and_then(|value| value.checked_add(comment_length))
            .ok_or_else(|| {
                truncated(
                    ERROR_ZIP_CENTRAL_DIRECTORY,
                    PHASE_ZIP_CENTRAL_DIRECTORY,
                    offset(cursor),
                    u64::MAX,
                    b"metadados da entrada ZIP excedem o pacote",
                )
            })?;
        let metadata_end = cursor
            .checked_add(ZIP_CENTRAL_HEADER_SIZE)
            .and_then(|value| value.checked_add(metadata_length))
            .ok_or_else(|| {
                truncated(
                    ERROR_ZIP_CENTRAL_DIRECTORY,
                    PHASE_ZIP_CENTRAL_DIRECTORY,
                    offset(cursor),
                    u64::MAX,
                    b"metadados da entrada ZIP excedem o pacote",
                )
            })?;
        if metadata_end > central_end {
            return Err(truncated(
                ERROR_ZIP_CENTRAL_DIRECTORY,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(cursor),
                u64::try_from(metadata_length).unwrap_or(u64::MAX),
                b"metadados da entrada ZIP truncados",
            ));
        }
        let name_start = cursor + ZIP_CENTRAL_HEADER_SIZE;
        let name = owned(&input[name_start..name_start + filename_length])?;
        let (normalized_name, _) = normalize_zip_name(&name, name_start)?;
        if !names.insert(normalized_name.clone()) {
            return Err(malformed(
                ERROR_ZIP_PATH,
                PHASE_ZIP_CENTRAL_DIRECTORY,
                offset(name_start),
                u64::try_from(filename_length).unwrap_or(u64::MAX),
                b"entradas ZIP colidem apos normalizacao",
            ));
        }
        if local_header_offset >= central_offset {
            return Err(truncated(
                ERROR_ZIP_LOCAL_HEADER,
                PHASE_ZIP_ENTRY,
                offset(name_start),
                u64::from(local_header_offset),
                b"local header ZIP fora da area de dados",
            ));
        }
        total_uncompressed += u64::from(uncompressed_size);
        entries.push(ZipEntry {
            name,
            normalized_name,
            compression_method,
            crc32,
            compressed_size,
            uncompressed_size,
            local_header_offset,
            central_offset: central_start,
        });
        cursor = metadata_end;
    }
    if cursor != central_end {
        return Err(malformed(
            ERROR_ZIP_CENTRAL_DIRECTORY,
            PHASE_ZIP_CENTRAL_DIRECTORY,
            offset(cursor),
            u64::try_from(central_end - cursor).unwrap_or(u64::MAX),
            b"central directory possui bytes extras",
        ));
    }
    Ok(ZipArchive { entries })
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    !crc
}

fn inflate_raw(input: &[u8], expected_length: usize) -> Result<Vec<u8>, Failure> {
    let mut output = Vec::new();
    output
        .try_reserve_exact(expected_length)
        .map_err(|_| internal_failure(b"falha ao reservar saida descompactada"))?;
    output.resize(expected_length, 0);
    #[cfg(not(test))]
    let input_length = u64::try_from(input.len()).map_err(|_| {
        failure(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_ZIP_DECOMPRESS,
            UNKNOWN_OFFSET,
            offset(input.len()),
            b"entrada DEFLATE nao cabe na ABI",
        )
    })?;
    let output_length = u64::try_from(expected_length).map_err(|_| {
        failure(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_ZIP_DECOMPRESS,
            UNKNOWN_OFFSET,
            u64::MAX,
            b"saida DEFLATE nao cabe na ABI",
        )
    })?;
    #[cfg(test)]
    let result = {
        let _ = (input, output_length);
        -1
    };
    #[cfg(not(test))]
    let result = unsafe {
        tl_msix_inflate_raw(
            input.as_ptr(),
            input_length,
            output.as_mut_ptr(),
            output_length,
        )
    };
    if result != 0 {
        return Err(malformed(
            ERROR_ZIP_ENTRY,
            PHASE_ZIP_DECOMPRESS,
            UNKNOWN_OFFSET,
            u64::try_from(result).unwrap_or(u64::MAX),
            b"dados DEFLATE invalidos",
        ));
    }
    Ok(output)
}

fn read_zip_entry(input: &[u8], entry: &ZipEntry) -> Result<Vec<u8>, Failure> {
    let local_offset = usize::try_from(entry.local_header_offset).map_err(|_| {
        truncated(
            ERROR_ZIP_LOCAL_HEADER,
            PHASE_ZIP_ENTRY,
            u64::from(entry.local_header_offset),
            ZIP_LOCAL_HEADER_SIZE as u64,
            b"local header ZIP nao cabe no host",
        )
    })?;
    let local = range(
        input,
        local_offset,
        ZIP_LOCAL_HEADER_SIZE,
        ERROR_ZIP_LOCAL_HEADER,
        PHASE_ZIP_ENTRY,
    )?;
    if u32::from_le_bytes([local[0], local[1], local[2], local[3]]) != ZIP_LOCAL_HEADER_MAGIC {
        return Err(malformed(
            ERROR_ZIP_LOCAL_HEADER,
            PHASE_ZIP_ENTRY,
            offset(local_offset),
            0,
            b"assinatura do local header ZIP invalida",
        ));
    }
    let local_flags = u16::from_le_bytes([local[6], local[7]]);
    let local_method = u16::from_le_bytes([local[8], local[9]]);
    let local_name_length = u16::from_le_bytes([local[26], local[27]]) as usize;
    let local_extra_length = u16::from_le_bytes([local[28], local[29]]) as usize;
    if local_method != entry.compression_method
        || (local_flags & 1) != 0
        || local_name_length != entry.name.len()
    {
        return Err(malformed(
            ERROR_ZIP_LOCAL_HEADER,
            PHASE_ZIP_ENTRY,
            offset(local_offset),
            u64::from(local_method),
            b"local header ZIP nao corresponde a central directory",
        ));
    }
    let name_start = local_offset + ZIP_LOCAL_HEADER_SIZE;
    let local_name = range(
        input,
        name_start,
        local_name_length,
        ERROR_ZIP_LOCAL_HEADER,
        PHASE_ZIP_ENTRY,
    )?;
    if local_name != entry.name {
        return Err(malformed(
            ERROR_ZIP_LOCAL_HEADER,
            PHASE_ZIP_ENTRY,
            offset(name_start),
            u64::try_from(local_name_length).unwrap_or(u64::MAX),
            b"nome do local header ZIP nao corresponde",
        ));
    }
    let data_start = name_start
        .checked_add(local_name_length)
        .and_then(|value| value.checked_add(local_extra_length))
        .ok_or_else(|| {
            truncated(
                ERROR_ZIP_LOCAL_HEADER,
                PHASE_ZIP_ENTRY,
                offset(name_start),
                u64::MAX,
                b"extra field do local header excede o pacote",
            )
        })?;
    let compressed_length = usize::try_from(entry.compressed_size).map_err(|_| {
        truncated(
            ERROR_ZIP_ENTRY,
            PHASE_ZIP_ENTRY,
            offset(data_start),
            u64::from(entry.compressed_size),
            b"entrada comprimida nao cabe no host",
        )
    })?;
    let data_end = data_start.checked_add(compressed_length).ok_or_else(|| {
        truncated(
            ERROR_ZIP_ENTRY,
            PHASE_ZIP_ENTRY,
            offset(data_start),
            u64::from(entry.compressed_size),
            b"dados da entrada ZIP excedem o pacote",
        )
    })?;
    if data_end > entry.central_offset || data_end > input.len() {
        return Err(truncated(
            ERROR_ZIP_ENTRY,
            PHASE_ZIP_ENTRY,
            offset(data_start),
            u64::from(entry.compressed_size),
            b"dados da entrada ZIP truncados",
        ));
    }
    let compressed = &input[data_start..data_end];
    let expected_length = usize::try_from(entry.uncompressed_size).map_err(|_| {
        truncated(
            ERROR_ZIP_ENTRY,
            PHASE_ZIP_ENTRY,
            offset(data_start),
            u64::from(entry.uncompressed_size),
            b"saida ZIP nao cabe no host",
        )
    })?;
    let output = if entry.compression_method == 0 {
        if entry.compressed_size != entry.uncompressed_size {
            return Err(malformed(
                ERROR_ZIP_ENTRY,
                PHASE_ZIP_ENTRY,
                offset(data_start),
                u64::from(entry.uncompressed_size),
                b"tamanho stored nao corresponde ao tamanho descompactado",
            ));
        }
        owned(compressed)?
    } else {
        inflate_raw(compressed, expected_length)?
    };
    if output.len() != expected_length || crc32(&output) != entry.crc32 {
        return Err(malformed(
            ERROR_ZIP_CRC,
            PHASE_ZIP_ENTRY,
            offset(data_start),
            u64::from(entry.crc32),
            b"CRC da entrada ZIP invalido",
        ));
    }
    Ok(output)
}

fn xml_local_name(name: &[u8]) -> &[u8] {
    name.iter()
        .rposition(|byte| *byte == b':')
        .map_or(name, |position| &name[position + 1..])
}

fn xml_name_start(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte == b'_' || byte == b':'
}

fn xml_name_character(byte: u8) -> bool {
    xml_name_start(byte) || byte.is_ascii_digit() || byte == b'-' || byte == b'.'
}

fn xml_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\r' | b'\n')
}

fn append_utf8(output: &mut Vec<u8>, code_point: u32) {
    if code_point <= 0x7f {
        output.push(code_point as u8);
    } else if code_point <= 0x7ff {
        output.push(0xc0 | ((code_point >> 6) as u8));
        output.push(0x80 | ((code_point & 0x3f) as u8));
    } else if code_point <= 0xffff {
        output.push(0xe0 | ((code_point >> 12) as u8));
        output.push(0x80 | (((code_point >> 6) & 0x3f) as u8));
        output.push(0x80 | ((code_point & 0x3f) as u8));
    } else {
        output.push(0xf0 | ((code_point >> 18) as u8));
        output.push(0x80 | (((code_point >> 12) & 0x3f) as u8));
        output.push(0x80 | (((code_point >> 6) & 0x3f) as u8));
        output.push(0x80 | ((code_point & 0x3f) as u8));
    }
}

fn decode_entities(value: &[u8]) -> Result<Vec<u8>, Failure> {
    if !value.contains(&b'&') {
        return owned(value);
    }
    let mut decoded = Vec::new();
    decoded
        .try_reserve_exact(value.len())
        .map_err(|_| internal_failure(b"falha ao reservar atributo XML"))?;
    let mut position = 0usize;
    while position < value.len() {
        if value[position] != b'&' {
            decoded.push(value[position]);
            position += 1;
            continue;
        }
        let relative_end = value[position + 1..]
            .iter()
            .position(|byte| *byte == b';')
            .ok_or_else(|| {
                malformed(
                    ERROR_XML_ENTITY,
                    PHASE_XML,
                    offset(position),
                    0,
                    b"entidade XML sem terminador",
                )
            })?;
        let end = position + 1 + relative_end;
        let entity = &value[position + 1..end];
        if entity.is_empty() || entity.len() > 15 {
            return Err(malformed(
                ERROR_XML_ENTITY,
                PHASE_XML,
                offset(position),
                u64::try_from(entity.len()).unwrap_or(u64::MAX),
                b"entidade XML excede o limite",
            ));
        }
        match entity {
            b"amp" => decoded.push(b'&'),
            b"lt" => decoded.push(b'<'),
            b"gt" => decoded.push(b'>'),
            b"quot" => decoded.push(b'"'),
            b"apos" => decoded.push(b'\''),
            _ if entity.first() == Some(&b'#') => {
                let (base, digits) = if entity.get(1) == Some(&b'x')
                    || entity.get(1) == Some(&b'X')
                {
                    (16u32, &entity[2..])
                } else {
                    (10u32, &entity[1..])
                };
                if digits.is_empty() {
                    return Err(malformed(
                        ERROR_XML_ENTITY,
                        PHASE_XML,
                        offset(position),
                        0,
                        b"entidade numerica XML vazia",
                    ));
                }
                let mut code_point = 0u32;
                for digit in digits {
                    let value_digit = match digit {
                        b'0'..=b'9' => u32::from(*digit - b'0'),
                        b'a'..=b'f' if base == 16 => u32::from(*digit - b'a' + 10),
                        b'A'..=b'F' if base == 16 => u32::from(*digit - b'A' + 10),
                        _ => {
                            return Err(malformed(
                                ERROR_XML_ENTITY,
                                PHASE_XML,
                                offset(position),
                                u64::from(*digit),
                                b"digito invalido em entidade XML",
                            ))
                        }
                    };
                    if value_digit >= base
                        || code_point > (0x10ffff - value_digit) / base
                    {
                        return Err(malformed(
                            ERROR_XML_ENTITY,
                            PHASE_XML,
                            offset(position),
                            u64::from(code_point),
                            b"entidade numerica XML fora do intervalo",
                        ));
                    }
                    code_point = code_point * base + value_digit;
                }
                if code_point == 0
                    || code_point > 0x10ffff
                    || (0xd800..=0xdfff).contains(&code_point)
                {
                    return Err(malformed(
                        ERROR_XML_ENTITY,
                        PHASE_XML,
                        offset(position),
                        u64::from(code_point),
                        b"ponto de codigo XML invalido",
                    ));
                }
                append_utf8(&mut decoded, code_point);
            }
            _ => {
                return Err(malformed(
                    ERROR_XML_ENTITY,
                    PHASE_XML,
                    offset(position),
                    0,
                    b"entidade XML nao suportada",
                ))
            }
        }
        position = end + 1;
    }
    Ok(decoded)
}

struct XmlAttribute {
    name: Vec<u8>,
    value: Vec<u8>,
}

struct XmlNode {
    name: Vec<u8>,
    application_index: Option<usize>,
}

struct ApplicationModel {
    id: Vec<u8>,
    executable: Vec<u8>,
    display_name: Vec<u8>,
    entry_point: Vec<u8>,
}

struct PackageModel {
    package_name: Vec<u8>,
    publisher: Vec<u8>,
    version: Vec<u8>,
    main_executable: Option<Vec<u8>>,
    applications: Vec<ApplicationModel>,
}

struct XmlParser<'a> {
    xml: &'a [u8],
    position: usize,
    root_name: Vec<u8>,
    root_seen: bool,
    root_closed: bool,
    stack: Vec<XmlNode>,
    info: PackageModel,
}

impl<'a> XmlParser<'a> {
    fn new(xml: &'a [u8]) -> Self {
        Self {
            xml,
            position: 0,
            root_name: Vec::new(),
            root_seen: false,
            root_closed: false,
            stack: Vec::new(),
            info: PackageModel {
                package_name: Vec::new(),
                publisher: Vec::new(),
                version: Vec::new(),
                main_executable: None,
                applications: Vec::new(),
            },
        }
    }

    fn skip_space(&mut self) {
        while self.position < self.xml.len() && xml_space(self.xml[self.position]) {
            self.position += 1;
        }
    }

    fn parse_name(&mut self) -> Result<Vec<u8>, Failure> {
        if self.position >= self.xml.len() || !xml_name_start(self.xml[self.position]) {
            return Err(malformed(
                ERROR_XML_SYNTAX,
                PHASE_XML,
                offset(self.position),
                0,
                b"nome XML invalido",
            ));
        }
        let start = self.position;
        self.position += 1;
        while self.position < self.xml.len() && xml_name_character(self.xml[self.position]) {
            self.position += 1;
        }
        owned(&self.xml[start..self.position])
    }

    fn skip_until(&mut self, terminator: &[u8], prefix_length: usize) -> Result<(), Failure> {
        let search_start = self.position + prefix_length;
        let relative = self.xml[search_start..]
            .windows(terminator.len())
            .position(|window| window == terminator)
            .ok_or_else(|| {
                truncated(
                    ERROR_XML_SYNTAX,
                    PHASE_XML,
                    offset(self.position),
                    u64::try_from(terminator.len()).unwrap_or(u64::MAX),
                    b"construcao XML nao terminada",
                )
            })?;
        self.position = search_start + relative + terminator.len();
        Ok(())
    }

    fn attribute<'b>(&self, attributes: &'b [XmlAttribute], local_name: &[u8]) -> Option<&'b [u8]> {
        attributes
            .iter()
            .find(|attribute| xml_local_name(&attribute.name) == local_name)
            .map(|attribute| attribute.value.as_slice())
    }

    fn parse_end_element(&mut self) -> Result<(), Failure> {
        self.position += 2;
        let name = self.parse_name()?;
        self.skip_space();
        if self.position >= self.xml.len()
            || self.xml[self.position] != b'>'
            || self.stack.last().is_none_or(|node| node.name != name)
        {
            return Err(malformed(
                ERROR_XML_SYNTAX,
                PHASE_XML,
                offset(self.position),
                0,
                    b"fechamento XML nao corresponde ao elemento aberto",
            ));
        }
        self.position += 1;
        self.stack.pop();
        if self.stack.is_empty() {
            self.root_closed = true;
        }
        Ok(())
    }

    fn parse_start_element(&mut self) -> Result<(), Failure> {
        if self.root_closed || self.stack.len() >= LIMIT_MAX_XML_DEPTH {
            return Err(malformed(
                ERROR_XML_LIMIT,
                PHASE_XML,
                offset(self.position),
                self.stack.len() as u64,
                b"profundidade XML excede o limite",
            ));
        }
        self.position += 1;
        let name = self.parse_name()?;
        let mut attributes = Vec::new();
        let mut self_closing = false;
        loop {
            self.skip_space();
            if self.position >= self.xml.len() {
                return Err(truncated(
                    ERROR_XML_SYNTAX,
                    PHASE_XML,
                    offset(self.position),
                    1,
                    b"elemento XML truncado",
                ));
            }
            match self.xml[self.position] {
                b'>' => {
                    self.position += 1;
                    break;
                }
                b'/' => {
                    self.position += 1;
                    self.skip_space();
                    if self.position >= self.xml.len() || self.xml[self.position] != b'>' {
                        return Err(malformed(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(self.position),
                            0,
                            b"elemento XML autocontido invalido",
                        ));
                    }
                    self.position += 1;
                    self_closing = true;
                    break;
                }
                _ => {
                    if attributes.len() >= LIMIT_MAX_XML_ATTRIBUTES {
                        return Err(malformed(
                            ERROR_XML_LIMIT,
                            PHASE_XML,
                            offset(self.position),
                            LIMIT_MAX_XML_ATTRIBUTES as u64,
                            b"quantidade de atributos XML excede o limite",
                        ));
                    }
                    let attribute_name = self.parse_name()?;
                    self.skip_space();
                    if self.position >= self.xml.len() || self.xml[self.position] != b'=' {
                        return Err(malformed(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(self.position),
                            0,
                            b"atributo XML sem sinal de igualdade",
                        ));
                    }
                    self.position += 1;
                    self.skip_space();
                    if self.position >= self.xml.len()
                        || (self.xml[self.position] != b'\'' && self.xml[self.position] != b'"')
                    {
                        return Err(malformed(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(self.position),
                            0,
                            b"valor de atributo XML sem aspas",
                        ));
                    }
                    let quote = self.xml[self.position];
                    self.position += 1;
                    let value_start = self.position;
                    while self.position < self.xml.len() && self.xml[self.position] != quote {
                        if self.xml[self.position] == b'<' {
                            return Err(malformed(
                                ERROR_XML_SYNTAX,
                                PHASE_XML,
                                offset(self.position),
                                0,
                                b"valor de atributo XML contem '<'",
                            ));
                        }
                        self.position += 1;
                    }
                    if self.position >= self.xml.len() {
                        return Err(truncated(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(value_start),
                            1,
                            b"valor de atributo XML truncado",
                        ));
                    }
                    let value = decode_entities(&self.xml[value_start..self.position])?;
                    if value.contains(&0) {
                        return Err(malformed(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(value_start),
                            0,
                            b"atributo XML contem NUL",
                        ));
                    }
                    self.position += 1;
                    if attributes
                        .iter()
                        .any(|attribute: &XmlAttribute| attribute.name == attribute_name)
                    {
                        return Err(malformed(
                            ERROR_XML_SYNTAX,
                            PHASE_XML,
                            offset(value_start),
                            0,
                            b"atributo XML repetido",
                        ));
                    }
                    attributes.push(XmlAttribute {
                        name: attribute_name,
                        value,
                    });
                }
            }
        }

        let inherited_application = self
            .stack
            .last()
            .and_then(|node| node.application_index);
        let mut node = XmlNode {
            name: name.clone(),
            application_index: inherited_application,
        };
        let local_name = xml_local_name(&name);
        if self.stack.is_empty() {
            if self.root_seen {
                return Err(malformed(
                    ERROR_XML_SYNTAX,
                    PHASE_XML,
                    offset(self.position),
                    0,
                    b"manifesto XML possui mais de uma raiz",
                ));
            }
            self.root_seen = true;
            self.root_name = owned(local_name)?;
        }
        if local_name == b"Identity" {
            if let Some(value) = self.attribute(&attributes, b"Name") {
                self.info.package_name = owned(value)?;
            }
            if let Some(value) = self.attribute(&attributes, b"Publisher") {
                self.info.publisher = owned(value)?;
            }
            if let Some(value) = self.attribute(&attributes, b"Version") {
                self.info.version = owned(value)?;
            }
        } else if local_name == b"Application" {
            let application = ApplicationModel {
                id: self.attribute(&attributes, b"Id").unwrap_or(&[]).to_vec(),
                executable: self
                    .attribute(&attributes, b"Executable")
                    .unwrap_or(&[])
                    .to_vec(),
                display_name: Vec::new(),
                entry_point: self
                    .attribute(&attributes, b"EntryPoint")
                    .unwrap_or(&[])
                    .to_vec(),
            };
            if !application.executable.is_empty() || !application.id.is_empty() {
                if self.info.main_executable.is_none() && !application.executable.is_empty() {
                    self.info.main_executable = Some(application.executable.clone());
                }
                self.info.applications.push(application);
                node.application_index = Some(self.info.applications.len() - 1);
            }
        } else if local_name == b"VisualElements" {
            if let Some(index) = node.application_index {
                if let Some(value) = self.attribute(&attributes, b"DisplayName") {
                    self.info.applications[index].display_name = value.to_vec();
                }
            }
        }
        if !self_closing {
            self.stack.push(node);
        } else if self.stack.is_empty() {
            self.root_closed = true;
        }
        Ok(())
    }

    fn parse(mut self) -> Result<PackageModel, Failure> {
        if self.xml.starts_with(XML_BOM) {
            self.position = XML_BOM.len();
        }
        while self.position < self.xml.len() {
            if self.xml[self.position] != b'<' {
                let start = self.position;
                while self.position < self.xml.len() && self.xml[self.position] != b'<' {
                    self.position += 1;
                }
                let text = &self.xml[start..self.position];
                if self.root_closed && text.iter().any(|byte| !xml_space(*byte)) {
                    return Err(malformed(
                        ERROR_XML_SYNTAX,
                        PHASE_XML,
                        offset(start),
                        0,
                        b"texto apos a raiz XML nao e espaco",
                    ));
                }
                if text.contains(&b'&') {
                    let _ = decode_entities(text)?;
                }
                continue;
            }
            if self.xml[self.position..].starts_with(b"<!--") {
                self.skip_until(b"-->", 4)?;
            } else if self.xml[self.position..].starts_with(b"<![CDATA[") {
                self.skip_until(b"]]>", 9)?;
            } else if self.xml[self.position..].starts_with(b"<?") {
                self.skip_until(b"?>", 2)?;
            } else if self.xml[self.position..].starts_with(b"<!") {
                return Err(malformed(
                    ERROR_XML_ENTITY,
                    PHASE_XML,
                    offset(self.position),
                    0,
                    b"DTD e declaracoes de entidade XML nao sao suportados",
                ));
            } else if self.xml[self.position..].starts_with(b"</") {
                self.parse_end_element()?;
            } else {
                self.parse_start_element()?;
            }
        }
        if !self.root_seen
            || !self.root_closed
            || !self.stack.is_empty()
            || self.root_name != b"Package"
        {
            return Err(malformed(
                ERROR_MANIFEST,
                PHASE_MANIFEST,
                offset(self.position),
                0,
                b"AppxManifest.xml incompleto ou sem raiz Package",
            ));
        }
        if self.info.applications.len() > LIMIT_MAX_ZIP_ENTRIES as usize {
            return Err(malformed(
                ERROR_XML_LIMIT,
                PHASE_MANIFEST,
                offset(self.position),
                self.info.applications.len() as u64,
                b"quantidade de aplicacoes excede o limite",
            ));
        }
        for application in &self.info.applications {
            if application.executable.is_empty() {
                continue;
            }
            let (_, directory) = normalize_zip_name(&application.executable, self.position)?;
            if directory {
                return Err(malformed(
                    ERROR_ZIP_PATH,
                    PHASE_MANIFEST,
                    offset(self.position),
                    0,
                    b"executavel declarado aponta para um diretorio",
                ));
            }
        }
        Ok(self.info)
    }
}

fn parse_manifest(xml: &[u8]) -> Result<PackageModel, Failure> {
    if xml.is_empty() || xml.len() > LIMIT_MAX_MANIFEST_BYTES as usize {
        return Err(malformed(
            ERROR_MANIFEST,
            PHASE_MANIFEST,
            offset(xml.len()),
            u64::try_from(xml.len()).unwrap_or(u64::MAX),
            b"manifesto excede o limite",
        ));
    }
    XmlParser::new(xml).parse()
}

#[derive(Clone, Copy, Default, Debug)]
struct StringRef {
    offset: u64,
    length: u64,
}

struct StringPool {
    values: Vec<Vec<u8>>,
    indexes: HashMap<Vec<u8>, usize>,
}

impl StringPool {
    fn new() -> Self {
        Self {
            values: Vec::new(),
            indexes: HashMap::new(),
        }
    }

    fn intern(&mut self, value: &[u8]) -> Result<Option<usize>, Failure> {
        if value.is_empty() {
            return Ok(None);
        }
        if let Some(index) = self.indexes.get(value) {
            return Ok(Some(*index));
        }
        let owned_value = owned(value)?;
        let index = self.values.len();
        self.values
            .try_reserve(1)
            .map_err(|_| internal_failure(b"falha ao reservar tabela de strings"))?;
        self.indexes
            .try_reserve(1)
            .map_err(|_| internal_failure(b"falha ao reservar indice de strings"))?;
        self.values.push(owned_value.clone());
        self.indexes.insert(owned_value, index);
        Ok(Some(index))
    }
}

#[derive(Debug)]
struct WirePlan {
    total_size: u64,
    info_offset: u64,
    applications_offset: u64,
    strings_offset: u64,
    info_refs: [StringRef; 4],
    application_refs: Vec<[StringRef; 4]>,
    string_values: Vec<Vec<u8>>,
    string_refs: Vec<StringRef>,
}

fn align8(value: u64) -> Result<u64, Failure> {
    value
        .checked_add(7)
        .map(|aligned| aligned & !7)
        .ok_or_else(|| {
            failure(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_OUTPUT_TOO_LARGE,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                value,
                b"tamanho TLMS excede o limite",
            )
        })
}

fn add_size(left: u64, right: u64) -> Result<u64, Failure> {
    left.checked_add(right).ok_or_else(|| {
        failure(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            right,
            b"tamanho TLMS excede o limite",
        )
    })
}

fn ref_for(index: Option<usize>, refs: &[StringRef]) -> StringRef {
    index.map_or_else(StringRef::default, |value| refs[value])
}

fn plan_wire(model: &PackageModel) -> Result<WirePlan, Failure> {
    if model.applications.len() > LIMIT_MAX_ZIP_ENTRIES as usize {
        return Err(malformed(
            ERROR_MANIFEST,
            PHASE_MANIFEST,
            UNKNOWN_OFFSET,
            model.applications.len() as u64,
            b"quantidade de aplicacoes excede o limite",
        ));
    }

    let mut pool = StringPool::new();
    let info_indexes = [
        pool.intern(&model.package_name)?,
        pool.intern(&model.publisher)?,
        pool.intern(&model.version)?,
        pool.intern(model.main_executable.as_deref().unwrap_or(&[]))?,
    ];
    let mut application_indexes = Vec::new();
    application_indexes
        .try_reserve_exact(model.applications.len())
        .map_err(|_| internal_failure(b"falha ao reservar aplicacoes TLMS"))?;
    for application in &model.applications {
        application_indexes.push([
            pool.intern(&application.id)?,
            pool.intern(&application.executable)?,
            pool.intern(&application.display_name)?,
            pool.intern(&application.entry_point)?,
        ]);
    }

    let mut cursor = WIRE_HEADER_SIZE as u64;
    let info_offset = cursor;
    cursor = add_size(cursor, WIRE_INFO_STRIDE as u64)?;
    let applications_offset = if application_indexes.is_empty() {
        0
    } else {
        cursor = align8(cursor)?;
        let result = cursor;
        let application_bytes = (application_indexes.len() as u64)
            .checked_mul(WIRE_APPLICATION_STRIDE as u64)
            .ok_or_else(|| {
                failure(
                    STATUS_OUTPUT_TOO_LARGE,
                    ERROR_OUTPUT_TOO_LARGE,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    application_indexes.len() as u64,
                    b"tabela de aplicacoes TLMS excede o limite",
                )
            })?;
        cursor = add_size(cursor, application_bytes)?;
        result
    };

    let strings_offset = if pool.values.is_empty() {
        0
    } else {
        cursor = align8(cursor)?;
        let result = cursor;
        for value in &pool.values {
            if value.len() > u32::MAX as usize {
                return Err(failure(
                    STATUS_OUTPUT_TOO_LARGE,
                    ERROR_OUTPUT_TOO_LARGE,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    u64::try_from(value.len()).unwrap_or(u64::MAX),
                    b"string TLMS excede o limite de tamanho",
                ));
            }
            cursor = add_size(cursor, WIRE_STRING_RECORD_HEADER_SIZE as u64)?;
            cursor = add_size(cursor, u64::try_from(value.len()).unwrap_or(u64::MAX))?;
            cursor = align8(cursor)?;
        }
        result
    };
    if cursor > LIMIT_MAX_SERIALIZED_BYTES {
        return Err(failure(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            cursor,
            b"resultado TLMS excede o limite de saida",
        ));
    }

    let mut string_refs = Vec::new();
    string_refs
        .try_reserve_exact(pool.values.len())
        .map_err(|_| internal_failure(b"falha ao reservar referencias TLMS"))?;
    let mut string_cursor = strings_offset;
    for value in &pool.values {
        let data_offset = add_size(string_cursor, WIRE_STRING_RECORD_HEADER_SIZE as u64)?;
        string_refs.push(StringRef {
            offset: data_offset,
            length: u64::try_from(value.len()).unwrap_or(u64::MAX),
        });
        string_cursor = align8(add_size(data_offset, u64::try_from(value.len()).unwrap_or(u64::MAX))?)?;
    }
    let info_refs = info_indexes.map(|index| ref_for(index, &string_refs));
    let application_refs = application_indexes
        .into_iter()
        .map(|indexes| indexes.map(|index| ref_for(index, &string_refs)))
        .collect();

    Ok(WirePlan {
        total_size: cursor,
        info_offset,
        applications_offset,
        strings_offset,
        info_refs,
        application_refs,
        string_values: pool.values,
        string_refs,
    })
}

fn write_u32(output: &mut [u8], position: usize, value: u32) {
    output[position..position + 4].copy_from_slice(&value.to_le_bytes());
}

fn write_u64(output: &mut [u8], position: usize, value: u64) {
    output[position..position + 8].copy_from_slice(&value.to_le_bytes());
}

fn write_ref(output: &mut [u8], position: usize, value: StringRef) {
    write_u64(output, position, value.offset);
    write_u64(output, position + 8, value.length);
}

fn write_descriptor(
    output: &mut [u8],
    table: usize,
    table_offset: u64,
    count: u64,
    stride: u32,
    flags: u32,
) {
    let position = WIRE_DESCRIPTOR_OFFSET + table * WIRE_DESCRIPTOR_SIZE;
    write_u64(output, position, table_offset);
    write_u64(output, position + 8, count);
    write_u32(output, position + 16, stride);
    write_u32(output, position + 20, flags);
}

fn write_wire(output: &mut [u8], plan: &WirePlan) {
    output[..plan.total_size as usize].fill(0);
    output[0..4].copy_from_slice(&WIRE_MAGIC);
    output[4..6].copy_from_slice(&WIRE_MAJOR.to_le_bytes());
    output[6..8].copy_from_slice(&WIRE_MINOR.to_le_bytes());
    write_u32(output, 8, WIRE_HEADER_SIZE as u32);
    write_u64(output, 12, plan.total_size);
    write_u32(output, 20, WIRE_TABLE_COUNT as u32);

    write_descriptor(
        output,
        0,
        plan.info_offset,
        1,
        WIRE_INFO_STRIDE as u32,
        0,
    );
    write_descriptor(
        output,
        1,
        plan.applications_offset,
        plan.application_refs.len() as u64,
        WIRE_APPLICATION_STRIDE as u32,
        0,
    );
    write_descriptor(
        output,
        2,
        plan.strings_offset,
        plan.string_values.len() as u64,
        0,
        if plan.string_values.is_empty() {
            0
        } else {
            WIRE_VARIABLE_RECORDS
        },
    );

    let info_position = plan.info_offset as usize;
    for (index, value) in plan.info_refs.iter().enumerate() {
        write_ref(output, info_position + index * WIRE_STRING_REF_SIZE, *value);
    }
    if plan.applications_offset != 0 {
        let mut position = plan.applications_offset as usize;
        for application in &plan.application_refs {
            for (index, value) in application.iter().enumerate() {
                write_ref(output, position + index * WIRE_STRING_REF_SIZE, *value);
            }
            position += WIRE_APPLICATION_STRIDE;
        }
    }
    let mut string_position = plan.strings_offset as usize;
    for (index, value) in plan.string_values.iter().enumerate() {
        write_u32(output, string_position, value.len() as u32);
        let data_position = string_position + WIRE_STRING_RECORD_HEADER_SIZE;
        output[data_position..data_position + value.len()].copy_from_slice(value);
        debug_assert_eq!(plan.string_refs[index].offset, data_position as u64);
        string_position = align8((data_position + value.len()) as u64)
            .unwrap_or(plan.total_size) as usize;
    }
}

#[allow(clippy::too_many_arguments)]
fn write_error(
    status: u32,
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: &[u8],
    error: *mut TlMsixErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    if error_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    unsafe {
        *error_required = match u64::try_from(message.len().saturating_add(1)) {
            Ok(value) => value,
            Err(_) => return STATUS_INTERNAL,
        };
        if !error.is_null() {
            (*error).code = code;
            (*error).phase = phase;
            (*error).input_offset = input_offset;
            (*error).detail_value = detail_value;
        }
    }
    let required = message.len().saturating_add(1);
    if error_capacity != 0 && error_message.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if error_capacity < required as u64 {
        if error_capacity != 0 {
            let writable = usize::try_from(error_capacity - 1)
                .unwrap_or(0)
                .min(message.len());
            unsafe {
                std::ptr::copy_nonoverlapping(
                    message.as_ptr(),
                    error_message.cast::<u8>(),
                    writable,
                );
                *error_message.cast::<u8>().add(writable) = 0;
            }
        }
        return STATUS_BUFFER_TOO_SMALL;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(
            message.as_ptr(),
            error_message.cast::<u8>(),
            message.len(),
        );
        *error_message.cast::<u8>().add(message.len()) = 0;
    }
    status
}

unsafe fn input_slice<'a>(input: *const u8, input_length: u64) -> Result<&'a [u8], Failure> {
    if input_length > LIMIT_MAX_PACKAGE_BYTES {
        return Err(failure(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            b"pacote excede o limite de entrada",
        ));
    }
    if input_length != 0 && input.is_null() {
        return Err(failure(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            b"ponteiro de entrada e nulo",
        ));
    }
    let length = usize::try_from(input_length).map_err(|_| {
        failure(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            b"tamanho da entrada nao cabe no host",
        )
    })?;
    if length == 0 {
        Ok(&[])
    } else {
        Ok(unsafe { slice::from_raw_parts(input, length) })
    }
}

unsafe fn call_size(
    input: *const u8,
    input_length: u64,
    output_required: *mut u64,
    error: *mut TlMsixErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    if output_required.is_null() {
        return write_error(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            0,
            b"output_required e obrigatorio",
            error,
            error_message,
            error_capacity,
            error_required,
        );
    }
    *output_required = 0;
    let bytes = match input_slice(input, input_length) {
        Ok(bytes) => bytes,
        Err(error_value) => {
            return write_error(
                error_value.status,
                error_value.code,
                error_value.phase,
                error_value.input_offset,
                error_value.detail_value,
                error_value.message,
                error,
                error_message,
                error_capacity,
                error_required,
            )
        }
    };
    match parse_and_plan(bytes) {
        Ok(plan) => {
            *output_required = plan.total_size;
            write_error(
                STATUS_SUCCESS,
                ERROR_NONE,
                PHASE_NONE,
                UNKNOWN_OFFSET,
                0,
                b"",
                error,
                error_message,
                error_capacity,
                error_required,
            )
        }
        Err(error_value) => write_error(
            error_value.status,
            error_value.code,
            error_value.phase,
            error_value.input_offset,
            error_value.detail_value,
            error_value.message,
            error,
            error_message,
            error_capacity,
            error_required,
        ),
    }
}

#[allow(clippy::too_many_arguments)]
unsafe fn call_fill(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlMsixErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    if output_required.is_null() {
        return write_error(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            0,
            b"output_required e obrigatorio",
            error,
            error_message,
            error_capacity,
            error_required,
        );
    }
    *output_required = 0;
    if output_capacity != 0 && output.is_null() {
        return write_error(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            output_capacity,
            b"ponteiro de saida e nulo",
            error,
            error_message,
            error_capacity,
            error_required,
        );
    }
    let bytes = match input_slice(input, input_length) {
        Ok(bytes) => bytes,
        Err(error_value) => {
            return write_error(
                error_value.status,
                error_value.code,
                error_value.phase,
                error_value.input_offset,
                error_value.detail_value,
                error_value.message,
                error,
                error_message,
                error_capacity,
                error_required,
            )
        }
    };
    let plan = match parse_and_plan(bytes) {
        Ok(plan) => plan,
        Err(error_value) => {
            return write_error(
                error_value.status,
                error_value.code,
                error_value.phase,
                error_value.input_offset,
                error_value.detail_value,
                error_value.message,
                error,
                error_message,
                error_capacity,
                error_required,
            )
        }
    };
    *output_required = plan.total_size;
    if output_capacity < plan.total_size {
        return write_error(
            STATUS_BUFFER_TOO_SMALL,
            ERROR_BUFFER_TOO_SMALL,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            plan.total_size,
            b"buffer de saida TLMS insuficiente",
            error,
            error_message,
            error_capacity,
            error_required,
        );
    }
    let output_length = match usize::try_from(plan.total_size) {
        Ok(value) => value,
        Err(_) => {
            return write_error(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_OUTPUT_TOO_LARGE,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                plan.total_size,
                b"saida TLMS nao cabe no host",
                error,
                error_message,
                error_capacity,
                error_required,
            )
        }
    };
    let output_slice = slice::from_raw_parts_mut(output, output_length);
    write_wire(output_slice, &plan);
    write_error(
        STATUS_SUCCESS,
        ERROR_NONE,
        PHASE_NONE,
        UNKNOWN_OFFSET,
        0,
        b"",
        error,
        error_message,
        error_capacity,
        error_required,
    )
}

fn parse_and_plan(input: &[u8]) -> Result<WirePlan, Failure> {
    let archive = parse_zip(input)?;
    if archive
        .entries
        .iter()
        .any(|entry| entry.normalized_name == b"AppxBundleManifest.xml")
    {
        return Err(unsupported_format(
            ERROR_MANIFEST,
            PHASE_MANIFEST,
            UNKNOWN_OFFSET,
            0,
            b"bundles MSIX/AppX nao sao suportados",
        ));
    }
    let manifest_entry = archive
        .entries
        .iter()
        .find(|entry| entry.normalized_name == b"AppxManifest.xml");
    let manifest_entry = manifest_entry.ok_or_else(|| {
        malformed(
            ERROR_MANIFEST,
            PHASE_MANIFEST,
            UNKNOWN_OFFSET,
            0,
            b"AppxManifest.xml nao foi encontrado",
        )
    })?;
    let manifest_compressed = u64::from(manifest_entry.compressed_size);
    let manifest_uncompressed = u64::from(manifest_entry.uncompressed_size);
    if manifest_compressed > LIMIT_MAX_MANIFEST_COMPRESSED_BYTES
        || manifest_uncompressed > LIMIT_MAX_MANIFEST_BYTES
    {
        return Err(malformed(
            ERROR_MANIFEST,
            PHASE_MANIFEST,
            UNKNOWN_OFFSET,
            manifest_uncompressed,
            b"AppxManifest.xml excede o limite",
        ));
    }
    let manifest = read_zip_entry(input, manifest_entry)?;
    let model = parse_manifest(&manifest)?;
    plan_wire(&model)
}

#[no_mangle]
pub unsafe extern "C" fn tl_msix_parse_v1_size(
    input: *const u8,
    input_length: u64,
    output_required: *mut u64,
    error: *mut TlMsixErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    match catch_unwind(AssertUnwindSafe(|| unsafe {
        call_size(
            input,
            input_length,
            output_required,
            error,
            error_message,
            error_capacity,
            error_required,
        )
    })) {
        Ok(status) => status,
        Err(_) => write_error(
            STATUS_INTERNAL,
            ERROR_INTERNAL,
            PHASE_NONE,
            UNKNOWN_OFFSET,
            0,
            b"panic interno capturado no parser MSIX",
            error,
            error_message,
            error_capacity,
            error_required,
        ),
    }
}

#[no_mangle]
pub unsafe extern "C" fn tl_msix_parse_v1_fill(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlMsixErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    match catch_unwind(AssertUnwindSafe(|| unsafe {
        call_fill(
            input,
            input_length,
            output,
            output_capacity,
            output_required,
            error,
            error_message,
            error_capacity,
            error_required,
        )
    })) {
        Ok(status) => status,
        Err(_) => write_error(
            STATUS_INTERNAL,
            ERROR_INTERNAL,
            PHASE_NONE,
            UNKNOWN_OFFSET,
            0,
            b"panic interno capturado no parser MSIX",
            error,
            error_message,
            error_capacity,
            error_required,
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_zip_reference() {
        assert_eq!(crc32(b"123456789"), 0xcbf4_3926);
    }

    #[test]
    fn zip_names_normalize_separators_and_reject_traversal() {
        let (name, directory) = normalize_zip_name(b"bin\\app.exe", 0).unwrap();
        assert_eq!(name, b"bin/app.exe");
        assert!(!directory);
        assert!(normalize_zip_name(b"../app.exe", 0).is_err());
        assert!(normalize_zip_name(b"bin//app.exe", 0).is_err());
        assert!(normalize_zip_name(b"C:/app.exe", 0).is_err());
    }

    #[test]
    fn entities_decode_without_requiring_utf8_input() {
        let decoded = decode_entities(b"A&amp;&#x1F680;").unwrap();
        assert_eq!(decoded, b"A&\xF0\x9F\x9A\x80");
        assert!(decode_entities(b"&evil;").is_err());
    }

    #[test]
    fn empty_input_is_truncated_without_panicking() {
        let result = parse_and_plan(&[]);
        assert_eq!(result.unwrap_err().status, STATUS_TRUNCATED);
    }
}
