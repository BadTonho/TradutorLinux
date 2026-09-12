# Matriz de compatibilidade — aplicativos

Este documento contém os aplicativos de teste, o corpus real e as evidências operacionais originalmente registradas na matriz.

## Aplicações de teste

| Fixture | Arquitetura | CRT | Imports esperados | Estado atual | Próximo marco |
|---|---|---:|---|---|---|
| `tl_nop.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, parseado, mapeado e com imports resolvidos na Fase 3; ainda não executado | Fase 4 |
| `tl_hello.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | Suportado no MVP: escreve `Ola do Windows no Linux!` em stdout, retorna `0` e emite trace | Fase 5 |
| `tl_echo.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `ReadFile`, `WriteFile` | Suportado no MVP: ecoa stdin para stdout com handles padrão | Fase 5 |
| `tl_file.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!CloseHandle`, `CreateFileA`, `ExitProcess`, `GetLastError`, `GetStdHandle`, `ReadFile`, `SetLastError`, `VirtualAlloc`, `VirtualFree`, `WriteFile` | Suportado no subconjunto da Fase 5: aloca memória e grava/reabre/lê arquivo relativo | Fase 6 |
| `tl_compat_file.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!CloseHandle`, `CreateFileA`, `ExitProcess`, `GetStdHandle`, `ReadFile`, `WriteFile` | Fixture B14.3/B14.5/B20.5/B20.6: `app run` copia um arquivo de `compat/files/` para `C:\\Program Files\\Compat Fixture`, lê e altera o destino, mantém a origem e remove o materializado ao terminar. A integração operacional repete a execução pelo catálogo com sessões Rust por fase, registra métricas, confirma fallback C++ sem Rust e mantém a origem e os prefixos isolados | B20.6 |
| `tl_compat_dll_app.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!LoadLibraryA`, `GetProcAddress`, `FreeLibrary`, `ExitProcess`; DLLs da fixture importam `KERNEL32.dll` e `compatdep.dll` | **Suportado no contrato B14.4:** o perfil v2 seleciona `compat.dll` e sua dependência PE32+ `compatdep.dll` em `compat/dlls/`; as DLLs são mapeadas sem cópia para `drive_c`, executam TLS/`DllMain`, resolvem imports genéricos e são descarregadas em ordem. A integração executa dois IDs/prefixos com variantes A/B, confirma isolamento, fontes preservadas, trace completo, fallback por export e exit `0`; perfil ausente, v1, DLL ausente ou provider rejeitado retornam ao comportamento genérico | B14.4 |
| `tl_proton_probe.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | **Piloto do backend Proton B14.6/B20.5/B20.6:** `app run` usa um perfil schema 3, estagia o aplicativo em `proton/compatdata/pfx`, materializa `files[]`, registra a métrica Rust no componente Proton quando habilitado, encaminha stdout/stderr, preserva o exit code do launcher mockado (`23`) e rejeita um Proton inválido com `Unsupported` (`5`) sem fallback nativo. A fixture permanece como prova de contrato do adaptador; isso não declara compatibilidade de um aplicativo real nem do Roblox | B20.6 |
| `tl_graphics_probe.exe` | PE32+ AMD64 | Não | `D3D11.dll!D3D11CreateDeviceAndSwapChain`; `KERNEL32.dll!ExitProcess`, `GetModuleHandleA`, `GetStdHandle`, `WriteFile`; `USER32.dll!CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `RegisterClassA`, `ShowWindow`, `UnregisterClassA` | **Fixture gráfica controlada da B14.6.5:** cria uma janela X11 via Proton, inicializa D3D11, cria swap chain/RTV, limpa o backbuffer e executa `Present`; `integration_proton_graphics` passa com Proton Experimental real sob Xvfb, preserva stdout (`D3D11 frame presented`) e retorna `0`. Valida somente o caminho D3D11→DXVK/Vulkan em X11; não declara suporte a D3D12/VKD3D-Proton, áudio, entrada ou jogos | B14.6.5 |
| `tl_d3d12_probe.exe` | PE32+ AMD64 | Não | `D3D12.dll!D3D12CreateDevice`; `DXGI.dll!CreateDXGIFactory2`; `KERNEL32.dll!ExitProcess`, `GetModuleHandleA`, `GetStdHandle`, `WriteFile`; `USER32.dll!CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `RegisterClassA`, `ShowWindow`, `UnregisterClassA` | **Fixture VKD3D-Proton controlada da B14.6.5:** cria dispositivo D3D12, fila direta, allocator, command list, fence, swapchain flip de dois buffers e `Present`; `integration_proton_d3d12` passa com Proton Experimental real sob Xvfb, preserva stdout (`D3D12 command path ready`) e retorna `0`. Valida esse caminho controlado D3D12→VKD3D-Proton/Vulkan; não declara suporte geral a D3D12, áudio, entrada ou jogos | B14.6.5 |
| `tl_input_probe.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetModuleHandleA`, `GetStdHandle`, `WriteFile`; `USER32.dll!CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `DispatchMessageA`, `GetMessageA`, `PostQuitMessage`, `RegisterClassExA`, `ShowWindow`, `TranslateMessage`, `UnregisterClassA`, `UpdateWindow` | **Fixture de entrada controlada da B14.6.5:** cria uma janela, valida `WM_CREATE`, `WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP` e `WM_KEYDOWN/CHAR/UP`; `integration_proton_input` injeta movimento, clique e `q` por X11/XTest, confirma stdout (`Proton input ready`), exit `0`, trace e limpeza do prefixo. Não declara raw input, gamepad/XInput ou suporte de jogos | B14.6.5 |
| `tl_audio_probe.exe` | PE32+ AMD64 | Não | `XAudio2_8.dll!XAudio2Create`; `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `Sleep`, `WriteFile` | **Fixture de áudio controlada da B14.6.5:** cria o engine XAudio2, voz master, voz PCM de origem, envia um buffer, inicia, para e destrói as vozes; `integration_proton_audio` passa com Proton Experimental real em Debug e Sanitize, preserva stdout (`Proton audio ready`) e retorna `0`. Valida a cadeia de engine/vozes, não fidelidade perceptual, mixagem, dispositivos específicos ou suporte multimídia amplo | B14.6.5 |
| `tl_virtual_query.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`, console e `ExitProcess` | Fixture de contrato: distingue reserva de commit, preserva `AllocationBase`/`AllocationProtect`, separa as duas páginas após proteger apenas a primeira e consulta a faixa liberada como `MEM_FREE`; imprime `virtual-query\n`, exit `0`. Metadata, `--report` e execução são regressões CTest | B12 |
| `tl_gui.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!MessageBoxA` | Protótipo manual: caixa modal X11 mínima; não executado automaticamente por depender de display | Fase 7 |
| `tl_win.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Janela real com message loop X11; fecha via `WM_CLOSE`/autoclose; teclado via `WM_KEYDOWN`/`WM_CHAR`; executado automaticamente sob Xvfb (teste `runtime_gui_smoke`, cenários autoclose, `WM_DELETE_WINDOW` e `KeyPress 'q'`) | Fase 7 |
| `tl_win2.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Duas janelas simultâneas com `WNDPROC`s independentes; eventos roteados por janela (fila por janela no pump); executado automaticamente sob Xvfb (cenário `janelas` do `runtime_gui_smoke`, `KeyPress 'q'` em A e `'k'` em B) | Fase 7 |
| `tl_win_w.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExW`, `CreateWindowExW`, `ShowWindow`, `UpdateWindow`, `GetMessageW`, `TranslateMessage`, `DispatchMessageW`, `DefWindowProcW`, `DestroyWindow`, `PostQuitMessage`, `SetWindowTextW`, `GetWindowTextW` | Janela real via `W` (wrappers `wide_to_utf8` → `A`): `RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`SetWindowTextW`/`GetWindowTextW`; validado `--report` 12/12, execução `Xvfb` análoga a `tl_win` | Fase 7 |
| `tl_key.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Teclado estendido: `Shift+q` → `WM_CHAR('Q')`, `Return` → `WM_KEYDOWN(VK_RETURN)` e `Left` → `WM_KEYUP(VK_LEFT)`; executado sob Xvfb (cenário `keys`, exit-code `7`) | Fase 7 |
| `tl_timer.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `SetTimer`, `KillTimer`, `DestroyWindow`, `PostQuitMessage` | Timer periódico de 200 ms: dois `WM_TIMER`, depois `KillTimer` + `DestroyWindow`; executado sob Xvfb (cenário `timer`, exit-code `7`) | Fase 7 |
| `tl_gdi.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`, `BeginPaint`, `EndPaint`; `GDI32.dll!GetStockObject`, `TextOutA` | Pintura mínima no `WM_PAINT` (`BeginPaint`/`TextOutA`/`EndPaint`) validando `HDC == HWND` e `rcPaint`; executado sob Xvfb (cenário `gdi`, exit-code `3`) | Fase 7 |
| `tl_reloc.exe` | PE32+ AMD64 | Não | Nenhum | Gerado com `-Wl,--dynamicbase`, verificado, parseado e mapeado na Fase 2; usado para validar base relocations | Fase 4 |
| `tl_missing_dll.exe` | PE32+ AMD64 | Não | `USER32.dll!TlUnknownSymbolW` | Gerado, verificado e rejeitado na Fase 3: `USER32.dll` é conhecida, mas o símbolo diagnostica `unknown-symbol`; retorna `5` sem executar o entry point. A import library do fixture é gerada via `dlltool` (`defs/tl_missing_dll.def`) porque o símbolo não existe nas bibliotecas reais do mingw. A expectativa `unknown-symbol` é vinculada ao teste de rejeição e à matriz | Fase 4 |
| `tl_delay_import.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess` somente no diretório delay-import | **Suportado no subconjunto eager:** descritor `grAttrs=0x1`, INT/IAT atrasadas e resolução antecipada; `--report` resolve 1/1 e a execução chama `ExitProcess` pela IAT atrasada. A variante com `TlMissingDelayImportW` retorna `5` antes do entry point e identifica `mechanism="delay-import"`; binding/unload sob demanda não são emulados | Delay imports RVA |
| `tl_unwind.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!RtlCaptureContext`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlPcToFileHeader`, console e `ExitProcess` | **Suportado no núcleo de unwinding:** possui `.pdata`/`.xdata`, captura um `CONTEXT`, localiza sua `RUNTIME_FUNCTION`, desempilha um frame real e valida a base da imagem; imprime `unwind\n`, exit `0`. Não prova nem declara despacho SEH/`try/catch`. | Núcleo de unwinding x64 |
| `tl_unwind_v2.exe` | PE32+ AMD64 | Não | Mesmo subconjunto `KERNEL32.dll!Rtl*` de `tl_unwind.exe` | **Suportado para metadado V2 fora de epílogo:** fixture determinística com `UOP_Epilog` V2, normalização no relatório/trace e desempilhamento real no corpo; imprime `unwind-v2\n`, exit `0`. Em epílogo V2, o runtime preserva o contexto e retorna controladamente; não há interpretação de instruções nem despacho SEH. | Unwind V2 / despacho SEH |
| `tl_seh.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!RaiseException`, VEH, `RtlCaptureContext`, `RtlUnwindEx`, `CreateThread`, console; `msvcrt.dll!__C_specific_handler` | **Suportado para exceção explícita:** valida transferência de `RtlUnwindEx`/RAX, chama `UHANDLER` durante o unwind, valida o callback de `STATUS_UNWIND_CONSOLIDATE`, registra/remove VEH, lança em thread convidada e seleciona um `__except` por `SCOPE_TABLE_AMD64`; imprime `seh\n`, exit `0`. Não cobre C++, `__finally` nem sinais Linux. | Despacho SEH x64 |
| `tl_seh_v2.exe` | PE32+ AMD64 | Não | Mesmo subconjunto de `tl_seh.exe` | **Suportado fora de epílogo V2:** a mesma busca e transferência SEH usa metadado V2 promovido deterministicamente; imprime `seh\n`, exit `0`. | Despacho SEH x64 |
| `tl_cxx_eh.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!RaiseException`, `ExitProcess`; `msvcrt.dll!__CxxFrameHandler3` | **Suportado no subconjunto C++:** captura `catch(...)` por funclet LLVM, com `FuncInfo` v3, mapas checked e retorno validado; exit `0`. | C++ EH x64 |
| `tl_cxx_eh_typed.exe` | PE32+ AMD64 | Não | Mesmo subconjunto de `tl_cxx_eh.exe` | **Suportado no subconjunto C++:** captura tipada por correspondência exata de `ThrowInfo`/`CatchableTypeArray`/`type descriptor`; conversões e herança não são aplicadas; exit `0`. | C++ EH x64 |
| `tl_cxx_eh_cleanup.exe` / `tl_cxx_eh_cleanup_chain.exe` | PE32+ AMD64 | Não | Mesmo subconjunto de `tl_cxx_eh.exe` | **Suportado no subconjunto C++:** um ou dois cleanups de término em cadeia, limite de 64 ações, rejeição de ciclos e continuação apenas após `cleanupret`; exit `0`. | C++ EH x64 |
| `tl_cxx_eh_nested.exe` / `tl_cxx_eh_unhandled.exe` | PE32+ AMD64 | Não | `RaiseException`, `ExitProcess` e, na primeira fixture, `__CxxFrameHandler3` | **Rejeição controlada:** reentrada C++ durante funclet termina com `nested-cxx-exception-unsupported`, e ausência de handler termina com `exceção não tratada`; ambos retornam o código da exceção reduzido pelo processo (`99`), sem loop. | Limites C++ EH x64 |
| `tl_locale_env_fls.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — ambiente W, CP/locale e FLS; thread, console e `ExitProcess` | **Suportado no núcleo determinístico:** altera/expande o ambiente isolado, valida bloco UTF-16, ACP 1252/OEMCP 437, CP437, `en-US`, `LCMapStringW/Ex` e callbacks FLS na thread filha e em `FlsFree`; imprime `locale-env-fls\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Ambiente, locale e FLS |
| `tl_locale_extended.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — validação de locale/code page, enumeração, tipo de caractere e formato de data/hora; console e `ExitProcess` | **Suportado no locale estático:** valida `en-US`/`0x0409`, CP1252/437/UTF-8, enumera o único locale por callback Microsoft x64, classifica `CT_CTYPE1` e formata data/hora en-US; imprime `locale-extended\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Locale determinístico ampliado |
| `tl_process_console.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — startup, handles/tipo, console W, diretório do sistema, recursos do processador, ponteiros e SList | **Suportado no contexto determinístico:** valida `STARTUPINFOW`, troca/restaura stdout, lê UTF-8 como UTF-16, escreve `process-console-é\n`, consulta `C:\Windows\System32`, testa SSE2, encode/decode e SList alinhada; exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Processo e console Win32 |
| `tl_file_metadata.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — enumeração ExW, atributos e metadados por handle | **Suportado no subconjunto de prefixo:** cria dados em `C:\`, enumera com `*`/`?`, alterna `READONLY`, aplica `FileBasicInfo` e testa exclusão no fechamento e POSIX; imprime `file-metadata\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Arquivos x64 |
| `tl_security.exe` | PE32+ AMD64 | Não | `ADVAPI32.dll` — token/SID, descritor, DACL e `SetEntriesInAclW`; `KERNEL32.dll` — arquivo/console | **Suportado no subconjunto virtual por prefixo:** cria `C:\tl_security\acl.bin`, consulta `TokenUser` pelo protocolo de tamanho, verifica usuário não elevado, mescla/grava DACL e a lê numa segunda execução; imprime `security-write\n` e depois `security-read\n`. `runtime_tl_security_prefix` prova persistência e isolamento entre prefixos. | Identidade/DACL virtual |
| `tl_dialog.exe` | PE32+ AMD64 | Não | `COMCTL32.dll!InitCommonControlsEx`; `KERNEL32.dll!ExitProcess`, `GetModuleHandleW`, `GetStdHandle`, `WriteFile`; `USER32.dll!CreateDialogParamW`, `DestroyWindow`, `DialogBoxParamW`, `EndDialog`, `GetDlgItem`, `SetDlgItemTextW`, `SendDlgItemMessageW`, `GetNextDlgTabItem`, `GetWindowRect`, `Get/SetWindowLongW`, `CopyImage`, `DestroyIcon`, `LoadIconW` | **Suportado no subconjunto modal/modeless:** `CreateDialogParamW` cria uma janela modeless real a partir de recurso `DIALOG` padrão, despacha `WM_INITDIALOG` e mantém o `HWND` válido até `DestroyWindow`; o mesmo fixture valida depois controles lógicos, tabulação, ícone copiado, modal e retorno 42; o cenário `dialog` do `runtime_gui_smoke` espera `dialog\n`, sem `WM_QUIT` modal | Fase 13.11 |
| `tl_crash.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, mapeado e executado em processo filho isolado: o convidado acessa o endereço `0`, o hospedeiro observa o `SIGSEGV` via `waitpid`, emite `terminated category="guest-signal" signal="SIGSEGV" fault-address="0x0"` (o crash log captura o `si_addr` no filho e o converte em RVA/seção/importação quando o endereço cai dentro da imagem) e retorna `71` (`GuestFault`) | Diagnóstico de falhas |
| `tl_hang.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado e executado em processo filho isolado com `--timeout 1`: o convidado entra em loop infinito, o hospedeiro o mata com `SIGKILL`, emite `terminated category="guest-timeout"` e retorna `72` (`GuestTimeout`) | Diagnóstico de falhas |
| `tl_memory_limit.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!VirtualAlloc`, `VirtualFree`, `GetLastError`, console e `ExitProcess` | Fixture de contenção: solicita 1 GiB com `--memory 128`; `VirtualAlloc` falha com `ERROR_NOT_ENOUGH_MEMORY`, imprime `memory-limit\n` e retorna `0`. O trace registra a instalação do limite no filho isolado | Limites de recursos |
| `tl_process_limit_parent.exe` / `tl_process_hang.exe` | PE32+ AMD64 | Não | `CreateProcessW`, espera, código de saída, handles e console | O pai cria `tl_process_hang.exe` com `CreateProcessW`; o filho herda `RLIMIT_CPU`, recebe `SIGXCPU` e termina com código observado `1`; o pai imprime `process-limit-inherited\n` e retorna `0` | Herança de limites |
| `tl_thread.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!CloseHandle`, `CreateThread`, `ExitProcess`, `ExitThread`, `GetStdHandle`, `WaitForSingleObject`, `WriteFile` | **Suportado no escopo da Fase 11**: cria duas threads sequenciais, cada uma escreve "Thread done" e termina via `ExitThread`; a thread principal aguarda cada handle, escreve "Main done" e encerra. Metadata e execução e2e passam em Debug, Release e Sanitize (`LSAN_OPTIONS=detect_leaks=0`); saída esperada: `Thread done\nThread done\nMain done\n` e exit `0` | Fase 11 |
| `tl_tls_generic.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | **Suportado no subconjunto TLS:** valida byte inicializado, zero-fill, slot pointer-backed `0x430` e bloco associado zerado; saída `tls-generic\n`, exit `0`. A fixture declara explicitamente o diretório PE TLS para o build sem CRT; unitário e 4 testes CTest passam no Debug | TLS genérico |
| `tl_files_wide.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — arquivos, metadados, tempos e caminhos Unicode | Fixture genérica suportada: cria arquivo com `é`, consulta tamanho/atributos/tempos, copia, move e remove; saída `files\n`, exit `0` | Base de arquivos |
| `tl_resources.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!FindResourceW`, `LoadResource`, `LockResource`, `SizeofResource` | Lê somente o recurso `RCDATA` embutido após validação de limites; saída byte-idêntica ao payload, exit `0`; `--report` não executa | Recursos PE |
| `tl_sync.exe` | PE32+ AMD64 | Não | eventos, mutex, semáforo e esperas em `KERNEL32.dll` | Cobre evento manual/automático, timeout, semáforo, mutex recursivo e `WaitForMultipleObjects`; saída `sync\n`, exit `0` | Sincronização |
| `tl_k32_gap.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!InitializeCriticalSectionAndSpinCount`, `InitializeCriticalSectionEx`, `FormatMessageA`, `AreFileApisANSI` e console | **Suportado no subconjunto:** valida seções críticas, flags inválidas, ACP ANSI fixo e `FormatMessageA` com buffer curto/mensagem de sistema; saída `k32-gap\n`, exit `0`; `--report` resolve 11/11 | Lacunas KERNEL32 do LGHub |
| `tl_globalmem.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!GlobalAlloc`, `GlobalLock`, `GlobalUnlock`, `GlobalFree`, `LocalAlloc`, `LocalFree` e console | **Suportado no subconjunto:** memória móvel/fixa e `ZEROINIT`, lock count e handles inválidos validados; saída `globalmem\n`, exit `0`; `--report` resolve 10/10 | Memória global/local compartilhada |
| `tl_crypt32.exe` | PE32+ AMD64 | Não | `CRYPT32.dll!CertGetNameStringW`, `CertNameToStrW`; `KERNEL32.dll` — console | **Suportado no subconjunto:** lê `CERT_CONTEXT` e `CERT_NAME_BLOB` DER, extrai nomes simples/issuer/DNS por CN e atributos X.500/OID, consulta de tamanho e erros de buffer; saída `crypt32\n`, exit `0`; `--report` resolve 6/6 | Nome de certificado DER |
| `tl_process_parent.exe` / `tl_process_child.exe` | PE32+ AMD64 | Não | `CreateProcessW`, ambiente W, `GetExitCodeProcess`, `TerminateProcess` e `WaitForSingleObject` | Pai cria filhos PE32+ pelo mesmo parser/loader/import resolver e define uma variável que o filho precisa ler, provando a cópia do ambiente Win32; o código de saída real viaja pelo pipe `[flag][exit_code LE32]`. Valida código `7`, encerramento `9` e saída `child\nparent\n`, exit `0`. | Processos filhos |
| `tl_install_setup.exe` / `tl_install_app.exe` | PE32+ AMD64 | Não | arquivos Unicode, ambiente, `GetModuleFileNameW`, `CreateProcessW`, espera e handles | **Fluxo de instalação suportado:** setup externo observa `Z:\\...`, copia a aplicação de `C:\\windows\\temp` para `C:\\Program Files` e a inicia com `CreateProcessW`; a aplicação observa `C:\\...`, diretório herdado e `%LOCALAPPDATA%` do mesmo prefixo. `install → catálogo → app run` é coberto por `integration_install_prefix_catalog_run`; prefixos distintos não compartilham estado. O setup de múltiplos candidatos confirma `InstallPending` (`6`) e a escolha no launcher | Instalação por prefixo |
| `tl_network_loopback.exe` | PE32+ AMD64 | Não | `WS2_32.dll` TCP/UDP, resolução local, `WSAAddressToStringA`, `WSAPoll` e eventos WSA | Fixture somente loopback, com TCP, UDP e `localhost`; valida conversão de endpoint IPv4, `WSAEventSelect`, `WSACreateEvent`, espera com timeout, `FD_ACCEPT` e `WSAEnumNetworkEvents`; passa com sockets permitidos e é skip controlado em sandbox que retorna `EACCES/EPERM` | WS2_32 |
| `tl_worker_rsl.exe` | PE32+ AMD64 | Não | `WS2_32.dll`/`IPHLPAPI.DLL`/`CRYPT32.dll`/`WTSAPI32.dll` + console | Fluxo combinado do Worker: `WSAStartup`/`getaddrinfo` local, enumeração IPv4 via `getifaddrs`, loja CRYPT32 em memória e sessão WTS local; `ExitProcess 0`, ou skip controlado `77` sem interface IPv4 | Worker RSL |
| `tl_wininet.exe` | PE32+ AMD64 | Não | `WININET.dll` — abertura, conexão HTTPS, requisição, cabeçalhos, resposta, leitura, consulta e fechamento; `KERNEL32.dll` — ambiente/console | **Suportado somente para protocolo HTTPS loopback:** o smoke cria servidor TLS e CA efêmeros em `127.0.0.1`, valida URL, cabeçalho, status `200`, leitura parcial e CA confiável; uma CA diferente falha de forma controlada. Sem Internet, proxy, cookies, credenciais, redirecionamento ou WinTrust. | WinINet HTTPS local |
| `tl_stream.exe` | PE32+ AMD64 | Não | `ole32.dll!CreateStreamOnHGlobal`; vtable `IStream` | **Suportado no subconjunto de stream em memória:** `QueryInterface`, referências, `Read`/`Write`, `Seek`, `SetSize`, `Stat`, `Commit`/`Revert`; saída `ole-stream\n`, exit `0` | OLE stream em memória |
| `tl_trust.exe` | PE32+ AMD64 | Não | `WINTRUST.dll!WinVerifyTrust`; `KERNEL32.dll` — console | **Suportado somente na política de blob TLTC:** cadeia DER explícita folha→raiz, UI desabilitada e sem revogação; rejeita raiz incorreta e política incompatível; saída `trust\n`, exit `0` | Cadeia WinTrust local |
| `tl_wthelper.exe` | PE32+ AMD64 | Não | `WINTRUST.dll!WinVerifyTrust`, `WTHelperProvDataFromStateData`, `WTHelperGetProvSignerFromChain`, `WTHelperGetProvCertFromChain`; `CRYPT32.dll!CertGetNameStringW`; `KERNEL32.dll` — console | **Suportado no subconjunto de estado:** cria/fecha estado WinTrust para cadeia TLTC, percorre signer e folha/raiz, extrai o CN DER e rejeita índices inválidos ou uso após `CLOSE`; saída `wthelper\n`, exit `0` | Travessia WTHelper |
| `tl_registry_unicode.exe` | PE32+ AMD64 | Não | `ADVAPI32.dll` chaves/valores Unicode | Cria, persiste, reabre, consulta e remove chave/valor UTF-16 em armazenamento genérico por escopo; saída `registry\n`, exit `0` | Registro |
| `tl_dynload.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `LoadLibraryA/W/ExA/ExW`, `FreeLibrary`, `GetModuleHandleA/W/ExA/ExW`, `GetProcAddress`, `GetLastError` | Fixture de carregamento dinâmico: `LoadLibrary` com caminho `C:\...`, API Set `api-ms-win-core-file-l1-1-0.dll`, `LoadLibraryEx`, `GetProcAddress` por nome e ordinal (36=`GetTickCount64`), `FreeLibrary`, `GetModuleHandleEx` `PIN`/`FROM_ADDRESS`; saída `dynload\n`, exit `0` | Carregamento dinâmico |
| `tl_dynamic_ws2.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!LoadLibraryA`, `GetProcAddress`, `GetStdHandle`, `WriteFile`, `ExitProcess`; exports resolvidos dinamicamente de `WS2_32.dll`: `WSAStartup`, `WSACleanup`, `socket`, `closesocket` | Fixture genérica de WinSock dinâmico: abre/fecha socket IPv4 TCP sem import estático de `WS2_32.dll`; saída `dynamic-ws2\n`, exit `0`; passou em Rust ON e C++ OFF. Em sandbox sem permissão de socket, o erro 13 é limitação ambiental reproduzível | WinSock dinâmico |
| `tl_version.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `GetVersionExA/W`, `VerifyVersionInfoW`, `VerSetConditionMask`, `GetUserDefaultLocaleName`, `LocaleNameToLCID` | Fixture versão/locale: `GetVersionExA/W` 10.0.19044, `VerifyVersionInfoW`/`VerSetConditionMask` cadeia `VER_MAJOR|MINOR`, `GetUserDefaultLocaleName` → `en-US` (6 com NUL, `122` em buffer curto), `LocaleNameToLCID` `en-US`/`pt-BR`; saída `version\n`, exit `0` | Versão/locale |
| `tl_waitaddr.exe` | PE32+ AMD64 | Não | `KERNEL32.dll`/`api-ms-win-core-synch-l1-2-0.dll` — `WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll`, `CreateThread`/`WaitForSingleObject` | Fixture espera por endereço: timeout 50ms `ERROR_TIMEOUT`, size inválido `87`, `WaitOnAddress` 1/2/4/8, thread waiter `WaitOnAddress`→`WakeByAddressSingle`→`WaitForSingleObject`; via `api-ms-win-core-synch-l1-2-0.dll` forwarder; saída `waitaddr\n`, exit `0` | Sincronização por endereço |
| `tl_fiber.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `ConvertThreadToFiber`/`ConvertThreadToFiberEx`/`ConvertFiberToThread`/`CreateFiber`/`CreateFiberEx`/`SwitchToFiber`/`DeleteFiber`/`GetFiberData` | Fixture fibras: `ConvertThreadToFiberEx` com flags, `CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread`; saída `fiber\n`, exit `0` | Fibras |
| `tl_toolhelp.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `CreateToolhelp32Snapshot`/`Process32FirstW`/`Process32NextW`/`OpenProcess`/`GetCurrentProcessId` | Fixture Toolhelp: `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` enumera `/proc`, `Process32FirstW`/`NextW` com `PROCESSENTRY32W` 568 bytes valida `dwSize`, `OpenProcess` via `/proc/[pid]` e `CloseHandle` para snapshot/process; saída `toolhelp\n`, exit `0` | Processos |
| `tl_shell.exe` | PE32+ AMD64 | Não | `SHELL32.dll` — `SHGetKnownFolderPath`/`SHGetFolderPathW`/`SHGetFolderPathAndSubDirW`/`ShellExecuteW`/`ShellExecuteExW` | Fixture SHELL32: `FOLDERID_RoamingAppData`→path Windows, `CSIDL_APPDATA`/`TestSub`; execução Shell retorna falha controlada e `SHELLEXECUTEINFOW.hInstApp`/`hProcess` são limpos nos offsets x64 corretos; saída `shell\n`, exit `0` | Pastas conhecidas |
| `tl_gdiex.exe` | PE32+ AMD64 | Não | `GDI32.dll` — `CreateFontW`/`SetDCBrushColor`/`SetDCPenColor`; `gdiplus.dll` — 8 APIs stub; `UxTheme.dll` — `SetWindowTheme` stub; `WINMM.dll` — `timeSetEvent`; `dbghelp.dll` — `SymFromAddr` | Fixture GDI estendido: valida GDI+ e UxTheme como rejeições controladas (sem ponteiros host), além de `CreateFontW` wide, `SetDCBrush/PenColor`, `timeSetEvent` `1` e `SymFromAddr` stub; `USER32` `GetDC`; saída `gdiex\n`, exit `0` | GDI estendido |
| `tl_com.exe` | PE32+ AMD64 | Não | `ole32.dll` — `CoInitialize`/`CoInitializeEx`/`CoUninitialize`/`CoCreateInstance`/`CoGetClassObject`/`OleInitialize`/`OleUninitialize`/`CoTaskMemAlloc/Free` | Fixture COM mínimo: `CoInitialize` `S_OK`, `CoCreateInstance` `REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`, `OleInitialize`; saída `com\n`, exit `0` | COM mínimo |
| `tl_7zfm_gui.exe` | PE32+ AMD64 | msvcrt | `KERNEL32`, `USER32`, `ADVAPI32`, `SHELL32`, `COMCTL32`, `MPR`, `msvcrt` | **Suportado:** valida menus, notificações de arquivo, drag & drop, shell folders e conexões de rede MPR; imprime `7ZFM GUI APIS OK\n`, exit `0` | 7-Zip GUI |
| `tl_putty.exe` | PE32+ AMD64 | Não | `KERNEL32`, `WS2_32`, `GDI32`, `USER32`, `comdlg32`, `IMM32`, `SHELL32`, `ADVAPI32` | **Suportado no subconjunto:** sockets assíncronos, eventos WSA, fontes GDI, desenho vetorial, caret, barras de rolagem e registro; `ChooseFontW` é uma rejeição controlada; imprime `PUTTY SSH APIS OK\n`, exit `0` | PuTTY SSH Client |
| `tl_notepadpp.exe` | PE32+ AMD64 | Não | `KERNEL32`, `USER32`, `GDI32`, `COMCTL32`, `UxTheme`, `dwmapi` | **Suportado:** DWM composition, temas visuais UxTheme, ImageList, DPI awareness, brushes e polígonos GDI; imprime `NOTEPAD++ APIS OK\n`, exit `0` | Notepad++ |
| `simple_todo.exe` | PE32+ AMD64 | mingw-w64 CRT | 105 imports em `GDI32`, `KERNEL32`, `msvcrt`, `SHELL32` e `USER32` | **Suportado no subconjunto da Fase 12**: fonte pinada no commit `bcdf3d5fcebb8c0b445edb791d54511194c1b6ca` com overlay Linux versionado; build e `--report` resolvem 105/105; `targetapp_simple_todo_gui_smoke` cobre o fluxo principal, persistência, menu da bandeja, encerramento pela bandeja e fechamento da janela, com coordenadas do layout Linux | Fase 12 |

As fontes e manifestos das fixtures ficam em `tests/samples/`. Os binários são produtos de build e ficam em `build/<preset>/tests/samples/generated/`.

### Validação operacional B20.5/B20.6

O teste `integration_rust_operational` usa `tl_compat_file.exe` duas vezes
com o catálogo, um perfil inválido de `tl_hello.exe`, `tl_hang.exe` com
`--timeout 1` e `tl_proton_probe.exe` com um launcher Proton mockado. O teste
exige stdout e exit code preservados, trace de perfil/materialização/limpeza,
remoção dos destinos temporários, fontes preservadas, invisibilidade de
`compat/` e ausência de eventos Rust no build `TL_BUILD_RUST=OFF`. Ele é uma
regressão de integração do runtime. A B20.6 promove a adoção seletiva do
validador lexical Rust, mas não promove nenhuma fixture a aplicativo suportado
nem altera a matriz de compatibilidade de aplicativos reais.

### Promoção seletiva B20.6

A promoção cobre somente a execução opt-in da validação lexical de caminhos no
`app run`. O resultado foi comparado com `TL_BUILD_RUST=OFF`: o caminho C++,
o fallback genérico, stdout, stderr, exit codes, materialização, limpeza e
isolamento permanecem equivalentes; o modo sem Rust não emite eventos
`path-validation`. Os testes também preservam a classificação de uma DLL
conhecida com export ausente como `unknown-symbol`, enquanto uma DLL não
registrada continua sendo `unknown-dll`.

Como evidência operacional da B20.6, as suítes Rust Debug e Release passaram
sem falhas entre 668 testes cada (sem os cinco testes opcionais de Proton real;
quatro testes ambientais foram `skipped`), o gate B20 passou 7/7 em Debug,
Sanitize e Release, e o baseline C++ Debug com `TL_BUILD_RUST=OFF` passou sem
falhas entre 659 testes. O piloto do Proton real passou 5/5 em Debug. A suíte
Sanitize completa foi executada, mas conserva dez falhas
históricas ou ambientais fora do gate promovido; elas envolvem ASan/UBSan em
helpers/fixtures, `RLIMIT_AS`, imagens sem relocations e Xvfb/LSan. Isso não
altera o status das fixtures nem declara suporte automático a aplicativos
reais.

A fixture `native-fixture.msix` é gerada pelo teste `integration_msix_install` a
partir de `tl_hello.exe`. Ela valida o fluxo de pacote nativo: `--report`,
extração segura para o prefixo, cadastro e `app run`; não representa suporte a
bundles, .NET/Mono ou assinatura Authenticode.

## Backend Proton (B14.6)

`tl_proton_probe.exe` é uma fixture de contrato, não um aplicativo suportado
por Proton. O teste `integration_proton_backend` usa um launcher mockado para
validar o caminho `app run` do catálogo: seleção explícita no schema 3,
`proton runinprefix`, variáveis de ambiente, prefixo persistente por aplicativo,
staging do executável, materialização temporária de `files[]`, stdout intacto,
stderr contextualizado e propagação do código `23`.

O mesmo teste configura uma raiz inexistente e confirma retorno `5`
(`Unsupported`) sem executar o runtime nativo. As fixtures
`tl_graphics_probe.exe` e `tl_d3d12_probe.exe` acrescentam pilotos reais
controlados: sob Xvfb e Proton Experimental instalado em `TL_PROTON_ROOT`,
o primeiro cria uma janela, inicializa D3D11 via DXVK, apresenta um frame e
retorna `0`; o segundo cria dispositivo, fila, command list, fence e uma
swapchain flip D3D12, apresenta um buffer e retorna `0` via VKD3D-Proton.
Ambos mantêm os streams e o prefixo isolado. Essa evidência valida somente
esses caminhos mínimos em X11; não representa suporte geral a D3D12, áudio,
entrada, jogos ou aplicações do catálogo. Roblox permanece apenas como alvo
exploratório até existir execução reproduzível com suas limitações publicadas.

O teste `integration_proton_input` acrescenta uma validação real controlada de
entrada. O `proton_input_driver` localiza a janela da fixture no Xvfb, mapeia e
foca sua árvore de janelas e envia movimento, botão esquerdo e a tecla `q`
por X11/XTest. A fixture confirma a sequência Win32 e encerra somente após
`WM_KEYUP` de `VK_Q`, retornando `0`. Essa evidência não cobre raw input,
gamepad/XInput, IME ou semânticas específicas de jogos.

O teste `integration_proton_isolation` conclui o piloto real da B14.6.6 com a
fixture `tl_compat_file.exe` cadastrada duas vezes (`proton-isolation-a` e
`proton-isolation-b`). Cada perfil schema 3 usa uma fonte diferente em
`compat/files/injected.dat`, mas o mesmo destino
`C:\\Program Files\\Compat Fixture\\injected.dat`; a fixture lê o conteúdo
observado e o escreve no stdout antes de alterar o arquivo. Em seguida, o
adaptador limpa somente o materializado, preserva as duas fontes nativas e
mantém o executável estagiado e o manifesto em prefixos separados. A prova
passa em Debug e Sanitize com Proton Experimental real, incluindo trace de
seleção, staging, launch, limpeza e exit code `0`. O teste não transforma a
fixture em aplicativo suportado e não promove o Roblox.

O teste `integration_proton_audio` acrescenta uma validação real controlada de
áudio. A fixture `tl_audio_probe.exe` usa `XAudio2_8.dll` para criar o engine,
uma voz master e uma voz PCM, submeter um buffer silencioso, iniciar/parar a
reprodução e liberar os recursos. O teste passa em Debug e Sanitize com o
Proton Experimental e o servidor de áudio disponível no host. Isso não
constitui garantia de que todo aplicativo emitirá áudio corretamente.

## Portfólio Aplicativos_Windows_Populares (Fase 13 — 2026-08-31)

Ciclo `A→D→B` concluído com uma coleta histórica em Linux. Os `.exe/.dll` em
`Aplicativos_Windows_Populares/` foram reanalisados com `--report`; somente os
cenários explicitamente listados na coluna de execução foram executados. A
coluna `Compat` não substitui a evidência de execução. O relatório atual é
implementado em `src/cli/report.cpp`; `stdout` permanece do convidado e
`stderr` traz `category`/`status`/`detail` e `fault-address` quando há `SIGSEGV`.

Nesta tabela, `Compat` registra o resultado da resolução de imports. Isso não
é uma afirmação de equivalência comportamental: o `--report` também informa
`runtime-support: full|limited|stub` — ou `unresolved` quando a resolução falha —
para exports que resolvem, mas têm semântica parcial ou apenas um retorno controlado.
A execução e os testes do aplicativo
continuam sendo a evidência necessária para registrá-lo como suportado.

| # | Aplicativo | Arquitetura | Imports | `--report` | Execução `--timeout 3` | Observação |
|---|---|---|---:|---|---|---|
| 1 | `7z_x64.exe` | PE32+ x86-64 | 133/133 (100%) | `supported` | smoke CTest cria, lista e extrai ZIP por `7z.dll`, com exit `0` em Rust/C++ | DLL PE ao lado do executável é encontrada, mapeada, anexada e descarregada; criação/listagem/extração do smoke são o subconjunto validado |
| 2 | `7zFM_x64.exe` | PE32+ x86-64 | 298/298 (100%) | smoke Xvfb da A5 com exit `0`; tentativa B2 não revalidada por X11 indisponível | smoke externo `seven_zip_smoke` seleciona `input.txt`, aciona `Copy` (`idCommand=546`), verifica o arquivo copiado e encerra o runtime com exit `0` | janela X11 abre com shell visual experimental na evidência A5; a tentativa B2 registrou falha de inicialização do Xvfb antes do aplicativo e não altera essa conclusão; menu de classe `RT_MENU`/MENUEX real (6 itens de nível superior), dropdowns aninhados e seleção básica por mouse/teclado de itens folha encaminham `WM_COMMAND`; lista imediata do diretório do executável é selecionável, recebe hover e permite navegação visual por pastas com Enter ou duplo clique; árvore lateral recebe hover, retorna à raiz e seleciona diretórios Linux conhecidos; barra `Address` permite navegar somente dentro da raiz visual; toolbar segue os `idCommand` reais, recebe hover, mostra pressão e cancela soltura fora do botão; `Copy` (`idCommand=546`) copia, de forma opt-in, um arquivo selecionado para destino existente dentro da raiz, sem sobrescrever | geometria inválida normalizada para `800x600`; limite da lista em 128 linhas; delay `MPR.dll 6/6`; demais operações ainda não concluídas |
| 3 | `7z.dll` | PE32+ DLL x86-64 | 86/86 (100%) | `imports-resolved` | carregada como dependência do `7z_x64.exe`; não executada como aplicação independente | **Fase 13.A**: o módulo é validado e executado somente como DLL dependente no grafo da execução |
| 4 | `putty_x64.exe` | PE32+ x86-64 | 348/348 (100%) | `guest-timeout` exit `72` no fluxo geral e no probe SSH local | D2 abre/fecha `PuTTY Configuration`; E31 preenche host/porta, aciona `Open` e alcança a janela principal `PuTTY`, mas o listener local recebe zero bytes em Rust ON/C++ OFF. O SSH completo continua não validado; FLS 0/1 ok; não promover como suporte geral |
| 5 | `WinRAR_x64.exe` `winrar-x64-723.exe` | PE32+ x86-64 | 251/251 (100%) | `supported` | `ExitProcess 0` com extração real completa (28 arquivos, incluindo `WinRAR.exe`, `Rar.exe`, `License.txt`) no smoke automatizado `winrar_extract_real_smoke`; sob Xvfb, o smoke de cancelamento termina com `ExitProcess 0` após `WM_DELETE_WINDOW` | delay `GDI32/ADVAPI32/SHELL32/ole32`; extração de arquivo SFX validada de ponta a ponta com exit `0` e integridade de arquivos conferida; uso diário GUI interativo completo continua experimental |
| 6 | `Rufus_x64.exe` | PE32+ x86-64 | 14/14 (100%) | `supported` | `map-failed` exit `4` | `UPX1` é marcada `rwx`; o loader aplica W^X, mapeia a combinação como `RW` e rejeita o entry point antes da execução, sem página `RWX` |
| 7 | `HWiNFO64.exe` | PE32+ x86-64 | — | `malformed` (PE empacotado) | `not-attempted` | `UPX0` tem `SizeOfRawData=0`, enquanto o diretório de exports aponta para RVA sem bytes no arquivo; o desempacotamento permanece fora do escopo |
| 8 | `RobloxPlayerInstaller.exe` | PE32+ x86-64 | 430/430 (100%) | `execution-failed` | `RBXCRASH FatalRuntimeError Worker,28` `ExitProcess 3` (antes `SIGSEGV 0x68 rva 0x39ab exit 71`) | **Fase 13.D**: imports resolvidos, mas o fluxo ainda não conclui com sucesso; o slot TLS específico continua sendo benchmark, não suporte declarado |
| 9 | `Rockstar-Games-Launcher.exe` | PE32+ x86-64 | 338/338 (100%) | `execution-failed` | `ExitProcess 3` | imports resolvidos; fluxo principal ainda não validado como concluído |
| 10 | `Logitech_GHUB_x64.exe` `lghub_installer.exe` | PE32+ x86-64 | 114/114 (100%) | `supported` | `GuestTimeout 72` durante a inicialização | imports resolvidos; o fluxo do instalador não foi concluído e não é suporte funcional |
| 11 | `notepad++.exe` | PE32+ x86-64 | 584/584 (100%) | `guest-timeout` exit `124` no fluxo sem interação; execução controlada observada com `ExitProcess(3)` | a corrupção de heap da C2 foi corrigida na D1; em F2, `shlwapi` (`PathFileExistsA/W`, `PathIsDirectoryA/W`) passou a usar resolução canônica (`translate_windows_path`), eliminando o erro de `Load stylers.xml failed`. O smoke verifica que esse erro não reaparece; no caminho direto atual o import de `WinVerifyTrust` é resolvido, mas a exceção C++ `0xE06D7363` ocorre antes da chamada e termina controladamente em `ExitProcess(3)` com `unsupported-cxx-handler-during-search` | não declarar suporte GUI: exceções C++/SEH fora do `FuncInfo` v3 são o bloqueio atual; `WinVerifyTrust` para arquivos continua como gate posterior e o fluxo principal permanece não validado |
| 11a | `Notepad++/updater/GUP.exe` | PE32+ x86-64 | 149/153 (97%) | `unsupported` | exit `5` antes do entry point | o `libcurl.dll` local é encontrado e mapeado, mas a cadeia de imports rejeita `WLDAP32.dll` com 18 ordinais não registrados; a imagem principal é desmontada sem execução ou instalação |
| 12 | `RTSSHooks64.dll` | PE32+ DLL x86-64 | 256/256 (100%) | `imports-resolved` | `not-attempted` (DLL) | **Fase 13.RTSS**: imports resolvidos para análise; `CreateRemoteThread` e `WriteProcessMemory` agora falham com `ERROR_NOT_SUPPORTED` (sem fingir execução remota). O restante inclui `GDI32 ...`, `USER32 ...`, `KERNEL32 ...`, `SHLWAPI ...`, `WINMM ...`, `SETUPAPI 7` e `delay DirectX 11`; os stubs DirectX retornam `E_FAIL/S_OK` controlados |
| 13 | `Affinity x64.msix` | Zip/MSIX ZIP64 | — | `malformed` exit `4` | `not-attempted` | Rust e C++ rejeitam o pacote no limite agregado de 512 MiB antes da extração; não houve instalação, cadastro ou execução. O conteúdo interno `.NET` continua fora do escopo e o pacote permanece sem suporte funcional |
| 14 | `*_x64_Installer.exe` `CapCut/Epic/Creative/Everything/RTSS.exe` | PE32 (x86) | — | `unsupported-architecture` `0x14c` `exit 5` | `parse-failed status="unsupported-architecture"` `src/pe/pe_reader.cpp:685` |

| 2a | `7-Zip/7zG.exe` | PE32+ x86-64 | 208/208 (100%) | `supported` | sob Xvfb uma execução controlada de `a` cria a janela `Progress`, gera um arquivo 7z válido e termina com exit `0`; `7z_x64.exe l` confirma `input.txt`; repetições controladas também terminam em `guest-timeout 72` | `lstrcatW`, `PostMessageA/W` cross-thread e o trace genérico de threads/eventos possuem fixtures; no caso intermitente o worker/eventos concluem, mas não há `WM_TIMER` antes do timeout e a fila modal permanece investigada |

### Atualização do corpus externo — rodada B (2026-09-07)

A matriz B1 analisou 26 PE e 1 MSIX em Rust ON e C++ OFF. Os exit codes
coincidiram em todas as entradas (`13` com `0`, `12` com `5` e `2` com `4`),
e o stdout foi byte-a-byte igual. A B2 executou somente PE32+ não-DLL
selecionados com limites controlados; a tentativa inicial de Xvfb falhou antes
dos casos GUI e foi registrada como skip ambiental. A C2 repetiu 7-Zip File
Manager, PuTTY e Notepad++ sob Xvfb válido, com resultados ON/OFF iguais:
timeouts controlados para os dois primeiros e o bloqueio de heap então observado
para o último. A D1 corrigiu a largura guest/host das quatro APIs `lstr*W` e
repetiu o Notepad++ com Rust ON/C++ OFF: ambos agora terminam em timeout
controlado `124`, com stdout e trace semântico iguais e sem corrupção de heap.
A B3 testou apenas os candidatos PE32+ e MSIX em prefixos temporários. Os
instaladores PE32/x86, DLLs e o HWiNFO empacotado não foram executados como
aplicativos.

A C3 repetiu a instalação dos três candidatos PE32+ e do MSIX em Rust ON e
C++ OFF, com `--trace`, prefixos temporários e limites controlados. Roblox
terminou no setup com `ExitProcess(3)`; G HUB e seu alias terminaram no setup
com `ExitProcess(1)`. Affinity foi rejeitado no `package-parse` antes da
extração, com Rust registrando o limite estruturado e C++ mantendo a mesma
etapa. stdout foi vazio e idêntico nos oito casos, sem eventos `extracted` ou
`registered`; nenhum prefixo recebeu arquivo. Isso diagnostica término do
setup e limite seguro do pacote, não suporte funcional de instalação.

A validação D3 repetiu os mesmos três casos após a reorganização das extensões
por aplicativo. Os pares Rust ON/C++ OFF preservaram stdout e exit code:
Roblox `3`, G HUB `1` e Affinity `4`. Todos os prefixos e diretórios APPDATA
isolados ficaram sem arquivos, e nenhum trace registrou `extracted` ou
`registered`; a diferença esperada ficou restrita ao diagnóstico estruturado
`package-parse` do backend Rust para o Affinity.

Esses resultados são uma atualização da matriz de evidência, não uma promoção
geral de compatibilidade: `exit 0` em uma execução controlada indica apenas
que aquele cenário terminou, e falhas `3`, `4`, `71` e `72` permanecem
limitações explícitas.

> Detalhe das novas APIs `B`: `GDI32.dll!Arc` `SHLWAPI.dll!PathIsUNCW/PathIsUNCA` `MSIMG32.dll!AlphaBlend/TransparentBlt` `NETAPI32.dll!NetApiBufferFree` `OLEACC.dll!LresultFromObject` `tdh.dll!TdhGetPropertySize` `WINSPOOL.DRV!OpenPrinterW/ClosePrinter` `WTSAPI32.dll!WTSFreeMemory` — todas registradas para resolver imports; `OpenPrinterW` falha com `ERROR_NOT_SUPPORTED` quando a operação é chamada.

### Evidência E28 — probe SSH local do PuTTY (2026-09-07)

O alvo `tests/apps/putty/putty_ssh_smoke.cpp`, registrado no CTest como
`putty_ssh_local_probe`, inicia um listener TCP privado em `127.0.0.1` numa
porta efêmera e configura a janela `PuTTY Configuration` por eventos X11 de
teclado. O servidor aceita no máximo uma conexão, lê no máximo 256 bytes e
considera válido somente um banner `SSH-*` terminado por `\r\n`; não há acesso
à Internet.

O probe passou nos builds `build/debug-rust` e `build/debug`, com Rust ON e
C++ OFF, mas os dois chegaram ao mesmo primeiro bloqueio: a configuração foi
criada, nenhum byte foi recebido pelo listener e o convidado terminou por
`guest-timeout` `72`. O teste registra essa limitação como resultado esperado,
não como sucesso SSH. O prefixo, o servidor local, o Xvfb e os processos do
próprio cenário são limpos ao final; nenhum código de runtime, DLL, shim ou
regra específica do PuTTY foi adicionado.

### Evidência E31 — matriz recursiva e ação `Open` do PuTTY (2026-09-07)

`popular_apps_recursive_report_matrix` percorre o corpus pinado e analisou 64
arquivos PE, DLL e MSIX nos builds Rust ON e C++ OFF. Os dois resultados foram
idênticos: 25 sucessos, 2 rejeições estruturais e 37 formatos/arquiteturas não
suportados. A matriz usa somente `--report`; não inicia DLLs, instaladores ou
pacotes rejeitados.

O `putty_ssh_local_probe` passou nos dois builds com o mesmo resultado
controlado: preencheu host e porta, acionou `Open` e confirmou a janela
principal `PuTTY`, mas o listener TCP local recebeu zero bytes; o convidado
terminou com `guest-timeout` `72`. A inspeção de rede não observou `socket` ou
`connect` do convidado. Isso confirma o alcance da ação genérica de abertura,
não um handshake SSH nem suporte funcional ao PuTTY.

## Aplicativos Windows Populares (histórico)

| Aplicativo | Arquitetura | Imports Resolvidos | Compatibilidade | Estado de Execução |
|---|---|---:|---|---|
| **7-Zip File Manager (`7zFM_x64.exe`)** | PE32+ x86-64 | 100% (298/298) | Fluxo principal restrito | Menu de classe MENUEX real, dropdowns aninhados, seleção básica por mouse/teclado de itens folha, toolbar orientada pelos `idCommand` reais com captura de pressão e hover, endereço editável restrito à raiz visual, navegação lateral interativa com hover, lista selecionável com hover e navegação visual por pastas via Enter ou duplo clique e status aparecem; `WM_COMMAND` básico pode ser encaminhado; o smoke externo versionado seleciona um arquivo e conclui `Copy` (`546`) dentro da raiz, sem sobrescrever; o CTest pode registrá-lo com o corpus; demais operações continuam limitadas |
| **7-Zip CLI (`7z_x64.exe`)** | PE32+ x86-64 | 100% (133/133) | Suportado no subconjunto | Smoke isolado cria, testa, remove, atualiza e renomeia entradas no ZIP `stored`, além de criar/listar/extrair ZIP `DEFLATE` e arquivos `7z` com `LZMA2` normal e protegido por senha, pela `7z.dll` carregada do diretório da aplicação; preserva `input-data/café-日本.txt`, verifica `renamed.txt`, exercita staging com espaços, caminhos relativos ao diretório de trabalho, o filtro stdin/stdout (`-si`/`-so`) e sobrescrita existente com `-aoa`, confirma rejeição controlada de senha incorreta sem payload válido e termina com exit `0` nos fluxos válidos em Rust ON/OFF |
| **PuTTY SSH Client (`putty_x64.exe`)** | PE32+ x86-64 | 100% (348/348) | Parcial: configuração e janela de sessão; conexão não alcançada | C2 do fluxo geral e E31 terminam em `GuestTimeout 72`; D2 abre/fecha `PuTTY Configuration`, enquanto o probe preenche host/porta, aciona `Open`, alcança a janela `PuTTY` e observa zero bytes enviados em Rust ON/C++ OFF; SSH completo ainda não é suportado |
| **WinRAR (`WinRAR_x64.exe`)** | PE32+ x86-64 | 100% (251/251) | Suportado | Inicializou FLS, subsistema CRT e APIs do Shell/OLE com sucesso; extração completa de arquivos validada por `winrar_extract_real_smoke` (28 arquivos, incluindo `WinRAR.exe`, `Rar.exe`, `License.txt`) com exit 0; smoke SFX de cancelamento também validado com exit 0 |
| **HWiNFO64 (`HWiNFO64.exe`)** | PE32+ x86-64 | — | Análise estrutural rejeitada | `UPX0` não tem dados crus para o RVA do diretório de exports; desempacotamento não é implementado |
| **Roblox Player Installer (`RobloxPlayerInstaller.exe`)** | PE32+ x86-64 | 100% (430/430) | Não suportado como fluxo concluído | Fase 13.D — `RBXCRASH` + `ExitProcess 3` (antes `SIGSEGV`); resolução de imports e correção TLS não equivalem a suporte |
| **Notepad++ (`notepad++.exe`)** | PE32+ x86-64 | 100% (584/584) | Não suportado como fluxo concluído | D1 corrigiu a ABI guest UTF-16 das APIs `lstr*W`; F2/F3 normalizou a resolução de caminhos em `shlwapi` (`PathFileExistsA/W`) e as pastas especiais de `shell32` (`SHGetKnownFolderPath`, `SHGetFolderPathW`) para caminhos Windows canônicos. O smoke confirma a ausência de `Load stylers.xml failed`; no caminho direto atual, `WINTRUST.dll!WinVerifyTrust` é resolvido mas não chamado antes da exceção C++/SEH, que termina em `ExitProcess(3)` com `unsupported-cxx-handler-during-search`; fluxo principal permanece não validado |
| **Rufus (`Rufus_x64.exe`)** | PE32+ x86-64 | 100% (14/14) | Análise aprovada; execução rejeitada | UPX marca `UPX1` como `rwx`; o loader mantém W^X e retorna `map-failed` antes do entry point |
| **7-Zip Installer / Notepad++ Installer / Everything Search** | PE32 (x86) | — | Unsupported | Rejeitados controladamente como arquitetura x86 32-bit (0x14c) |
