# Roadmap do TradutorLinux

Este arquivo acompanha a execução do projeto. O documento de visão, escopo e arquitetura está em [PROJETO.md](PROJETO.md).

## Como usar este roadmap

Cada fase só deve avançar quando seus critérios de saída estiverem atendidos. Uma API nova entra no projeto apenas quando existir um teste ou aplicativo-alvo que justifique seu comportamento.

Os itens marcados como concluídos devem ter evidência no repositório: código, teste, documentação ou um artefato reproduzível. O roadmap descreve ordem de dependências, não uma promessa de prazo.

## Stack decidido

- **Linguagem principal:** C++20.
- **C:** estruturas PE, interfaces C e trechos que precisem de ABI simples.
- **Assembly x86-64:** somente trampolins, bootstrap ou outras fronteiras que não possam ser expressas com segurança pelo compilador.
- **Build:** CMake + Ninja.
- **Hospedeiro inicial:** Linux x86-64.
- **Binários de teste:** PE32+ x86-64 produzidos com `mingw-w64`.

## Estado atual

- **Fase atual:** Fase 13 — compatibilidade ampla por portfólio.
- **Marco concluído:** a Fase 7 foi validada de ponta a ponta e a decisão de produto foi tomada: **seguir com a GUI Win32 mínima como objetivo experimental**. `tl_gui.exe` abriu a janela X11, recebeu o clique em OK e encerrou com código `0`; `tl_win.exe` criou uma janela real e executou um message loop completo (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `PostQuitMessage`), encerrando via `WM_CLOSE`/autoclose com código `0`; o modo `--report` lista imports suportados sem executar o PE; `tl_hello`, `tl_echo` e `tl_file` têm regressões e limitações publicadas na matriz.
- **Marco concluído:** o smoke test de GUI passou a ter cobertura automática em CI. O teste `runtime_gui_smoke` sobe um `Xvfb` próprio e executa `tl_win.exe` de ponta a ponta em dois cenários: autoclose (message loop encerra sozinho via `WM_QUIT`) e fechamento real por `WM_DELETE_WINDOW` (mesmo `ClientMessage` do botão de fechar do WM), exigindo exit-code `1`, stdout vazio e os eventos esperados no trace. A conexão X11 do runtime é fechada no teardown (`DisplayCloser`), validado sob ASAN com `detect_leaks=1`.
- **Marco concluído:** `CreateWindowExA` agora despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` (retorno `-1` aborta a criação e devolve `NULL`). A fixture `tl_win.c` marca uma flag no `WM_CREATE` e propaga no exit code via `PostQuitMessage`, então o `runtime_gui_smoke` prova o despacho exigindo exit-code `1`.
- **Marco concluído:** o message loop ganhou entrada real de teclado. `KeyPress` X11 vira `WM_KEYDOWN` (virtual key: letras em maiúsculas) com o caractere guardado; `TranslateMessage` converte em `WM_CHAR` enfileirado (entregue antes dos próximos eventos X11) e registra o evento de trace `TranslateMessage message="WM_CHAR" wparam status="translated"`. A fixture `tl_win.c` encerra a janela ao receber `WM_CHAR('q')`, e o terceiro cenário do `runtime_gui_smoke` envia um `KeyPress` sintético sob `Xvfb` e exige exit-code `3`. O teste agora sobe sempre um `Xvfb` próprio (sem window manager): com WM a janela é reparentada e o `XSendEvent` para o frame não chega ao cliente.
- **Marco concluído:** o pump passou a usar fila de eventos por janela. Todos os eventos X11 pendentes são demultiplexados para a fila da janela-alvo a cada consulta, então nada se perde entre janelas independentemente da ordem do message loop. A fixture `tl_win2.exe` cria duas janelas simultâneas com `WNDPROC`s independentes ("Janela A" e "Janela B"); o quarto cenário do `runtime_gui_smoke` envia `KeyPress 'q'` à A e `'k'` à B e exige exit-code `15` (flags 1+2+4+8), provando o roteamento independente por janela.
- **Marco concluído:** diagnóstico de falhas com isolamento em processo filho (ideia §1). O convidado agora executa em um processo filho (`fork`/`waitpid`); o pai prepara o PE, o mapeamento e os imports, e o filho só executa o entry point e reporta o resultado por um pipe antes de `_exit`. Sinais fatais do filho são restaurados para `SIG_DFL` para que um `SIGSEGV` do convidado não seja engolido por handlers do hospedeiro (ex.: AddressSanitizer). O pai distingue saída normal de término por sinal: no término por sinal emite `[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"` e retorna `71` (`GuestFault`), novo código de saída do hospedeiro. A fixture `tl_crash.exe` (sem imports) acessa o endereço `0` e valida o diagnóstico no preset `sanitize` (onde o ASan interceptaria o sinal sem o reset), no `debug` e no `release`; o exit code do convidado continua propagado integralmente pelo pipe (não truncado pelo status POSIX).
- **Marco concluído:** a Fase 8 começou com a definição dos primeiros aplicativos-alvo reais, de código aberto e compilados em CI: `xxd` (vim `v9.2.0957`), `bzip2` (`1.0.8`) e `dos2unix`/`unix2dos` (`7.5.6`). O módulo `tests/targets` baixa as fontes pinadas por hash SHA-256, faz o cross-build com `mingw-w64` (opção `TL_BUILD_TARGET_APPS=ON`, job `target-apps` do CI) e protege os imports reais em manifests via `llvm-readobj` e `--report` (8 testes, label `targetapp`). Nenhum alvo executava ainda: todos importam `msvcrt.dll` (fora de escopo até a Fase 9) e o `--report` os classificava como `result: unsupported` / `execution: not-attempted`. Os imports capturados (xxd: 73 símbolos; bzip2: 69; dos2unix/unix2dos: 88, incluindo `SHELL32.dll!CommandLineToArgvW`) guiam o subconjunto mínimo de CRT da Fase 9.
- **Marco concluído:** o subconjunto mínimo de `msvcrt.dll` foi implementado e registrado (57 símbolos, ordinais 1–57), junto com as 14 APIs de `KERNEL32.dll` que o CRT interno do mingw e o `xxd.exe` exigem (`VirtualQuery`/`VirtualProtect` via `/proc/self/maps` + `mprotect`, `MultiByteToWideChar`/`WideCharToMultiByte` com CP 0/1252/65001, critical sections no-op para convidado single-thread, `TlsGetValue`, `GetConsoleMode`/`SetConsoleMode`, `Sleep`, `SetUnhandledExceptionFilter`, `IsDBCSLeadByteEx`). O `--report` do `xxd.exe` passou a `result: supported`.
- **Marco concluído:** `xxd.exe` executa de ponta a ponta com saída **byte-idêntica** ao `xxd` do sistema (exit `0`). Para isso a fronteira agora aloca um TEB de uma página e aponta o segmento `%gs` via `arch_prctl(ARCH_SET_GS)` durante a execução do convidado (o mingw lê `%gs:[0x30]` no `__mingw_CRTStartup`), restaurando o `GS` e liberando o TEB em seguida. O teste e2e fixa um ouro em `tests/targets/golden/xxd/` (entrada de 4880 bytes que cruza a coluna de offset em `0x1000`) e verifica em CTest: modo padrão, `-p` (plain) e caminho de erro (arquivo inexistente → exit `2`, stderr não vazio), 3 testes novos com label `targetapp`. As conversões de código de página, `VirtualQuery`/`VirtualProtect`, `TlsGetValue`, critical sections e o subconjunto de CRT têm 42 testes unitários novos (`test_win32.cpp`, `test_msvcrt.cpp`). Total: 180 testes verdes no preset com alvos; 169 em `debug`, `release` e `sanitize`.
- **Marco concluído:** `bzip2.exe` executa de ponta a ponta nos dois sentidos. `__iob_func()` devolve o ponteiro do array `GuestFile` (o convidado indexa `[0..2]` com `sizeof(_iobuf)` = 48 bytes — o argumento `rcx` é ignorado, como no CRT MSVC clássico) e `_stat64` passou de stub `ENOSYS` para implementação real que preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) via `stat()` do host; a verificação `S_ISREG` do bzip2 (`testb $0x40, +7`) depende dos bits de tipo do Linux, idênticos aos do Windows (`S_IFREG = 0x8000`). Compressão (`-c`) e descompressão (`-d`, `-d -c`) de arquivo são byte-idênticas ao `bzip2` nativo; e2e em CTest (ouro em `tests/targets/golden/bzip2/` e `golden/bzip2_decompress/`, comparação binária via `verify_target_run_bytes.cmake`), 2 testes novos com label `targetapp`, e 3 testes unitários novos para `_stat64` (`test_msvcrt.cpp`). Total: 265 testes verdes no preset com alvos; 218 em `debug` e `sanitize`.
- **Marco concluído:** `tl_thread.exe` fecha a validação do subconjunto atual de concorrência. O fixture cria duas threads convidadas sequenciais, cada uma escreve `Thread done`, termina via `ExitThread` e é aguardada por `WaitForSingleObject`; a thread principal escreve `Main done` e encerra com código `0`. Metadata e execução passam em Debug, Release e Sanitize (`LSAN_OPTIONS=detect_leaks=0`); a regressão é coberta por `fixture_tl_thread_metadata` e `runtime_tl_thread_matches_readobj`. A expansão seguinte de concorrência e WinSock passou a ser validada pelas fixtures genéricas descritas no marco abaixo.
- **Marco concluído:** a primeira entrega genérica do plano foi validada por fixtures próprias. `tl_files_wide.exe` cobre arquivos Unicode, posição, metadados, tempos, cópia e movimentação; `tl_resources.exe` acessa `RCDATA` somente leitura com limites validados; `tl_sync.exe` cobre eventos, mutex recursivo, semáforo, timeouts e `WaitForMultipleObjects`; `tl_process_parent.exe` cria filhos PE32+ pelo mesmo loader e testa código de saída/encerramento; `tl_network_loopback.exe` cobre TCP/UDP local, `localhost` e `WSAPoll`; `tl_registry_unicode.exe` cobre armazenamento genérico persistente Unicode. Cada fixture possui manifesto, metadata, execução e `--report`, sem tratamento específico para Roblox.
- **Marco concluído:** o alvo pinado `Efeckc17/simple-todo-c` (`bcdf3d5fcebb8c0b445edb791d54511194c1b6ca`) compila como PE32+ x64 com manifesto/ícone, overlay Linux versionado e metadata/`--report` protegidos. O relatório resolve **105/105 imports**; `USER32` possui controles lógicos EDIT/BUTTON/COMBOBOX/STATIC/SysListView32, comandos/notificações, foco, teclado e fechamento nativo; `SHELL32` usa menu X11 como bandeja emulada, sem a opção de autorun exclusiva do Windows. O smoke foi atualizado para o layout Linux e cobre adicionar, editar, buscar, concluir, excluir, esconder, mostrar, persistência e saída pela bandeja ou pela janela.
- **Marco concluído:** `dos2unix.exe` e `unix2dos.exe` executam o fluxo de conversão validado. O `--report` resolve 91/91 imports em cada binário; regressões e2e cobrem CRLF→LF, LF→CRLF e expansão de `uni_el_*.txt` com nome UTF-8. Os testes `targetapp_dos2unix_eol`, `targetapp_unix2dos_eol` e `targetapp_dos2unix_unicode-glob` passam com exit `0`; os arquivos de entrada CRLF/LF vêm da fonte pinada do dos2unix e o ouro UTF-8 está versionado em `tests/targets/golden/dos2unix/`.
- **Marco concluído:** carregamento dinâmico `KERNEL32` completo em `tl_dynload.exe` (`LoadLibraryA/W`, `LoadLibraryExA/W`, `FreeLibrary`, `GetModuleHandleExA/W`, `GetProcAddress` por nome e ordinal). A fixture prova `C:\Windows\System32\kernel32.dll` (extração de filename), API Set `api-ms-win-core-file-l1-1-0.dll` via forwarder, `LoadLibraryEx` com flags ignoradas, `GetProcAddress("GetTickCount64")` chamado dinamicamente, `FreeLibrary` e `GetModuleHandleEx` com `PIN`/`FROM_ADDRESS` (token `0x1000` e endereço `tl_entry`). `--report` resolve 14/14 imports, execução `dynload\n` exit `0`.
- **Marco concluído:** versão e locale `KERNEL32` em `tl_version.exe` (`GetVersionExA/W` 10.0.19044 `VER_PLATFORM_WIN32_NT`, `VerifyVersionInfoW`/`VerSetConditionMask` chain `VER_MAJOR|MINOR` `GREATER_EQUAL`, `GetUserDefaultLocaleName` `en-US` com `ERROR_INSUFFICIENT_BUFFER` e `LocaleNameToLCID` `en-US`→`0x0409`/`pt-BR`→`0x0416` case-insensitive). `--report` 10/10, `version\n` exit `0`.
- **Marco concluído:** espera por endereço `KERNEL32`/`api-ms-win-core-synch-l1-2-0.dll` em `tl_waitaddr.exe` (`WaitOnAddress` 1/2/4/8 alinhado, `ERROR_TIMEOUT` 1460, `ERROR_INVALID_PARAMETER` 87, `WakeByAddressSingle`/`All` com `version`+`cv`). Thread waiter bloqueia 5s e acorda via `Wake`, `waitaddr\n` exit `0`, `--report` 12/12.
- **Marco concluído:** fibras `KERNEL32` em `tl_fiber.exe` (`ConvertThreadToFiber`/`ConvertThreadToFiberEx` com flags, `CreateFiber`/`CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread` com `g_current_fiber_data`). `--report` 11/11, `fiber\n` exit `0`.
- **Marco concluído:** enumeração de processos `KERNEL32` em `tl_toolhelp.exe` (`CreateToolhelp32Snapshot` `TH32CS_SNAPPROCESS` via `/proc`, `Process32FirstW`/`NextW` `PROCESSENTRY32W` 568, `OpenProcess` token `kProcessHandleBase+pid`, `CloseHandle` para snapshot/process). `--report` 10/10, `toolhelp\n` exit `0`.
- **Marco concluído:** GUI Unicode `USER32` em `tl_win_w.exe` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` via `wide_to_utf8`). `--report` 12/12, execução `Xvfb` análoga a `tl_win` com `WM_CREATE` e `WM_CLOSE`.
- **Marco concluído:** `SHELL32` pastas conhecidas em `tl_shell.exe` (`SHGetKnownFolderPath` `FOLDERID_RoamingAppData`→`$HOME/.config`, `SHGetFolderPathW` `CSIDL_APPDATA`, `SHGetFolderPathAndSubDirW` `TestSub`, `ShellExecuteW` `42`, `ShellExecuteExW` dummy `hProcess`). `--report` 8/8, `shell\n` exit `0`.
- **Marco concluído:** `GDI` estendido em `tl_gdiex.exe` (`GDI32` `CreateFontW`/`SetDCBrush/PenColor`, `gdiplus` 8 APIs, `UxTheme` `SetWindowTheme`, `WINMM` `timeSetEvent`, `dbghelp` `SymFromAddr`, `USER32` `GetDC`). `--report` 21/21, `gdiex\n` exit `0`.
- **Marco concluído:** `COM` mínimo `ole32.dll` em `tl_com.exe` (`CoInitialize`/`CoInitializeEx`/`CoUninitialize`/`OleInitialize`/`OleUninitialize` `S_OK`, `CoCreateInstance`/`CoGetClassObject` `REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`, `CoTaskMemAlloc/Free`). `--report` 12/12, `com\n` exit `0`.
- **Marco concluído (Fase 13.1):** `tl_install_setup.exe` instala
  `tl_install_app.exe` em `C:\\Program Files` de um prefixo exclusivo, cria o
  processo-filho no mesmo ambiente e o runtime cadastra/reabre a aplicação.
  `integration_install_prefix_catalog_run` cobre descoberta automática,
  `--app-exe`, múltiplos/nenhum candidato e isolamento; `qt_launcher_smoke`
  cobre o botão **Instalar** e a atualização da biblioteca. O benchmark WinRAR
  permanece `unsupported`.
- **Marco concluído (Fase 13.2):** o leitor, `--report` e o resolvedor tratam
  `IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT` em descritores RVA (`grAttrs=0x1`),
  classificam cada símbolo e preenchem a IAT antes do entry point. A fixture
  reproduzível `tl_delay_import.exe` executa usando somente
  `KERNEL32.dll!ExitProcess` atrasado; o caso de símbolo ausente retorna `5`
  com diagnóstico `mechanism="delay-import"`. WinRAR e Rockstar foram
  reanalisados apenas com `--report` e continuam `unsupported` pelas APIs
  restantes.
- **Marco concluído (Fase 13.3):** o núcleo reutilizável de unwinding AMD64
  lê e valida `.pdata`/`.xdata` v1, todos os opcodes x64 v1, handlers e
  cadeias; `RtlCaptureContext`, `RtlLookupFunctionEntry`,
  `RtlVirtualUnwind` e `RtlPcToFileHeader` são exports `KERNEL32`. A fixture
  `tl_unwind.exe` captura o contexto, desempilha um frame real e imprime
  `unwind\n`; CTest protege parser, APIs, trace e `--report`. Não há despacho
  SEH nem execução de handlers. WinRAR encontra `UNWIND_INFO` v2 e Rockstar
  um `SET_FPREG` não canônico; ambos permanecem `unsupported` e não foram
  executados.
- **Marco concluído (Fase 13.4):** o núcleo aceita `UNWIND_INFO` V1/V2,
  normaliza epílogos V2 e aceita `UWOP_SET_FPREG` estendido somente quando
  `OpInfo == FrameOffset`. `tl_unwind_v2.exe` prova desempilhamento no corpo,
  trace e `--report`; em epílogo V2 o contexto é preservado e há diagnóstico
  controlado, sem decodificação de instruções. WinRAR (151/251) e Rockstar
  (191/338) foram reanalisados somente com `--report`, continuam
  `unsupported` pelas APIs e pelo despacho SEH ausentes e não foram executados.
- **Marco concluído (Fase 13.5):** o despacho SEH explícito usa
  `RaiseException`, VEH, `__C_specific_handler`, `RtlUnwind`/`RtlUnwindEx` e
  filtro não tratado sobre `.pdata/.xdata` V1/V2 fora de epílogos. As fixtures
  `tl_seh.exe` e `tl_seh_v2.exe` executam `__try/__except` dentro de
  `CreateThread`, imprimem `seh\n` e são protegidas por metadata, trace,
  relatório e execução. WinRAR (153/251) e Rockstar (194/338) foram
  reanalisados apenas com `--report`, continuam `unsupported` e não foram
  executados.
- **Marco concluído (Fase 13.6):** `tl_locale_env_fls.exe` valida ambiente
  Win32 por processo (incluindo bloco UTF-16, expansão e `msvcrt!getenv`),
  ACP `1252`/OEMCP `437`, CP437, locale determinístico `en-US`,
  `GetLocaleInfoW`, `LCMapStringW/Ex` e FLS por thread com callbacks no fim
  da thread e em `FlsFree`; imprime `locale-env-fls\n`. Metadata, `--report`,
  trace e execução são protegidos por CTest. WinRAR (166/251) e Rockstar
  (207/338) foram reanalisados somente com `--report`, continuam
  `unsupported` e não foram executados.
- **Marco concluído (Fase 13.7):** `tl_locale_extended.exe` valida o locale
  estático `en-US` por `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoEx`,
  `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW` e
  `GetTimeFormatW`; enumera somente `0409` por callback Microsoft x64 e
  imprime `locale-extended\n`. Metadata, `--report`, trace e execução passam
  em CTest. WinRAR (170/251), Logitech G HUB (92/114) e Rockstar (213/338)
  foram reanalisados somente com `--report`, reduziram lacunas e continuam
  `unsupported`; nenhum binário comercial foi executado.
- **Marco concluído (Fase 13.8):** `tl_process_console.exe` valida
  `STARTUPINFOW`, handles padrão mutáveis, tipo de arquivo, console UTF-16,
  `C:\Windows\System32`, recursos AMD64, encode/decode de ponteiro e SList
  alinhada; imprime `process-console-é\n`. Metadata, `--report`, trace e
  execução passam em Debug e Sanitize. WinRAR (179/251), Logitech G HUB
  (103/114) e Rockstar (223/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.9):** `tl_file_metadata.exe` valida
  `FindFirstFileExW`, atributos, `FileBasicInfo`, `FileDispositionInfo` e
  `FileDispositionInfoEx` dentro de `C:\\` no prefixo; cobre exclusão no
  fechamento, exclusão POSIX e isolamento entre prefixos. Metadata, `--report`,
  trace e execução passam em Debug e Sanitize. WinRAR (181/251), Logitech G
  HUB (105/114) e Rockstar (225/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.10):** `tl_security.exe` cria um arquivo em
  `C:\\`, obtém `TokenUser` pelo protocolo de tamanho, confirma
  `TokenElevation=0`, mescla/grava uma DACL e a relê na próxima execução. O
  armazenamento versionado mantém SID artificial e DACL por prefixo; CTest
  prova persistência em A, isolamento em B, metadata, `--report`, saída, exit
  code e trace em Debug e Sanitize. WinRAR (191/251) e Rockstar (232/338) foram
  reanalisados somente com `--report`, continuam `unsupported` e não foram
  executados; Logitech G HUB (105/114) não foi alterado porque o binário não
  está disponível localmente.
- **Marco concluído (Fase 13.11):** `tl_dialog.exe` valida template `DIALOG`
  padrão, `WM_INITDIALOG`, filhos lógicos, texto por ID, tabulação, ícone
  copiado e retorno modal 42. O cenário Xvfb envia Tab/Enter e protege o trace
  de `DialogBoxParamW`, `IsDialogMessageW` e `EndDialog`; o CTest registra 417
  casos sem falhas em Debug e em Sanitize (`LSAN_OPTIONS=detect_leaks=0`),
  com 416 executados e o smoke marcado `Skipped` quando o socket X11 não está
  disponível. WinRAR
  (202/251) e Rockstar (241/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e `execution: not-attempted`.
- **Marco concluído (Fase 13.12 — WinINet HTTPS local):** `tl_wininet.exe`
  valida `InternetOpenW`, `InternetConnectW`, `HttpOpenRequestW`,
  cabeçalhos, envio, status, disponibilidade, leitura parcial, fechamento e
  `InternetCrackUrlW` contra um servidor TLS efêmero em `127.0.0.1`.
  O smoke usa uma CA local confiável no fluxo positivo e outra CA no fluxo
  negativo; hosts externos e HTTP simples têm rejeições unitárias. Não há
  proxy, cookies, credenciais, redirecionamento, Internet ou afirmação de
  WinTrust, e o Rockstar não foi executado.
- **Marco concluído (Fase 13.12 — OLE stream em memória):** `tl_stream.exe`
  resolve `ole32.dll!CreateStreamOnHGlobal` e valida a vtable Microsoft x64 de
  `IStream`, referências, `Read`/`Write`, `Seek`, `SetSize`, `Stat`,
  `Commit`/`Revert` e liberação. Cópia, clone, regiões bloqueadas, `OLEAUT32`
  e `IDispatch` continuam fora do contrato; o Rockstar não foi executado.
- **Marco concluído (Fase 13.12 — cadeia WinTrust explícita):** `tl_trust.exe`
  valida uma cadeia X.509 DER de dois certificados com raiz fornecida pela
  fixture, rejeita política com UI e raiz incorreta e registra o mecanismo
  `libcrypto`. Não há loja Windows, revogação, Authenticode ou suporte ao
  Rockstar.
- **Marco concluído (Fase 13.12 — lacunas KERNEL32 do LGHub):** `tl_k32_gap.exe`
  cobre `InitializeCriticalSectionAndSpinCount`, `InitializeCriticalSectionEx`,
  `FormatMessageA` e `AreFileApisANSI` com buffers, flags e erros controlados.
  O `--report` de `lghub_installer.exe` passou a resolver 114/114 imports; a
  execução em prefixo temporário entrou na fase de execução, mas expirou em 20 s
  (`GuestTimeout`) sem produzir arquivos, portanto o fluxo do instalador não é
  declarado compatível.
- **Marco concluído (Fase 13.12 — memória Global/Local compartilhada):**
  `tl_globalmem.exe` cobre `GlobalAlloc`, `GlobalLock`, `GlobalUnlock`,
  `GlobalFree`, `LocalAlloc` e `LocalFree`, incluindo `GMEM_MOVEABLE`,
  `GMEM_ZEROINIT`, contagem de locks e handles inválidos. A reanálise de
  2026-08-26 passou a resolver 209/251 imports do WinRAR e 261/338 do Rockstar;
  ambos continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.12 — nome de certificado DER):**
  `tl_crypt32.exe` cobre `CRYPT32.dll!CertGetNameStringW` com
  `CERT_CONTEXT` explícito, nomes subject/issuer, consulta de capacidade e
  buffers insuficientes. O subconjunto não consulta SAN, loja Windows,
  Authenticode ou cadeia; a reanálise passou a resolver 262/338 imports do
  Rockstar, que continua `unsupported` e não foi executado.
- **Próximo resultado observável (Fase 13.12):** ampliar somente se surgir
  uma fixture que justifique `WTHelper*` ou Authenticode; os limites atuais
  permanecem publicados.

### Estudo de caso: `RobloxPlayerInstaller.exe` (benchmark de cobertura)

Em 2026-08-19, o instalador encontrado localmente em `Downloads` foi analisado
estaticamente, sem executar o entry point. O arquivo analisado é um PE32+ x86-64
com 17 DLLs importadas e 430 imports; o `--report` resolveu 75/430 imports
(17%), classificou o resultado como `unsupported` e registrou
`execution: not-attempted`. SHA-256 do arquivo analisado:
`d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59`.

Em 2026-08-22, o mesmo arquivo foi reanalisado com o `--report` atual:
196/430 imports resolvidos (45%), ainda `unsupported`, com `execution:
not-attempted`; o avanço vem das fases 10–12.

Em 2026-08-22, após `LoadLibrary`/`GetVersionEx`/`Locale`, o `--report` resolve
206/430 imports (47%), ainda `unsupported`, com `execution: not-attempted`.

Em 2026-08-22, após `WaitOnAddress`, o `--report` resolve 208/430 (48%), ainda
`unsupported`, com `execution: not-attempted` (imports `api-ms-win-core-synch-l1-2-0.dll` agora resolvidos via `KERNEL32`).

Em 2026-08-22, após `Fibers`, o `--report` resolve 210/430 (48%), ainda
`unsupported`, com `execution: not-attempted` (`CreateFiberEx`/`ConvertThreadToFiberEx`/`SwitchToFiber`/`DeleteFiber`).

Em 2026-08-22, após `Toolhelp`, o `--report` resolve 214/430 (49%), ainda
`unsupported`, com `execution: not-attempted` (`CreateToolhelp32Snapshot`/`Process32FirstW`/`NextW`/`OpenProcess`).

Em 2026-08-22, após `GUI Unicode`, o `--report` resolve 221/430 (51%), ainda
`unsupported`, com `execution: not-attempted` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW` etc.).

Em 2026-08-22, após `SHELL32`, o `--report` resolve 225/430 (52%), ainda
`unsupported`, com `execution: not-attempted` (`SHGetKnownFolderPath`/`SHGetFolderPathW`/`ShellExecuteW`/`ExW`).

Em 2026-08-22, após `GDI` estendido, o `--report` resolve 240/430 (55%), ainda
`unsupported`, com `execution: not-attempted` (`GDI32` `CreateFontW`/`SetDCBrush/PenColor`, `gdiplus` 8, `UxTheme` `SetWindowTheme`, `WINMM` `timeSetEvent`, `dbghelp` `SymFromAddr`).

Em uma leitura completa histórica de 2026-08-23, o relatório resolveu 244/430
(56%). No runtime atual, a mesma amostra para antes dos imports: o
`UWOP_SET_FPREG` estendido em RVA `0xbdb0b8` traz `OpInfo=10` e
`FrameOffset=0`, combinação fora do padrão aceito na Fase 13.4. O parser retorna
controladamente `unsupported-mechanism`/exit `5`; o arquivo continua
`unsupported` e essa variante de unwind deve ser tratada como dependência de
portfólio, sem criar uma exceção específica para Roblox.

O Roblox não é o único alvo nem autoriza implementação exclusiva para si. Ele
fica registrado como um benchmark grande para priorizar capacidades
reutilizáveis por várias classes de aplicativos. As lacunas observadas são:

- [x] ampliar o núcleo `KERNEL32` para arquivos e caminhos Unicode, recursos,
  tempo, sincronização e processos filhos, com fixtures próprias; a memória
  mapeada (`MapViewOfFile`/`CreateFileMappingW`) foi entregue depois;
- [x] implementar o núcleo de unwinding x64 da imagem convidada:
  `.pdata`/`.xdata` V1/V2, epílogos V2, `RtlCaptureContext`,
  `RtlLookupFunctionEntry`, `RtlVirtualUnwind` e `RtlPcToFileHeader`, com
  fixtures e regressões;
- [x] completar o despacho SEH explícito x64: `__C_specific_handler`,
  `RtlUnwind`/`RtlUnwindEx`, transferência de contexto, VEH, filtro não
  tratado e `__try/__except` V1/V2 fora de epílogos; C++/`__finally` e sinais
  Linux continuam fora do escopo;
- [x] implementar carregamento dinâmico real: `LoadLibraryA/W`,
  `LoadLibraryExA/W`, `FreeLibrary` e `GetModuleHandleExA/W` + `GetProcAddress` por nome e ordinal (fixture `tl_dynload.exe` cobre `C:\` path, API Set `api-ms-win-core-file-l1-1-0.dll`, `LoadLibraryEx`, `GetProcAddress`/`FreeLibrary`/`GetModuleHandleEx` `PIN`/`FROM_ADDRESS`); `GetProcAddress` agora resolve via `find_export_global`;
- [x] implementar APIs de versão e locale: `GetVersionExA`,
  `VerifyVersionInfoW`/`VerSetConditionMask`, `GetUserDefaultLocaleName` e
  `LocaleNameToLCID` (fixture `tl_version.exe` cobre 10.0.19044, `Verify`/`VerSetConditionMask` chain, `en-US`→`0x0409`/`pt-BR`→`0x0416`);
- [x] implementar espera por endereço (`WaitOnAddress`/
  `WakeByAddressSingle`/`WakeByAddressAll`) exposta pela API Set
  `api-ms-win-core-synch-l1-2-0.dll` (fixture `tl_waitaddr.exe` cobre `size` 1/2/4/8, timeout 1460, tamanho inválido 87, `WaitOnAddress` em thread e `WakeByAddressSingle`);
- [x] implementar fibers (`CreateFiberEx`, `ConvertThreadToFiberEx` e
  `SwitchToFiber`) sobre a infraestrutura de threads da Fase 11 (fixture `tl_fiber.exe` cobre `ConvertThreadToFiberEx` com flags, `CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread`);
- [x] implementar enumeração de processos: `CreateToolhelp32Snapshot`,
  `Process32FirstW`/`Process32NextW`, `OpenProcess` (fixture `tl_toolhelp.exe` cobre `/proc` enumeração, `PROCESSENTRY32W` 568, `OpenProcess` token); `K32*` permanece via `PSAPI` existente;
- [x] ampliar `SHELL32`/`SHLWAPI` para pastas conhecidas, execução de processos
  e manipulação de caminhos (fixture `tl_shell.exe` cobre `FOLDERID_RoamingAppData`/`CSIDL_APPDATA`/`TestSub`, `ShellExecuteW`/`ExW`);
- [x] criar uma camada `WS2_32`/rede com sockets, resolução local e polling,
  validada somente em loopback;
- [x] criar o armazenamento genérico Unicode de `ADVAPI32` para registro;
  demais APIs `CRYPT32`, loja Windows e Authenticode continuam pendentes fora
  do envelope `TLTC` (a extração restrita de nomes DER é coberta por
  `tl_crypt32.exe`);
- [x] definir uma camada `OLE32`/COM mínima (`CoCreateInstance`/`CoGetClassObject`/`OleInitialize` via `ole32.dll`, fixture `tl_com.exe` cobre `S_OK`/`REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`);
- [x] ampliar a GUI de forma genérica: variantes Unicode de `USER32` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` etc. via wrappers `wide_to_utf8`), validado por `tl_win_w.exe` sob `Xvfb` análogo a `tl_win`;
- [ ] ampliar `SHELL32`/`SHLWAPI` para pastas conhecidas, execução de processos
  e manipulação de caminhos;
- [x] avaliar `GDI32`, `gdiplus`, `UxTheme`, `WINMM`, `dbghelp` para desenho, imagens, temas,
  temporizadores multimídia e diagnóstico (fixture `tl_gdiex.exe` cobre 3+8+1+1+1); `POWRPROF.dll` e `IPHLPAPI.DLL` permanecem avaliação futura;
- [ ] manter isolamento, timeout, limites de recursos, `--report`, mensagens
  de falha e testes de integração para qualquer nova família de DLL.

A ordem de implementação continua subordinada à fase atual e ao método do
projeto: cada item precisa de um aplicativo-alvo ou fixture independente,
teste de regressão, contrato documentado e registro na matriz de
compatibilidade. O instalador do Roblox só será executado quando o portfólio
da Fase 13 tiver entregado as famílias de dependências necessárias; ele não
substitui os demais alvos do portfólio.

## Fase 0 — Fundação e contrato

- [x] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [x] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [x] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [x] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [x] Documentar as convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.
- [x] Configurar sanitizers e análise estática para os testes quando possível.

### Critério de saída — atendido

O projeto compila de forma reproduzível, executa seus testes básicos e possui fixtures Windows versionadas com seus imports documentados.

## Fase 1 — Leitor de PE seguro

- [x] Ler e validar DOS header, NT headers, optional header e section headers.
- [x] Exibir seções, entry point, imports, relocations e arquitetura.
- [x] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [x] Cobrir o parser com testes unitários e corpus de arquivos malformados.
- [x] Comparar a saída com `llvm-readobj` nas fixtures geradas.

### Critério de saída

O leitor identifica corretamente os fixtures válidos e nunca acessa memória fora dos limites ao processar fixtures inválidos. Validação: fixtures `tl_hello.exe`/`tl_nop.exe` parseados e comparados com `llvm-readobj` em CTest, corpus malformado coberto por testes, presets `debug` e `sanitize` verdes e análise estática sem pendências.

## Fase 2 — Mapeamento de imagem

- [x] Reservar a imagem no endereço preferencial quando possível.
- [x] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [x] Aplicar base relocations para PE32+ x86-64.
- [x] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.
- [x] Garantir que a imagem não permaneça inteira com permissão RWX por conveniência.

### Critério de saída

Um executável mínimo pode ser mapeado e inspecionado pelo runtime sem executar funcionalidades fora do escopo.

Validação: `tl_nop.exe`, `tl_hello.exe` e `tl_reloc.exe` mapeados via CLI em `debug` e `sanitize`; relocations aplicadas e verificadas em memória mapeada quando a base difere da preferencial (ASan bloqueia `0x140000000` no preset `sanitize`); permissões de região verificadas via `/proc/self/maps` (headers `r--`, `.text` `r-x`, nunca `rwx`); presets `debug` e `sanitize` verdes e análise estática sem pendências. O contrato de mapeamento está em `docs/arquitetura/mapeamento-imagem.md`.

## Fase 3 — Imports e bootstrap mínimo

- [x] Resolver a import table para módulos internos suportados.
- [x] Implementar trampolins e ponte de ABI para chamadas do programa à camada hospedeira.
- [x] Preparar as estruturas mínimas de processo e thread exigidas pelo escopo inicial.
- [x] Adicionar diagnóstico para DLL, símbolo, ordinal, forwarder ou delay import ausente.
- [x] Definir o comportamento de falha antes do entry point quando uma dependência não for suportada.

### Critério de saída — atendido

O runtime resolve imports conhecidos com o ABI correto e informa de maneira reproduzível qualquer dependência desconhecida.

Validação: registro interno de `KERNEL32.dll`, `USER32.dll` e `GDI32.dll`; resolução por nome e ordinal com patch da IAT e restauração das permissões; fronteiras `ms_abi` testadas diretamente; processo convidado com pilha de 1 MiB e guard page; fixture `tl_missing_dll.exe` retorna `5` sem executar o entry point e emite `unknown-symbol`; falhas de símbolo, ordinal, símbolo sem implementação, delay import e slot de IAT inválido têm testes unitários. Os testes Sanitizer locais exigem `LSAN_OPTIONS=detect_leaks=0` porque a descoberta do GoogleTest falha no LeakSanitizer sob ptrace.

## Fase 4 — Console: primeiro marco público

- [x] Implementar `GetStdHandle`, `WriteFile`, `ReadFile` e `ExitProcess`.
- [x] Definir e testar conversão entre handles Windows e descritores Linux.
- [x] Executar `tl_hello.exe` e uma ferramenta de eco construída no repositório.
- [x] Verificar saída, retorno, trace e tratamento de erros em CI.
- [x] Documentar exatamente quais flags, handles e encodings são suportados.

### Critério de saída — MVP atendido

```text
./tradutorlinux --trace tests/samples/tl_hello.exe
```

O comando escreve a saída esperada, retorna o código correto, produz trace reproduzível e possui testes para todas as APIs usadas pelo fixture.

Validação: `tl_hello.exe` e `tl_echo.exe` executados com stdout verificado; `ExitProcess` e o código de retorno registrados no trace; handles padrão convertidos para descritores Linux por tokens opacos; `ReadFile`/`WriteFile` limitados a I/O síncrono de console e bytes sem conversão de encoding; 95 testes passaram nos presets `debug` e `sanitize` (sanitize local com `LSAN_OPTIONS=detect_leaks=0` por limitação do LeakSanitizer durante descoberta sob ptrace); `cppcheck` e `clang-tidy` passaram.

## Fase 5 — Runtime básico

- [x] Implementar `GetLastError`/`SetLastError` e o mapeamento de erros necessário.
- [x] Implementar `VirtualAlloc`/`VirtualFree` com semântica limitada e documentada.
- [x] Implementar abertura, leitura, escrita e fechamento de arquivos para um subconjunto de flags.
- [x] Definir normalização de caminhos e política explícita para caminhos Windows.
- [x] Adicionar testes de concorrência somente quando o modelo de threads fizer parte do escopo.

### Critério de saída — atendido

Uma aplicação de console consegue ler e escrever arquivos e usar memória alocada pelo runtime, com erros verificáveis e documentados.

Validação: `tl_file.exe` usa `VirtualAlloc`, `CreateFileA`, `WriteFile`, `ReadFile`, `CloseHandle`, `VirtualFree`, `GetLastError` e `SetLastError`; caminhos relativos são normalizados de `\\` para `/`, enquanto caminhos absolutos e drives são rejeitados; `VirtualAlloc` aceita somente `MEM_COMMIT | MEM_RESERVE` com `PAGE_READONLY` ou `PAGE_READWRITE`, e `VirtualFree` somente `MEM_RELEASE` com tamanho zero. Debug e sanitize passaram com 101 testes; `cppcheck`, `clang-tidy` e `git diff --check` também passaram.

## Fase 6 — Carregamento e cobertura controlada

- [x] Adicionar APIs somente guiadas por aplicações-alvo e testes de regressão.
- [x] Evoluir suporte a DLLs, resources, TLS callbacks, forwarders e delay-load conforme necessário.
- [x] Publicar uma matriz com aplicativo, arquitetura, imports, APIs usadas, estado e limitações.
- [x] Adicionar um modo de relatório que mostre o que falta para tentar executar um `.exe`.
- [x] Revisar periodicamente o custo de cada API em relação ao valor para os aplicativos-alvo.

### Critério de saída — atendido

Cada aplicação declarada como suportada possui um teste de regressão e uma lista explícita de limitações.

Validação: `tl_hello.exe`, `tl_echo.exe` e `tl_file.exe` possuem testes de integração e entradas na matriz; `tl_missing_dll.exe` protege o caminho de rejeição; `--report` tem teste unitário e CTest real, retorna `0` para `tl_file.exe`, lista cada import e declara `execution: not-attempted`. O modo não mapeia nem executa a imagem.

## Fase 7 — Avaliar GUI

- [x] Decidir que uma interface Win32 mínima é um objetivo experimental de produto.
- [x] Criar um subsistema de janela e eventos separado do runtime de console.
- [x] Começar por `MessageBoxA` e uma janela simples, com fixture PE32+ e teste automatizado de metadata/report.
- [x] Definir a integração inicial com X11 direto, mantendo a camada isolada para futura decisão sobre Wayland/toolkit.
- [x] Implementar um subconjunto mínimo de janela e eventos (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`) com fixture `tl_win.exe` que cria janela, desenha texto e encerra ao fechar (`WM_CLOSE`).
- [x] Provar a fronteira de ABI host→convidado invocando o `WNDPROC` do convidado pela convenção Microsoft x64.
- [x] Estender o teclado: `WM_KEYUP`, virtual keys por `keysym` (incluindo teclas sem caractere), `Shift` refletido no `WM_CHAR` (fixture `tl_key.exe`).
- [x] Implementar `SetTimer`/`KillTimer` com despacho periódico de `WM_TIMER` no pump (fixture `tl_timer.exe`).
- [x] Implementar o GDI mínimo (`GDI32.dll!GetStockObject`/`TextOutA`; `USER32.dll!BeginPaint`/`EndPaint`/`GetDC`/`ReleaseDC`) com `HDC == HWND` e `PAINTSTRUCT` real (fixture `tl_gdi.exe`).

Validação local: os 129 testes dos presets `debug`, `sanitize` e `release` passam quando o ambiente fornece X11/Xvfb; sem acesso a `/tmp/.X11-unix`, o `runtime_gui_smoke` é marcado como `Skipped` rapidamente. A suíte inclui `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe` e `tl_paint.exe`, resolução dos imports de `USER32.dll`/`GDI32.dll`, relatório sem execução, testes de layout ABI e relocations PE32+. O loader valida o entry point, usa a pilha convidada com guard page, aplica somente `DIR64`/`ABSOLUTE` para PE32+ e rejeita execução fora da base quando não há relocations. O `runtime_gui_smoke` usa `Xvfb -displayfd`, não depende de displays fixos e aborta/limpa o servidor de forma controlada. `cppcheck` e `clang-tidy` passam sem pendências.

### Critério de saída — atendido

Uma aplicação gráfica de teste cria uma janela, recebe eventos básicos e encerra corretamente, sem comprometer o runtime de console.

Validação visual (2026-08-15, sessão X11 `DISPLAY=:0` acessível): `tl_gui.exe` executado com `--trace` abriu a janela "TradutorLinux GUI / Fase 7" com botão OK; ao clicar, o runtime registrou `[tl][runtime][info] ExitProcess exit-code="0" status="success" mechanism="guest-transfer"`, `[tl][process][info] exit exit-code="0" explicit="sim"` e encerrou com código `0`, liberando a imagem. `tl_win.exe` executado com `--trace` e `TL_GUI_AUTOCLOSE_MS=1` registrou `RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"` e `ExitProcess exit-code="0"`, confirmando o message loop de ponta a ponta (autoclose → `WM_CLOSE` → `DefWindowProcA` → `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage(0)`).

## Fase 8 — Aplicativos-alvo reais

O projeto deixa de medir progresso apenas por fixtures e passa a medir por
aplicativos pequenos, úteis e reproduzíveis. Cada aplicativo-alvo deve ser
fixado por versão, arquitetura, toolchain e lista de imports.

- [x] Definir de 3 a 5 aplicativos-alvo reais, preferencialmente de código aberto e compiláveis no CI.
- [x] Priorizar utilitários de console: ferramentas de texto, arquivos, configuração e empacotamento simples.
- [x] Criar teste por aplicativo com stdout, stderr, exit code, arquivos produzidos e timeout.
- [x] Fazer o `--report` agrupar imports ausentes por DLL e por fase.
- [x] Separar "não suportado", "falhou durante a execução" e "resultado incorreto".
- [x] Publicar pontuação de compatibilidade por aplicativo; iniciar não é suficiente.

### Critério de saída

Pelo menos três aplicativos reais, pequenos e úteis executam um fluxo completo
de teste no Linux, com limitações publicadas e regressão automatizada. Fixtures
continuam obrigatórias para proteger contratos de ABI, mas deixam de ser a
única evidência do produto.

## Fase 9 — Base de processo e CRT

A maior barreira para aplicativos compilados normalmente será a camada de
runtime C e o estado básico do processo. Esta fase é guiada pelos imports dos
aplicativos escolhidos.

- [x] Implementar `GetModuleHandleA/W`, `GetProcAddress` limitado aos módulos registrados e informações básicas do processo.
- [x] Implementar linha de comando e ambiente: `GetCommandLineA/W`, `GetEnvironmentVariableA/W` e conversão documentada de encoding.
- [x] Implementar heap básico: `HeapAlloc`, `HeapFree`, `HeapReAlloc`, `GetProcessHeap`.
- [x] Implementar a espera exigida pelo primeiro alvo: `Sleep`.
- [x] Implementar tempo adicional quando um aplicativo-alvo justificar: `GetTickCount64` e `GetSystemTimeAsFileTime`.
- [x] Adicionar o subconjunto mínimo de `msvcrt.dll` exigido pelo primeiro alvo (`xxd`).
- [x] Expandir a CRT somente pelos imports e fluxos exigidos pelos próximos aplicativos-alvo.
- [x] Cobrir inicialização/encerramento do CRT, argumentos `argc/argv`, retorno de `main` e erros para o primeiro alvo.
- [ ] Ampliar essa cobertura para cada nova família de CRT ou aplicativo suportado.

### Critério de saída

Ao menos dois aplicativos compilados com CRT executam seus fluxos principais,
recebem argumentos e retornam seus códigos corretamente.

## Fase 10 — Sistema de arquivos e utilitários

Expandir a camada para programas que trabalham com diretórios, configuração e
arquivos, mantendo uma tradução de caminhos segura e explícita.

- [x] Implementar `FindFirstFileA/W`, `FindNextFileA/W` e `FindClose`.
- [x] Implementar `GetFileAttributesA/W`, `DeleteFileA/W`, `MoveFileA/W` e `CreateDirectoryA/W`.
- [x] Implementar `SetFilePointer`, tamanhos de arquivo e modo append quando exigidos.
- [x] Definir diretório atual, diretório do executável e variáveis de ambiente sem inventar letras de drive.
- [x] Implementar conversão UTF-16/UTF-8 e testar nomes não ASCII.
- [x] Adicionar testes de permissões, arquivos inexistentes, diretórios e concorrência controlada.
- [x] Adicionar tamanho/posição, tempos, metadados, cópia, movimentação,
  diretórios wide e leitura segura de recursos PE com fixtures independentes.

### Critério de saída

Um aplicativo-alvo consegue descobrir arquivos, criar saída em diretório, ler
configuração e lidar com erros de filesystem sem caminhos fixos do projeto.

## Fase 11 — Concorrência e rede opcional

Esta fase só começa se um aplicativo-alvo justificar threads ou rede.

- [x] Implementar `CreateThread`, `ExitThread`, `WaitForSingleObject` e `CloseHandle`.
- [x] Implementar `CRITICAL_SECTION` compatível com o escopo atual de convidado single-thread.
- [x] Ampliar sincronização para eventos, mutexes, semáforos e `WaitForMultipleObjects` com fixture própria.
- [x] Definir TLS, encerramento de threads e chamadas ABI em threads convidadas.
- [x] Criar uma camada WinSock mínima separada de `KERNEL32.dll`, com teste TCP/UDP em loopback.
- [x] Criar `CreateProcessW`, `GetExitCodeProcess` e `TerminateProcess` sob o
  contrato de filhos PE32+ validados pelo mesmo loader.
- [x] Testar deadlock, timeout, cancelamento e propagação de falha do convidado.

### Critério de saída

Um aplicativo-alvo multithread passa testes repetíveis sem corrida conhecida,
deadlock ou corrupção de estado. Rede só entra com alvo concreto e testes
reprodutíveis.

## Fase 12 — GUI útil por aplicativo

A GUI evolui a partir de um aplicativo-alvo, e não de uma lista abstrata de
APIs.

- [x] Escolher e fixar `Efeckc17/simple-todo-c` no commit `bcdf3d5fcebb8c0b445edb791d54511194c1b6ca`.
- [x] Implementar o subconjunto de mouse, foco, teclado, comandos, notificações, menus e ciclo de vida exigido pelo alvo.
- [x] Implementar fontes, brushes, desenho e invalidação somente no subconjunto usado pelo alvo.
- [x] Implementar EDIT, BUTTON, COMBOBOX, STATIC e SysListView32 como controles lógicos.
- [x] Validar o smoke completo sob Xvfb e promover o alvo após evidência de integração.
- [ ] Avaliar Wayland/toolkit depois de existir uma aplicação GUI real suportada.
- [x] Automatizar propriedades observáveis, stdout, exit code, trace, persistência e visibilidade; screenshot permanece fora do escopo.

### Critério de saída

Um aplicativo GUI real abre, recebe interação, renderiza seu fluxo principal e
encerra corretamente em uma sessão X11 de teste, com limitações publicadas.

## Fase 13 — Compatibilidade ampla por portfólio

Esta fase transforma a expansão por um único aplicativo GUI em cobertura por
classes de uso. A meta de longo prazo é maximizar a cobertura prática de
aplicativos Win32 PE32+ x86-64 de espaço de usuário; ela não equivale a prometer
compatibilidade imediata com qualquer executável, jogo ou mecanismo protegido.

- [ ] Fixar um portfólio versionado de aplicativos-alvo de código aberto ou
  redistribuição autorizada, com pelo menos um representante de instalador,
  aplicativo GUI de produtividade e ferramenta de rede.
- [x] **Prioridade 13.1 — instaladores x64 nativos:** usar as amostras WinRAR,
  Logitech G HUB, Rockstar e Roblox como evidência de cobertura, mas escolher
  um instalador PE32+ x86-64 reproduzível como alvo de regressão inicial.
- [x] Criar um prefixo exclusivo para cada instalação e propagá-lo de forma
  explícita ao runtime, ao catálogo e a todos os processos-filhos do
  instalador; o prefixo padrão compartilhado não é suficiente para este fluxo.
- [x] Implementar `delay-import` no leitor, relatório e resolvedor, com
  diagnóstico separado para cada símbolo atrasado.
- [x] **Prioridade 13.4 — metadados de unwinding x64 V2:** normalizar
  `UOP_Epilog`, aceitar a extensão compatível de `UWOP_SET_FPREG` e manter
  `RtlVirtualUnwind` seguro fora de epílogos, com fixture e regressões.
- [x] Implementar o despacho SEH explícito e o núcleo reutilizável de
  ambiente/locale/FLS: `tl_seh*.exe` e `tl_locale_env_fls.exe` cobrem os
  contratos sem declarar suporte aos benchmarks comerciais.
- [x] Validar `install -> arquivos no prefixo -> cadastro do executável
  instalado -> app run` com teste de integração e artefatos reproduzíveis.
- [ ] Adicionar descoberta de formatos de distribuição ao portfólio: distinguir
  PE direto de pacotes MSIX/AppX, extrair de forma estruturalmente validada para
  um prefixo próprio, ler `AppxManifest.xml` e só então analisar o PE interno.
- [ ] Registrar imports, versão, hash e fluxo principal de cada alvo, e usar a
  interseção e a frequência dessas dependências para ordenar o trabalho.
- [ ] Expandir famílias de APIs somente quando a implementação servir a mais de
  um alvo ou completar uma capacidade de sistema bem delimitada; cada API ganha
  fixture independente e regressão de integração.
- [ ] Priorizar o núcleo comum na ordem publicada abaixo: locale ampliado,
  contexto de processo/console, enumeração de arquivos, identidade/ACL,
  controles GUI e, somente depois, automação, HTTP e confiança.
- [ ] Manter o `--report` como porta de entrada: apresentar imports faltantes
  por DLL e por capacidade, mas considerar carregamentos dinâmicos e o fluxo
  de execução antes de declarar suporte.
- [ ] Avaliar o `RobloxPlayerInstaller.exe` como benchmark do portfólio, sem
  criar stubs específicos para Roblox e sem declarar suporte ao cliente/jogo.

### Sequência planejada a partir do portfólio local

Esta ordem usa somente as lacunas já registradas em
`docs/requisitos-aplicativos.md`. Cada item ainda precisa de um plano técnico
aprovado antes de começar; não autoriza implementar APIs extras por antecipação
nem declarar os benchmarks comerciais suportados.

#### Fase 13.7 — locale determinístico ampliado

- [x] Implementar `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoEx`,
  `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW` e `GetTimeFormatW`
  sobre a mesma tabela estática `en-US` da Fase 13.6.
- [x] Manter a enumeração limitada a locales estáticos documentados, validar
  callbacks e flags e retornar erro controlado para sort keys, host locale,
  normalização, CJK e mutação de locale por thread.
- [x] Criar `tl_locale_extended.exe`, sem CRT implícito, para provar consulta,
  enumeração por callback, tipo de caractere e formatação; cobrir buffers,
  flags e callbacks inválidos em CTest.
- [x] Atualizar `--report` de WinRAR, Logitech G HUB e Rockstar somente depois
  dos testes. A fase só é concluída se reduzir lacunas nos três, sem executar
  binários comerciais.

#### Fase 13.8 — contexto de processo e console Win32

- [x] Implementar o grupo compartilhado `GetStartupInfoW`,
  `GetSystemDirectoryW`, `GetFileType`, `SetStdHandle`, `ReadConsoleW`,
  `WriteConsoleW`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`,
  `EncodePointer`, `DecodePointer` e `InitializeSListHead`.
- [x] Definir o contrato para handles padrão por processo/prefixo e para o
  comportamento sem console, sem criar `AllocConsole`/`AttachConsole` nesta
  etapa.
- [x] Criar uma fixture de processo/console que valida dados de startup,
  redirecionamento, tipo de handle, codificação UTF-16 e operações de lista;
  proteger APIs, trace e execução em CTest.
- [x] Reanalisar WinRAR, Logitech G HUB e Rockstar apenas com `--report`.

#### Fase 13.9 — enumeração e metadados de arquivos x64

- [x] Completar as operações de arquivos que se repetem no portfólio:
  `FindFirstFileExW`, `SetFileAttributesW` e a extensão de metadados de handle
  justificada pelas amostras; long/short paths e APIs exclusivas ficam fora
  até aparecerem em outro alvo.
- [x] Reutilizar o mapeamento de caminhos e o prefixo existente, validando
  flags, estruturas, buffers e `GetLastError` sem expor caminhos do host.
- [x] Criar fixture de enumeração/metadados no prefixo e testes de isolamento
  entre prefixos; atualizar os relatórios de pelo menos dois benchmarks x64.

#### Fase 13.10 — identidade e ACLs funcionais por prefixo

- [x] Implementar uma representação coerente, limitada e persistente de SID,
  token, descritor de segurança e DACL para os arquivos do prefixo, cobrindo
  as operações comuns exigidas por Logitech G HUB, WinRAR e Rockstar.
- [x] Incluir somente APIs validadas pelo fluxo: consulta de token/SID,
  `Get/SetNamedSecurityInfoW`, `SetEntriesInAclW`,
  `InitializeSecurityDescriptor` e operações de SID/ACL associadas.
- [x] Criar fixture de segurança que consulta identidade e grava/lê uma DACL
  dentro do prefixo; provar que isso é compatibilidade funcional, **não**
  sandbox, autenticação do host ou aplicação real de permissões Linux.
- [x] Manter certificados, WinTrust, privilégios elevados, ACLs de rede e
  herança complexa fora desta fase.

#### Fase 13.11 — diálogos e controles GUI reutilizáveis

- [x] Promover somente o subconjunto compartilhado de `USER32`/`COMCTL32`
  necessário para diálogos modais, tabulação, textos/ícones e controles comuns
  observados em WinRAR e Rockstar.
- [x] Criar fixture X11 determinística com interação automatizada; não incluir
  GDI completo, impressão, shell de arquivos ou todos os controles Windows.
- [x] Reanalisar os dois benchmarks e só iniciar execução manual quando todos
  os imports estáticos e atrasados correspondentes estiverem resolvidos.

#### Fase 13.12 — automação, rede e confiança, em entregas separadas

- [x] Separar OLE Automation/streams, HTTP WinINet e
  certificados/WinTrust em subfases independentes, cada qual exigindo ao menos
  duas evidências do portfólio ou uma fixture de protocolo reproduzível.
- [x] Para OLE streams, limitar a primeira entrega a `CreateStreamOnHGlobal`
  com `IStream` em memória e ABI Microsoft x64 explícita: referências,
  `Read`/`Write`, `Seek`, `SetSize`, `Stat`, `Commit`/`Revert`; `tl_stream.exe`
  cobre metadados, `--report`, trace e execução.
- [x] Para HTTP, limitar a primeira entrega a cliente HTTPS previsível por
  prefixo, sem cookies globais ou credenciais do host: `tl_wininet.exe`
  cobre HTTPS em loopback com CA TLS efêmera, CA não confiável, protocolo,
  handles e leitura; proxy, DNS externo, Internet e redirecionamento são
  rejeitados ou inexistentes.
- [x] Para confiança, limitar a primeira entrega a `WinVerifyTrust` com
  `WTD_CHOICE_BLOB`, política sem UI/revogação e cadeia DER explícita
  folha→raiz; `tl_trust.exe` cobre metadados, `--report`, trace, sucesso,
  política incompatível e raiz incorreta. `WTD_CHOICE_FILE`, Authenticode,
  loja Windows, revogação e `WTHelper*` continuam fora do contrato. A API
  separada `CRYPT32!CertGetNameStringW` é coberta por `tl_crypt32.exe` apenas
  para `CERT_CONTEXT`/DER explícito e extração de nomes.
- [ ] Não usar esses componentes para declarar compatibilidade do Rockstar
  antes de validar um fluxo de instalação/atualização inteiro.

#### Backlog condicionado — formatos, arquitetura e unwind adicional

- [ ] MSIX/AppX: primeiro detectar pacote, ler `AppxManifest.xml` e localizar
  estruturalmente o executável interno; instalação/executar pacote exige fase
  própria e não é coberta pelo prefixo atual.
- [ ] PE32/x86, .NET/Mono, ARM e WOW64 continuam fora do alvo. Não há plano de
  executar esses binários sem uma decisão explícita de arquitetura/emulação.
- [ ] A forma de `UWOP_SET_FPREG` do Roblox (`OpInfo=10`, `FrameOffset=0`)
  permanece diagnóstico de portfólio. Só será promovida a uma fase de unwind
  genérica se outra amostra confirmar a mesma semântica e houver fixture
  determinística; não será criada uma exceção exclusiva para Roblox.

PE32/x86, .NET/Mono e MSIX/AppX continuam requisitos separados nesta primeira
subetapa. Eles ficam registrados no portfólio para a expansão posterior, mas
não bloqueiam a base de instalação PE32+ x86-64.

### Critério de saída

O portfólio contém alvos de pelo menos três classes de uso, cada um com fluxo
principal automatizado e limitações publicadas. O runtime demonstra que novas
famílias de APIs atendem mais de um alvo ou uma capacidade reutilizável, e o
catálogo distingue honestamente o que inicia, o que executa o fluxo principal e
o que ainda não é suportado.

## O que fica explicitamente fora do estágio atual

- Jogos, DirectX, drivers, anti-cheat, .NET, COM, ActiveX e serviços Windows, até que exista decisão explícita, alvo concreto e fase própria.
- Implementar centenas de APIs sem aplicativo-alvo e regressão.
- Declarar suporte porque o programa abriu; o fluxo principal precisa ser verificável.

## Objetivo estratégico — compatibilidade ampla por etapas

O objetivo do projeto é tornar o TradutorLinux útil para aplicativos Windows em
geral, aumentando continuamente a quantidade, as categorias e o tamanho dos
aplicativos que funcionam no Linux. Isso inclui chegar progressivamente a
aplicativos grandes, desde que suas dependências possam ser implementadas com
segurança e testadas.

“Aplicativos em geral” é um objetivo de cobertura, não uma declaração de que
qualquer `.exe` já funciona. O progresso será medido por dados: aplicativos
reais testados, categorias cobertas, imports implementados, fluxos principais
aprovados, resultados corretos e falhas reproduzíveis. Não será medido por
quantidade de APIs declaradas sem uso real.

### Estratégia de expansão

- [x] Criar um catálogo de aplicativos reais por categoria: console, arquivos, rede, ferramentas de desenvolvimento, produtividade e GUI (`docs/catalog.md` com `xxd`/`bzip2`/`dos2unix`/`tl_*`/`simple_todo`/`Roblox`).
- [x] Manter níveis de compatibilidade: inicia, fluxo principal, uso diário e cobertura avançada (definidos em `docs/catalog.md`).
- [x] Coletar imports de muitos aplicativos e priorizar APIs que aparecem em vários alvos (`Roblox` `430` imports, `gdiplus` `8/8`, `SHELL32` `5/5`).
- [x] Implementar famílias de DLLs por demanda: `KERNEL32`, `NTDLL` limitada, `ADVAPI32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, `COMDLG32`, `WS2_32`, `WININET`, `WINTRUST`, `CRYPT32` e CRTs (23 módulos `tests/test_module.cpp:87`).
- [x] Criar testes de integração por aplicativo e uma matriz pública de limitações (`docs/compatibilidade.md` + `tests/samples` 35 fixtures).
- [x] Adicionar execução isolada, timeout, limites de recursos e diagnóstico para que aplicativos grandes não derrubem o host (`process/isolate.cpp` `71`/`72`).
- [ ] Avaliar compatibilidade por versões e builds específicos, sem assumir que duas versões do mesmo aplicativo usam as mesmas APIs.

### Etapas para aplicativos grandes

1. **Base de execução:** PE, relocations, imports, TLS, exceções, processo,
   argumentos, ambiente, heap e CRT.
2. **Sistema operacional básico:** arquivos, diretórios, Unicode, registry
   limitado, sincronização, threads, processos filhos e tempo.
3. **Bibliotecas comuns:** shell, diálogos, controles, recursos, clipboard,
   fontes, GDI e rede.
4. **Aplicativos de médio porte:** ferramentas com múltiplas DLLs, plugins,
   configuração e vários threads.
5. **Aplicativos grandes:** GUI complexa, instaladores, suítes de produtividade
   e outros alvos escolhidos por cobertura e valor.
6. **Recursos especializados:** COM, DirectX, áudio, impressão e .NET somente
   quando houver decisão explícita de escopo e aplicativos-alvo.

Cada etapa depende da anterior. Um aplicativo grande não será considerado
suportado por simplesmente abrir a janela: ele precisa concluir operações
representativas sem corrupção, travamento ou resultado incorreto.

## Próximos marcos

| Marco | Resultado verificável |
|---|---|
| M1 — Parser | PE32+ válido lido; PE inválido rejeitado com segurança. |
| M2 — Image mapper | Imagem mínima mapeada, relocada e protegida. |
| M3 — Imports | Imports conhecidos resolvidos; ausentes diagnosticados. |
| M4 — Console | `tl_hello.exe` executa com saída e retorno corretos. |
| M5 — Arquivos | Fixture lê e escreve arquivo com semântica documentada. |
| M6 — Cobertura | Primeira aplicação-alvo adicional incluída na matriz e na regressão. |
| M7 — GUI | Janela real com message loop (`tl_win.exe`) e decisão de produto tomada: GUI Win32 mínima segue como objetivo experimental. |
| M8 — Diagnóstico controlado | Convidado executado em processo filho isolado; término por sinal vira `guest-signal` no trace e `71` no exit code; falhas controladas (ponteiro, arquivo, memória, imports) cobertas por testes. |
| M9 — Alvos reais | Três aplicativos pequenos e úteis executam fluxos completos com regressão no CI. |
| M10 — CRT mínimo | Um aplicativo compilado com CRT recebe argumentos, usa ambiente e termina corretamente. |
| M11 — Arquivos reais | Um aplicativo cria, enumera e manipula arquivos e diretórios usando caminhos traduzidos. |
| M12 — GUI real | Um aplicativo GUI escolhido por seus imports completa um fluxo principal sob X11. |

Com o marco M7 concluído, a GUI mínima avançou além do planejado e passa a ser
acompanhada no próprio roadmap da Fase 7: teclado estendido (`WM_KEYUP`, virtual
keys, `Shift`), timers (`WM_TIMER` periódico) e um GDI mínimo (pintura com
`BeginPaint`/`EndPaint`/`TextOut`). Cada nova API exige fixture e regressão; o
próximo passo natural, quando justificado por um aplicativo-alvo, é o desenho de
formas (`Rectangle`/`FillRect`), fontes/cores ou a entrada de mouse completa.

## Definição de pronto

Uma tarefa do roadmap só é considerada pronta quando:

- o código foi compilado com as configurações suportadas;
- existe um teste automatizado ou uma justificativa documentada para teste manual;
- falhas são observáveis pelo trace ou por uma mensagem de erro útil;
- a documentação da API ou limitação foi atualizada;
- o comportamento não quebra os fixtures já suportados.
