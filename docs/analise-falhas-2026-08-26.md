# Análise completa do sistema — TradutorLinux — 2026-08-26

**Branch verificado:** `d0e6839` | **Fase ROADMAP.md:22:** `Fase 13 — compatibilidade ampla por portfólio` | **README.md:7 desatualizado (cita Fase 8)** |
**Verificação:** leitura direta de `src/`, `include/`, `docs/`, `CMakeLists.txt`/`CMakePresets.json`; sem build completo (regra `AGENTS.md` — máquina limitada).

## Resumo executivo

- 4 achados **CRÍTICOS** com quebra de isolamento/host compromise (path traversal, `Z: -> /`, truncamento de thunk, data race global).
- 8 achados **ALTA** (DoS, diagnóstico quebrado, isolamento filho frágil, validação ausente).
- ~10 **MÉDIA/BAIXA** (vazamentos, ABI, divergência de docs, vazamento de ASLR, cache stale).

---

## CRÍTICO — quebra de isolamento / execução arbitrária no host

### C1. Path traversal `C:\..\..` escapa do prefixo
- **Arquivo:** `src/prefix/prefix.cpp:122-129` `resolve_windows_path()` faz `dosdevice_target / std::filesystem::path(view)` sem normalizar `..` nem resolver symlink.
- **Mecanismo:** `c: -> ../drive_c` em `prefix.cpp:73` + `Z:\` em `136` permitem `C:\..\..\..\etc\passwd` → `dosdevices/c:/../../etc/passwd` → `weakly_canonical` fora de `$PREFIX/drive_c`.
- **Validação ausente:** só `src/cli.cpp:166-168` (`resolve_installed_executable`) valida com `is_path_within(weakly_canonical)`. Runtime `src/runtime/winapi.cpp:222` (`translate_windows_path`), `src/runtime/kernel32.cpp:1028,1538,1656`, `src/runtime/legacy_winapi.cpp:1034` chamam `translate_windows_path` sem `is_path_within`. Convidado lê/escreve host como UID do usuário.
- **Agravante:** `dosdevices/z: -> /` em `prefix.cpp:79` e fallback `136` expõe `/` inteiro por design Wine — viola `docs/compatibilidade.md:147`.

### C2. Truncamento `thunk_value` em imports estáticos
- **Arquivo:** `src/pe/pe_reader.cpp:475-482` faz `static_cast<uint32_t>(thunk_value)` sem checar `> UINT32_MAX`. Delay imports checam em `629` (`if (thunk_value > UINT32_MAX) fail`).
- **Exploração:** PE hostil com `thunk=0x1_00000000` vira `0` → `rva_to_file_offset({0,2})` lê header como `Hint/Name`, bypass de limite.

### C3. Data race em tabelas globais sem lock
- **Arquivo:** `src/runtime/winapi.cpp:274` `find_file_slot`, `300` `find_thread_slot`, `318` `find_sync_slot` escaneiam `g_files/g_threads/g_syncs` sem `g_files_mutex/g_threads_mutex/g_sync_mutex`.
- **Impacto:** concorrente `tl_CreateFileA:1058` (com lock) vs `tl_WriteFile:935` (`handle_fd()->find_file_slot` sem lock) e `tl_CloseHandle:1157` → leitura de `fd` half-initialized / UAF.

### C4. Sobrescrita de proteção de headers
- **Arquivo:** `src/loader/image_mapper.cpp:394-422`
- **Fluxo:** `header_protect_size=align_up(SizeOfHeaders)` → `mprotect(PROT_READ)`, depois loop por região chama `permissions_for_page` e `mprotect(page_start, protect_size, ...)` sobrepondo a cauda da mesma página com `RW/RX`.
- **Condição:** se `SizeOfHeaders % page != 0`, headers ficam graváveis/executáveis.

---

## ALTA — correção, DoS, diagnóstico quebrado

### A1. Degradação `RWX→RW` silenciosa
- **Arquivo:** `src/loader/image_mapper.cpp:44-58` `section_permissions()` ignora `EXECUTE` se `WRITE` setado; `permissions_for_page:102` igual.
- **Efeito:** seção `IMAGE_SCN_MEM_WRITE|EXECUTE` perde `X`, guest packed/JIT falha com `SIGSEGV` não diagnosticado como `Unsupported`.

### A2. Check de entry-point super-restritivo
- **Arquivo:** `src/loader/process.cpp:40` exige `effective_page_permissions(...) == ReadExecute`.
- **Problema:** se `.text(RX)` compartilha página com `.data(RW)` (`SectionAlignment < page`), `permissions_for_page` retorna `ReadWrite` → rejeita PE válido. Inverso do comentário `34`.

### A3. Cache de `memory_validator` stale
- **Arquivo:** `src/loader/process.cpp:91-104` `destroy_process()` faz `unmap_image()` sem `invalidate_memory_map_cache()`; só `munmap(stack)` invalida.
- **Efeito:** `validate_mapped_range` segue válido para imagem já liberada → `mapped_guest_range` aceita dangling pointer.

### A4. Isolamento filho: `poll` sem `POLLHUP` + fd sem `CLOEXEC`
- **Arquivo:** `src/process/isolate.cpp:230` espera só `POLLIN`. Filho escreve 5B e `_exit`; `HUP` sem `POLLIN` espera até timeout. `pipe(pipe_fds):164` sem `O_CLOEXEC`; guest `CreateProcessW` em `src/runtime/kernel32.cpp:3847` (`fork` interno) herda write-end → `read_exact:33` bloqueia até timeout do pai.

### A5. Handler global não `sig_atomic_t` e restauração parcial
- **Arquivo:** `src/process/isolate.cpp:31` `g_crash_report_fd` plain `int`; handler `77-84` restaura só sinal faulting para `DFL`, outros 7 permanecem hookados → segundo fault recurse.

### A6. Leitura de PE sem limite no filho `CreateProcess`
- **Arquivo:** `src/runtime/kernel32.cpp:3768` → `read_guest_file_for_process` usa `ifstream` + `istreambuf_iterator` sem `kMaxPeFileSize` (`src/cli.cpp:65` limita 512 MiB) e sem `O_NOFOLLOW` → symlink `/dev/zero` ou arquivo de GiB DoS forkado.

### A7. `install` aceita `--trace` duplicado
- **Arquivo:** `src/cli.cpp:689` seta `trace_enabled=true` sem checar duplicata, diferente de `855` (DirectRun) → channels duplicados.

### A8. `WaitForSingleObject` para `Process` busy-loop 1ms
- **Arquivo:** `src/runtime/winapi.cpp:353-408` `wait_process_slot` faz `waitpid(WNOHANG)+sleep(1ms)` sob timeout, queima CPU; deveria `poll` em `child_result_fd`.

---

## MÉDIA — vazamentos, ABI, robustez e docs

- **M1. `initialize_prefix` esconde erro** `src/prefix/prefix.cpp:73,79` `ec.clear()` sempre, mesmo se `EACCES` vs `EEXIST` → retorna `true` com prefix meio inicializado.
- **M2. `snapshot_executables` double-parse + TOCTOU** `src/cli.cpp:115-135` `read_file()+parse_pe()` então `file_size()/last_write_time()` separados; installer que substitui `.exe` mesmo tamanho e mesmo segundo → `changed_executables:144` falha (`no-candidate`).
- **M3. `unwind.cpp:262` `unwind_one` infere falha por `memcmp(Context)`; handler que só muda `RSP` ou leaf sem unwind codes misclassificado. `708` só detecta epílogo V2 no primeiro frame (`end_rva` como pc para chained falha).
- **M4. `ntdll.cpp:169` tabela `g_allocations` 256 esgota → `mmap` sem tracking, `NtFreeVirtualMemory:209` vaza mem e `NtQueryVirtualMemory` fallback `/proc/maps` com `Type=MEM_IMAGE` se path contém `/`.
- **M5. `kernel32.cpp:3798` `current_path / directory` relativo: `directory` já contém `../` de guest, resolve contra CWD host sem `is_path_within` antes de `is_directory` → pode checar `/etc` como diretório.
- **M6. `diagnostics/crash_context.cpp:28` `nearest_import` usa `>=` e `string_view` para `section` apontando `MapRegion::name` em `MappedImage` que morre após `destroy_process:99` → view dangling se logado depois.
- **M7. `winapi.cpp:591` `allocate_guest_teb` seta `g_current_teb` antes de `arch_prctl`; falha libera mas perde TEB anterior do pai.
- **M8. `README.md:7-9` divergente de `ROADMAP.md:22` e `docs/compatibilidade.md` — matriz tem 60 fixtures, doc refere `install`/`Fase 13` sem atualizar overview.
- **M9. `src/runtime/msvcrt.cpp:1172,1478` `fprintf(stderr,"malloc %p")` vaza ASLR em trace (deveria ir só por `write_trace`).
- **M10. `src/runtime/memory_validator.cpp:104` `escape_value` não escapa `\0..\x1F`, quebra parsing `key="value"` de `docs/diagnostico.md:7`.

---

## Priorização de correção

1. **Validar `resolve_windows_path` com `weakly_canonical + is_path_within(drive_c)` em todo `translate_windows_path` e remover/bloquear `Z: -> /` por default (`prefix.cpp:77-80,122-137`).**
2. Checar `thunk_value > UINT32_MAX` em `pe_reader.cpp:480` (igual delay).
3. Proteger `find_*_slot` com `lock_guard(g_*_mutex)` ou impor caller com lock documentado.
4. Corrigir ordem `mprotect` headers vs regiões (proteger headers por último ou recalcular `header_protect_size` como `page_start` da primeira região).
5. `invalidate_memory_map_cache()` em `destroy_process` após `unmap_image`.
6. `pipe2(O_CLOEXEC)` e `poll(POLLIN|POLLHUP)` em `isolate.cpp:164,230`.
7. Aplicar limite 512 MiB / `O_NOFOLLOW` em `kernel32.cpp` `CreateProcess`.
