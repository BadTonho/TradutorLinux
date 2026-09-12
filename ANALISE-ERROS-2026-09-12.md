# Análise completa — TradutorLinux — 2026-09-12

Análise estática por leitura, sem build, conforme `AGENTS.md` custo de build.
Escopo: loader, ABI, memória, imports, robustez PE, cobertura Win32, matriz, build/CI/docs.

## 0. Estado documental — incompatibilidade imediata

* `PROJETO.md:94` aponta `feitos/ROADMAP.md`, que não existe mais. Só há `feitos/ROADMAP-LEGADO.md` + `feitos/PROXIMAS-ETAPAS.md` após `6f3c5ec`.
* `feitos/ROADMAP-LEGADO.md:13-14,5003` tem links quebrados para `feitos/ROADMAP.md` / `ROADMAP-RUST.md` deletados.
* `git log --oneline -3`: houve commit automático durante a análise consolidando roadmaps. Se há hook que comita sozinho, viola `AGENTS.md:127`.
* Artefatos na raiz: `.tl-registry.db` ignorado mas gerado por teste, `tl_phase5_data.bin` 6B trackeado, `tradutorlinux_0.0.0_amd64.deb` 5.2M trackeado apesar de `.gitignore:7 *.deb`, `_CPack_Packages/`, `Testing/` trackeados.

## 1. Bugs críticos — loader / isolamento [prioridade alta]

1. `src/loader/import_resolver.cpp:109-116` confirmado: no `Delay`, `patch_address(image,entry)` muta `entry.status` para `UnsupportedMechanism` mas nunca chama `fail(result,...)`. Resultado fica `Resolved` com IAT corrompida e executa. No `Static:121-124` propaga corretamente.
2. `src/loader/module_graph.cpp:809-852` confirmado: `is_guest_executable` só checa `x` em `/proc/self/maps`, sem `base/size` do módulo. `relocate_va:119-127` devolve original se fora da imagem. Callback `TLS/DllMain` hostil apontando p/ libc `r-x` passa e é chamado. `src/runtime/core/winapi.cpp:1157-1167` faz bound-check correto — igualar.
3. `src/pe/pe_reader.cpp:871-880` confirmado: loop `callback_vas` sem `kMaxTlsCallbacks`, limitado só por fim de arquivo → OOM/DoS com PE hostil.
4. `src/pe/pe_reader.cpp:842-854,1234-1252`: TLS truncado / `.pdata` em seção virtual retornam `nullopt` silencioso, não `Malformed`. Some sem diagnóstico.
5. `src/pe/pe_reader.cpp:1297-1317`: validação `CHAININFO` `O(n²)+O(n³)` com `kMaxRuntimeFunctions=65536` → trava parser com `.pdata` grande. Precisa índice `map<RVA>`.
6. `src/process/isolate.cpp:111-149`: handler usa `sigaction/sigemptyset` não async-signal-safe; após `SIGABRT/SIGTRAP` retorna e continua corrompido em vez de re-entregar. Timeout `502-509` mata só pid, não pgid — netos `CreateProcess` sobrevivem, diferente de `run_external_isolated:661-665`.
7. `src/runtime/dlls/kernel32/thread.cpp:194,137-152`: retorno `arch_prctl(SET_GS)` ignorado em thread secundária; `start_address` só checa legível, não executável; stack secundária sem guard page, diferente de `src/loader/process.cpp:63-83`.
8. `src/loader/module_graph.cpp:461-466,1033-1064`: `file_bytes` retém cópia integral até 512MiB por DLL sem uso posterior; `LoadLibrary/GetModuleHandle` dão `push_back` sem limite → DoS por convidado.
9. `src/runtime/core/environment.cpp:216-237`: `units=1+Σ` sem checked-arith → wrap + overflow.
10. `src/loader/image_mapper.cpp:488-536`: `write_image_bytes` faz `mprotect(RW)` global, janela RW visível a todas as threads (TOCTOU). Se IAT divide página com código, página perde `X` permanentemente.
11. `src/runtime/core/memory_validator.cpp:106-164`: TOCTOU validação-uso via `/proc/self/maps`; `validate_mapped_wstring:183-195` sem exigir alinhamento 2.
12. `src/pe/pe_reader.cpp:251-255`: não valida `SectionAlignment/FileAlignment` potência de 2, nem `AddressOfEntryPoint < SizeOfImage`. Só pego depois com mensagem genérica em `src/loader/process.cpp:48-53`.
13. `src/pe/resource_inspector.cpp:15-33,373-410`: `read_u16/u32` retornam 0 em OOB indistinguível de válido; `find_leaf_rva_and_size` só segue primeira entrada, ignora `l2_total/l3_total>1`.
14. `src/pe/pe_analyzer.cpp:38-51`: `inspect_pe_mitigations` não valida `opt_magic` antes de ler `DllCharacteristics` — offset errado p/ PE32.

## 2. Falso-sucesso — stubs marcados `Full`

Confirmado em `src/runtime/dwmapi.cpp:20-82` e `register_dwmapi_module:91-98`: todos retornam `S_OK`, `IsCompositionEnabled=1`, cor fixa `0xAA0078D7`, sem `ExportSupport`. Mesmo padrão em:

* `src/runtime/uxtheme.cpp:12-145`, `comdlg32.cpp:57-124` injeta `C:\document.txt` inexistente, `imm32.cpp:21-111`, `version.cpp:20-82` `512/memset/"1.0.0.0"`, `gdiplus.cpp:41-80` ignora stream, `dlls/net/mpr.cpp:19-81`, `core/winapi.cpp:1374-1482` `OpenEvent/FileMapping/SetupDi*` fakes, `shell32.cpp:512-586` `DESK/PIDL/ITEM`, `shlwapi.cpp:367-422`, `dlls/crypto/crypt32.cpp:910-921` confirmado: `CertNameToStrW` ignora params e retorna `L"CN=Notepad++"` fixo, `dlls/user32/menu.cpp:378-425` menu fantasma 5 itens, `dlls/kernel32/file.cpp:1707-1793` disco fixo.
* Impacto: `--report runtime-support: full`, `notepad++ 584/584`, `Roblox 430/430`, `lghub 114/114`, `Rockstar 338/338` parecem ok, mas execução dá `GuestTimeout 72` / `Exit 3` / `exit 44544`. `tl_gdiex/tl_shell/tl_crypt32` cristalizam falso-sucesso com `exit 0`.
* `GetLastError` ausente/errado: `imm32,dwmapi,winmm,comdlg32,version` nunca `set_last_error`; `dbghelp.cpp:24-28 SymFromAddr 0+SUCCESS`; `shell32.cpp:475-479 SHBrowseForFolder nullptr+SUCCESS`.

Inverso correto mas confuso: `core/winapi.cpp:1566-1657 SensApi/SetupApi/D3D/DXGI Stub` retornam `E_FAIL/DXGI_ERROR_UNSUPPORTED` — correto, mas `tl_graphics_probe/d3d12_probe` só passam via Proton, nativo falharia; matriz não separa.

## 3. `--report supported` vs execução real — `src/cli/report.cpp:954-965`

`result: supported` = só `Resolved`; `runtime-support: stub/limited/full` separado. Matriz cita só o primeiro:

* `Roblox 430/430 supported` vs `RBXCRASH Worker,28 ExitProcess 3`
* `lghub 114/114` vs `GuestTimeout 72`
* `Rockstar 338/338` vs `exit 3`
* `notepad++ 584/584` vs `GuestTimeout`
* `putty 348/348` vs `ExitProcess 1 sem argumentos`
* `HWiNFO 28/28` vs `exit 44544 OpenPrinterW limitado`
* `Rufus 14/14` vs `exit 56832`
* `RTSSHooks64.dll 256/256` / `7z.dll 86/86` não executadas
* `docs/compatibilidade.md:255-257` `tl_7zfm_gui/tl_putty/tl_notepadpp Suportado` são fixtures sintéticas, não os `.exe` reais — confusão de nome.

Fixtures faltantes / nomes divergentes: `tests/samples/src/` tem `tl_version.c` sem `GetFileVersionInfo/VerQueryValue`; `tl_shell.c` sem `ShellExecute/SHFileOperation/Drag`; sem `tl_comdlg/tl_imm/tl_mpr/tl_powr/tl_setupapi/tl_dwm` dedicados. `docs/arquitetura/api-win32.md:622-626` cita `tl_ws2/tl_wintrust/tl_user_ext/tl_shell_path` — em disco `tl_network_loopback.c/tl_trust.c/...`.

## 4. Unicode / caminhos

* `src/runtime/dlls/kernel32/locale.cpp:210,1232`: só `ACP/1252/1250/1251/437/28591/UTF8`, `en-US/0x0409`. `WideCharToMultiByte:1212` cai para `cp1252` silencioso — `CP936/932/949` viram mojibake.
* `shlwapi.cpp:300-312 StrStrIW`: offset em bytes UTF-8 somado em `uint16_t*` — selvagem p/ não-ASCII.
* `src/prefix/prefix.cpp:186-210`: relativo resolve em CWD host se existir, senão `drive_c` — não-determinístico, quebra isolamento. `gui_controls.cpp:328-364` usa sempre `Z:\`, resto usa `C:\windows\temp`, `C:\Windows\System32`.
* `src/loader/module_graph.cpp:572-589,660-677`: `fallback-export` só tracejado se `profile_was_loaded`; `DriveC` vencedor comum é silencioso.
* `file.cpp:1237-1258 GetPrivateProfileString` retorna `default` tanto se arquivo falta quanto se chave falta.

## 5. Build / testes / CI

* `CMakeLists.txt:13` `Qt6 Widgets REQUIRED` incondicional p/ CLI; `70-76` `FetchContent googletest GIT_SHALLOW FALSE` pesado.
* `CMakePresets.json`: 6 presets, mas `build/` tem 21 dirs extras. `ci.yml:165-172` `target-apps` usa `build/targetapps` fora de preset. `compile_commands.json` symlink fixo p/ `debug`.
* Xvfb opcional: 7 smokes com `SKIP 77`; Proton real com `TIMEOUT 240/420` nunca roda por padrão. Sanitize tem 10 falhas históricas fora do gate `ASan/UBSan, RLIMIT_AS, sem relocations, Xvfb/LSan`.
* `TL_BUILD_RUST OFF` padrão: Debug/Release Rust 668 testes vs OFF 659 — deltas são Rust-only, ok, mas política `backend="rust"` só p/ imagem principal `--report`/`app run` nativo, DLLs continuam C++.
* `AGENTS.md:92 --parallel 2` vs `ci.yml:28,63,169` sem limite.

## Próximo passo sugerido

Corrigir nesta ordem: 1) `import_resolver Delay fail`, 2) `is_guest_executable bound-check`, 3) limite `TLS callbacks`, 4) rebaixar `DWM/UxTheme/COMDLG/IMM/Version/Gdiplus/MPR/CertNameToStr` de `Full` para `Stub/Limited` + `SetLastError`, 5) restaurar `feitos/ROADMAP.md` ou atualizar `PROJETO.md:94` + remover artefatos trackeados.
