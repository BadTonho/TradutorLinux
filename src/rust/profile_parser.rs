use crate::profile_contract::*;

use std::collections::HashMap;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;

#[derive(Clone, Debug, PartialEq, Eq)]
struct FileMapping {
    source: Vec<u8>,
    target: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct DllMapping {
    module: Vec<u8>,
    source: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct ProfileModel {
    schema: u32,
    app_id: Vec<u8>,
    app_sha256: Vec<u8>,
    app_version: Vec<u8>,
    extension: Vec<u8>,
    files: Vec<FileMapping>,
    dlls: Vec<DllMapping>,
    backend_kind: u32,
    backend_declared: bool,
    min_version: Vec<u8>,
}

#[derive(Clone, Debug)]
struct Failure {
    status: u32,
    error: TlProfileErrorV1,
    message: Vec<u8>,
}

struct Success {
    wire: Vec<u8>,
}

impl Failure {
    fn new(status: u32, code: u32, phase: u32, offset: u64, detail: u64, message: &[u8]) -> Self {
        Self {
            status,
            error: TlProfileErrorV1 {
                code,
                phase,
                input_offset: offset,
                detail_value: detail,
            },
            message: message.to_vec(),
        }
    }

    fn malformed(phase: u32, offset: usize, message: &[u8]) -> Self {
        Self::new(
            STATUS_MALFORMED,
            if phase == PHASE_SCHEMA {
                ERROR_SCHEMA
            } else if phase == PHASE_IDENTITY {
                ERROR_IDENTITY
            } else if phase == PHASE_PATHS {
                ERROR_PATH
            } else {
                ERROR_JSON_SYNTAX
            },
            phase,
            offset as u64,
            0,
            message,
        )
    }

    fn internal(message: &[u8]) -> Self {
        Self::new(
            STATUS_INTERNAL,
            ERROR_INTERNAL,
            PHASE_NONE,
            UNKNOWN_OFFSET,
            0,
            message,
        )
    }
}

struct JsonParser<'a> {
    input: &'a [u8],
    position: usize,
}

impl<'a> JsonParser<'a> {
    fn new(input: &'a [u8]) -> Self {
        Self { input, position: 0 }
    }

    fn parse(mut self) -> Result<ProfileModel, Failure> {
        self.skip_whitespace();
        if !self.consume(b'{') {
            return Err(Failure::malformed(
                PHASE_JSON,
                self.position,
                b"o perfil deve comecar com um objeto JSON",
            ));
        }

        let mut profile = ProfileModel {
            schema: 0,
            app_id: Vec::new(),
            app_sha256: Vec::new(),
            app_version: Vec::new(),
            extension: Vec::new(),
            files: Vec::new(),
            dlls: Vec::new(),
            backend_kind: WIRE_BACKEND_NATIVE,
            backend_declared: false,
            min_version: Vec::new(),
        };
        let mut seen = [false; 8];
        self.skip_whitespace();
        if self.consume(b'}') {
            return Err(Failure::malformed(
                PHASE_JSON,
                self.position,
                b"o perfil nao pode ser vazio",
            ));
        }

        loop {
            let key = self.parse_string()?;
            self.skip_whitespace();
            if !self.consume(b':') {
                return Err(self.error(b"faltou ':' apos chave JSON"));
            }
            let slot = match key.as_slice() {
                b"schema" => 0,
                b"app_id" => 1,
                b"app_sha256" => 2,
                b"app_version" => 3,
                b"files" => 4,
                b"dlls" => 5,
                b"backend" => 6,
                b"extension" => 7,
                _ => {
                    return Err(self.error(b"campo JSON desconhecido"));
                }
            };
            if seen[slot] {
                return Err(self.error(b"campo JSON repetido"));
            }
            seen[slot] = true;

            match slot {
                0 => profile.schema = self.parse_u32()?,
                1 => profile.app_id = self.parse_string()?,
                2 => profile.app_sha256 = self.parse_string()?,
                3 => profile.app_version = self.parse_string()?,
                4 => profile.files = self.parse_files()?,
                5 => profile.dlls = self.parse_dlls()?,
                6 => {
                    let (kind, min_version) = self.parse_backend()?;
                    profile.backend_kind = kind;
                    profile.min_version = min_version;
                    profile.backend_declared = true;
                }
                7 => profile.extension = self.parse_string()?,
                _ => unreachable!(),
            }

            self.skip_whitespace();
            if self.consume(b'}') {
                break;
            }
            if !self.consume(b',') {
                return Err(self.error(b"faltou ',' entre campos JSON"));
            }
            self.skip_whitespace();
            if self.peek(b'}') {
                return Err(self.error(b"virgula final nao permitida"));
            }
        }

        self.skip_whitespace();
        if self.position != self.input.len() {
            return Err(self.error(b"conteudo apos o objeto JSON"));
        }
        if !seen[0] || !seen[1] {
            return Err(self.error(b"schema e app_id sao obrigatorios"));
        }
        Ok(profile)
    }

    fn parse_files(&mut self) -> Result<Vec<FileMapping>, Failure> {
        if !self.consume_after_whitespace(b'[') {
            return Err(self.error(b"campo files invalido"));
        }
        let mut values = Vec::new();
        self.skip_whitespace();
        if self.consume(b']') {
            return Ok(values);
        }
        loop {
            if !self.consume_after_whitespace(b'{') {
                return Err(self.error(b"entrada files invalida"));
            }
            let mut source = None;
            let mut target = None;
            let mut source_seen = false;
            let mut target_seen = false;
            self.skip_whitespace();
            if self.consume(b'}') {
                return Err(self.error(b"entrada files vazia"));
            }
            loop {
                let key = self.parse_string()?;
                self.skip_whitespace();
                if !self.consume(b':') {
                    return Err(self.error(b"entrada files sem ':'"));
                }
                match key.as_slice() {
                    b"source" if !source_seen => {
                        source = Some(self.parse_string()?);
                        source_seen = true;
                    }
                    b"target" if !target_seen => {
                        target = Some(self.parse_string()?);
                        target_seen = true;
                    }
                    b"source" | b"target" => {
                        return Err(self.error(b"campo files repetido"));
                    }
                    _ => return Err(self.error(b"campo files desconhecido")),
                }
                self.skip_whitespace();
                if self.consume(b'}') {
                    break;
                }
                if !self.consume(b',') {
                    return Err(self.error(b"entrada files sem ','"));
                }
                self.skip_whitespace();
                if self.peek(b'}') {
                    return Err(self.error(b"virgula final em files"));
                }
            }
            if !source_seen || !target_seen {
                return Err(self.error(b"source e target sao obrigatorios"));
            }
            values.push(FileMapping {
                source: source.unwrap_or_default(),
                target: target.unwrap_or_default(),
            });
            if values.len() as u64 > LIMIT_MAX_FILES {
                return Err(Failure::new(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_INPUT_TOO_LARGE,
                    PHASE_JSON,
                    self.position as u64,
                    LIMIT_MAX_FILES,
                    b"quantidade de files excede o limite",
                ));
            }
            self.skip_whitespace();
            if self.consume(b']') {
                return Ok(values);
            }
            if !self.consume(b',') {
                return Err(self.error(b"campo files sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b']') {
                return Err(self.error(b"virgula final em files"));
            }
        }
    }

    fn parse_dlls(&mut self) -> Result<Vec<DllMapping>, Failure> {
        if !self.consume_after_whitespace(b'[') {
            return Err(self.error(b"campo dlls invalido"));
        }
        let mut values = Vec::new();
        self.skip_whitespace();
        if self.consume(b']') {
            return Ok(values);
        }
        loop {
            if !self.consume_after_whitespace(b'{') {
                return Err(self.error(b"entrada dlls invalida"));
            }
            let mut module = None;
            let mut source = None;
            let mut module_seen = false;
            let mut source_seen = false;
            self.skip_whitespace();
            if self.consume(b'}') {
                return Err(self.error(b"entrada dlls vazia"));
            }
            loop {
                let key = self.parse_string()?;
                self.skip_whitespace();
                if !self.consume(b':') {
                    return Err(self.error(b"entrada dlls sem ':'"));
                }
                match key.as_slice() {
                    b"module" if !module_seen => {
                        module = Some(self.parse_string()?);
                        module_seen = true;
                    }
                    b"source" if !source_seen => {
                        source = Some(self.parse_string()?);
                        source_seen = true;
                    }
                    b"module" | b"source" => {
                        return Err(self.error(b"campo dlls repetido"));
                    }
                    _ => return Err(self.error(b"campo dlls desconhecido")),
                }
                self.skip_whitespace();
                if self.consume(b'}') {
                    break;
                }
                if !self.consume(b',') {
                    return Err(self.error(b"entrada dlls sem ','"));
                }
                self.skip_whitespace();
                if self.peek(b'}') {
                    return Err(self.error(b"virgula final em dlls"));
                }
            }
            if !module_seen || !source_seen {
                return Err(self.error(b"module e source sao obrigatorios"));
            }
            values.push(DllMapping {
                module: module.unwrap_or_default(),
                source: source.unwrap_or_default(),
            });
            if values.len() as u64 > LIMIT_MAX_DLLS {
                return Err(Failure::new(
                    STATUS_INPUT_TOO_LARGE,
                    ERROR_INPUT_TOO_LARGE,
                    PHASE_JSON,
                    self.position as u64,
                    LIMIT_MAX_DLLS,
                    b"quantidade de dlls excede o limite",
                ));
            }
            self.skip_whitespace();
            if self.consume(b']') {
                return Ok(values);
            }
            if !self.consume(b',') {
                return Err(self.error(b"campo dlls sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b']') {
                return Err(self.error(b"virgula final em dlls"));
            }
        }
    }

    fn parse_backend(&mut self) -> Result<(u32, Vec<u8>), Failure> {
        if !self.consume_after_whitespace(b'{') {
            return Err(self.error(b"campo backend invalido"));
        }
        let mut kind = None;
        let mut min_version = Vec::new();
        let mut min_seen = false;
        self.skip_whitespace();
        if self.consume(b'}') {
            return Err(self.error(b"backend vazio"));
        }
        loop {
            let key = self.parse_string()?;
            self.skip_whitespace();
            if !self.consume(b':') {
                return Err(self.error(b"backend sem ':'"));
            }
            match key.as_slice() {
                b"kind" if kind.is_none() => {
                    let value = self.parse_string()?;
                    kind = Some(match value.as_slice() {
                        b"native" => WIRE_BACKEND_NATIVE,
                        b"proton" => WIRE_BACKEND_PROTON,
                        _ => return Err(self.error(b"backend.kind invalido")),
                    });
                }
                b"kind" => return Err(self.error(b"backend.kind repetido")),
                b"min_version" if !min_seen => {
                    min_version = self.parse_string()?;
                    min_seen = true;
                }
                b"min_version" => return Err(self.error(b"backend.min_version repetido")),
                _ => return Err(self.error(b"campo backend desconhecido")),
            }
            self.skip_whitespace();
            if self.consume(b'}') {
                break;
            }
            if !self.consume(b',') {
                return Err(self.error(b"backend sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b'}') {
                return Err(self.error(b"virgula final em backend"));
            }
        }
        match kind {
            Some(value) => Ok((value, min_version)),
            None => Err(self.error(b"backend.kind e obrigatorio")),
        }
    }

    fn parse_string(&mut self) -> Result<Vec<u8>, Failure> {
        self.skip_whitespace();
        if !self.consume(b'"') {
            return Err(self.error(b"string JSON invalida"));
        }
        let mut output = Vec::new();
        while self.position < self.input.len() {
            let byte = self.input[self.position];
            self.position += 1;
            match byte {
                b'"' => return Ok(output),
                b'\\' => {
                    if self.position >= self.input.len() {
                        return Err(self.error(b"escape JSON truncado"));
                    }
                    let escaped = self.input[self.position];
                    self.position += 1;
                    output.push(match escaped {
                        b'"' => b'"',
                        b'\\' => b'\\',
                        b'/' => b'/',
                        b'b' => 8,
                        b'f' => 12,
                        b'n' => b'\n',
                        b'r' => b'\r',
                        b't' => b'\t',
                        _ => return Err(self.error(b"escape JSON nao suportado")),
                    });
                }
                0..=0x1f => return Err(self.error(b"controle em string JSON")),
                _ => output.push(byte),
            }
        }
        Err(self.error(b"string JSON sem fechamento"))
    }

    fn parse_u32(&mut self) -> Result<u32, Failure> {
        self.skip_whitespace();
        if self.position >= self.input.len() || !self.input[self.position].is_ascii_digit() {
            return Err(self.error(b"numero schema invalido"));
        }
        let mut value = 0u32;
        while self.position < self.input.len() && self.input[self.position].is_ascii_digit() {
            let digit = u32::from(self.input[self.position] - b'0');
            value = value
                .checked_mul(10)
                .and_then(|current| current.checked_add(digit))
                .ok_or_else(|| self.error(b"numero schema excede u32"))?;
            self.position += 1;
        }
        Ok(value)
    }

    fn skip_whitespace(&mut self) {
        while self.position < self.input.len()
            && matches!(self.input[self.position], b' ' | b'\t' | b'\n' | b'\r' | 0x0c | 0x0b)
        {
            self.position += 1;
        }
    }

    fn consume_after_whitespace(&mut self, expected: u8) -> bool {
        self.skip_whitespace();
        self.consume(expected)
    }

    fn consume(&mut self, expected: u8) -> bool {
        if self.peek(expected) {
            self.position += 1;
            true
        } else {
            false
        }
    }

    fn peek(&self, expected: u8) -> bool {
        self.position < self.input.len() && self.input[self.position] == expected
    }

    fn error(&self, message: &[u8]) -> Failure {
        Failure::malformed(PHASE_JSON, self.position, message)
    }
}

fn validate_profile(
    mut profile: ProfileModel,
    expected: (&[u8], &[u8], &[u8]),
) -> Result<ProfileModel, Failure> {
    if profile.schema != 1 && profile.schema != 2 && profile.schema != 3 && profile.schema != 4 {
        return Err(Failure::new(
            STATUS_UNSUPPORTED_FORMAT,
            ERROR_SCHEMA,
            PHASE_SCHEMA,
            UNKNOWN_OFFSET,
            profile.schema as u64,
            b"schema de perfil nao suportado",
        ));
    }
    if profile.schema == 1 && !profile.dlls.is_empty() {
        return Err(Failure::malformed(
            PHASE_SCHEMA,
            UNKNOWN_OFFSET as usize,
            b"campo dlls requer schema 2",
        ));
    }
    if profile.schema != 3 && profile.schema != 4 && profile.backend_declared {
        return Err(Failure::malformed(
            PHASE_SCHEMA,
            UNKNOWN_OFFSET as usize,
            b"campo backend requer schema 3",
        ));
    }
    if profile.schema != 4 && !profile.extension.is_empty() {
        return Err(Failure::malformed(
            PHASE_SCHEMA,
            UNKNOWN_OFFSET as usize,
            b"campo extension requer schema 4",
        ));
    }
    if profile.backend_kind == WIRE_BACKEND_NATIVE && !profile.min_version.is_empty() {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_BACKEND,
            PHASE_SCHEMA,
            UNKNOWN_OFFSET,
            0,
            b"min_version requer backend proton",
        ));
    }
    if profile.backend_kind == WIRE_BACKEND_PROTON
        && !profile.min_version.is_empty()
        && !valid_backend_version(&profile.min_version)
    {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_BACKEND,
            PHASE_SCHEMA,
            UNKNOWN_OFFSET,
            0,
            b"min_version do Proton invalida",
        ));
    }
    if !safe_app_id(&profile.app_id) || profile.app_id != expected.0 {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_IDENTITY,
            PHASE_IDENTITY,
            UNKNOWN_OFFSET,
            0,
            b"app_id do perfil nao corresponde ao aplicativo",
        ));
    }
    if !profile.extension.is_empty() && !safe_app_id(&profile.extension) {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_SCHEMA,
            PHASE_SCHEMA,
            UNKNOWN_OFFSET,
            0,
            b"extension invalida",
        ));
    }
    if !profile.app_sha256.is_empty() && !is_hex(&profile.app_sha256, 64) {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_IDENTITY,
            PHASE_IDENTITY,
            UNKNOWN_OFFSET,
            0,
            b"app_sha256 deve conter 64 digitos hexadecimais",
        ));
    }
    if !profile.app_version.is_empty() && !valid_version(&profile.app_version) {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_IDENTITY,
            PHASE_IDENTITY,
            UNKNOWN_OFFSET,
            0,
            b"app_version invalida",
        ));
    }
    if !profile.app_sha256.is_empty()
        && lowercase(&profile.app_sha256) != lowercase(expected.1)
    {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_IDENTITY,
            PHASE_IDENTITY,
            UNKNOWN_OFFSET,
            0,
            b"hash SHA-256 do perfil nao corresponde ao aplicativo",
        ));
    }
    if !profile.app_version.is_empty() && profile.app_version != expected.2 {
        return Err(Failure::new(
            STATUS_MALFORMED,
            ERROR_IDENTITY,
            PHASE_IDENTITY,
            UNKNOWN_OFFSET,
            0,
            b"versao do perfil nao corresponde ao aplicativo",
        ));
    }

    let mut normalized_modules = Vec::with_capacity(profile.dlls.len());
    for mapping in &mut profile.dlls {
        normalize_dll_module(&mut mapping.module).ok_or_else(|| {
            Failure::new(
                STATUS_MALFORMED,
                ERROR_PATH,
                PHASE_PATHS,
                UNKNOWN_OFFSET,
                0,
                b"modulo de DLL invalido",
            )
        })?;
        if normalized_modules.iter().any(|module| module == &mapping.module) {
            return Err(Failure::new(
                STATUS_MALFORMED,
                ERROR_PATH,
                PHASE_PATHS,
                UNKNOWN_OFFSET,
                0,
                b"mapeamento de DLL duplicado",
            ));
        }
        normalized_modules.push(mapping.module.clone());
        if !valid_relative_path(&mapping.source) {
            return Err(Failure::new(
                STATUS_MALFORMED,
                ERROR_PATH,
                PHASE_PATHS,
                UNKNOWN_OFFSET,
                0,
                b"origem de DLL deve ser relativa e usar apenas '/'",
            ));
        }
    }
    for (index, mapping) in profile.files.iter().enumerate() {
        if !valid_relative_path(&mapping.source) {
            return Err(Failure::new(
                STATUS_MALFORMED,
                ERROR_PATH,
                PHASE_PATHS,
                index as u64,
                0,
                b"origem de arquivo deve ser relativa e usar apenas '/'",
            ));
        }
        if !valid_c_drive_path(&mapping.target) {
            return Err(Failure::new(
                STATUS_MALFORMED,
                ERROR_PATH,
                PHASE_PATHS,
                index as u64,
                0,
                b"destino de arquivo fora de drive_c",
            ));
        }
        for previous in &profile.files[..index] {
            if previous.source == mapping.source || same_target(&previous.target, &mapping.target) {
                return Err(Failure::new(
                    STATUS_MALFORMED,
                    ERROR_PATH,
                    PHASE_PATHS,
                    index as u64,
                    0,
                    b"mapeamento de arquivo duplicado",
                ));
            }
        }
    }
    Ok(profile)
}

fn safe_app_id(value: &[u8]) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && !value.windows(2).any(|pair| pair == b"..")
        && value.iter().all(|byte| {
            byte.is_ascii_alphanumeric() || matches!(*byte, b'-' | b'_' | b'.')
        })
}

fn is_hex(value: &[u8], length: usize) -> bool {
    value.len() == length && value.iter().all(u8::is_ascii_hexdigit)
}

fn valid_version(value: &[u8]) -> bool {
    !value.is_empty() && value.len() <= 128 && value.iter().all(|byte| *byte >= 0x20)
}

fn valid_backend_version(value: &[u8]) -> bool {
    if value.is_empty() || value.len() > 64 {
        return false;
    }
    let mut components = 0usize;
    let mut digits = 0usize;
    for byte in value {
        if byte.is_ascii_digit() {
            digits += 1;
        } else if *byte == b'.' && digits != 0 {
            components += 1;
            digits = 0;
        } else {
            return false;
        }
    }
    digits != 0 && (1..=2).contains(&components)
}

fn lowercase(value: &[u8]) -> Vec<u8> {
    value.iter().map(|byte| byte.to_ascii_lowercase()).collect()
}

fn normalize_dll_module(value: &mut Vec<u8>) -> Option<()> {
    if value.is_empty()
        || value.len() > 255
        || value.iter().any(|byte| matches!(*byte, b'/' | b'\\' | b':'))
        || value.iter().any(|byte| {
            !byte.is_ascii_alphanumeric() && !matches!(*byte, b'-' | b'_' | b'.')
        })
    {
        return None;
    }
    value.make_ascii_lowercase();
    if !value.ends_with(b".dll") {
        value.extend_from_slice(b".dll");
    }
    (value.len() > 4 && value != b".dll").then_some(())
}

fn valid_relative_path(value: &[u8]) -> bool {
    !value.is_empty()
        && value[0] != b'/'
        && !value.contains(&0)
        && !value.contains(&b'\\')
        && !value
            .split(|byte| *byte == b'/')
            .any(|component| component == b"." || component == b"..")
}

fn valid_c_drive_path(value: &[u8]) -> bool {
    if value.len() < 3
        || !matches!(value[0], b'C' | b'c')
        || value[1] != b':'
        || !matches!(value[2], b'/' | b'\\')
        || value.last().is_some_and(|byte| matches!(*byte, b'/' | b'\\'))
        || value.contains(&0)
    {
        return false;
    }
    let mut depth = 0usize;
    for component in value[3..].split(|byte| matches!(*byte, b'/' | b'\\')) {
        if component.is_empty() || component == b"." {
            continue;
        }
        if component == b".." {
            if depth == 0 {
                return false;
            }
            depth -= 1;
        } else {
            depth = depth.saturating_add(1);
        }
    }
    true
}

fn same_target(first: &[u8], second: &[u8]) -> bool {
    first.iter().map(|byte| normalize_target_byte(*byte)).eq(
        second
            .iter()
            .map(|byte| normalize_target_byte(*byte)),
    )
}

fn normalize_target_byte(byte: u8) -> u8 {
    match byte {
        b'/' => b'\\',
        other => other.to_ascii_lowercase(),
    }
}

fn add_u64(left: u64, right: u64) -> Option<u64> {
    left.checked_add(right)
}

fn align8(value: u64) -> Option<u64> {
    add_u64(value, 7).map(|aligned| aligned & !7)
}

struct StringTable {
    values: Vec<Vec<u8>>,
    indices: HashMap<Vec<u8>, usize>,
}

impl StringTable {
    fn new() -> Self {
        Self {
            values: Vec::new(),
            indices: HashMap::new(),
        }
    }

    fn intern(&mut self, value: &[u8]) -> Result<usize, Failure> {
        if value.is_empty() {
            return Ok(usize::MAX);
        }
        if let Some(index) = self.indices.get(value) {
            return Ok(*index);
        }
        if self.values.len() as u64 >= LIMIT_MAX_STRINGS {
            return Err(Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_OUTPUT_TOO_LARGE,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                LIMIT_MAX_STRINGS,
                b"quantidade de strings excede o limite",
            ));
        }
        let index = self.values.len();
        let owned = value.to_vec();
        self.values.push(owned.clone());
        self.indices.insert(owned, index);
        Ok(index)
    }
}

fn serialize(profile: &ProfileModel) -> Result<Success, Failure> {
    let mut strings = StringTable::new();
    let app_id = strings.intern(&profile.app_id)?;
    let app_sha256 = strings.intern(&profile.app_sha256)?;
    let app_version = strings.intern(&profile.app_version)?;
    let min_version = strings.intern(&profile.min_version)?;
    let extension = strings.intern(&profile.extension)?;
    let mut file_refs = Vec::with_capacity(profile.files.len());
    for mapping in &profile.files {
        file_refs.push((
            strings.intern(&mapping.source)?,
            strings.intern(&mapping.target)?,
        ));
    }
    let mut dll_refs = Vec::with_capacity(profile.dlls.len());
    for mapping in &profile.dlls {
        dll_refs.push((
            strings.intern(&mapping.module)?,
            strings.intern(&mapping.source)?,
        ));
    }

    let mut total = WIRE_HEADER_SIZE as u64;
    let info_offset = total;
    total = add_u64(total, WIRE_INFO_STRIDE as u64).ok_or_else(|| {
        Failure::new(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            0,
            b"tamanho TLPR excede u64",
        )
    })?;
    let files_offset = if profile.files.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLPR"))?;
        let offset = total;
        let bytes = (profile.files.len() as u64)
            .checked_mul(WIRE_FILE_STRIDE as u64)
            .ok_or_else(|| Failure::internal(b"overflow da tabela files"))?;
        total = add_u64(total, bytes).ok_or_else(|| Failure::internal(b"overflow da tabela files"))?;
        offset
    };
    let dlls_offset = if profile.dlls.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLPR"))?;
        let offset = total;
        let bytes = (profile.dlls.len() as u64)
            .checked_mul(WIRE_DLL_STRIDE as u64)
            .ok_or_else(|| Failure::internal(b"overflow da tabela dlls"))?;
        total = add_u64(total, bytes).ok_or_else(|| Failure::internal(b"overflow da tabela dlls"))?;
        offset
    };
    let strings_offset = if strings.values.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLPR"))?;
        let offset = total;
        for value in &strings.values {
            let record = (WIRE_STRING_RECORD_HEADER_SIZE as u64)
                .checked_add(value.len() as u64)
                .ok_or_else(|| Failure::internal(b"overflow de registro string TLPR"))?;
            total = align8(add_u64(total, record).ok_or_else(|| {
                Failure::internal(b"overflow da tabela strings TLPR")
            })?)
            .ok_or_else(|| Failure::internal(b"overflow de alinhamento TLPR"))?;
        }
        offset
    };
    if total > LIMIT_MAX_SERIALIZED_BYTES {
        return Err(Failure::new(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            total,
            b"saida TLPR excede o limite",
        ));
    }
    let total_usize = usize::try_from(total).map_err(|_| {
        Failure::new(
            STATUS_OUTPUT_TOO_LARGE,
            ERROR_OUTPUT_TOO_LARGE,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            total,
            b"saida TLPR nao cabe em usize",
        )
    })?;
    let mut wire = vec![0u8; total_usize];
    wire[0..4].copy_from_slice(&WIRE_MAGIC);
    put_u16(&mut wire, 4, WIRE_MAJOR);
    put_u16(&mut wire, 6, WIRE_MINOR);
    put_u32(&mut wire, 8, WIRE_HEADER_SIZE as u32);
    put_u64(&mut wire, 12, total);
    put_u32(&mut wire, 20, WIRE_TABLE_COUNT as u32);
    put_u32(&mut wire, 24, 0);
    put_u32(&mut wire, 28, 0);
    descriptor(&mut wire, WIRE_TABLE_INFO, info_offset, 1, WIRE_INFO_STRIDE as u32, 0);
    descriptor(
        &mut wire,
        WIRE_TABLE_FILES,
        files_offset,
        profile.files.len() as u64,
        WIRE_FILE_STRIDE as u32,
        0,
    );
    descriptor(
        &mut wire,
        WIRE_TABLE_DLLS,
        dlls_offset,
        profile.dlls.len() as u64,
        WIRE_DLL_STRIDE as u32,
        0,
    );
    descriptor(
        &mut wire,
        WIRE_TABLE_STRINGS,
        strings_offset,
        strings.values.len() as u64,
        0,
        WIRE_VARIABLE_RECORDS,
    );

    let info = usize::try_from(info_offset).map_err(|_| Failure::internal(b"offset info invalido"))?;
    put_u32(&mut wire, info + WIRE_INFO_SCHEMA_OFFSET, profile.schema);
    put_u32(&mut wire, info + WIRE_INFO_BACKEND_OFFSET, profile.backend_kind);
    put_u32(
        &mut wire,
        info + WIRE_INFO_FLAGS_OFFSET,
        (if profile.backend_declared { WIRE_INFO_FLAG_BACKEND_DECLARED } else { 0 }) |
            (if !profile.extension.is_empty() {
                WIRE_INFO_FLAG_EXTENSION_DECLARED
            } else {
                0
            }),
    );
    put_ref(&mut wire, info + WIRE_INFO_APP_ID_OFFSET, &strings, app_id, strings_offset, total)?;
    put_ref(
        &mut wire,
        info + WIRE_INFO_SHA256_OFFSET,
        &strings,
        app_sha256,
        strings_offset,
        total,
    )?;
    put_ref(
        &mut wire,
        info + WIRE_INFO_VERSION_OFFSET,
        &strings,
        app_version,
        strings_offset,
        total,
    )?;
    put_ref(
        &mut wire,
        info + WIRE_INFO_MIN_VERSION_OFFSET,
        &strings,
        min_version,
        strings_offset,
        total,
    )?;
    put_ref(
        &mut wire,
        info + WIRE_INFO_EXTENSION_OFFSET,
        &strings,
        extension,
        strings_offset,
        total,
    )?;

    for (index, (source, target)) in file_refs.iter().enumerate() {
        let record = usize::try_from(files_offset)
            .ok()
            .and_then(|offset| offset.checked_add(index * WIRE_FILE_STRIDE))
            .ok_or_else(|| Failure::internal(b"offset files invalido"))?;
        put_ref(&mut wire, record, &strings, *source, strings_offset, total)?;
        put_ref(&mut wire, record + WIRE_STRING_REF_SIZE, &strings, *target, strings_offset, total)?;
    }
    for (index, (module, source)) in dll_refs.iter().enumerate() {
        let record = usize::try_from(dlls_offset)
            .ok()
            .and_then(|offset| offset.checked_add(index * WIRE_DLL_STRIDE))
            .ok_or_else(|| Failure::internal(b"offset dlls invalido"))?;
        put_ref(&mut wire, record, &strings, *module, strings_offset, total)?;
        put_ref(&mut wire, record + WIRE_STRING_REF_SIZE, &strings, *source, strings_offset, total)?;
    }
    if strings_offset != 0 {
        let mut cursor = usize::try_from(strings_offset)
            .map_err(|_| Failure::internal(b"offset strings invalido"))?;
        for value in &strings.values {
            put_u32(&mut wire, cursor, value.len() as u32);
            let data = cursor + WIRE_STRING_RECORD_HEADER_SIZE;
            wire[data..data + value.len()].copy_from_slice(value);
            cursor = align8((data + value.len()) as u64)
                .and_then(|value| usize::try_from(value).ok())
                .ok_or_else(|| Failure::internal(b"cursor strings invalido"))?;
        }
    }
    Ok(Success { wire })
}

fn string_offset(
    strings: &StringTable,
    index: usize,
    strings_offset: u64,
) -> Result<(u64, u64), Failure> {
    if index == usize::MAX {
        return Ok((0, 0));
    }
    let mut cursor = strings_offset;
    for (current, value) in strings.values.iter().enumerate() {
        let data = add_u64(cursor, WIRE_STRING_RECORD_HEADER_SIZE as u64)
            .ok_or_else(|| Failure::internal(b"overflow de offset string"))?;
        if current == index {
            return Ok((data, value.len() as u64));
        }
        let record = add_u64(data, value.len() as u64)
            .and_then(align8)
            .ok_or_else(|| Failure::internal(b"overflow de registro string"))?;
        cursor = record;
    }
    Err(Failure::internal(b"indice de string invalido"))
}

fn put_ref(
    wire: &mut [u8],
    offset: usize,
    strings: &StringTable,
    index: usize,
    strings_offset: u64,
    _total: u64,
) -> Result<(), Failure> {
    let (data, length) = string_offset(strings, index, strings_offset)?;
    put_u64(wire, offset, data);
    put_u64(wire, offset + 8, length);
    Ok(())
}

fn descriptor(wire: &mut [u8], table: usize, offset: u64, count: u64, stride: u32, flags: u32) {
    let position = WIRE_DESCRIPTOR_OFFSET + table * WIRE_DESCRIPTOR_SIZE;
    if count == 0 {
        return;
    }
    put_u64(wire, position, offset);
    put_u64(wire, position + 8, count);
    put_u32(wire, position + 16, stride);
    put_u32(wire, position + 20, flags);
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

unsafe fn input_slice<'a>(pointer: *const u8, length: u64, limit: u64) -> Result<&'a [u8], Failure> {
    if length > limit {
        return Err(Failure::new(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            length,
            b"entrada excede o limite",
        ));
    }
    let length_usize = usize::try_from(length).map_err(|_| {
        Failure::new(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            length,
            b"entrada nao cabe em usize",
        )
    })?;
    if length_usize != 0 && pointer.is_null() {
        return Err(Failure::new(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            0,
            b"ponteiro de entrada nulo",
        ));
    }
    if length_usize == 0 {
        Ok(&[])
    } else {
        Ok(unsafe { slice::from_raw_parts(pointer, length_usize) })
    }
}

unsafe fn identity_slice<'a>(pointer: *const u8, length: u64) -> Result<&'a [u8], Failure> {
    input_slice(pointer, length, LIMIT_MAX_INPUT_BYTES)
}

fn parse_input(input: &[u8], identity: (&[u8], &[u8], &[u8])) -> Result<Success, Failure> {
    let profile = JsonParser::new(input).parse()?;
    let profile = validate_profile(profile, identity)?;
    serialize(&profile)
}

struct CallResult {
    status: u32,
    error: TlProfileErrorV1,
    message: Vec<u8>,
    required: u64,
    wire: Option<Vec<u8>>,
}

fn internal_call_result() -> CallResult {
    let failure = Failure::internal(b"panic capturado no parser de perfis");
    CallResult {
        status: failure.status,
        error: failure.error,
        message: failure.message,
        required: 0,
        wire: None,
    }
}

unsafe fn execute(
    input: *const u8,
    input_length: u64,
    identity: *const TlProfileIdentityV1,
    output: *mut u8,
    output_capacity: u64,
    fill: bool,
) -> CallResult {
    if identity.is_null() {
        let failure = Failure::new(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            0,
            b"contexto de identidade nulo",
        );
        return CallResult {
            status: failure.status,
            error: failure.error,
            message: failure.message,
            required: 0,
            wire: None,
        };
    }
    if fill && output_capacity != 0 && output.is_null() {
        let failure = Failure::new(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            0,
            b"buffer de saida nulo",
        );
        return CallResult {
            status: failure.status,
            error: failure.error,
            message: failure.message,
            required: 0,
            wire: None,
        };
    }
    let bytes = match input_slice(input, input_length, LIMIT_MAX_INPUT_BYTES) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            }
        }
    };
    let context = unsafe { &*identity };
    let app_id = match identity_slice(context.app_id, context.app_id_length) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            }
        }
    };
    let app_sha256 = match identity_slice(context.app_sha256, context.app_sha256_length) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            }
        }
    };
    let app_version = match identity_slice(context.app_version, context.app_version_length) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            }
        }
    };
    let parsed = match parse_input(bytes, (app_id, app_sha256, app_version)) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            }
        }
    };
    let required = match u64::try_from(parsed.wire.len()) {
        Ok(value) => value,
        Err(_) => {
            let failure = Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_OUTPUT_TOO_LARGE,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                parsed.wire.len() as u64,
                b"saida nao cabe em u64",
            );
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                wire: None,
            };
        }
    };
    if fill && output_capacity < required {
        let failure = Failure::new(
            STATUS_BUFFER_TOO_SMALL,
            ERROR_BUFFER_TOO_SMALL,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            required,
            b"buffer de saida insuficiente",
        );
        return CallResult {
            status: failure.status,
            error: failure.error,
            message: failure.message,
            required,
            wire: None,
        };
    }
    CallResult {
        status: STATUS_SUCCESS,
        error: TlProfileErrorV1 {
            code: ERROR_NONE,
            phase: PHASE_NONE,
            input_offset: UNKNOWN_OFFSET,
            detail_value: 0,
        },
        message: Vec::new(),
        required,
        wire: Some(parsed.wire),
    }
}

unsafe fn write_message(
    status: u32,
    error: TlProfileErrorV1,
    message: &[u8],
    error_out: *mut TlProfileErrorV1,
    message_out: *mut c_char,
    message_capacity: u64,
    message_required: *mut u64,
) -> u32 {
    if error_out.is_null() || message_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    unsafe {
        *error_out = error;
    }
    let required = match message.len().checked_add(1).and_then(|value| u64::try_from(value).ok()) {
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
                .min(message.len());
            unsafe {
                std::ptr::copy_nonoverlapping(
                    message.as_ptr(),
                    message_out.cast::<u8>(),
                    writable,
                );
                *message_out.cast::<u8>().add(writable) = 0;
            }
        }
        return STATUS_BUFFER_TOO_SMALL;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), message_out.cast::<u8>(), message.len());
        *message_out.cast::<u8>().add(message.len()) = 0;
    }
    status
}

#[allow(clippy::too_many_arguments)]
unsafe fn ffi_entry(
    input: *const u8,
    input_length: u64,
    identity: *const TlProfileIdentityV1,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlProfileErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
    fill: bool,
) -> u32 {
    if output_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    unsafe {
        *output_required = 0;
    }
    let result = match catch_unwind(AssertUnwindSafe(|| unsafe {
        execute(input, input_length, identity, output, output_capacity, fill)
    })) {
        Ok(value) => value,
        Err(_) => internal_call_result(),
    };
    if !error.is_null() {
        unsafe {
            *output_required = result.required;
        }
    }
    let status = unsafe {
        write_message(
            result.status,
            result.error,
            &result.message,
            error,
            error_message,
            error_capacity,
            error_required,
        )
    };
    if status != STATUS_SUCCESS {
        return status;
    }
    if result.status == STATUS_SUCCESS && fill {
        if let Some(wire) = result.wire {
            unsafe {
                std::ptr::copy_nonoverlapping(wire.as_ptr(), output, wire.len());
            }
        }
    }
    result.status
}

#[no_mangle]
pub unsafe extern "C" fn tl_profile_parse_v1_size(
    input: *const u8,
    input_length: u64,
    identity: *const TlProfileIdentityV1,
    output_required: *mut u64,
    error: *mut TlProfileErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    unsafe {
        ffi_entry(
            input,
            input_length,
            identity,
            std::ptr::null_mut(),
            0,
            output_required,
            error,
            error_message,
            error_capacity,
            error_required,
            false,
        )
    }
}

#[no_mangle]
pub unsafe extern "C" fn tl_profile_parse_v1_fill(
    input: *const u8,
    input_length: u64,
    identity: *const TlProfileIdentityV1,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlProfileErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    unsafe {
        ffi_entry(
            input,
            input_length,
            identity,
            output,
            output_capacity,
            output_required,
            error,
            error_message,
            error_capacity,
            error_required,
            true,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    fn identity<'a>(app_id: &'a [u8], sha: &'a [u8], version: &'a [u8]) -> TlProfileIdentityV1 {
        TlProfileIdentityV1 {
            app_id: app_id.as_ptr(),
            app_id_length: app_id.len() as u64,
            app_sha256: sha.as_ptr(),
            app_sha256_length: sha.len() as u64,
            app_version: version.as_ptr(),
            app_version_length: version.len() as u64,
        }
    }

    #[test]
    fn parser_accepts_schema_three_and_serializes_deterministically() {
        let input = br#"{
          "schema":3,"app_id":"fixture","files":[{"source":"a.dat","target":"C:\\Fixture\\a.dat"}],
          "dlls":[{"module":"COMPAT","source":"compat.dll"}],
          "backend":{"kind":"proton","min_version":"11.0"}
        }"#;
        let context = identity(b"fixture", b"", b"");
        let first = parse_input(input, (b"fixture", b"", b"")).expect("first parse");
        let second = parse_input(input, (b"fixture", b"", b"")).expect("second parse");
        assert_eq!(first.wire, second.wire);
        assert_eq!(&first.wire[0..4], b"TLPR");
        assert_eq!(first.wire.len() as u64, u64::from_le_bytes(first.wire[12..20].try_into().unwrap()));
        assert_eq!(size_of::<TlProfileIdentityV1>(), 48);
        assert_eq!(align_of::<TlProfileIdentityV1>(), 8);
        let _ = context;
    }

    #[test]
    fn parser_preserves_rejection_of_schema_and_identity_errors() {
        let unknown = JsonParser::new(br#"{"schema":9,"app_id":"fixture"}"#)
            .parse()
            .expect("syntax");
        let error = validate_profile(unknown, (b"fixture", b"", b"")).unwrap_err();
        assert_eq!(error.status, STATUS_UNSUPPORTED_FORMAT);

        let input = JsonParser::new(br#"{"schema":1,"app_id":"other"}"#)
            .parse()
            .expect("syntax");
        let error = validate_profile(input, (b"fixture", b"", b"")).unwrap_err();
        assert_eq!(error.status, STATUS_MALFORMED);
        assert_eq!(error.error.code, ERROR_IDENTITY);
    }

    #[test]
    fn serializer_rejects_buffer_without_writing() {
        let input = br#"{"schema":1,"app_id":"fixture"}"#;
        let context = identity(b"fixture", b"", b"");
        let mut required = 0;
        let mut error = TlProfileErrorV1 {
            code: 0,
            phase: 0,
            input_offset: 0,
            detail_value: 0,
        };
        let mut message = [0i8; 128];
        let status = unsafe {
            tl_profile_parse_v1_fill(
                input.as_ptr(),
                input.len() as u64,
                &context,
                std::ptr::null_mut(),
                0,
                &mut required,
                &mut error,
                message.as_mut_ptr(),
                message.len() as u64,
                &mut 0,
            )
        };
        assert_eq!(status, STATUS_BUFFER_TOO_SMALL);
        assert!(required > 0);
    }
}
