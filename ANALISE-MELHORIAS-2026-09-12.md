# Análise de melhorias e otimizações — TradutorLinux — 2026-09-12

Análise estática por leitura, sem build.
Foco em melhorias incrementais de baixo risco, sem expandir escopo (sem API nova sem alvo).

## 1. Performance / memória — quick wins

* `src/loader/module_graph.cpp:465`: `file_bytes = *bytes` copia arquivo inteiro. Virar `std::move` economiza 2x RAM por DLL.
* `src/loader/image_mapper.cpp:488-535`: `write_image_bytes` faz 2x `mprotect` por símbolo. Fazer batch por página: 1x RW, aplica todas IATs, restaura. Menos syscall + TLB shootdown.
* `src/loader/image_mapper.cpp:420-439`: `mprotect` por `region` + `permissions_for_page 82-126` `O(regions)` por página => `O(regions²)`. Coalescer por página `map<page,perms>` único.
* `src/loader/image_mapper.cpp:330-338`: overlap `any_of` aninhado `O(n²)`. Ordenar por `rva` e checar adjacentes.
* `src/loader/image_mapper.cpp:306`: `regions` sem `reserve`. `regions.reserve(info.sections.size())`.
* `src/loader/image_mapper.cpp:147-187`: `read_le_*`/`write_le_u64` byte-a-byte em `apply_relocations 248-275`. Usar `memcpy` (LE em x86-64, mesmo resultado).
* `src/loader/module_graph.cpp:809-823`: `is_guest_executable` abre `/proc/self/maps` + `sscanf` por callback/DllMain. Usar `MappedImage::regions` ou cache do `memory_validator`.
* `src/loader/module_graph.cpp:1183-1195,480-488`: `invalidate_memory_map_cache()` dentro do loop por módulo. 1 invalidação após o loop.
* `src/loader/module_graph.cpp:204-276,738-742,404,366-399`: `find_if` linear em exports / scan `profile->dlls` / `get_environment_paths()` por import => `O(imports*exports)` + re-`stat`. Índice local `unordered_map<string_view,size_t>` por `resolve_group`, cachear `EnvironmentPaths` + `unordered_set` p/ negativos.
* `src/loader/module_graph.cpp:502,509,788,1024,1053,1094`: `visits` / `result.imports` / `builtin_modules_` sem `reserve`. `reserve(32)` / `reserve(imports+delay)` / `reserve(64)`.
* `src/loader/module_graph.cpp:316-333`: `trace_event` constrói 3x `std::string` por import. Overload `TraceField(string_view)` sem copiar.
* `src/loader/module_graph.cpp:107-108`: `entry.path().filename().string()` dentro do loop `directory_iterator`. Hoist `requested_filename`, comparar via `string_view`/`ascii_iequals`.
* `src/pe/pe_reader.cpp:378-407`: `rva_to_file_offset` scan linear por chamada, milhares de vezes. Memoizar última seção / busca binária.
* `src/pe/pe_reader.cpp:466-467,484-487`: `rva_to_file_offset` chamado 2x p/ mesmo range. `auto o=...; if(!o)...; table=*o;`.
* `src/pe/pe_reader.cpp:143-156`: `read_cstring` retorna `std::string` (aloca) por DLL/símbolo/export. Retornar `string_view` + alocar só ao inserir em `PeInfo`.
* `src/pe/pe_reader.cpp:1285-1316`: validação `CHAININFO` com `find_if` + loop ciclo => `O(n²)`/`O(n³)`, `n<=65k`. `unordered_map<key64,idx>`.
* `src/pe/pe_reader.cpp:352,459,475,516,1367,1181`: `push_back` sem `reserve` com tamanho conhecido. `reserve()` correspondente.
* `src/runtime/core/memory_validator.cpp:29-64`: `rebuild_cache_locked` `getline` aloca por linha + `push_back` sem `reserve`. `line.reserve(256); g_map_regions.reserve(128)`.
* `src/runtime/core/memory_validator.cpp:189-193`: `validate_mapped_wstring` scan `u16` a `u16`. `wmemchr`/scan por palavra quando grande.
* `src/runtime/core/environment.cpp:56-64,158`: `normalized_name` aloca + `toupper` por lookup. Comparação case-insensitive sem alocar, alocar só no insert.
* `src/runtime/core/environment.cpp:111-126,184-199`: `rebuild_ansi_block_locked` a cada `set_` e a cada `block_a` => `O(sets*N)`. Flag `dirty`, rebuild só em `guest_environment_block_a`.
* `src/runtime/core/environment.cpp:201-237`: `entries_w` + `allocate_block_w` com `utf8_to_wide` 2x + `vector<u16string>` intermediário. Escrever direto no bloco contando `units` na 1ª passada.
* `src/process/isolate.cpp:708`: `run_external_isolated: fork()+execve` copia page-tables. `posix_spawn` (só este; `run_guest_isolated` mantém `fork`).
* `src/process/isolate.cpp:606-625`: `inherited_environment` copia `environ` + `find_if` `O(env*overrides)` + `replacement` sempre alocado. `reserve()` + construir só no branch.
* `src/process/isolate.cpp:628-651`: `forward_external_stderr` `pending.erase(0,n+1)` por linha => `O(n²)`. Índice `consumed` + compactação periódica.
* `src/compat/profile.cpp:399-409,687-692`: `same_target` aloca 2x `string` em loop `O(n²)`. Comparar on-the-fly sem alocar.
* `src/compat/profile.cpp:377,605-618`: `lowercase(string)` por valor + dup-check linear. `unordered_set<string>` + `ascii_iequals` sem copiar.
* `src/compat/materializer.cpp:69-96,158-197`: `validate_path_components`/`collect_parent`/`path_has_symlink_component` fazem `symlink_status` por componente por arquivo => `O(files*depth)`. `unordered_map<path,char>` cacheado no batch.
* `src/compat/materializer.cpp:113`: `string relative{substr}` + `lexically_normal` + `resolve_windows_path` duplicam parse (ainda mais com Rust). Quando Rust aceita, pular redundância C++.
* `src/compat/materializer.cpp:297`: `array<char,64k> buffer{}` zero-init por arquivo. Sem `{}`.
* `src/compat/rust_profile_parser.cpp:130-155`: `read_ref` scan linear `O(refs*strings)`. `unordered_map<pair<off,len>,idx>` se perfis grandes.
* `src/compat/rust_path_validator.cpp:75,96`: `steady_clock::now()` 2x + `array<char,256>{}` por path. Medir só se métrica ligada.
* `src/package/msix.cpp:618,1133,1145,1168`: `normalized_zip_name(string)` por valor em buscas lineares => `O(entries²)` cópias (até 10k). Helper `string_view` sem alocar ou pré-computar 1x.
* `src/package/msix.cpp:673,957`: `set<string> names` sem `reserve`. `unordered_set` + `reserve(total_entries)`.
* `src/package/msix.cpp:777,1206-1275`: `read_zip_entry` abre `ifstream` + `inflate` + `crc` por entry; `extract` reabre N vezes. 1 `ifstream` reusado; CRC incremental.
* `src/package/msix.cpp:587-606`: `inflate_raw` `vector(expected_size)` zero-fill até 512MB. Inflar em chunks com buffer reusado como passo seguinte.
* `src/package/msix.cpp:895-1043`: `inspect_msix_package` duplica `read_zip_archive`. Overload interno que recebe `ZipArchive` já parseado.

## 2. Arquitetura / qualidade

* Default `ExportSupport::Full` em `include/tradutorlinux/loader/module.hpp:24,39` esconde stub. Trocar default p/ `Stub` e exigir `Full/Limited` explícito. Rebaixar `dwmapi.cpp:91-98`, `uxtheme.cpp:262-288`, `comdlg32.cpp:133-142`, `imm32.cpp:120-132`, `version.cpp:117-126`, `gdiplus.cpp:89-96`, `mpr.cpp:90-96`, `crypt32.cpp:930-950`, `winapi.cpp:1586-1621` — só metadado, sem mudar código.
* `module_graph.cpp:217,235`: `direct_export()` força `Full` p/ PE convidado. Usar `Limited` p/ guest, `Full` só builtin.
* `winapi.cpp:1602`: `ClosePrinter→tl_CloseHandle` sem nível + alias indevido. Explicitar `Stub` e função própria com `SetLastError`.
* `winapi.cpp:1621`: `{"",101,...}` nome vazio para `D3D12`. Remover ou dar nome/ordinal correto.
* Quebrar arquivos gigantes sem mudar comportamento: `file.cpp:2477L`, `process.cpp:1821L`, `winapi.cpp:1663L` → `winapi_stubs.cpp`, `message.cpp:1082 SendMessageA 308L`, `window.cpp:223 CreateWindowExA 272L`, `unwind.cpp:1051L`.
* `module_graph.cpp:513,601,711`: lambdas `try_guest/fail_entry/resolve_group` aninhadas. Extrair `try_guest_export/patch_import_record`, sem mudar ordem de fallback.
* Desacoplar: `runtime_context.hpp:1-30` god-header → fwd-decl; `register_*_module()` de `runtime/*` → `src/loader/`; `loader→invalidate_memory_map_cache()` → callback `on_image_changed` injetado pelo runner; `user32→gui/platform` proibir include direto de `x11.hpp` fora de `src/gui/`.
* `include/tradutorlinux/util/handle_table.hpp:14` tem 0 usos. Piloto: trocar `security.cpp:53-59`, `comctl32.cpp:20,26`, `msvcrt.cpp:73-75`, `winapi.cpp:85-124` por `allocate/release/find`.
* `const/noexcept/[[nodiscard]]`: adicionar `[[nodiscard]]/noexcept` em `loader/module.cpp:25`, `loader/module.hpp:59-63,78`, `TL_MSABI` sem `noexcept` em `gdi32.cpp:107,267`, `user32/window.cpp:223,496`, `user32/message.cpp:1467,1516`, `user32/dialog.cpp:355,754`, `kernel32/file.cpp:428,493,1446,1468`, `kernel32/process.cpp:1366,1796`, `ws2_32.cpp:1208`, `wininet.cpp:677,702,736,811,918`, `locale.cpp:1527`, `console.cpp:116`, `gui/platform.hpp:6-28`.
* Duplicação C++ vs Rust (manter C++, só paridade/teste): `pe_reader.cpp` vs `pe_parser.rs`, `msix.cpp` vs `msix_parser.rs`, `profile.cpp` vs `profile_parser.rs`, `app_catalog.cpp` vs `catalog_parser.rs`. Tabela de paridade em teste + `static_assert(sizeof/offset)` + constantes de limite em `docs/arquitetura/rust-ffi.md`.
* Docs: `feitos/ROADMAP-LEGADO.md:13-14,5003` links quebrados; `docs/arquitetura/*.md:616,630` links relativos; `PROJETO.md:144` árvore vs `src/runtime/dlls/*`; `CMakeLists.txt:19-20` vs `rust-toolchain.toml` fonte única p/ `1.97.1`.
* Build warnings: `cmake/CompilerWarnings.cmake:3-11` já `-Wall -Wextra -Wconversion -Werror`, adicionar `-Wnon-virtual-dtor -Woverloaded-virtual -Wunused-parameter` 2 por vez.

## 3. Produto / UX sem expandir API

* `src/cli/report.cpp:954-966,679,681`: `result: supported` + `100%` + `not-attempted` na mesma tela confunde. Renomear p/ `imports: supported` + `next-step: execute com timeout+trace`.
* Unificar glossário `catalog.md:24` vs `compatibilidade.md:13` vs `requisitos-aplicativos.md:23`; tabela `imports | execução | nível` p/ WinRAR/Roblox/Rockstar/LGHUB.
* `report.cpp:500-526`: recomendações sem comando. Adicionar caminho/prefixo, `upx -d` p/ W+X, distinguir `mscoree+AOT` vs `Mono→dotnet`, listar `service_apis`, aviso `requireAdministrator` com `906-908`.
* `src/gui/main_window.cpp:511-554,483-496`: Qt usa `--trace` textual, nunca `--report --json`. Parsear `imports/runtime_support/recommendations/manifest.uac` p/ badges; mapear exit `71/72/73/4/5`; `install 627-674` ganhar linha máquina p/ evitar regex.
* `src/cli/doctor.cpp:121-137,192-213`: `unprivileged_userns=true` fixo, `ready=arch+display`. Ler conteúdo `0/1`, `bwrap --version`, `ready` por perfil `console/rede/gui-2D/3D/proton`, checar `7z`, `Xvfb`, fontes `helvetica`, `TL_GUI_BACKEND`, botão “Diagnosticar Host”.
* `runner.cpp:1320-1336,1189-1294,961-970`: erro fora do trace sem `dll!symbol [static|delay] status`. Prefixar `fase/rva/exit 4/5`, expor `code/phase/offset` do MSIX no stderr.
* `runner.cpp:598-730,852-888`: `app list` sem `prefix/limits/backend`; `working_directory` cai p/ `drive_c` silencioso. Exibir `id | exe | prefix | cpu/mem | backend` + aviso de cwd.
* `src/gui/platform.cpp:39-56`, `wayland.cpp:440-441`: auto Wayland com fallback silencioso, `message_box/track_popup=0` stub. Avisar `menu/modal indisponível no Wayland, use X11` + trace backend.
* `src/gui/x11.cpp:357-403`: `kMaxWindows=16` sem aviso. Mensagem quando `>16` ou geometria inválida (caso `7zFM`).
* Backlog por recorrência sem API nova: `docs/requisitos-aplicativos.md:79-93` gerar tabela `--report --json` com `dll!symbol × nº apps` para ordenar `OLEAUT32 ordinal(2,6)`, `USER32/GDI32 delay`, `ADVAPI32 Registry`.

## 4. DX / build / CI

* `CMakeLists.txt:10-13`: `Qt6 REQUIRED` trava CLI. `option(TL_BUILD_GUI)` + `find_package QUIET`; job `cli-only` sem `qt6-base-dev`.
* `CMakeLists.txt:70-76`: `GTest GIT_SHALLOW FALSE`. `TRUE` ou `find_package` + cache CI.
* Presets: só 6 em `CMakePresets.json:8-64`, mas `build/` tem 21 dirs, `ci.yml:163-166 targetapps` fora de preset. Criar presets `targetapps/proton/no-gui`; documentar `build/<preset>` único.
* `compile_commands.json` symlink fixo em `debug`. Não versionar, atualizar por preset ativo ou `ln -sfn`.
* Trackeados apesar de `.gitignore`: `*.deb`, `_CPack_Packages/`, `Testing/`, `tl_phase5_data.bin`. `git rm --cached` + ignorar; `.tl-registry.db` gerado por teste fora da raiz.
* `tests/CMakeLists.txt:228,714`: Xvfb/Proton `SKIP 77` nunca no default. `LABELS xvfb;proton;gui`, gate rápido `ctest -LE`, job separado `xvfb-run`/Proton-real com `TIMEOUT 240/420`.
* Sanitize 10 falhas fora do gate mascaradas por `detect_leaks=0` em `CMakePresets.json:110-140`. Usar `WILL_FAIL/LABELS sanitize-known-fail` explícito.
* `ci.yml:28,63,169 --parallel` sem valor vs `AGENTS --parallel 2`. Fixar 2; split `lint` vs `build/test`; cache `ccache`, `build/_deps`, `cargo/target`; `FETCHCONTENT_BASE_DIR` cacheado; `ctest -R`/labels em vez de full repetido.

## Sugestão de começo

Lote mecânico de baixo risco: `move file_bytes` + `reserve()` + `default Stub` + `Qt opcional` + `GTest shallow` + `--parallel 2` no CI.
