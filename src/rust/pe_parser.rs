use std::collections::HashMap;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

const STATUS_SUCCESS: u32 = 0;
const STATUS_TRUNCATED: u32 = 1;
const STATUS_MALFORMED: u32 = 2;
const STATUS_UNSUPPORTED_ARCHITECTURE: u32 = 3;
const STATUS_UNSUPPORTED_FORMAT: u32 = 4;
const STATUS_UNSUPPORTED_MECHANISM: u32 = 5;
const STATUS_INVALID_ARGUMENT: u32 = 6;
const STATUS_BUFFER_TOO_SMALL: u32 = 7;
const STATUS_INPUT_TOO_LARGE: u32 = 8;
const STATUS_OUTPUT_TOO_LARGE: u32 = 9;
const STATUS_INTERNAL: u32 = 10;

const ERROR_NONE: u32 = 0;
const ERROR_INVALID_ARGUMENT: u32 = 1;
const ERROR_BUFFER_TOO_SMALL: u32 = 2;
const ERROR_INPUT_TOO_LARGE: u32 = 3;
const ERROR_INTERNAL: u32 = 5;
const ERROR_DOS_HEADER: u32 = 16;
const ERROR_PE_HEADER: u32 = 17;
const ERROR_COFF_HEADER: u32 = 18;
const ERROR_OPTIONAL_HEADER: u32 = 19;
const ERROR_SECTION_TABLE: u32 = 20;
const ERROR_DIRECTORY_RANGE: u32 = 21;
const ERROR_IMPORT_TABLE: u32 = 22;
const ERROR_DELAY_IMPORT_TABLE: u32 = 23;
const ERROR_EXPORT_TABLE: u32 = 24;
const ERROR_TLS_DIRECTORY: u32 = 25;
const ERROR_UNWIND_DIRECTORY: u32 = 26;
const ERROR_RELOCATION_DIRECTORY: u32 = 27;
const ERROR_SERIALIZATION_LIMIT: u32 = 28;

const PHASE_NONE: u32 = 0;
const PHASE_INPUT: u32 = 1;
const PHASE_DOS_HEADER: u32 = 2;
const PHASE_COFF_HEADER: u32 = 3;
const PHASE_OPTIONAL_HEADER: u32 = 4;
const PHASE_SECTIONS: u32 = 5;
const PHASE_EXPORTS: u32 = 6;
const PHASE_IMPORTS: u32 = 7;
const PHASE_DELAY_IMPORTS: u32 = 8;
const PHASE_TLS: u32 = 9;
const PHASE_UNWIND: u32 = 10;
const PHASE_RELOCATIONS: u32 = 11;
const PHASE_SERIALIZE: u32 = 12;
const PHASE_WIRE: u32 = 13;

const UNKNOWN_OFFSET: u64 = u64::MAX;
const MAX_SECTIONS: usize = 65_535;
const MAX_IMPORT_DLLS: usize = 1_024;
const MAX_SYMBOLS_PER_DLL: usize = 4_096;
const MAX_RELOC_BLOCKS: usize = 4_096;
const MAX_RUNTIME_FUNCTIONS: usize = 65_536;
const MAX_CSTRING: usize = 65_535;
const MAX_EXPORT_FUNCTIONS: usize = 65_536;
const MAX_EXPORT_NAMES: usize = 65_536;
const MAX_TLS_CALLBACKS: usize = 65_536;
const MAX_SERIALIZED_BYTES: u64 = 268_435_456;

const DOS_HEADER_SIZE: usize = 64;
const COFF_HEADER_SIZE: usize = 20;
const SECTION_HEADER_SIZE: usize = 40;
const OPTIONAL_HEADER_BASE_SIZE: usize = 112;
const IMPORT_DESCRIPTOR_SIZE: usize = 20;
const DELAY_IMPORT_DESCRIPTOR_SIZE: usize = 32;
const RUNTIME_FUNCTION_SIZE: usize = 12;
const UNWIND_HEADER_SIZE: usize = 4;
const UNWIND_CODE_SIZE: usize = 2;
const THUNK_SIZE: usize = 8;
const RELOC_BLOCK_HEADER_SIZE: usize = 8;
const RELOC_ENTRY_SIZE: usize = 2;
const EXPORT_DIRECTORY_SIZE: usize = 40;

const MACHINE_AMD64: u16 = 0x8664;
const OPTIONAL_MAGIC_32: u16 = 0x10b;
const OPTIONAL_MAGIC_64: u16 = 0x20b;
const ORDINAL_FLAG_64: u64 = 0x8000_0000_0000_0000;
const IMAGE_FILE_DLL: u16 = 0x2000;

const WIRE_HEADER_SIZE: usize = 416;
const WIRE_DESCRIPTOR_SIZE: usize = 24;
const WIRE_DESCRIPTOR_OFFSET: usize = 32;
const WIRE_TABLE_COUNT: usize = 16;
const WIRE_STRING_RECORD_HEADER_SIZE: usize = 8;
const WIRE_FLAG_VARIABLE: u32 = 1;

const STRIDES: [u32; WIRE_TABLE_COUNT] =
    [136, 40, 0, 40, 32, 40, 32, 48, 8, 24, 72, 8, 8, 32, 8, 0];

const TABLE_INFO: usize = 0;
const TABLE_SECTIONS: usize = 1;
const TABLE_STRINGS: usize = 2;
const TABLE_IMPORT_DLLS: usize = 3;
const TABLE_IMPORT_SYMBOLS: usize = 4;
const TABLE_DELAY_DLLS: usize = 5;
const TABLE_DELAY_SYMBOLS: usize = 6;
const TABLE_EXPORTS: usize = 7;
const TABLE_TLS_CALLBACKS: usize = 8;
const TABLE_RUNTIME_FUNCTIONS: usize = 9;
const TABLE_UNWIND_INFOS: usize = 10;
const TABLE_UNWIND_CODES: usize = 11;
const TABLE_UNWIND_EPILOGS: usize = 12;
const TABLE_RELOC_BLOCKS: usize = 13;
const TABLE_RELOC_ENTRIES: usize = 14;
const TABLE_RESERVED: usize = 15;

const DIR_EXPORT: usize = 0;
const DIR_IMPORT: usize = 1;
const DIR_RESOURCE: usize = 2;
const DIR_EXCEPTION: usize = 3;
const DIR_RELOC: usize = 5;
const DIR_TLS: usize = 9;
const DIR_DELAY_IMPORT: usize = 13;

#[repr(C)]
pub struct TlPeErrorV1 {
    pub code: u32,
    pub phase: u32,
    pub input_offset: u64,
    pub detail_value: u64,
}

#[derive(Clone)]
struct PeError {
    status: u32,
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: Vec<u8>,
    output_required: Option<u64>,
}

fn error(
    status: u32,
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
    message: impl Into<String>,
) -> PeError {
    PeError {
        status,
        code,
        phase,
        input_offset,
        detail_value,
        message: message.into().into_bytes(),
        output_required: None,
    }
}

fn output_buffer_too_small(required: usize) -> PeError {
    let mut result = error(
        STATUS_BUFFER_TOO_SMALL,
        ERROR_BUFFER_TOO_SMALL,
        PHASE_WIRE,
        UNKNOWN_OFFSET,
        required as u64,
        "buffer de saída insuficiente",
    );
    result.output_required = Some(required as u64);
    result
}

fn truncated(phase: u32, offset: usize, detail: u64, message: impl Into<String>) -> PeError {
    error(
        STATUS_TRUNCATED,
        match phase {
            PHASE_DOS_HEADER => ERROR_DOS_HEADER,
            PHASE_COFF_HEADER => ERROR_COFF_HEADER,
            PHASE_OPTIONAL_HEADER => ERROR_OPTIONAL_HEADER,
            PHASE_SECTIONS => ERROR_SECTION_TABLE,
            PHASE_UNWIND => ERROR_UNWIND_DIRECTORY,
            _ => ERROR_DIRECTORY_RANGE,
        },
        phase,
        offset as u64,
        detail,
        message,
    )
}

fn malformed(
    code: u32,
    phase: u32,
    offset: usize,
    detail: u64,
    message: impl Into<String>,
) -> PeError {
    error(
        STATUS_MALFORMED,
        code,
        phase,
        offset as u64,
        detail,
        message,
    )
}

struct Reader<'a> {
    data: &'a [u8],
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data }
    }

    fn len(&self) -> usize {
        self.data.len()
    }

    fn has_range(&self, offset: usize, length: usize) -> bool {
        offset <= self.data.len() && length <= self.data.len() - offset
    }

    fn bytes(&self, offset: usize, length: usize) -> Option<&'a [u8]> {
        if self.has_range(offset, length) {
            Some(&self.data[offset..offset + length])
        } else {
            None
        }
    }

    fn u16(&self, offset: usize) -> Option<u16> {
        let b = self.bytes(offset, 2)?;
        Some(u16::from_le_bytes([b[0], b[1]]))
    }

    fn u32(&self, offset: usize) -> Option<u32> {
        let b = self.bytes(offset, 4)?;
        Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }

    fn u64(&self, offset: usize) -> Option<u64> {
        let b = self.bytes(offset, 8)?;
        Some(u64::from_le_bytes([
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        ]))
    }

    fn cstring(&self, offset: usize) -> Option<Vec<u8>> {
        if offset >= self.len() {
            return None;
        }
        let max_end = offset.saturating_add(MAX_CSTRING).min(self.len());
        for index in offset..max_end {
            if self.data[index] == 0 {
                return Some(self.data[offset..index].to_vec());
            }
        }
        None
    }
}

#[derive(Default, Clone)]
struct Info {
    is_pe32_plus: bool,
    is_dll: bool,
    machine: u16,
    number_of_sections: u16,
    entry_point: u32,
    image_base: u64,
    section_alignment: u32,
    size_of_image: u32,
    size_of_headers: u32,
    subsystem: u16,
    directories: [[u32; 2]; 7],
    export_ordinal_base: u32,
    tls: Tls,
}

#[derive(Default, Clone)]
struct Section {
    name: Vec<u8>,
    virtual_address: u32,
    virtual_size: u32,
    raw_data_pointer: u32,
    raw_data_size: u32,
    characteristics: u32,
}

#[derive(Default, Clone)]
struct Symbol {
    by_ordinal: bool,
    ordinal: u16,
    name: Vec<u8>,
    iat_rva: u32,
}

#[derive(Default, Clone)]
struct Dll {
    name: Vec<u8>,
    symbols: Vec<Symbol>,
}

#[derive(Default, Clone)]
struct Export {
    by_name: bool,
    name: Vec<u8>,
    ordinal: u16,
    rva: u32,
    forwarder: Vec<u8>,
}

#[derive(Default, Clone)]
struct RelocEntry {
    kind: u16,
    offset: u16,
}

#[derive(Default, Clone)]
struct RelocBlock {
    page_rva: u32,
    entries: Vec<RelocEntry>,
}

#[derive(Default, Clone)]
struct UnwindCode {
    code_offset: u8,
    operation: u8,
    operation_info: u8,
    operand: u32,
}

#[derive(Default, Clone)]
struct Epilog {
    begin_rva: u32,
    end_rva: u32,
}

#[derive(Default, Clone)]
struct Unwind {
    version: u8,
    flags: u8,
    prolog_size: u8,
    frame_register: u8,
    frame_offset: u8,
    extended_set_fpreg: bool,
    handler_rva: u32,
    handler_data_rva: u32,
    chained: Option<(u32, u32, u32)>,
    codes: Vec<UnwindCode>,
    epilogs: Vec<Epilog>,
}

#[derive(Default, Clone)]
struct RuntimeFunction {
    begin_rva: u32,
    end_rva: u32,
    unwind_info_rva: u32,
    unwind: Unwind,
}

#[derive(Default, Clone)]
struct Tls {
    start_address_of_raw_data: u64,
    end_address_of_raw_data: u64,
    address_of_index: u64,
    address_of_callbacks: u64,
    size_of_zero_fill: u32,
    characteristics: u32,
    callback_vas: Vec<u64>,
}

#[derive(Default, Clone)]
struct Model {
    info: Info,
    sections: Vec<Section>,
    imports: Vec<Dll>,
    delay_imports: Vec<Dll>,
    exports: Vec<Export>,
    runtime_functions: Vec<RuntimeFunction>,
    relocations: Vec<RelocBlock>,
}

struct Parser<'a> {
    reader: Reader<'a>,
    model: Model,
}

impl<'a> Parser<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self {
            reader: Reader::new(data),
            model: Model::default(),
        }
    }

    fn get_u16(&self, offset: usize, phase: u32, code: u32) -> Result<u16, PeError> {
        self.reader
            .u16(offset)
            .ok_or_else(|| truncated(phase, offset, 2, "leitura de uint16 além do fim da entrada"))
            .map_err(|mut e| {
                e.code = code;
                e
            })
    }

    fn get_u32(&self, offset: usize, phase: u32, code: u32) -> Result<u32, PeError> {
        self.reader
            .u32(offset)
            .ok_or_else(|| truncated(phase, offset, 4, "leitura de uint32 além do fim da entrada"))
            .map_err(|mut e| {
                e.code = code;
                e
            })
    }

    fn get_u64(&self, offset: usize, phase: u32, code: u32) -> Result<u64, PeError> {
        self.reader
            .u64(offset)
            .ok_or_else(|| truncated(phase, offset, 8, "leitura de uint64 além do fim da entrada"))
            .map_err(|mut e| {
                e.code = code;
                e
            })
    }

    fn rva_to_file_offset(
        &self,
        rva: u32,
        length: usize,
        phase: u32,
        code: u32,
    ) -> Result<usize, PeError> {
        let end = u64::from(rva).checked_add(length as u64).ok_or_else(|| {
            malformed(
                code,
                phase,
                rva as usize,
                length as u64,
                "intervalo RVA excede uint32",
            )
        })?;
        if end <= u64::from(self.model.info.size_of_headers) && end <= self.reader.len() as u64 {
            return Ok(rva as usize);
        }
        for section in &self.model.sections {
            let span = u64::from(section.virtual_size.max(section.raw_data_size));
            let section_end = u64::from(section.virtual_address) + span;
            if u64::from(rva) >= u64::from(section.virtual_address) && u64::from(rva) < section_end
            {
                let delta = u64::from(rva) - u64::from(section.virtual_address);
                if delta + length as u64 > u64::from(section.raw_data_size) {
                    break;
                }
                let file_offset = u64::from(section.raw_data_pointer) + delta;
                if file_offset + length as u64 > self.reader.len() as u64 {
                    break;
                }
                return Ok(file_offset as usize);
            }
        }
        Err(malformed(
            code,
            phase,
            rva as usize,
            length as u64,
            "RVA não possui intervalo correspondente no arquivo",
        ))
    }

    fn cstring_at(
        &self,
        offset: usize,
        phase: u32,
        code: u32,
        message: impl Into<String>,
    ) -> Result<Vec<u8>, PeError> {
        self.reader
            .cstring(offset)
            .ok_or_else(|| malformed(code, phase, offset, MAX_CSTRING as u64, message))
    }

    fn parse(mut self) -> Result<Model, PeError> {
        if self.reader.len() < DOS_HEADER_SIZE {
            return Err(truncated(
                PHASE_DOS_HEADER,
                0,
                DOS_HEADER_SIZE as u64,
                "arquivo menor que o cabeçalho DOS (64 bytes)",
            ));
        }
        let dos_magic = self.get_u16(0, PHASE_DOS_HEADER, ERROR_DOS_HEADER)?;
        let e_lfanew = self.get_u32(60, PHASE_DOS_HEADER, ERROR_DOS_HEADER)?;
        if dos_magic != 0x5a4d {
            return Err(malformed(
                ERROR_DOS_HEADER,
                PHASE_DOS_HEADER,
                0,
                dos_magic as u64,
                "assinatura DOS ausente (esperado MZ)",
            ));
        }
        let nt = usize::try_from(e_lfanew).map_err(|_| {
            error(
                STATUS_INPUT_TOO_LARGE,
                ERROR_INPUT_TOO_LARGE,
                PHASE_INPUT,
                60,
                u64::from(e_lfanew),
                "e_lfanew não cabe em usize",
            )
        })?;
        if !self.reader.has_range(nt, 4 + COFF_HEADER_SIZE) {
            return Err(truncated(
                PHASE_COFF_HEADER,
                nt,
                (4 + COFF_HEADER_SIZE) as u64,
                "cabeçalho NT/COFF truncado",
            ));
        }
        if self.get_u32(nt, PHASE_COFF_HEADER, ERROR_PE_HEADER)? != 0x4550 {
            return Err(malformed(
                ERROR_PE_HEADER,
                PHASE_COFF_HEADER,
                nt,
                0x4550,
                "assinatura PE ausente (esperado PE\\0\\0)",
            ));
        }
        let coff = nt + 4;
        let machine = self.get_u16(coff, PHASE_COFF_HEADER, ERROR_COFF_HEADER)?;
        let section_count = self.get_u16(coff + 2, PHASE_COFF_HEADER, ERROR_COFF_HEADER)?;
        let optional_size = self.get_u16(coff + 16, PHASE_COFF_HEADER, ERROR_COFF_HEADER)?;
        let characteristics = self.get_u16(coff + 18, PHASE_COFF_HEADER, ERROR_COFF_HEADER)?;
        if machine != MACHINE_AMD64 {
            return Err(error(
                STATUS_UNSUPPORTED_ARCHITECTURE,
                ERROR_COFF_HEADER,
                PHASE_COFF_HEADER,
                coff as u64,
                machine as u64,
                format!("arquitetura de máquina 0x{machine:x} não suportada (esperado AMD64)"),
            ));
        }
        if (optional_size as usize) < OPTIONAL_HEADER_BASE_SIZE {
            return Err(malformed(
                ERROR_OPTIONAL_HEADER,
                PHASE_OPTIONAL_HEADER,
                coff + COFF_HEADER_SIZE,
                optional_size as u64,
                "SizeOfOptionalHeader menor que o mínimo PE32+ (112 bytes)",
            ));
        }
        let opt = coff + COFF_HEADER_SIZE;
        let section_table = opt.checked_add(optional_size as usize).ok_or_else(|| {
            error(
                STATUS_INPUT_TOO_LARGE,
                ERROR_INPUT_TOO_LARGE,
                PHASE_INPUT,
                opt as u64,
                optional_size as u64,
                "tabela de seções excede usize",
            )
        })?;
        if section_table > self.reader.len()
            || u64::from(section_count)
                > ((self.reader.len() - section_table) / SECTION_HEADER_SIZE) as u64
        {
            return Err(truncated(
                PHASE_SECTIONS,
                section_table,
                u64::from(section_count) * SECTION_HEADER_SIZE as u64,
                "tabela de seções não cabe no arquivo",
            ));
        }
        let optional_magic = self.get_u16(opt, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?;
        if optional_magic == OPTIONAL_MAGIC_32 {
            return Err(error(
                STATUS_UNSUPPORTED_FORMAT,
                ERROR_OPTIONAL_HEADER,
                PHASE_OPTIONAL_HEADER,
                opt as u64,
                u64::from(optional_magic),
                "PE32 (x86) não é suportado; apenas PE32+ x86-64",
            ));
        }
        if optional_magic != OPTIONAL_MAGIC_64 {
            return Err(malformed(
                ERROR_OPTIONAL_HEADER,
                PHASE_OPTIONAL_HEADER,
                opt,
                u64::from(optional_magic),
                format!("magic do optional header 0x{optional_magic:x} não reconhecido (esperado 0x20b)"),
            ));
        }

        let mut info = Info {
            is_pe32_plus: true,
            is_dll: characteristics & IMAGE_FILE_DLL != 0,
            machine,
            number_of_sections: section_count,
            entry_point: self.get_u32(opt + 16, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
            image_base: self.get_u64(opt + 24, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
            section_alignment: self.get_u32(
                opt + 32,
                PHASE_OPTIONAL_HEADER,
                ERROR_OPTIONAL_HEADER,
            )?,
            size_of_image: self.get_u32(opt + 56, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
            size_of_headers: self.get_u32(
                opt + 60,
                PHASE_OPTIONAL_HEADER,
                ERROR_OPTIONAL_HEADER,
            )?,
            subsystem: self.get_u16(opt + 68, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
            ..Info::default()
        };
        if info.size_of_image == 0
            || info.size_of_headers == 0
            || info.size_of_headers > info.size_of_image
            || info.section_alignment == 0
        {
            return Err(malformed(
                ERROR_OPTIONAL_HEADER,
                PHASE_OPTIONAL_HEADER,
                opt,
                u64::from(info.size_of_image),
                "optional header com SizeOfImage, SizeOfHeaders ou SectionAlignment inválido",
            ));
        }
        let directory_count = self
            .get_u32(opt + 108, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?
            .min(16) as usize;
        let directory_bytes = directory_count * 8;
        if directory_bytes > optional_size as usize - OPTIONAL_HEADER_BASE_SIZE {
            return Err(truncated(
                PHASE_OPTIONAL_HEADER,
                opt + OPTIONAL_HEADER_BASE_SIZE,
                directory_bytes as u64,
                "diretórios de dados excedem o optional header",
            ));
        }
        let directory_offset = opt + OPTIONAL_HEADER_BASE_SIZE;
        let directory_slots = [
            (DIR_IMPORT, 0),
            (DIR_EXPORT, 1),
            (DIR_RESOURCE, 2),
            (DIR_EXCEPTION, 3),
            (DIR_RELOC, 4),
            (DIR_DELAY_IMPORT, 5),
            (DIR_TLS, 6),
        ];
        for (index, model_index) in directory_slots {
            if directory_count > index {
                let offset = directory_offset + index * 8;
                info.directories[model_index] = [
                    self.get_u32(offset, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
                    self.get_u32(offset + 4, PHASE_OPTIONAL_HEADER, ERROR_OPTIONAL_HEADER)?,
                ];
            }
        }

        let mut sections = Vec::with_capacity(section_count as usize);
        for index in 0..section_count as usize {
            if index >= MAX_SECTIONS {
                return Err(error(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_SECTION_TABLE,
                    PHASE_SECTIONS,
                    section_table as u64,
                    index as u64,
                    "número de seções excede o limite",
                ));
            }
            let offset = section_table + index * SECTION_HEADER_SIZE;
            let name_raw = self
                .reader
                .bytes(offset, 8)
                .ok_or_else(|| truncated(PHASE_SECTIONS, offset, 8, "nome de seção truncado"))?;
            let mut name_len = 8;
            while name_len > 0 && name_raw[name_len - 1] == 0 {
                name_len -= 1;
            }
            let section = Section {
                name: name_raw[..name_len].to_vec(),
                virtual_size: self.get_u32(offset + 8, PHASE_SECTIONS, ERROR_SECTION_TABLE)?,
                virtual_address: self.get_u32(offset + 12, PHASE_SECTIONS, ERROR_SECTION_TABLE)?,
                raw_data_size: self.get_u32(offset + 16, PHASE_SECTIONS, ERROR_SECTION_TABLE)?,
                raw_data_pointer: self.get_u32(offset + 20, PHASE_SECTIONS, ERROR_SECTION_TABLE)?,
                characteristics: self.get_u32(offset + 36, PHASE_SECTIONS, ERROR_SECTION_TABLE)?,
            };
            let span = u64::from(section.virtual_size.max(section.raw_data_size));
            let section_end = u64::from(section.virtual_address) + span;
            if span != 0
                && (section.virtual_address < info.size_of_headers
                    || section_end > u64::from(info.size_of_image))
            {
                return Err(malformed(
                    ERROR_SECTION_TABLE,
                    PHASE_SECTIONS,
                    offset,
                    section_end,
                    "seção excede os limites da imagem",
                ));
            }
            if section.raw_data_size > 0
                && u64::from(section.raw_data_pointer) + u64::from(section.raw_data_size)
                    > self.reader.len() as u64
            {
                return Err(malformed(
                    ERROR_SECTION_TABLE,
                    PHASE_SECTIONS,
                    offset + 20,
                    u64::from(section.raw_data_size),
                    "dados crus de seção excedem o arquivo",
                ));
            }
            sections.push(section);
        }
        info.number_of_sections = section_count;
        self.model.info = info;
        self.model.sections = sections;

        self.parse_exports()?;
        self.parse_imports(false)?;
        self.parse_imports(true)?;
        self.parse_runtime_functions()?;
        self.parse_tls()?;
        self.parse_relocations()?;
        Ok(self.model)
    }

    fn directory(&self, index: usize) -> (u32, u32) {
        let directory = self.model.info.directories[index];
        (directory[0], directory[1])
    }

    fn parse_exports(&mut self) -> Result<(), PeError> {
        let (rva, size) = self.directory(1);
        if rva == 0 && size == 0 {
            return Ok(());
        }
        if size < EXPORT_DIRECTORY_SIZE as u32 {
            return Err(malformed(
                ERROR_EXPORT_TABLE,
                PHASE_EXPORTS,
                rva as usize,
                size as u64,
                "diretório de exports menor que IMAGE_EXPORT_DIRECTORY",
            ));
        }
        let directory =
            self.rva_to_file_offset(rva, size as usize, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        let name_rva = self.get_u32(directory + 12, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        let ordinal_base = self.get_u32(directory + 16, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        let function_count =
            self.get_u32(directory + 20, PHASE_EXPORTS, ERROR_EXPORT_TABLE)? as usize;
        let name_count = self.get_u32(directory + 24, PHASE_EXPORTS, ERROR_EXPORT_TABLE)? as usize;
        let functions_rva = self.get_u32(directory + 28, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        let names_rva = self.get_u32(directory + 32, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        let name_ordinals_rva = self.get_u32(directory + 36, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        if name_rva != 0 {
            self.rva_to_file_offset(name_rva, 1, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
        }
        if function_count > MAX_EXPORT_FUNCTIONS
            || name_count > MAX_EXPORT_NAMES
            || name_count > function_count
        {
            return Err(malformed(
                ERROR_EXPORT_TABLE,
                PHASE_EXPORTS,
                directory,
                function_count as u64,
                "tabelas de exports excedem os limites suportados",
            ));
        }
        if function_count > 0 && functions_rva == 0 {
            return Err(malformed(
                ERROR_EXPORT_TABLE,
                PHASE_EXPORTS,
                directory,
                0,
                "tabela de endereços de exports ausente",
            ));
        }
        if name_count > 0 && (names_rva == 0 || name_ordinals_rva == 0) {
            return Err(malformed(
                ERROR_EXPORT_TABLE,
                PHASE_EXPORTS,
                directory,
                name_count as u64,
                "tabelas de nomes de exports ausentes",
            ));
        }
        if ordinal_base > u16::MAX as u32
            || function_count > (u16::MAX as usize + 1).saturating_sub(ordinal_base as usize)
        {
            return Err(malformed(
                ERROR_EXPORT_TABLE,
                PHASE_EXPORTS,
                directory,
                ordinal_base as u64,
                "ordinais de exports excedem 16 bits",
            ));
        }
        let mut function_rvas = vec![0u32; function_count];
        if function_count > 0 {
            let bytes = function_count.checked_mul(4).ok_or_else(|| {
                error(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_EXPORT_TABLE,
                    PHASE_EXPORTS,
                    directory as u64,
                    function_count as u64,
                    "tabela de exports excede usize",
                )
            })?;
            let table =
                self.rva_to_file_offset(functions_rva, bytes, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
            for (index, value) in function_rvas.iter_mut().enumerate() {
                *value = self.get_u32(table + index * 4, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
            }
        }
        let mut names = vec![Vec::new(); function_count];
        let mut has_name = vec![false; function_count];
        if name_count > 0 {
            let name_bytes = name_count.checked_mul(4).ok_or_else(|| {
                error(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_EXPORT_TABLE,
                    PHASE_EXPORTS,
                    directory as u64,
                    name_count as u64,
                    "tabela de nomes excede usize",
                )
            })?;
            let ordinal_bytes = name_count.checked_mul(2).ok_or_else(|| {
                error(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_EXPORT_TABLE,
                    PHASE_EXPORTS,
                    directory as u64,
                    name_count as u64,
                    "tabela ordinal de exports excede usize",
                )
            })?;
            let names_table =
                self.rva_to_file_offset(names_rva, name_bytes, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
            let ordinals_table = self.rva_to_file_offset(
                name_ordinals_rva,
                ordinal_bytes,
                PHASE_EXPORTS,
                ERROR_EXPORT_TABLE,
            )?;
            for index in 0..name_count {
                let export_name_rva =
                    self.get_u32(names_table + index * 4, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
                let function_index = self.get_u16(
                    ordinals_table + index * 2,
                    PHASE_EXPORTS,
                    ERROR_EXPORT_TABLE,
                )? as usize;
                if function_index >= function_count {
                    return Err(malformed(
                        ERROR_EXPORT_TABLE,
                        PHASE_EXPORTS,
                        names_table + index * 4,
                        function_index as u64,
                        "índice de nome de export inválido",
                    ));
                }
                let name_offset =
                    self.rva_to_file_offset(export_name_rva, 1, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
                let name = self.cstring_at(
                    name_offset,
                    PHASE_EXPORTS,
                    ERROR_EXPORT_TABLE,
                    "nome de export inválido",
                )?;
                if name.is_empty() || has_name[function_index] {
                    return Err(malformed(
                        ERROR_EXPORT_TABLE,
                        PHASE_EXPORTS,
                        name_offset,
                        function_index as u64,
                        "nome de export inválido ou repetido",
                    ));
                }
                names[function_index] = name;
                has_name[function_index] = true;
            }
        }
        let export_end = u64::from(rva) + u64::from(size);
        self.model.info.export_ordinal_base = ordinal_base;
        let mut exports = Vec::with_capacity(function_count);
        for (index, function_rva) in function_rvas.into_iter().enumerate() {
            if function_rva == 0 {
                continue;
            }
            if function_rva >= self.model.info.size_of_image {
                return Err(malformed(
                    ERROR_EXPORT_TABLE,
                    PHASE_EXPORTS,
                    directory,
                    function_rva as u64,
                    "RVA de export fora da imagem",
                ));
            }
            let mut export = Export {
                by_name: has_name[index],
                name: names[index].clone(),
                ordinal: (ordinal_base as usize + index) as u16,
                rva: function_rva,
                forwarder: Vec::new(),
            };
            if u64::from(function_rva) >= u64::from(rva) && u64::from(function_rva) < export_end {
                let offset =
                    self.rva_to_file_offset(function_rva, 1, PHASE_EXPORTS, ERROR_EXPORT_TABLE)?;
                export.forwarder = self.cstring_at(
                    offset,
                    PHASE_EXPORTS,
                    ERROR_EXPORT_TABLE,
                    "forwarder de export inválido",
                )?;
                if export.forwarder.is_empty() {
                    return Err(malformed(
                        ERROR_EXPORT_TABLE,
                        PHASE_EXPORTS,
                        offset,
                        function_rva as u64,
                        "forwarder de export inválido",
                    ));
                }
            }
            exports.push(export);
        }
        self.model.exports = exports;
        Ok(())
    }

    fn parse_imports(&mut self, delay: bool) -> Result<(), PeError> {
        let (rva, size) = self.directory(if delay { 5 } else { 0 });
        if rva == 0 && size == 0 {
            return Ok(());
        }
        let descriptor_size = if delay {
            DELAY_IMPORT_DESCRIPTOR_SIZE
        } else {
            IMPORT_DESCRIPTOR_SIZE
        };
        let code = if delay {
            ERROR_DELAY_IMPORT_TABLE
        } else {
            ERROR_IMPORT_TABLE
        };
        let phase = if delay {
            PHASE_DELAY_IMPORTS
        } else {
            PHASE_IMPORTS
        };
        if size < descriptor_size as u32 {
            return Err(malformed(
                code,
                phase,
                rva as usize,
                size as u64,
                "diretório de imports menor que um descritor",
            ));
        }
        let directory = self.rva_to_file_offset(rva, size as usize, phase, code)?;
        let descriptor_limit = (size as usize / descriptor_size).min(MAX_IMPORT_DLLS);
        let mut terminated = false;
        let mut result = Vec::new();
        for descriptor_index in 0..descriptor_limit {
            let offset = directory + descriptor_index * descriptor_size;
            let (attributes, original, name_rva, first_thunk, int_rva) = if delay {
                (
                    self.get_u32(offset, phase, code)?,
                    0,
                    self.get_u32(offset + 4, phase, code)?,
                    self.get_u32(offset + 12, phase, code)?,
                    self.get_u32(offset + 16, phase, code)?,
                )
            } else {
                (
                    0,
                    self.get_u32(offset, phase, code)?,
                    self.get_u32(offset + 12, phase, code)?,
                    self.get_u32(offset + 16, phase, code)?,
                    0,
                )
            };
            let all_zero = if delay {
                attributes == 0
                    && name_rva == 0
                    && self.get_u32(offset + 8, phase, code)? == 0
                    && first_thunk == 0
                    && int_rva == 0
                    && self.get_u32(offset + 20, phase, code)? == 0
                    && self.get_u32(offset + 24, phase, code)? == 0
                    && self.get_u32(offset + 28, phase, code)? == 0
            } else {
                original == 0 && name_rva == 0 && first_thunk == 0
            };
            if all_zero {
                terminated = true;
                break;
            }
            if delay && attributes != 1 {
                return Err(error(
                    STATUS_UNSUPPORTED_MECHANISM,
                    code,
                    phase,
                    offset as u64,
                    attributes as u64,
                    "atributos de delay import não suportados (esperado 0x1)",
                ));
            }
            if name_rva == 0 || (delay && (first_thunk == 0 || int_rva == 0)) {
                return Err(malformed(
                    code,
                    phase,
                    offset,
                    descriptor_index as u64,
                    "descritor de import sem nome, INT ou IAT",
                ));
            }
            let name_offset = self.rva_to_file_offset(name_rva, 1, phase, code)?;
            let dll_name =
                self.cstring_at(name_offset, phase, code, "nome de DLL sem terminação nula")?;
            let thunk_rva = if delay {
                int_rva
            } else if original != 0 {
                original
            } else {
                first_thunk
            };
            if thunk_rva == 0 || first_thunk == 0 {
                return Err(malformed(
                    code,
                    phase,
                    offset,
                    descriptor_index as u64,
                    "descritor de import sem tabela de thunks",
                ));
            }
            let thunk_table = self.rva_to_file_offset(thunk_rva, THUNK_SIZE, phase, code)?;
            let mut symbols = Vec::new();
            let mut thunk_terminated = false;
            for symbol_index in 0..MAX_SYMBOLS_PER_DLL {
                let thunk_offset = match thunk_table.checked_add(symbol_index * THUNK_SIZE) {
                    Some(value) => value,
                    None => break,
                };
                if !self.reader.has_range(thunk_offset, THUNK_SIZE) {
                    break;
                }
                let thunk_value = self.get_u64(thunk_offset, phase, code)?;
                if thunk_value == 0 {
                    thunk_terminated = true;
                    break;
                }
                let iat_slot = u64::from(first_thunk) + symbol_index as u64 * THUNK_SIZE as u64;
                if iat_slot > u64::from(self.model.info.size_of_image)
                    || iat_slot + THUNK_SIZE as u64 > u64::from(self.model.info.size_of_image)
                {
                    return Err(malformed(
                        code,
                        phase,
                        thunk_offset,
                        iat_slot,
                        "IAT fora dos limites da imagem",
                    ));
                }
                if delay {
                    self.rva_to_file_offset(iat_slot as u32, THUNK_SIZE, phase, code)?;
                }
                let mut symbol = Symbol {
                    iat_rva: iat_slot as u32,
                    ..Symbol::default()
                };
                if thunk_value & ORDINAL_FLAG_64 != 0 {
                    symbol.by_ordinal = true;
                    symbol.ordinal = (thunk_value & 0xffff) as u16;
                } else {
                    if thunk_value > u32::MAX as u64 {
                        return Err(malformed(
                            code,
                            phase,
                            thunk_offset,
                            thunk_value,
                            "nome de símbolo de import fora da imagem",
                        ));
                    }
                    let by_name = self.rva_to_file_offset(thunk_value as u32, 2, phase, code)?;
                    symbol.name = self.cstring_at(
                        by_name + 2,
                        phase,
                        code,
                        "nome de símbolo sem terminação nula",
                    )?;
                }
                symbols.push(symbol);
            }
            if !thunk_terminated {
                return Err(malformed(
                    code,
                    phase,
                    thunk_table,
                    symbols.len() as u64,
                    "tabela de thunks sem terminador nulo",
                ));
            }
            result.push(Dll {
                name: dll_name,
                symbols,
            });
        }
        if !terminated {
            return Err(malformed(
                code,
                phase,
                directory,
                descriptor_limit as u64,
                "diretório de imports sem descritor terminador",
            ));
        }
        if delay {
            self.model.delay_imports = result;
        } else {
            self.model.imports = result;
        }
        Ok(())
    }

    fn parse_tls(&mut self) -> Result<(), PeError> {
        let (rva, size) = self.directory(6);
        if rva == 0 || size == 0 || size < 40 {
            return Ok(());
        }
        let directory =
            match self.rva_to_file_offset(rva, size as usize, PHASE_TLS, ERROR_TLS_DIRECTORY) {
                Ok(value) => value,
                Err(_) => return Ok(()),
            };
        let mut tls = Tls {
            start_address_of_raw_data: self.get_u64(directory, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            end_address_of_raw_data: self.get_u64(directory + 8, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            address_of_index: self.get_u64(directory + 16, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            address_of_callbacks: self.get_u64(directory + 24, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            size_of_zero_fill: self.get_u32(directory + 32, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            characteristics: self.get_u32(directory + 36, PHASE_TLS, ERROR_TLS_DIRECTORY)?,
            ..Tls::default()
        };
        if tls.address_of_callbacks >= self.model.info.image_base {
            let callbacks_rva = tls.address_of_callbacks - self.model.info.image_base;
            if callbacks_rva <= u32::MAX as u64 {
                if let Ok(mut offset) =
                    self.rva_to_file_offset(callbacks_rva as u32, 8, PHASE_TLS, ERROR_TLS_DIRECTORY)
                {
                    while self.reader.has_range(offset, 8) {
                        let callback = self.get_u64(offset, PHASE_TLS, ERROR_TLS_DIRECTORY)?;
                        if callback == 0 {
                            break;
                        }
                        if tls.callback_vas.len() >= MAX_TLS_CALLBACKS {
                            return Err(error(
                                STATUS_INPUT_TOO_LARGE,
                                ERROR_TLS_DIRECTORY,
                                PHASE_TLS,
                                offset as u64,
                                tls.callback_vas.len() as u64,
                                "número de callbacks TLS excede o limite",
                            ));
                        }
                        tls.callback_vas.push(callback);
                        offset += 8;
                    }
                }
            }
        }
        self.model.info.tls = tls;
        Ok(())
    }

    fn parse_unwind(
        &self,
        unwind_rva: u32,
        function_begin: u32,
        function_end: u32,
    ) -> Result<Unwind, PeError> {
        if unwind_rva & 3 != 0 {
            return Err(malformed(
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                unwind_rva as usize,
                unwind_rva as u64,
                "UNWIND_INFO sem alinhamento de 4 bytes",
            ));
        }
        let header = self.rva_to_file_offset(
            unwind_rva,
            UNWIND_HEADER_SIZE,
            PHASE_UNWIND,
            ERROR_UNWIND_DIRECTORY,
        )?;
        let word = self.get_u32(header, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?;
        let version = (word & 7) as u8;
        let flags = ((word >> 3) & 0x1f) as u8;
        let prolog_size = ((word >> 8) & 0xff) as u8;
        let code_count = ((word >> 16) & 0xff) as usize;
        let frame_register = ((word >> 24) & 0xf) as u8;
        let frame_offset = ((word >> 28) & 0xf) as u8;
        if version != 1 && version != 2 {
            return Err(error(
                STATUS_UNSUPPORTED_MECHANISM,
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                unwind_rva as u64,
                version as u64,
                "versão de UNWIND_INFO não suportada",
            ));
        }
        if flags & !7 != 0 {
            return Err(error(
                STATUS_UNSUPPORTED_MECHANISM,
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                unwind_rva as u64,
                flags as u64,
                "flags de UNWIND_INFO não suportadas",
            ));
        }
        let code_bytes = code_count * UNWIND_CODE_SIZE;
        let code_end = u64::from(unwind_rva)
            .checked_add(UNWIND_HEADER_SIZE as u64 + code_bytes as u64)
            .ok_or_else(|| {
                malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header,
                    code_count as u64,
                    "códigos de UNWIND_INFO excedem uint32",
                )
            })?;
        if code_end > u64::from(self.model.info.size_of_image) || code_end > u32::MAX as u64 {
            return Err(malformed(
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                header,
                code_end,
                "códigos de UNWIND_INFO excedem a imagem",
            ));
        }
        let codes_offset = if code_count == 0 {
            0
        } else {
            let codes_rva = unwind_rva
                .checked_add(UNWIND_HEADER_SIZE as u32)
                .ok_or_else(|| {
                    malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        unwind_rva as u64,
                        "códigos de UNWIND_INFO excedem uint32",
                    )
                })?;
            self.rva_to_file_offset(codes_rva, code_bytes, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?
        };
        let raw_code = |slot: usize| -> Result<u16, PeError> {
            self.get_u16(
                codes_offset + slot * 2,
                PHASE_UNWIND,
                ERROR_UNWIND_DIRECTORY,
            )
        };
        let operation = |raw: u16| ((raw >> 8) & 0xf) as u8;
        let code_offset = |raw: u16| (raw & 0xff) as u8;
        let operation_info = |raw: u16| ((raw >> 12) & 0xf) as u8;
        let mut unwind = Unwind {
            version,
            flags,
            prolog_size,
            frame_register,
            frame_offset,
            ..Unwind::default()
        };
        let mut slot = 0usize;
        if version == 2 && code_count > 0 && operation(raw_code(0)?) == 6 {
            let first = raw_code(0)?;
            let epilog_size = code_offset(first) as u32;
            let epilog_flags = operation_info(first);
            if epilog_size == 0 {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header,
                    0,
                    "descritor de epílogo V2 com tamanho zero",
                ));
            }
            if epilog_flags & !1 != 0 {
                return Err(error(
                    STATUS_UNSUPPORTED_MECHANISM,
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header as u64,
                    epilog_flags as u64,
                    "flags de epílogo V2 não suportadas",
                ));
            }
            let append_epilog = |distance: u32, unwind: &mut Unwind| -> Result<(), PeError> {
                let function_size = function_end - function_begin;
                if distance < epilog_size || distance > function_size {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        distance as u64,
                        "epílogo V2 fora do intervalo da RUNTIME_FUNCTION",
                    ));
                }
                let begin = function_end - distance;
                let end = u64::from(begin) + u64::from(epilog_size);
                if end > u64::from(function_end) {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        end,
                        "epílogo V2 excede a RUNTIME_FUNCTION",
                    ));
                }
                unwind.epilogs.push(Epilog {
                    begin_rva: begin,
                    end_rva: end as u32,
                });
                Ok(())
            };
            slot = 1;
            if epilog_flags & 1 != 0 {
                append_epilog(epilog_size, &mut unwind)?;
            } else {
                if slot >= code_count || operation(raw_code(slot)?) != 6 {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        slot as u64,
                        "offset de epílogo V2 sem UOP_Epilog",
                    ));
                }
                let distance = (u32::from(operation_info(raw_code(slot)?)) << 8)
                    | u32::from(code_offset(raw_code(slot)?));
                if distance == 0 {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        0,
                        "offset de epílogo V2 nulo",
                    ));
                }
                append_epilog(distance, &mut unwind)?;
                slot += 1;
            }
            while slot < code_count {
                let raw = raw_code(slot)?;
                if operation(raw) != 6 {
                    break;
                }
                let distance = (u32::from(operation_info(raw)) << 8) | u32::from(code_offset(raw));
                if distance == 0 {
                    slot += 1;
                    break;
                }
                append_epilog(distance, &mut unwind)?;
                slot += 1;
            }
            unwind.epilogs.sort_by_key(|epilog| epilog.begin_rva);
            for pair in unwind.epilogs.windows(2) {
                if pair[1].begin_rva < pair[0].end_rva {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        pair[1].begin_rva as u64,
                        "epílogos V2 sobrepostos",
                    ));
                }
            }
        }
        let mut previous = 0u8;
        let mut has_previous = false;
        while slot < code_count {
            let raw = raw_code(slot)?;
            let offset = code_offset(raw);
            let op = operation(raw);
            let info = operation_info(raw);
            if has_previous && offset > previous {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header,
                    offset as u64,
                    "códigos de UNWIND_INFO fora de ordem decrescente",
                ));
            }
            previous = offset;
            has_previous = true;
            let mut extra = 0usize;
            match op {
                0 => {
                    if !valid_gpr(info) {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            info as u64,
                            "UWOP_PUSH_NONVOL usa registrador inválido",
                        ));
                    }
                }
                1 => {
                    if info == 0 {
                        extra = 1;
                    } else if info == 1 {
                        extra = 2;
                    } else {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            info as u64,
                            "UWOP_ALLOC_LARGE com OpInfo inválido",
                        ));
                    }
                }
                2 => {}
                3 => {
                    if info != 0 {
                        if !valid_gpr(info) {
                            return Err(error(
                                STATUS_UNSUPPORTED_MECHANISM,
                                ERROR_UNWIND_DIRECTORY,
                                PHASE_UNWIND,
                                header as u64,
                                info as u64,
                                "UWOP_SET_FPREG estendido não suportado",
                            ));
                        }
                        unwind.extended_set_fpreg = true;
                    }
                    if !valid_gpr(frame_register) {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            frame_register as u64,
                            "UWOP_SET_FPREG com FrameRegister inválido",
                        ));
                    }
                }
                4 => {
                    if !valid_gpr(info) {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            info as u64,
                            "UWOP_SAVE_NONVOL usa registrador inválido",
                        ));
                    }
                    extra = 1;
                }
                5 => {
                    if !valid_gpr(info) {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            info as u64,
                            "UWOP_SAVE_NONVOL_FAR usa registrador inválido",
                        ));
                    }
                    extra = 2;
                }
                8 => extra = 1,
                9 => extra = 2,
                10 => {
                    if info > 1 {
                        return Err(malformed(
                            ERROR_UNWIND_DIRECTORY,
                            PHASE_UNWIND,
                            header,
                            info as u64,
                            "UWOP_PUSH_MACHFRAME com OpInfo inválido",
                        ));
                    }
                }
                _ => {
                    return Err(error(
                        STATUS_UNSUPPORTED_MECHANISM,
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header as u64,
                        op as u64,
                        "operação de UNWIND_INFO não suportada",
                    ))
                }
            }
            if extra > code_count.saturating_sub(slot + 1) {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header,
                    slot as u64,
                    "operando de UNWIND_INFO truncado",
                ));
            }
            let mut operand = if op == 2 { u32::from(info) * 8 + 8 } else { 0 };
            if extra == 1 {
                let value = raw_code(slot + 1)?;
                operand = match op {
                    1 | 4 => u32::from(value) * 8,
                    8 => u32::from(value) * 16,
                    _ => 0,
                };
            } else if extra == 2 {
                let low = raw_code(slot + 1)? as u32;
                let high = raw_code(slot + 2)? as u32;
                operand = low | (high << 16);
                if op == 9 && operand & 0xf != 0 {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        header,
                        operand as u64,
                        "UWOP_SAVE_XMM128_FAR com offset não alinhado",
                    ));
                }
            }
            unwind.codes.push(UnwindCode {
                code_offset: offset,
                operation: op,
                operation_info: info,
                operand,
            });
            slot += extra + 1;
        }
        let aligned_tail = (code_end + 3) & !3;
        if aligned_tail > u64::from(self.model.info.size_of_image) || aligned_tail > u32::MAX as u64
        {
            return Err(malformed(
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                header,
                aligned_tail,
                "cauda de UNWIND_INFO excede a imagem",
            ));
        }
        let tail_rva = aligned_tail as u32;
        if flags & 4 != 0 {
            if flags & 3 != 0 {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    header,
                    flags as u64,
                    "UNWIND_INFO combina handler e CHAININFO",
                ));
            }
            let chain =
                self.rva_to_file_offset(tail_rva, 12, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?;
            unwind.chained = Some((
                self.get_u32(chain, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
                self.get_u32(chain + 4, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
                self.get_u32(chain + 8, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
            ));
        } else if flags & 3 != 0 {
            let handler =
                self.rva_to_file_offset(tail_rva, 4, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?;
            unwind.handler_rva = self.get_u32(handler, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?;
            if unwind.handler_rva == 0 {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    handler,
                    0,
                    "handler de UNWIND_INFO fora da imagem",
                ));
            }
            self.rva_to_file_offset(unwind.handler_rva, 1, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?;
            unwind.handler_data_rva = tail_rva.checked_add(4).ok_or_else(|| {
                malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    handler,
                    tail_rva as u64,
                    "dados de handler excedem uint32",
                )
            })?;
            if unwind.handler_data_rva > self.model.info.size_of_image {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    handler,
                    unwind.handler_data_rva as u64,
                    "dados de handler fora da imagem",
                ));
            }
        }
        Ok(unwind)
    }

    fn parse_runtime_functions(&mut self) -> Result<(), PeError> {
        let (rva, size) = self.directory(3);
        if rva == 0 && size == 0 {
            return Ok(());
        }
        if rva == 0 || size == 0 || size % RUNTIME_FUNCTION_SIZE as u32 != 0 {
            return Err(malformed(
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                rva as usize,
                size as u64,
                "diretório de exceções deve conter RUNTIME_FUNCTIONs completos",
            ));
        }
        let directory =
            match self.rva_to_file_offset(rva, size as usize, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)
            {
                Ok(value) => value,
                Err(error) => {
                    let virtual_only = self.model.sections.iter().any(|section| {
                        u64::from(rva) >= u64::from(section.virtual_address)
                            && u64::from(rva) + u64::from(size)
                                <= u64::from(section.virtual_address)
                                    + u64::from(section.virtual_size)
                    });
                    if virtual_only {
                        return Ok(());
                    }
                    return Err(error);
                }
            };
        let count = size as usize / RUNTIME_FUNCTION_SIZE;
        if count > MAX_RUNTIME_FUNCTIONS {
            return Err(error(
                STATUS_INPUT_TOO_LARGE,
                ERROR_UNWIND_DIRECTORY,
                PHASE_UNWIND,
                directory as u64,
                count as u64,
                "número excessivo de RUNTIME_FUNCTIONs",
            ));
        }
        let mut runtime = Vec::with_capacity(count);
        let mut previous_end = 0u32;
        for index in 0..count {
            let offset = directory + index * RUNTIME_FUNCTION_SIZE;
            let function = RuntimeFunction {
                begin_rva: self.get_u32(offset, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
                end_rva: self.get_u32(offset + 4, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
                unwind_info_rva: self.get_u32(offset + 8, PHASE_UNWIND, ERROR_UNWIND_DIRECTORY)?,
                ..RuntimeFunction::default()
            };
            if function.begin_rva >= function.end_rva
                || function.end_rva > self.model.info.size_of_image
            {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    offset,
                    function.end_rva as u64,
                    "RUNTIME_FUNCTION com intervalo inválido",
                ));
            }
            if index > 0 && function.begin_rva < previous_end {
                return Err(malformed(
                    ERROR_UNWIND_DIRECTORY,
                    PHASE_UNWIND,
                    offset,
                    function.begin_rva as u64,
                    "RUNTIME_FUNCTIONs fora de ordem ou sobrepostos",
                ));
            }
            let mut function = function;
            function.unwind = self.parse_unwind(
                function.unwind_info_rva,
                function.begin_rva,
                function.end_rva,
            )?;
            previous_end = function.end_rva;
            runtime.push(function);
        }
        for function in &runtime {
            if let Some(chain) = function.unwind.chained {
                if !runtime.iter().any(|candidate| {
                    candidate.begin_rva == chain.0
                        && candidate.end_rva == chain.1
                        && candidate.unwind_info_rva == chain.2
                }) {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        directory,
                        chain.0 as u64,
                        "CHAININFO não referencia uma RUNTIME_FUNCTION da tabela",
                    ));
                }
            }
        }
        for start in 0..runtime.len() {
            let mut current = start;
            for depth in 0..=runtime.len() {
                let Some(chain) = runtime[current].unwind.chained else {
                    break;
                };
                let Some(next) = runtime.iter().position(|candidate| {
                    candidate.begin_rva == chain.0
                        && candidate.end_rva == chain.1
                        && candidate.unwind_info_rva == chain.2
                }) else {
                    break;
                };
                current = next;
                if depth == runtime.len() {
                    return Err(malformed(
                        ERROR_UNWIND_DIRECTORY,
                        PHASE_UNWIND,
                        directory,
                        start as u64,
                        "ciclo em CHAININFO de UNWIND_INFO",
                    ));
                }
            }
        }
        self.model.runtime_functions = runtime;
        Ok(())
    }

    fn parse_relocations(&mut self) -> Result<(), PeError> {
        let (rva, size) = self.directory(4);
        if rva == 0 && size == 0 {
            return Ok(());
        }
        if size < RELOC_BLOCK_HEADER_SIZE as u32 {
            return Err(malformed(
                ERROR_RELOCATION_DIRECTORY,
                PHASE_RELOCATIONS,
                rva as usize,
                size as u64,
                "diretório de relocations menor que o cabeçalho de bloco",
            ));
        }
        let directory = self.rva_to_file_offset(
            rva,
            size as usize,
            PHASE_RELOCATIONS,
            ERROR_RELOCATION_DIRECTORY,
        )?;
        let mut consumed = 0usize;
        let mut blocks = Vec::new();
        while consumed + RELOC_BLOCK_HEADER_SIZE <= size as usize {
            if blocks.len() >= MAX_RELOC_BLOCKS {
                return Err(error(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_RELOCATION_DIRECTORY,
                    PHASE_RELOCATIONS,
                    directory as u64 + consumed as u64,
                    blocks.len() as u64,
                    "número excessivo de blocos de relocations",
                ));
            }
            let offset = directory + consumed;
            let page_rva = self.get_u32(offset, PHASE_RELOCATIONS, ERROR_RELOCATION_DIRECTORY)?;
            let block_size =
                self.get_u32(offset + 4, PHASE_RELOCATIONS, ERROR_RELOCATION_DIRECTORY)? as usize;
            if block_size < RELOC_BLOCK_HEADER_SIZE || block_size > size as usize - consumed {
                return Err(malformed(
                    ERROR_RELOCATION_DIRECTORY,
                    PHASE_RELOCATIONS,
                    offset,
                    block_size as u64,
                    "bloco de relocations com tamanho inválido",
                ));
            }
            let entry_bytes = block_size - RELOC_BLOCK_HEADER_SIZE;
            if !entry_bytes.is_multiple_of(RELOC_ENTRY_SIZE) {
                return Err(malformed(
                    ERROR_RELOCATION_DIRECTORY,
                    PHASE_RELOCATIONS,
                    offset,
                    entry_bytes as u64,
                    "bloco de relocations com entradas truncadas",
                ));
            }
            let mut entries = Vec::with_capacity(entry_bytes / RELOC_ENTRY_SIZE);
            for index in 0..entry_bytes / RELOC_ENTRY_SIZE {
                let raw = self.get_u16(
                    offset + RELOC_BLOCK_HEADER_SIZE + index * 2,
                    PHASE_RELOCATIONS,
                    ERROR_RELOCATION_DIRECTORY,
                )?;
                entries.push(RelocEntry {
                    kind: raw >> 12,
                    offset: raw & 0xfff,
                });
            }
            blocks.push(RelocBlock { page_rva, entries });
            consumed += block_size;
        }
        if consumed != size as usize {
            return Err(malformed(
                ERROR_RELOCATION_DIRECTORY,
                PHASE_RELOCATIONS,
                directory + consumed,
                (size as usize - consumed) as u64,
                "diretório de relocations com bytes residuais",
            ));
        }
        self.model.relocations = blocks;
        Ok(())
    }
}

fn valid_gpr(register: u8) -> bool {
    register <= 15 && register != 4
}

#[derive(Clone)]
struct StringEntry {
    bytes: Vec<u8>,
    record_offset: usize,
    data_offset: usize,
}

#[derive(Clone, Copy, Default)]
struct Descriptor {
    offset: usize,
    count: usize,
    stride: usize,
    flags: u32,
}

struct WirePlan {
    descriptors: [Descriptor; WIRE_TABLE_COUNT],
    strings: Vec<StringEntry>,
    total: usize,
}

struct StringPool {
    entries: Vec<Vec<u8>>,
    indices: HashMap<Vec<u8>, usize>,
}

impl StringPool {
    fn new() -> Self {
        Self {
            entries: Vec::new(),
            indices: HashMap::new(),
        }
    }

    fn intern(&mut self, bytes: &[u8]) -> usize {
        if let Some(index) = self.indices.get(bytes) {
            return *index;
        }
        let owned = bytes.to_vec();
        let index = self.entries.len();
        self.indices.insert(owned.clone(), index);
        self.entries.push(owned);
        index
    }
}

fn align8(value: usize) -> Option<usize> {
    value.checked_add(7).map(|aligned| aligned & !7)
}

fn all_strings(model: &Model) -> StringPool {
    let mut pool = StringPool::new();
    for section in &model.sections {
        pool.intern(&section.name);
    }
    for dll in &model.imports {
        pool.intern(&dll.name);
        for symbol in &dll.symbols {
            if !symbol.by_ordinal {
                pool.intern(&symbol.name);
            }
        }
    }
    for dll in &model.delay_imports {
        pool.intern(&dll.name);
        for symbol in &dll.symbols {
            if !symbol.by_ordinal {
                pool.intern(&symbol.name);
            }
        }
    }
    for export in &model.exports {
        if export.by_name {
            pool.intern(&export.name);
        }
        if !export.forwarder.is_empty() {
            pool.intern(&export.forwarder);
        }
    }
    pool
}

fn count_symbols(dlls: &[Dll]) -> usize {
    dlls.iter().map(|dll| dll.symbols.len()).sum()
}

fn checked_table_count(count: usize, stride: usize, phase: u32) -> Result<usize, PeError> {
    count.checked_mul(stride).ok_or_else(|| {
        error(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_SERIALIZATION_LIMIT,
            phase,
            UNKNOWN_OFFSET,
            count as u64,
            "tabela serializada excede usize",
        )
    })
}

fn make_plan(model: &Model) -> Result<WirePlan, PeError> {
    let pool = all_strings(model);
    let counts = [
        1,
        model.sections.len(),
        pool.entries.len(),
        model.imports.len(),
        count_symbols(&model.imports),
        model.delay_imports.len(),
        count_symbols(&model.delay_imports),
        model.exports.len(),
        model.info.tls.callback_vas.len(),
        model.runtime_functions.len(),
        model.runtime_functions.len(),
        model
            .runtime_functions
            .iter()
            .map(|function| function.unwind.codes.len())
            .sum(),
        model
            .runtime_functions
            .iter()
            .map(|function| function.unwind.epilogs.len())
            .sum(),
        model.relocations.len(),
        model
            .relocations
            .iter()
            .map(|block| block.entries.len())
            .sum(),
        0,
    ];
    let mut descriptors = [Descriptor::default(); WIRE_TABLE_COUNT];
    let mut cursor = WIRE_HEADER_SIZE;
    for table in 0..TABLE_RESERVED {
        if counts[table] == 0 {
            continue;
        }
        cursor = align8(cursor).ok_or_else(|| {
            error(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_SERIALIZATION_LIMIT,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                cursor as u64,
                "alinhamento do wire excede usize",
            )
        })?;
        let stride = STRIDES[table] as usize;
        if table == TABLE_STRINGS {
            descriptors[table] = Descriptor {
                offset: cursor,
                count: counts[table],
                stride: 0,
                flags: WIRE_FLAG_VARIABLE,
            };
            for bytes in &pool.entries {
                let record_size = WIRE_STRING_RECORD_HEADER_SIZE
                    .checked_add(bytes.len())
                    .ok_or_else(|| {
                        error(
                            STATUS_OUTPUT_TOO_LARGE,
                            ERROR_SERIALIZATION_LIMIT,
                            PHASE_SERIALIZE,
                            UNKNOWN_OFFSET,
                            bytes.len() as u64,
                            "registro de string excede usize",
                        )
                    })?;
                cursor = cursor
                    .checked_add(align8(record_size).ok_or_else(|| {
                        error(
                            STATUS_OUTPUT_TOO_LARGE,
                            ERROR_SERIALIZATION_LIMIT,
                            PHASE_SERIALIZE,
                            UNKNOWN_OFFSET,
                            record_size as u64,
                            "alinhamento de string excede usize",
                        )
                    })?)
                    .ok_or_else(|| {
                        error(
                            STATUS_OUTPUT_TOO_LARGE,
                            ERROR_SERIALIZATION_LIMIT,
                            PHASE_SERIALIZE,
                            UNKNOWN_OFFSET,
                            cursor as u64,
                            "tamanho total do wire excede usize",
                        )
                    })?;
            }
        } else {
            let bytes = checked_table_count(counts[table], stride, PHASE_SERIALIZE)?;
            descriptors[table] = Descriptor {
                offset: cursor,
                count: counts[table],
                stride,
                flags: 0,
            };
            cursor = cursor.checked_add(bytes).ok_or_else(|| {
                error(
                    STATUS_OUTPUT_TOO_LARGE,
                    ERROR_SERIALIZATION_LIMIT,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    bytes as u64,
                    "tamanho total do wire excede usize",
                )
            })?;
        }
    }
    if cursor > MAX_SERIALIZED_BYTES as usize {
        return Err(error(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_SERIALIZATION_LIMIT,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            cursor as u64,
            "resultado serializado excede 256 MiB",
        ));
    }
    let mut strings = Vec::with_capacity(pool.entries.len());
    if !pool.entries.is_empty() {
        let mut cursor = descriptors[TABLE_STRINGS].offset;
        for bytes in pool.entries {
            let data_offset = cursor
                .checked_add(WIRE_STRING_RECORD_HEADER_SIZE)
                .ok_or_else(|| {
                    error(
                        STATUS_OUTPUT_TOO_LARGE,
                        ERROR_SERIALIZATION_LIMIT,
                        PHASE_SERIALIZE,
                        UNKNOWN_OFFSET,
                        cursor as u64,
                        "offset de string excede usize",
                    )
                })?;
            strings.push(StringEntry {
                bytes: bytes.clone(),
                record_offset: cursor,
                data_offset,
            });
            let record_size = WIRE_STRING_RECORD_HEADER_SIZE
                .checked_add(bytes.len())
                .ok_or_else(|| {
                    error(
                        STATUS_OUTPUT_TOO_LARGE,
                        ERROR_SERIALIZATION_LIMIT,
                        PHASE_SERIALIZE,
                        UNKNOWN_OFFSET,
                        bytes.len() as u64,
                        "registro de string excede usize",
                    )
                })?;
            let aligned_size = align8(record_size).ok_or_else(|| {
                error(
                    STATUS_OUTPUT_TOO_LARGE,
                    ERROR_SERIALIZATION_LIMIT,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    record_size as u64,
                    "alinhamento de string excede usize",
                )
            })?;
            cursor = cursor.checked_add(aligned_size).ok_or_else(|| {
                error(
                    STATUS_OUTPUT_TOO_LARGE,
                    ERROR_SERIALIZATION_LIMIT,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    aligned_size as u64,
                    "tamanho total do wire excede usize",
                )
            })?;
        }
    }
    Ok(WirePlan {
        descriptors,
        strings,
        total: cursor.max(WIRE_HEADER_SIZE),
    })
}

fn put_u16(output: &mut [u8], offset: usize, value: u16) {
    output[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}
fn put_u32(output: &mut [u8], offset: usize, value: u32) {
    output[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}
fn put_u64(output: &mut [u8], offset: usize, value: u64) {
    output[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}
fn string_ref(output: &mut [u8], offset: usize, value: Option<usize>, plan: &WirePlan) {
    if let Some(index) = value {
        let entry = &plan.strings[index];
        put_u64(output, offset, entry.data_offset as u64);
        put_u32(output, offset + 8, entry.bytes.len() as u32);
    }
}

fn string_index(pool: &HashMap<Vec<u8>, usize>, bytes: &[u8], plan: &WirePlan) -> Option<usize> {
    pool.get(bytes)
        .copied()
        .or_else(|| plan.strings.iter().position(|entry| entry.bytes == bytes))
}

fn fill_wire(model: &Model, plan: &WirePlan, output: &mut [u8]) -> Result<(), PeError> {
    if output.len() < plan.total {
        return Err(output_buffer_too_small(plan.total));
    }
    output[..plan.total].fill(0);
    output[0..4].copy_from_slice(b"TLPE");
    put_u16(output, 4, 1);
    put_u16(output, 6, 0);
    put_u32(output, 8, WIRE_HEADER_SIZE as u32);
    put_u64(output, 12, plan.total as u64);
    put_u32(output, 20, WIRE_TABLE_COUNT as u32);
    for (index, descriptor) in plan.descriptors.iter().enumerate() {
        let offset = WIRE_DESCRIPTOR_OFFSET + index * WIRE_DESCRIPTOR_SIZE;
        if descriptor.count != 0 {
            put_u64(output, offset, descriptor.offset as u64);
            put_u64(output, offset + 8, descriptor.count as u64);
            put_u32(output, offset + 16, descriptor.stride as u32);
            put_u32(output, offset + 20, descriptor.flags);
        }
    }
    for entry in &plan.strings {
        put_u32(output, entry.record_offset, entry.bytes.len() as u32);
        output[entry.data_offset..entry.data_offset + entry.bytes.len()]
            .copy_from_slice(&entry.bytes);
    }
    let mut pool = HashMap::new();
    for (index, entry) in plan.strings.iter().enumerate() {
        pool.insert(entry.bytes.clone(), index);
    }
    let info = plan.descriptors[TABLE_INFO].offset;
    put_u32(
        output,
        info,
        (if model.info.is_pe32_plus { 1 } else { 0 }) | (if model.info.is_dll { 2 } else { 0 }),
    );
    put_u16(output, info + 4, model.info.machine);
    put_u16(output, info + 6, model.info.number_of_sections);
    put_u32(output, info + 8, model.info.entry_point);
    put_u64(output, info + 12, model.info.image_base);
    put_u32(output, info + 20, model.info.section_alignment);
    put_u32(output, info + 24, model.info.size_of_image);
    put_u32(output, info + 28, model.info.size_of_headers);
    put_u16(output, info + 32, model.info.subsystem);
    let mut directory_offset = info + 36;
    for directory in model.info.directories {
        put_u32(output, directory_offset, directory[0]);
        put_u32(output, directory_offset + 4, directory[1]);
        directory_offset += 8;
    }
    put_u32(output, info + 92, model.info.export_ordinal_base);
    put_u64(output, info + 96, model.info.tls.start_address_of_raw_data);
    put_u64(output, info + 104, model.info.tls.end_address_of_raw_data);
    put_u64(output, info + 112, model.info.tls.address_of_index);
    put_u64(output, info + 120, model.info.tls.address_of_callbacks);
    put_u32(output, info + 128, model.info.tls.size_of_zero_fill);
    put_u32(output, info + 132, model.info.tls.characteristics);

    let sections = plan.descriptors[TABLE_SECTIONS].offset;
    for (index, section) in model.sections.iter().enumerate() {
        let offset = sections + index * STRIDES[TABLE_SECTIONS] as usize;
        string_ref(
            output,
            offset,
            string_index(&pool, &section.name, plan),
            plan,
        );
        put_u32(output, offset + 16, section.virtual_address);
        put_u32(output, offset + 20, section.virtual_size);
        put_u32(output, offset + 24, section.raw_data_pointer);
        put_u32(output, offset + 28, section.raw_data_size);
        put_u32(output, offset + 32, section.characteristics);
    }
    write_dlls(
        output,
        model,
        &model.imports,
        TABLE_IMPORT_DLLS,
        TABLE_IMPORT_SYMBOLS,
        plan,
        &mut pool,
    );
    write_dlls(
        output,
        model,
        &model.delay_imports,
        TABLE_DELAY_DLLS,
        TABLE_DELAY_SYMBOLS,
        plan,
        &mut pool,
    );
    let exports = plan.descriptors[TABLE_EXPORTS].offset;
    for (index, export) in model.exports.iter().enumerate() {
        let offset = exports + index * STRIDES[TABLE_EXPORTS] as usize;
        let flags = (if export.by_name { 1 } else { 0 })
            | (if !export.forwarder.is_empty() { 2 } else { 0 });
        put_u32(output, offset, flags);
        put_u16(output, offset + 4, export.ordinal);
        put_u32(output, offset + 8, export.rva);
        string_ref(
            output,
            offset + 16,
            if export.by_name {
                string_index(&pool, &export.name, plan)
            } else {
                None
            },
            plan,
        );
        string_ref(
            output,
            offset + 32,
            if export.forwarder.is_empty() {
                None
            } else {
                string_index(&pool, &export.forwarder, plan)
            },
            plan,
        );
    }
    let callbacks = plan.descriptors[TABLE_TLS_CALLBACKS].offset;
    for (index, callback) in model.info.tls.callback_vas.iter().enumerate() {
        put_u64(output, callbacks + index * 8, *callback);
    }
    let runtime = plan.descriptors[TABLE_RUNTIME_FUNCTIONS].offset;
    let unwind_infos = plan.descriptors[TABLE_UNWIND_INFOS].offset;
    let unwind_codes = plan.descriptors[TABLE_UNWIND_CODES].offset;
    let unwind_epilogs = plan.descriptors[TABLE_UNWIND_EPILOGS].offset;
    let mut code_index = 0usize;
    let mut epilog_index = 0usize;
    for (index, function) in model.runtime_functions.iter().enumerate() {
        let offset = runtime + index * 24;
        put_u32(output, offset, function.begin_rva);
        put_u32(output, offset + 4, function.end_rva);
        put_u32(output, offset + 8, function.unwind_info_rva);
        put_u64(output, offset + 12, index as u64);
        let unwind = unwind_infos + index * 72;
        put_u8(output, unwind, function.unwind.version);
        put_u8(output, unwind + 1, function.unwind.flags);
        put_u8(output, unwind + 2, function.unwind.prolog_size);
        put_u8(output, unwind + 3, function.unwind.frame_register);
        put_u8(output, unwind + 4, function.unwind.frame_offset);
        put_u8(
            output,
            unwind + 5,
            (if function.unwind.extended_set_fpreg {
                1
            } else {
                0
            }) | (if function.unwind.chained.is_some() {
                2
            } else {
                0
            }),
        );
        put_u64(
            output,
            unwind + 8,
            if function.unwind.codes.is_empty() {
                0
            } else {
                (code_index) as u64
            },
        );
        put_u64(output, unwind + 16, function.unwind.codes.len() as u64);
        put_u64(
            output,
            unwind + 24,
            if function.unwind.epilogs.is_empty() {
                0
            } else {
                epilog_index as u64
            },
        );
        put_u64(output, unwind + 32, function.unwind.epilogs.len() as u64);
        put_u32(output, unwind + 40, function.unwind.handler_rva);
        put_u32(output, unwind + 44, function.unwind.handler_data_rva);
        if let Some(chain) = function.unwind.chained {
            put_u32(output, unwind + 48, chain.0);
            put_u32(output, unwind + 52, chain.1);
            put_u32(output, unwind + 56, chain.2);
        }
        for code in &function.unwind.codes {
            let code_offset = unwind_codes + code_index * 8;
            put_u8(output, code_offset, code.code_offset);
            put_u8(output, code_offset + 1, code.operation);
            put_u8(output, code_offset + 2, code.operation_info);
            put_u32(output, code_offset + 4, code.operand);
            code_index += 1;
        }
        for epilog in &function.unwind.epilogs {
            let epilog_offset = unwind_epilogs + epilog_index * 8;
            put_u32(output, epilog_offset, epilog.begin_rva);
            put_u32(output, epilog_offset + 4, epilog.end_rva);
            epilog_index += 1;
        }
    }
    let reloc_blocks = plan.descriptors[TABLE_RELOC_BLOCKS].offset;
    let reloc_entries = plan.descriptors[TABLE_RELOC_ENTRIES].offset;
    let mut reloc_index = 0usize;
    for (index, block) in model.relocations.iter().enumerate() {
        let offset = reloc_blocks + index * 32;
        put_u32(output, offset, block.page_rva);
        put_u64(
            output,
            offset + 8,
            if block.entries.is_empty() {
                0
            } else {
                reloc_index as u64
            },
        );
        put_u64(output, offset + 16, block.entries.len() as u64);
        for entry in &block.entries {
            let entry_offset = reloc_entries + reloc_index * 8;
            put_u16(output, entry_offset, entry.kind);
            put_u16(output, entry_offset + 2, entry.offset);
            reloc_index += 1;
        }
    }
    Ok(())
}

fn put_u8(output: &mut [u8], offset: usize, value: u8) {
    output[offset] = value;
}

fn write_dlls(
    output: &mut [u8],
    _model: &Model,
    dlls: &[Dll],
    dll_table: usize,
    symbol_table: usize,
    plan: &WirePlan,
    pool: &mut HashMap<Vec<u8>, usize>,
) {
    let dll_offset = plan.descriptors[dll_table].offset;
    let symbol_offset = plan.descriptors[symbol_table].offset;
    let mut symbol_index = 0usize;
    for (index, dll) in dlls.iter().enumerate() {
        let offset = dll_offset + index * 40;
        string_ref(output, offset, string_index(pool, &dll.name, plan), plan);
        put_u64(
            output,
            offset + 16,
            if dll.symbols.is_empty() {
                0
            } else {
                symbol_index as u64
            },
        );
        put_u64(output, offset + 24, dll.symbols.len() as u64);
        for symbol in &dll.symbols {
            let symbol_offset = symbol_offset + symbol_index * 32;
            put_u32(output, symbol_offset, if symbol.by_ordinal { 1 } else { 0 });
            put_u16(output, symbol_offset + 4, symbol.ordinal);
            string_ref(
                output,
                symbol_offset + 8,
                if symbol.by_ordinal {
                    None
                } else {
                    string_index(pool, &symbol.name, plan)
                },
                plan,
            );
            put_u32(output, symbol_offset + 24, symbol.iat_rva);
            symbol_index += 1;
        }
    }
}

fn write_error(
    result: &PeError,
    error_out: *mut TlPeErrorV1,
    message_out: *mut c_char,
    message_capacity: u64,
    message_required: *mut u64,
) -> u32 {
    if error_out.is_null() || message_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    unsafe {
        std::ptr::write(
            error_out,
            TlPeErrorV1 {
                code: result.code,
                phase: result.phase,
                input_offset: result.input_offset,
                detail_value: result.detail_value,
            },
        );
    }
    let required = match result
        .message
        .len()
        .checked_add(1)
        .and_then(|value| u64::try_from(value).ok())
    {
        Some(value) => value,
        None => return STATUS_INTERNAL,
    };
    unsafe {
        *message_required = required;
    }
    if message_capacity != 0 && message_out.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if message_capacity < required {
        if message_capacity != 0 {
            let writable = usize::try_from(message_capacity - 1)
                .unwrap_or(0)
                .min(result.message.len());
            unsafe {
                std::ptr::copy_nonoverlapping(
                    result.message.as_ptr(),
                    message_out.cast::<u8>(),
                    writable,
                );
                *message_out.cast::<u8>().add(writable) = 0;
            }
        }
        return STATUS_BUFFER_TOO_SMALL;
    }
    if required != 0 {
        unsafe {
            std::ptr::copy_nonoverlapping(
                result.message.as_ptr(),
                message_out.cast::<u8>(),
                result.message.len(),
            );
            *message_out.cast::<u8>().add(result.message.len()) = 0;
        }
    }
    result.status
}

fn success_error() -> PeError {
    error(
        STATUS_SUCCESS,
        ERROR_NONE,
        PHASE_NONE,
        UNKNOWN_OFFSET,
        0,
        "",
    )
}

struct PreparedOutput {
    required: usize,
    bytes: Option<Vec<u8>>,
}

unsafe fn parse_input<'a>(input: *const u8, input_length: u64) -> Result<&'a [u8], PeError> {
    if input.is_null() {
        if input_length == 0 {
            return Ok(&[]);
        }
        return Err(error(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            "input nulo com comprimento não nulo",
        ));
    }
    if input_length > isize::MAX as u64 {
        return Err(error(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            "comprimento de input excede o limite de um slice Rust",
        ));
    }
    let length = usize::try_from(input_length).map_err(|_| {
        error(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            "comprimento de input não cabe em usize",
        )
    })?;
    Ok(unsafe { std::slice::from_raw_parts(input, length) })
}

#[allow(clippy::too_many_arguments)]
fn invoke<F>(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error_out: *mut TlPeErrorV1,
    message_out: *mut c_char,
    message_capacity: u64,
    message_required: *mut u64,
    fill: bool,
    operation: F,
) -> u32
where
    F: FnOnce(&[u8], u64, bool) -> Result<PreparedOutput, PeError>,
{
    let result = catch_unwind(AssertUnwindSafe(|| {
        if output_required.is_null() || error_out.is_null() || message_required.is_null() {
            return Err(error(
                STATUS_INVALID_ARGUMENT,
                ERROR_INVALID_ARGUMENT,
                PHASE_INPUT,
                UNKNOWN_OFFSET,
                0,
                "ponteiro obrigatório nulo",
            ));
        }
        if fill && output_capacity != 0 && output.is_null() {
            return Err(error(
                STATUS_INVALID_ARGUMENT,
                ERROR_INVALID_ARGUMENT,
                PHASE_INPUT,
                UNKNOWN_OFFSET,
                output_capacity,
                "output nulo com capacidade não nula",
            ));
        }
        let bytes = unsafe { parse_input(input, input_length)? };
        let prepared = operation(bytes, output_capacity, fill)?;
        unsafe {
            *output_required = prepared.required as u64;
        }
        if fill {
            let serialized = prepared.bytes.as_ref().ok_or_else(|| {
                error(
                    STATUS_INTERNAL,
                    ERROR_INTERNAL,
                    PHASE_SERIALIZE,
                    UNKNOWN_OFFSET,
                    0,
                    "serializer não produziu o buffer solicitado",
                )
            })?;
            if !serialized.is_empty() {
                unsafe {
                    std::ptr::copy_nonoverlapping(serialized.as_ptr(), output, serialized.len());
                }
            }
        }
        Ok(prepared)
    }));
    match result {
        Ok(Ok(_)) => write_error(
            &success_error(),
            error_out,
            message_out,
            message_capacity,
            message_required,
        ),
        Ok(Err(error)) => {
            if error.status != STATUS_BUFFER_TOO_SMALL {
                unsafe {
                    if !output_required.is_null() {
                        *output_required = 0;
                    }
                }
            } else if let Some(required) = error.output_required {
                unsafe {
                    if !output_required.is_null() {
                        *output_required = required;
                    }
                }
            }
            write_error(
                &error,
                error_out,
                message_out,
                message_capacity,
                message_required,
            )
        }
        Err(_) => {
            let internal = error(
                STATUS_INTERNAL,
                ERROR_INTERNAL,
                PHASE_NONE,
                UNKNOWN_OFFSET,
                0,
                "panic capturado na fronteira FFI",
            );
            unsafe {
                if !output_required.is_null() {
                    *output_required = 0;
                }
            }
            write_error(
                &internal,
                error_out,
                message_out,
                message_capacity,
                message_required,
            )
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn tl_pe_parse_v1_size(
    input: *const u8,
    input_length: u64,
    output_required: *mut u64,
    error_out: *mut TlPeErrorV1,
    message_out: *mut c_char,
    message_capacity: u64,
    message_required: *mut u64,
) -> u32 {
    invoke(
        input,
        input_length,
        std::ptr::null_mut(),
        0,
        output_required,
        error_out,
        message_out,
        message_capacity,
        message_required,
        false,
        |bytes, _, _| {
            let model = Parser::new(bytes).parse()?;
            let plan = make_plan(&model)?;
            Ok(PreparedOutput {
                required: plan.total,
                bytes: None,
            })
        },
    )
}

#[no_mangle]
pub unsafe extern "C" fn tl_pe_parse_v1_fill(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error_out: *mut TlPeErrorV1,
    message_out: *mut c_char,
    message_capacity: u64,
    message_required: *mut u64,
) -> u32 {
    invoke(
        input,
        input_length,
        output,
        output_capacity,
        output_required,
        error_out,
        message_out,
        message_capacity,
        message_required,
        true,
        |bytes, output_capacity, _| {
            let model = Parser::new(bytes).parse()?;
            let plan = make_plan(&model)?;
            if output_capacity < plan.total as u64 {
                return Err(output_buffer_too_small(plan.total));
            }
            let mut serialized = vec![0u8; plan.total];
            fill_wire(&model, &plan, &mut serialized)?;
            Ok(PreparedOutput {
                required: plan.total,
                bytes: Some(serialized),
            })
        },
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checked_wire_layout_has_expected_sizes() {
        assert_eq!(WIRE_HEADER_SIZE, 416);
        assert_eq!(STRIDES[TABLE_INFO], 136);
        assert_eq!(STRIDES[TABLE_RUNTIME_FUNCTIONS], 24);
        assert_eq!(STRIDES[TABLE_UNWIND_INFOS], 72);
        assert_eq!(MAX_SERIALIZED_BYTES, 256 * 1024 * 1024);
    }

    #[test]
    fn reader_is_little_endian_and_bounds_checked() {
        let reader = Reader::new(&[0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0, 0]);
        assert_eq!(reader.u16(0), Some(0x1234));
        assert_eq!(reader.u32(2), Some(0x12345678));
        assert_eq!(reader.u64(1), None);
    }

    #[test]
    fn string_pool_deduplicates_without_reordering() {
        let mut pool = StringPool::new();
        assert_eq!(pool.intern(b"a"), 0);
        assert_eq!(pool.intern(b"b"), 1);
        assert_eq!(pool.intern(b"a"), 0);
        assert_eq!(pool.entries, vec![b"a".to_vec(), b"b".to_vec()]);
    }

    #[test]
    fn serializer_arithmetic_is_checked() {
        assert!(align8(usize::MAX).is_none());
        let result = checked_table_count(usize::MAX, 8, PHASE_SERIALIZE);
        assert_eq!(
            result.as_ref().map_err(|error| error.status),
            Err(STATUS_OUTPUT_TOO_LARGE)
        );
    }

    #[test]
    fn bounded_malformed_inputs_never_panic() {
        let mut state = 0x52_32_31_2e_32_u64;
        for _ in 0..2048 {
            state = state
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1_442_695_040_888_963_407);
            let length = (state as usize) % 512;
            let mut bytes = vec![0u8; length];
            for byte in &mut bytes {
                state = state
                    .wrapping_mul(6_364_136_223_846_793_005)
                    .wrapping_add(1_442_695_040_888_963_407);
                *byte = (state >> 32) as u8;
            }
            let result = catch_unwind(AssertUnwindSafe(|| Parser::new(&bytes).parse()));
            assert!(result.is_ok());
        }
    }
}
