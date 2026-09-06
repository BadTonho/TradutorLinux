use crate::catalog_contract::*;

use std::collections::HashMap;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;

#[derive(Clone, Debug, PartialEq, Eq)]
struct AppRecord {
    fields: [Vec<u8>; 9],
    args: Vec<Vec<u8>>,
    cpu_limit_seconds: u64,
    memory_limit_mib: u64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct CatalogModel {
    version: u32,
    apps: Vec<AppRecord>,
}

#[derive(Clone, Copy, Debug)]
struct Failure {
    status: u32,
    error: TlAppCatalogErrorV1,
    message: &'static [u8],
}

impl Failure {
    fn new(
        status: u32,
        code: u32,
        phase: u32,
        input_offset: u64,
        detail_value: u64,
        message: &'static [u8],
    ) -> Self {
        Self {
            status,
            error: TlAppCatalogErrorV1 {
                code,
                phase,
                input_offset,
                detail_value,
            },
            message,
        }
    }

    fn malformed(code: u32, phase: u32, offset: usize, message: &'static [u8]) -> Self {
        Self::new(
            STATUS_MALFORMED,
            code,
            phase,
            u64::try_from(offset).unwrap_or(UNKNOWN_OFFSET),
            0,
            message,
        )
    }

    fn limit(offset: usize, detail: u64, message: &'static [u8]) -> Self {
        Self::new(
            STATUS_MALFORMED,
            ERROR_LIMIT,
            PHASE_SCHEMA,
            u64::try_from(offset).unwrap_or(UNKNOWN_OFFSET),
            detail,
            message,
        )
    }

    fn unsupported(detail: u64, message: &'static [u8]) -> Self {
        Self::new(
            STATUS_UNSUPPORTED_FORMAT,
            ERROR_SCHEMA,
            PHASE_SCHEMA,
            UNKNOWN_OFFSET,
            detail,
            message,
        )
    }

    fn internal(message: &'static [u8]) -> Self {
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

    fn parse(mut self) -> Result<CatalogModel, Failure> {
        self.skip_whitespace();
        if !self.consume(b'{') {
            return Err(self.syntax(b"catalogo deve comecar com objeto JSON"));
        }

        let mut version = None;
        let mut apps = None;
        let mut seen_version = false;
        let mut seen_apps = false;
        self.skip_whitespace();
        if self.consume(b'}') {
            return Err(self.schema_error(b"catalogo vazio"));
        }

        loop {
            let key = self.parse_string()?;
            self.skip_whitespace();
            if !self.consume(b':') {
                return Err(self.syntax(b"faltou ':' apos campo do catalogo"));
            }
            match key.as_slice() {
                b"version" => {
                    if seen_version {
                        return Err(self.field_error(b"campo version repetido"));
                    }
                    seen_version = true;
                    version = Some(self.parse_u64()?);
                }
                b"apps" => {
                    if seen_apps {
                        return Err(self.field_error(b"campo apps repetido"));
                    }
                    seen_apps = true;
                    apps = Some(self.parse_apps()?);
                }
                _ => return Err(self.field_error(b"campo do catalogo desconhecido")),
            }
            self.skip_whitespace();
            if self.consume(b'}') {
                break;
            }
            if !self.consume(b',') {
                return Err(self.syntax(b"faltou ',' entre campos do catalogo"));
            }
            self.skip_whitespace();
            if self.peek(b'}') {
                return Err(self.syntax(b"virgula final nao permitida"));
            }
        }

        self.skip_whitespace();
        if self.position != self.input.len() {
            return Err(self.syntax(b"conteudo apos o catalogo"));
        }
        let version = version.ok_or_else(|| self.schema_error(b"version e obrigatorio"))?;
        let version =
            u32::try_from(version).map_err(|_| self.schema_error(b"version excede u32"))?;
        if version != 1 {
            return Err(Failure::unsupported(
                u64::from(version),
                b"versao do catalogo nao suportada",
            ));
        }
        let apps = apps.ok_or_else(|| self.schema_error(b"apps e obrigatorio"))?;
        Ok(CatalogModel { version, apps })
    }

    fn parse_apps(&mut self) -> Result<Vec<AppRecord>, Failure> {
        self.skip_whitespace();
        if !self.consume(b'[') {
            return Err(self.schema_error(b"apps deve ser um array"));
        }
        let mut apps = Vec::new();
        self.skip_whitespace();
        if self.consume(b']') {
            return Ok(apps);
        }
        loop {
            if u64::try_from(apps.len()).unwrap_or(u64::MAX) >= LIMIT_MAX_APPS {
                return Err(Failure::limit(
                    self.position,
                    LIMIT_MAX_APPS,
                    b"quantidade de aplicativos excede o limite",
                ));
            }
            let app = self.parse_app()?;
            if apps
                .iter()
                .any(|existing: &AppRecord| existing.fields[0] == app.fields[0])
            {
                return Err(Failure::new(
                    STATUS_MALFORMED,
                    ERROR_VALUE,
                    PHASE_SCHEMA,
                    u64::try_from(self.position).unwrap_or(UNKNOWN_OFFSET),
                    0,
                    b"id de aplicativo repetido",
                ));
            }
            apps.push(app);
            self.skip_whitespace();
            if self.consume(b']') {
                return Ok(apps);
            }
            if !self.consume(b',') {
                return Err(self.syntax(b"apps sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b']') {
                return Err(self.syntax(b"virgula final em apps"));
            }
        }
    }

    fn parse_app(&mut self) -> Result<AppRecord, Failure> {
        self.skip_whitespace();
        if !self.consume(b'{') {
            return Err(self.schema_error(b"entrada de aplicativo invalida"));
        }
        let mut app = AppRecord {
            fields: std::array::from_fn(|_| Vec::new()),
            args: Vec::new(),
            cpu_limit_seconds: 0,
            memory_limit_mib: 0,
        };
        let mut seen = [false; 12];
        self.skip_whitespace();
        if self.consume(b'}') {
            return Err(self.schema_error(b"entrada de aplicativo vazia"));
        }

        loop {
            let key = self.parse_string()?;
            self.skip_whitespace();
            if !self.consume(b':') {
                return Err(self.syntax(b"entrada de aplicativo sem ':'"));
            }
            let slot = match key.as_slice() {
                b"id" => 0,
                b"name" => 1,
                b"executable_path" => 2,
                b"prefix_path" => 3,
                b"icon_path" => 4,
                b"working_directory" => 5,
                b"app_sha256" => 6,
                b"app_version" => 7,
                b"created_at" => 8,
                b"cpu_limit_seconds" => 9,
                b"memory_limit_mib" => 10,
                b"args" => 11,
                _ => return Err(self.field_error(b"campo de aplicativo desconhecido")),
            };
            if seen[slot] {
                return Err(self.field_error(b"campo de aplicativo repetido"));
            }
            seen[slot] = true;
            if slot <= 8 {
                app.fields[slot] = self.parse_string_value()?;
            } else if slot == 9 {
                app.cpu_limit_seconds = self.parse_u64()?;
            } else if slot == 10 {
                app.memory_limit_mib = self.parse_u64()?;
            } else {
                app.args = self.parse_args()?;
            }

            self.skip_whitespace();
            if self.consume(b'}') {
                break;
            }
            if !self.consume(b',') {
                return Err(self.syntax(b"entrada de aplicativo sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b'}') {
                return Err(self.syntax(b"virgula final no aplicativo"));
            }
        }

        if !seen[0] || !seen[2] {
            return Err(self.schema_error(b"id e executable_path sao obrigatorios"));
        }
        if !safe_app_id(&app.fields[0]) {
            return Err(self.value_error(b"id de aplicativo invalido"));
        }
        if app.fields[2].is_empty() {
            return Err(self.value_error(b"executable_path nao pode ser vazio"));
        }
        Ok(app)
    }

    fn parse_args(&mut self) -> Result<Vec<Vec<u8>>, Failure> {
        self.skip_whitespace();
        if !self.consume(b'[') {
            return Err(self.schema_error(b"args deve ser um array"));
        }
        let mut args = Vec::new();
        self.skip_whitespace();
        if self.consume(b']') {
            return Ok(args);
        }
        loop {
            if u64::try_from(args.len()).unwrap_or(u64::MAX) >= LIMIT_MAX_ARGS {
                return Err(Failure::limit(
                    self.position,
                    LIMIT_MAX_ARGS,
                    b"quantidade de argumentos excede o limite",
                ));
            }
            args.push(self.parse_string_value()?);
            self.skip_whitespace();
            if self.consume(b']') {
                return Ok(args);
            }
            if !self.consume(b',') {
                return Err(self.syntax(b"args sem ','"));
            }
            self.skip_whitespace();
            if self.peek(b']') {
                return Err(self.syntax(b"virgula final em args"));
            }
        }
    }

    fn parse_string_value(&mut self) -> Result<Vec<u8>, Failure> {
        let value = self.parse_string()?;
        if u64::try_from(value.len()).unwrap_or(u64::MAX) > LIMIT_MAX_STRING_BYTES {
            return Err(Failure::limit(
                self.position,
                LIMIT_MAX_STRING_BYTES,
                b"string excede o limite",
            ));
        }
        Ok(value)
    }

    fn parse_string(&mut self) -> Result<Vec<u8>, Failure> {
        self.skip_whitespace();
        if !self.consume(b'"') {
            return Err(self.syntax(b"string JSON invalida"));
        }
        let mut value = Vec::new();
        loop {
            if self.position >= self.input.len() {
                return Err(self.syntax(b"string JSON sem fechamento"));
            }
            let byte = self.input[self.position];
            self.position += 1;
            match byte {
                b'"' => return Ok(value),
                b'\\' => {
                    if self.position >= self.input.len() {
                        return Err(self.syntax(b"escape JSON truncado"));
                    }
                    let escaped = self.input[self.position];
                    self.position += 1;
                    match escaped {
                        b'"' => value.push(b'"'),
                        b'\\' => value.push(b'\\'),
                        b'/' => value.push(b'/'),
                        b'b' => value.push(8),
                        b'f' => value.push(12),
                        b'n' => value.push(b'\n'),
                        b'r' => value.push(b'\r'),
                        b't' => value.push(b'\t'),
                        b'u' => self.parse_unicode_escape(&mut value)?,
                        _ => return Err(self.syntax(b"escape JSON nao suportado")),
                    }
                }
                0..=0x1f => return Err(self.syntax(b"controle em string JSON")),
                _ => value.push(byte),
            }
        }
    }

    fn parse_unicode_escape(&mut self, output: &mut Vec<u8>) -> Result<(), Failure> {
        let high = self.parse_hex_code_unit()?;
        if (0xd800..=0xdbff).contains(&high) {
            if self.position + 6 > self.input.len()
                || self.input[self.position] != b'\\'
                || self.input[self.position + 1] != b'u'
            {
                return Err(self.syntax(b"surrogate Unicode sem par"));
            }
            self.position += 2;
            let low = self.parse_hex_code_unit()?;
            if !(0xdc00..=0xdfff).contains(&low) {
                return Err(self.syntax(b"surrogate Unicode invalido"));
            }
            let codepoint =
                0x1_0000 + ((u32::from(high) - 0xd800) << 10) + (u32::from(low) - 0xdc00);
            append_utf8(output, codepoint);
            return Ok(());
        }
        if (0xdc00..=0xdfff).contains(&high) {
            return Err(self.syntax(b"surrogate Unicode sem par superior"));
        }
        append_utf8(output, u32::from(high));
        Ok(())
    }

    fn parse_hex_code_unit(&mut self) -> Result<u16, Failure> {
        if self.position + 4 > self.input.len() {
            return Err(self.syntax(b"escape Unicode truncado"));
        }
        let mut value = 0u16;
        for _ in 0..4 {
            let digit = hex_digit(self.input[self.position])
                .ok_or_else(|| self.syntax(b"escape Unicode invalido"))?;
            value = (value << 4) | u16::from(digit);
            self.position += 1;
        }
        Ok(value)
    }

    fn parse_u64(&mut self) -> Result<u64, Failure> {
        self.skip_whitespace();
        if self.position >= self.input.len() || !self.input[self.position].is_ascii_digit() {
            return Err(self.schema_error(b"numero JSON invalido"));
        }
        let start = self.position;
        let mut value = 0u64;
        if self.input[self.position] == b'0' {
            self.position += 1;
            if self.position < self.input.len() && self.input[self.position].is_ascii_digit() {
                return Err(self.schema_error(b"numero JSON com zero inicial"));
            }
        } else {
            while self.position < self.input.len() && self.input[self.position].is_ascii_digit() {
                let digit = u64::from(self.input[self.position] - b'0');
                value = value
                    .checked_mul(10)
                    .and_then(|current| current.checked_add(digit))
                    .ok_or_else(|| self.schema_error(b"numero JSON excede u64"))?;
                self.position += 1;
            }
        }
        if self.position == start {
            return Err(self.schema_error(b"numero JSON invalido"));
        }
        Ok(value)
    }

    fn skip_whitespace(&mut self) {
        while self.position < self.input.len()
            && matches!(self.input[self.position], b' ' | b'\t' | b'\n' | b'\r')
        {
            self.position += 1;
        }
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

    fn syntax(&self, message: &'static [u8]) -> Failure {
        Failure::malformed(ERROR_JSON_SYNTAX, PHASE_JSON, self.position, message)
    }

    fn field_error(&self, message: &'static [u8]) -> Failure {
        Failure::malformed(ERROR_JSON_FIELD, PHASE_SCHEMA, self.position, message)
    }

    fn schema_error(&self, message: &'static [u8]) -> Failure {
        Failure::malformed(ERROR_SCHEMA, PHASE_SCHEMA, self.position, message)
    }

    fn value_error(&self, message: &'static [u8]) -> Failure {
        Failure::malformed(ERROR_VALUE, PHASE_SCHEMA, self.position, message)
    }
}

fn hex_digit(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        b'A'..=b'F' => Some(value - b'A' + 10),
        _ => None,
    }
}

fn append_utf8(output: &mut Vec<u8>, codepoint: u32) {
    if codepoint <= 0x7f {
        output.push(codepoint as u8);
    } else if codepoint <= 0x7ff {
        output.push(0xc0 | ((codepoint >> 6) as u8));
        output.push(0x80 | ((codepoint & 0x3f) as u8));
    } else if codepoint <= 0xffff {
        output.push(0xe0 | ((codepoint >> 12) as u8));
        output.push(0x80 | (((codepoint >> 6) & 0x3f) as u8));
        output.push(0x80 | ((codepoint & 0x3f) as u8));
    } else {
        output.push(0xf0 | ((codepoint >> 18) as u8));
        output.push(0x80 | (((codepoint >> 12) & 0x3f) as u8));
        output.push(0x80 | (((codepoint >> 6) & 0x3f) as u8));
        output.push(0x80 | ((codepoint & 0x3f) as u8));
    }
}

fn safe_app_id(value: &[u8]) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && value != b"."
        && value != b".."
        && value
            .iter()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(*byte, b'-' | b'_' | b'.'))
}

struct StringTable {
    values: Vec<Vec<u8>>,
    indices: HashMap<Vec<u8>, usize>,
    total_bytes: u64,
}

impl StringTable {
    fn new() -> Self {
        Self {
            values: Vec::new(),
            indices: HashMap::new(),
            total_bytes: 0,
        }
    }

    fn intern(&mut self, value: &[u8]) -> Result<usize, Failure> {
        if value.is_empty() {
            return Ok(usize::MAX);
        }
        if let Some(index) = self.indices.get(value) {
            return Ok(*index);
        }
        if u64::try_from(self.values.len()).unwrap_or(u64::MAX) >= LIMIT_MAX_STRINGS {
            return Err(Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_LIMIT,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                LIMIT_MAX_STRINGS,
                b"quantidade de strings excede o limite",
            ));
        }
        let length = u64::try_from(value.len()).map_err(|_| {
            Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_LIMIT,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                LIMIT_MAX_STRING_BYTES,
                b"tamanho de string nao cabe em u64",
            )
        })?;
        let total = self.total_bytes.checked_add(length).ok_or_else(|| {
            Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_LIMIT,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                LIMIT_MAX_DECODED_STRING_BYTES,
                b"soma de strings excede u64",
            )
        })?;
        if total > LIMIT_MAX_DECODED_STRING_BYTES {
            return Err(Failure::new(
                STATUS_OUTPUT_TOO_LARGE,
                ERROR_LIMIT,
                PHASE_SERIALIZE,
                UNKNOWN_OFFSET,
                LIMIT_MAX_DECODED_STRING_BYTES,
                b"bytes de strings excedem o limite",
            ));
        }
        let index = self.values.len();
        let owned = value.to_vec();
        self.values.push(owned.clone());
        self.indices.insert(owned, index);
        self.total_bytes = total;
        Ok(index)
    }
}

#[derive(Clone, Copy)]
struct StringRef {
    offset: u64,
    length: u64,
}

struct WirePlan {
    total_size: u64,
    info_offset: u64,
    apps_offset: u64,
    args_offset: u64,
    strings_offset: u64,
    app_refs: Vec<[StringRef; 9]>,
    arg_refs: Vec<StringRef>,
    string_refs: Vec<StringRef>,
    strings: StringTable,
}

fn align8(value: u64) -> Option<u64> {
    value.checked_add(7).map(|aligned| aligned & !7)
}

fn plan_wire(model: &CatalogModel) -> Result<WirePlan, Failure> {
    let mut strings = StringTable::new();
    let mut app_refs = Vec::with_capacity(model.apps.len());
    let mut arg_refs = Vec::new();

    for app in &model.apps {
        let mut refs = [usize::MAX; 9];
        for (index, slot) in refs.iter_mut().enumerate() {
            *slot = strings.intern(&app.fields[index])?;
        }
        app_refs.push(refs);
    }
    for app in &model.apps {
        for arg in &app.args {
            arg_refs.push(strings.intern(arg)?);
        }
    }

    let mut total = u64::try_from(WIRE_HEADER_SIZE + INFO_STRIDE)
        .map_err(|_| Failure::internal(b"tamanho inicial TLAC invalido"))?;
    let info_offset = u64::try_from(WIRE_HEADER_SIZE).unwrap_or(0);

    let apps_offset = if model.apps.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLAC"))?;
        let offset = total;
        let bytes = u64::try_from(model.apps.len())
            .ok()
            .and_then(|count| count.checked_mul(APP_STRIDE as u64))
            .ok_or_else(|| Failure::internal(b"overflow da tabela apps TLAC"))?;
        total = total
            .checked_add(bytes)
            .ok_or_else(|| Failure::internal(b"overflow da tabela apps TLAC"))?;
        offset
    };
    let args_offset = if arg_refs.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLAC"))?;
        let offset = total;
        let bytes = u64::try_from(arg_refs.len())
            .ok()
            .and_then(|count| count.checked_mul(ARG_STRIDE as u64))
            .ok_or_else(|| Failure::internal(b"overflow da tabela args TLAC"))?;
        total = total
            .checked_add(bytes)
            .ok_or_else(|| Failure::internal(b"overflow da tabela args TLAC"))?;
        offset
    };

    let strings_offset = if strings.values.is_empty() {
        0
    } else {
        total = align8(total).ok_or_else(|| Failure::internal(b"overflow de alinhamento TLAC"))?;
        let offset = total;
        for value in &strings.values {
            let record = (STRING_RECORD_HEADER_SIZE as u64)
                .checked_add(
                    u64::try_from(value.len())
                        .map_err(|_| Failure::internal(b"tamanho de string TLAC invalido"))?,
                )
                .ok_or_else(|| Failure::internal(b"overflow de registro string TLAC"))?;
            total = align8(
                total
                    .checked_add(record)
                    .ok_or_else(|| Failure::internal(b"overflow da tabela strings TLAC"))?,
            )
            .ok_or_else(|| Failure::internal(b"overflow de alinhamento TLAC"))?;
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
            b"saida TLAC excede o limite",
        ));
    }
    let mut string_refs = Vec::with_capacity(strings.values.len());
    if strings_offset != 0 {
        let mut cursor = strings_offset;
        for value in &strings.values {
            let payload = cursor
                .checked_add(STRING_RECORD_HEADER_SIZE as u64)
                .ok_or_else(|| Failure::internal(b"offset de string TLAC invalido"))?;
            string_refs.push(StringRef {
                offset: payload,
                length: u64::try_from(value.len())
                    .map_err(|_| Failure::internal(b"length de string TLAC invalido"))?,
            });
            let record = (STRING_RECORD_HEADER_SIZE as u64)
                .checked_add(u64::try_from(value.len()).unwrap_or(u64::MAX))
                .ok_or_else(|| Failure::internal(b"registro de string TLAC invalido"))?;
            cursor = align8(
                cursor
                    .checked_add(record)
                    .ok_or_else(|| Failure::internal(b"cursor de string TLAC invalido"))?,
            )
            .ok_or_else(|| Failure::internal(b"alinhamento de string TLAC invalido"))?;
        }
    }
    let map_ref = |index: usize| -> StringRef {
        if index == usize::MAX {
            StringRef {
                offset: 0,
                length: 0,
            }
        } else {
            string_refs[index]
        }
    };
    let app_refs = app_refs.into_iter().map(|refs| refs.map(map_ref)).collect();
    let arg_refs = arg_refs.into_iter().map(map_ref).collect();

    Ok(WirePlan {
        total_size: total,
        info_offset,
        apps_offset,
        args_offset,
        strings_offset,
        app_refs,
        arg_refs,
        string_refs,
        strings,
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

fn put_descriptor(
    output: &mut [u8],
    table: usize,
    offset: u64,
    count: u64,
    stride: u32,
    flags: u32,
) {
    let base = WIRE_DESCRIPTOR_OFFSET + table * WIRE_DESCRIPTOR_SIZE;
    put_u64(output, base, offset);
    put_u64(output, base + 8, count);
    put_u32(output, base + 16, stride);
    put_u32(output, base + 20, flags);
}

fn put_ref(output: &mut [u8], offset: usize, value: StringRef) {
    put_u64(output, offset, value.offset);
    put_u64(output, offset + 8, value.length);
}

fn write_wire(model: &CatalogModel, plan: &WirePlan) -> Result<Vec<u8>, Failure> {
    let output_length = usize::try_from(plan.total_size)
        .map_err(|_| Failure::internal(b"saida TLAC nao cabe no host"))?;
    let mut output = vec![0u8; output_length];
    output[0..4].copy_from_slice(&WIRE_MAGIC);
    put_u16(&mut output, 4, WIRE_MAJOR);
    put_u16(&mut output, 6, WIRE_MINOR);
    put_u32(&mut output, 8, WIRE_HEADER_SIZE as u32);
    put_u64(&mut output, 12, plan.total_size);
    put_u32(&mut output, 20, WIRE_TABLE_COUNT as u32);
    put_u32(&mut output, 24, 0);
    put_u32(&mut output, 28, 0);
    put_descriptor(
        &mut output,
        TABLE_INFO,
        plan.info_offset,
        1,
        INFO_STRIDE as u32,
        0,
    );
    put_descriptor(
        &mut output,
        TABLE_APPS,
        plan.apps_offset,
        u64::try_from(model.apps.len())
            .map_err(|_| Failure::internal(b"contagem apps invalida"))?,
        APP_STRIDE as u32,
        0,
    );
    put_descriptor(
        &mut output,
        TABLE_ARGS,
        plan.args_offset,
        u64::try_from(plan.arg_refs.len())
            .map_err(|_| Failure::internal(b"contagem args invalida"))?,
        ARG_STRIDE as u32,
        0,
    );
    put_descriptor(
        &mut output,
        TABLE_STRINGS,
        plan.strings_offset,
        u64::try_from(plan.strings.values.len())
            .map_err(|_| Failure::internal(b"contagem strings invalida"))?,
        0,
        WIRE_VARIABLE_RECORDS,
    );

    let info =
        usize::try_from(plan.info_offset).map_err(|_| Failure::internal(b"info invalido"))?;
    put_u32(&mut output, info + INFO_VERSION_OFFSET, model.version);
    put_u32(&mut output, info + INFO_FLAGS_OFFSET, 0);
    put_u64(
        &mut output,
        info + INFO_APP_COUNT_OFFSET,
        u64::try_from(model.apps.len())
            .map_err(|_| Failure::internal(b"contagem apps invalida"))?,
    );
    put_u64(
        &mut output,
        info + INFO_ARG_COUNT_OFFSET,
        u64::try_from(plan.arg_refs.len())
            .map_err(|_| Failure::internal(b"contagem args invalida"))?,
    );

    let mut arg_index = 0usize;
    for (index, app) in model.apps.iter().enumerate() {
        let record = usize::try_from(plan.apps_offset)
            .ok()
            .and_then(|offset| offset.checked_add(index.checked_mul(APP_STRIDE)?))
            .ok_or_else(|| Failure::internal(b"registro apps invalido"))?;
        for (field, value) in plan.app_refs[index].iter().enumerate() {
            put_ref(&mut output, record + field * STRING_REF_SIZE, *value);
        }
        let app_arg_index = if app.args.is_empty() {
            0
        } else {
            u64::try_from(arg_index).map_err(|_| Failure::internal(b"indice args invalido"))?
        };
        put_u64(&mut output, record + APP_ARGS_INDEX_OFFSET, app_arg_index);
        put_u64(
            &mut output,
            record + APP_ARGS_COUNT_OFFSET,
            u64::try_from(app.args.len())
                .map_err(|_| Failure::internal(b"contagem args invalida"))?,
        );
        put_u64(
            &mut output,
            record + APP_CPU_LIMIT_OFFSET,
            app.cpu_limit_seconds,
        );
        put_u64(
            &mut output,
            record + APP_MEMORY_LIMIT_OFFSET,
            app.memory_limit_mib,
        );
        arg_index = arg_index
            .checked_add(app.args.len())
            .ok_or_else(|| Failure::internal(b"indice args excede usize"))?;
    }
    for (index, value) in plan.arg_refs.iter().enumerate() {
        let record = usize::try_from(plan.args_offset)
            .ok()
            .and_then(|offset| offset.checked_add(index.checked_mul(ARG_STRIDE)?))
            .ok_or_else(|| Failure::internal(b"registro args invalido"))?;
        put_ref(&mut output, record, *value);
    }

    if plan.strings_offset != 0 {
        let mut cursor = usize::try_from(plan.strings_offset)
            .map_err(|_| Failure::internal(b"offset strings invalido"))?;
        for (index, value) in plan.strings.values.iter().enumerate() {
            put_u32(
                &mut output,
                cursor,
                u32::try_from(value.len())
                    .map_err(|_| Failure::internal(b"length strings excede u32"))?,
            );
            put_u32(&mut output, cursor + 4, 0);
            let payload = cursor + STRING_RECORD_HEADER_SIZE;
            output[payload..payload + value.len()].copy_from_slice(value);
            if plan.string_refs[index].offset != u64::try_from(payload).unwrap_or(u64::MAX) {
                return Err(Failure::internal(b"referencia strings inconsistente"));
            }
            let record = STRING_RECORD_HEADER_SIZE
                .checked_add(value.len())
                .ok_or_else(|| Failure::internal(b"registro strings excede usize"))?;
            cursor = usize::try_from(
                align8(
                    u64::try_from(cursor)
                        .ok()
                        .and_then(|value| value.checked_add(u64::try_from(record).ok()?))
                        .ok_or_else(|| Failure::internal(b"cursor strings excede u64"))?,
                )
                .ok_or_else(|| Failure::internal(b"cursor strings invalido"))?,
            )
            .map_err(|_| Failure::internal(b"cursor strings nao cabe no host"))?;
        }
    }
    Ok(output)
}

fn parse_and_serialize(input: &[u8]) -> Result<Vec<u8>, Failure> {
    let model = JsonParser::new(input).parse()?;
    let plan = plan_wire(&model)?;
    write_wire(&model, &plan)
}

unsafe fn input_slice<'a>(input: *const u8, input_length: u64) -> Result<&'a [u8], Failure> {
    if input_length > LIMIT_MAX_INPUT_BYTES {
        return Err(Failure::new(
            STATUS_INPUT_TOO_LARGE,
            ERROR_INPUT_TOO_LARGE,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            b"entrada do catalogo excede o limite",
        ));
    }
    if input_length != 0 && input.is_null() {
        return Err(Failure::new(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_INPUT,
            UNKNOWN_OFFSET,
            input_length,
            b"ponteiro de entrada e nulo",
        ));
    }
    let length = usize::try_from(input_length).map_err(|_| {
        Failure::new(
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
        Ok(slice::from_raw_parts(input, length))
    }
}

#[allow(clippy::too_many_arguments)]
unsafe fn write_error(
    status: u32,
    error_value: TlAppCatalogErrorV1,
    message: &[u8],
    error: *mut TlAppCatalogErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    if error_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if !error.is_null() {
        *error = error_value;
    }
    let required = match message
        .len()
        .checked_add(1)
        .and_then(|value| u64::try_from(value).ok())
    {
        Some(value) => value,
        None => return STATUS_INTERNAL,
    };
    *error_required = required;
    if error_capacity != 0 && error_message.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if error_capacity < required {
        if error_capacity != 0 {
            let writable = usize::try_from(error_capacity - 1)
                .unwrap_or(0)
                .min(message.len());
            std::ptr::copy_nonoverlapping(message.as_ptr(), error_message.cast::<u8>(), writable);
            *error_message.cast::<u8>().add(writable) = 0;
        }
        return STATUS_BUFFER_TOO_SMALL;
    }
    std::ptr::copy_nonoverlapping(message.as_ptr(), error_message.cast::<u8>(), message.len());
    *error_message.cast::<u8>().add(message.len()) = 0;
    status
}

struct CallResult {
    status: u32,
    error: TlAppCatalogErrorV1,
    message: &'static [u8],
    required: u64,
    output: Option<Vec<u8>>,
}

fn internal_result() -> CallResult {
    let failure = Failure::internal(b"panic interno capturado no parser TLAC");
    CallResult {
        status: failure.status,
        error: failure.error,
        message: failure.message,
        required: 0,
        output: None,
    }
}

unsafe fn execute(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    fill: bool,
) -> CallResult {
    let bytes = match input_slice(input, input_length) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                output: None,
            }
        }
    };
    let wire = match parse_and_serialize(bytes) {
        Ok(value) => value,
        Err(failure) => {
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                output: None,
            }
        }
    };
    let required = match u64::try_from(wire.len()) {
        Ok(value) => value,
        Err(_) => {
            let failure = Failure::internal(b"saida TLAC nao cabe em u64");
            return CallResult {
                status: failure.status,
                error: failure.error,
                message: failure.message,
                required: 0,
                output: None,
            };
        }
    };
    if !fill {
        return CallResult {
            status: STATUS_SUCCESS,
            error: TlAppCatalogErrorV1 {
                code: ERROR_NONE,
                phase: PHASE_NONE,
                input_offset: UNKNOWN_OFFSET,
                detail_value: 0,
            },
            message: b"",
            required,
            output: None,
        };
    }
    if output_capacity < required {
        let failure = Failure::new(
            STATUS_BUFFER_TOO_SMALL,
            ERROR_BUFFER_TOO_SMALL,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            required,
            b"buffer de saida TLAC insuficiente",
        );
        return CallResult {
            status: failure.status,
            error: failure.error,
            message: failure.message,
            required,
            output: None,
        };
    }
    if output.is_null() {
        let failure = Failure::new(
            STATUS_INVALID_ARGUMENT,
            ERROR_INVALID_ARGUMENT,
            PHASE_SERIALIZE,
            UNKNOWN_OFFSET,
            required,
            b"ponteiro de saida e nulo",
        );
        return CallResult {
            status: failure.status,
            error: failure.error,
            message: failure.message,
            required,
            output: None,
        };
    }
    CallResult {
        status: STATUS_SUCCESS,
        error: TlAppCatalogErrorV1 {
            code: ERROR_NONE,
            phase: PHASE_NONE,
            input_offset: UNKNOWN_OFFSET,
            detail_value: 0,
        },
        message: b"",
        required,
        output: Some(wire),
    }
}

#[allow(clippy::too_many_arguments)]
unsafe fn ffi_entry(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlAppCatalogErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
    fill: bool,
) -> u32 {
    if output_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    *output_required = 0;
    let result = match catch_unwind(AssertUnwindSafe(|| unsafe {
        execute(input, input_length, output, output_capacity, fill)
    })) {
        Ok(value) => value,
        Err(_) => internal_result(),
    };
    *output_required = result.required;
    let written_status = write_error(
        result.status,
        result.error,
        result.message,
        error,
        error_message,
        error_capacity,
        error_required,
    );
    if written_status != STATUS_SUCCESS {
        return written_status;
    }
    if result.status == STATUS_SUCCESS && fill {
        if let Some(wire) = result.output {
            std::ptr::copy_nonoverlapping(wire.as_ptr(), output, wire.len());
        }
    }
    result.status
}

#[no_mangle]
pub unsafe extern "C" fn tl_app_catalog_parse_v1_size(
    input: *const u8,
    input_length: u64,
    output_required: *mut u64,
    error: *mut TlAppCatalogErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    ffi_entry(
        input,
        input_length,
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

#[no_mangle]
pub unsafe extern "C" fn tl_app_catalog_parse_v1_fill(
    input: *const u8,
    input_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_required: *mut u64,
    error: *mut TlAppCatalogErrorV1,
    error_message: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    ffi_entry(
        input,
        input_length,
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

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_model(input: &[u8]) -> Result<CatalogModel, Failure> {
        JsonParser::new(input).parse()
    }

    fn valid_catalog() -> &'static [u8] {
        br#"{"version":1,"apps":[{"id":"editor","name":"Editor","executable_path":"/tmp/editor.exe","args":["--safe","--name=\u00e1"],"cpu_limit_seconds":12,"memory_limit_mib":256}]}"#
    }

    #[test]
    fn parses_catalog_and_preserves_bytes() {
        let model = parse_model(valid_catalog()).expect("catalogo valido");
        assert_eq!(model.version, 1);
        assert_eq!(model.apps.len(), 1);
        assert_eq!(model.apps[0].fields[0], b"editor");
        assert_eq!(model.apps[0].fields[2], b"/tmp/editor.exe");
        assert_eq!(model.apps[0].args[1], "--name=á".as_bytes());
        assert_eq!(model.apps[0].cpu_limit_seconds, 12);
    }

    #[test]
    fn preserves_non_utf8_bytes_and_rejects_number_overflow() {
        let mut input =
            br#"{"version":1,"apps":[{"id":"app","executable_path":"x","name":""}]}"#.to_vec();
        let marker = input
            .windows(2)
            .position(|window| window == b"\"\"")
            .expect("string vazia")
            + 1;
        input.splice(marker..marker, [0x80]);
        let model = parse_model(&input).expect("byte nao UTF-8 deve ser preservado");
        assert_eq!(model.apps[0].fields[1], [b'\x80']);

        let overflow = br#"{"version":1,"apps":[{"id":"app","executable_path":"x","cpu_limit_seconds":18446744073709551616}]}"#;
        assert_eq!(parse_model(overflow).unwrap_err().status, STATUS_MALFORMED);
    }

    #[test]
    fn rejects_unknown_duplicate_and_trailing_fields() {
        for input in [
            br#"{"version":1,"apps":[],"other":1}"#.as_slice(),
            br#"{"version":1,"version":1,"apps":[]}"#.as_slice(),
            br#"{"version":1,"apps":[],} trailing"#.as_slice(),
        ] {
            assert_eq!(parse_model(input).unwrap_err().status, STATUS_MALFORMED);
        }
    }

    #[test]
    fn validates_unicode_escapes_and_ids() {
        let input =
            br#"{"version":1,"apps":[{"id":"app","executable_path":"x","name":"\uD83D\uDE00"}]}"#;
        let model = parse_model(input).expect("escape valido");
        assert_eq!(model.apps[0].fields[1], "😀".as_bytes());
        let invalid_inputs: &[&[u8]] = &[
            br#"{"version":1,"apps":[{"id":"../x","executable_path":"x"}]}"#,
            br#"{"version":1,"apps":[{"id":"app","executable_path":"x","name":"\uD800"}]}"#,
        ];
        for input in invalid_inputs {
            assert_eq!(parse_model(input).unwrap_err().status, STATUS_MALFORMED);
        }
    }

    #[test]
    fn rejects_unknown_version_and_duplicate_ids() {
        let unknown = parse_model(br#"{"version":2,"apps":[]}"#).unwrap_err();
        assert_eq!(unknown.status, STATUS_UNSUPPORTED_FORMAT);
        let duplicate = parse_model(
            br#"{"version":1,"apps":[{"id":"a","executable_path":"x"},{"id":"a","executable_path":"y"}]}"#,
        )
        .unwrap_err();
        assert_eq!(duplicate.error.code, ERROR_VALUE);
    }

    #[test]
    fn serializer_is_aligned_and_deduplicates_strings() {
        let model = parse_model(
            br#"{"version":1,"apps":[{"id":"a","executable_path":"same","name":"same"},{"id":"b","executable_path":"same"}]}"#,
        )
        .expect("catalogo valido");
        let plan = plan_wire(&model).expect("plano valido");
        assert_eq!(
            plan.strings.values,
            vec![b"a".to_vec(), b"same".to_vec(), b"b".to_vec()]
        );
        assert_eq!(plan.info_offset % 8, 0);
        assert_eq!(plan.apps_offset % 8, 0);
        assert_eq!(plan.strings_offset % 8, 0);
        let wire = write_wire(&model, &plan).expect("wire valido");
        assert_eq!(&wire[0..4], b"TLAC");
        assert_eq!(
            u64::from_le_bytes(wire[12..20].try_into().unwrap()),
            wire.len() as u64
        );
        let info_reserved = plan.info_offset as usize + INFO_RESERVED_OFFSET;
        assert!(wire[info_reserved..info_reserved + 8]
            .iter()
            .all(|byte| *byte == 0));
        let app_reserved = plan.apps_offset as usize + APP_RESERVED_OFFSET;
        assert!(wire[app_reserved..app_reserved + 16]
            .iter()
            .all(|byte| *byte == 0));
    }

    #[test]
    fn fill_does_not_write_when_buffer_is_small() {
        let input = valid_catalog();
        let mut required = 0;
        let mut error = TlAppCatalogErrorV1 {
            code: 0,
            phase: 0,
            input_offset: 0,
            detail_value: 0,
        };
        let mut message = [0u8; 128];
        let mut error_required = 0;
        let status = unsafe {
            tl_app_catalog_parse_v1_size(
                input.as_ptr(),
                input.len() as u64,
                &mut required,
                &mut error,
                message.as_mut_ptr().cast(),
                message.len() as u64,
                &mut error_required,
            )
        };
        assert_eq!(status, STATUS_SUCCESS);
        let mut output = vec![0xa5u8; required as usize + 16];
        let status = unsafe {
            tl_app_catalog_parse_v1_fill(
                input.as_ptr(),
                input.len() as u64,
                output[8..].as_mut_ptr(),
                required - 1,
                &mut required,
                &mut error,
                message.as_mut_ptr().cast(),
                message.len() as u64,
                &mut error_required,
            )
        };
        assert_eq!(status, STATUS_BUFFER_TOO_SMALL);
        assert!(output.iter().all(|byte| *byte == 0xa5));

        let status = unsafe {
            tl_app_catalog_parse_v1_fill(
                input.as_ptr(),
                input.len() as u64,
                output[8..].as_mut_ptr(),
                required + 8,
                &mut required,
                &mut error,
                message.as_mut_ptr().cast(),
                message.len() as u64,
                &mut error_required,
            )
        };
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(&output[8..12], b"TLAC");
        assert_eq!(message[0], 0);
    }

    #[test]
    fn empty_input_and_null_output_are_controlled() {
        let mut required = 0;
        let mut error = TlAppCatalogErrorV1 {
            code: 0,
            phase: 0,
            input_offset: 0,
            detail_value: 0,
        };
        let mut message = [0u8; 128];
        let mut error_required = 0;
        let status = unsafe {
            tl_app_catalog_parse_v1_size(
                std::ptr::null(),
                0,
                &mut required,
                &mut error,
                message.as_mut_ptr().cast(),
                message.len() as u64,
                &mut error_required,
            )
        };
        assert_eq!(status, STATUS_MALFORMED);
        let status = unsafe {
            tl_app_catalog_parse_v1_fill(
                valid_catalog().as_ptr(),
                valid_catalog().len() as u64,
                std::ptr::null_mut(),
                u64::MAX,
                &mut required,
                &mut error,
                message.as_mut_ptr().cast(),
                message.len() as u64,
                &mut error_required,
            )
        };
        assert_eq!(status, STATUS_INVALID_ARGUMENT);
    }
}
