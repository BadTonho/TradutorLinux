# Matriz de compatibilidade

Esta matriz registra resolução de imports, resultados de execução e limitações
conhecidas; ela não é uma promessa de compatibilidade geral com Windows.

## Como ler a matriz

Há três dimensões independentes:

- **`--report`/imports:** `supported` significa que todos os imports estáticos
  foram resolvidos; `unsupported` significa que ao menos uma dependência ou
  mecanismo não foi resolvido. Isso não comprova execução.
- **execução:** `not-attempted`, `passed` ou uma falha controlada como
  `GuestTimeout 72`, `GuestFault 71`, `GuestResourceLimit 73` ou um código
  explícito do convidado.
- **nível funcional:** o catálogo usa `inicia`, `fluxo principal restrito`,
  `fluxo principal` e `uso diário` somente quando há fluxo representativo,
  resultado observável e limitações publicadas.

Assim, um aplicativo pode ter `supported` na resolução de imports e continuar
`execution-failed` ou sem nível funcional no catálogo.

## Política de backend do parser PE — R21.3–R21.5

Com `TL_BUILD_RUST=ON`, a execução direta de `--report` usa o resultado Rust
como canônico e compara sua semântica com o parser C++ nos testes. O adaptador
decodifica o TLPE com validação de magic, versão, offsets, strides,
alinhamento, referências, campos reservados, overflow e limites, mantendo
strings como bytes. Não existe fallback silencioso: falhas de parsing são
controladas e aparecem no trace com os campos estruturados da ABI.

Essa promoção não altera o estado de compatibilidade de nenhum aplicativo.
`app run --report`, execução direta normal, instalação, Proton, o loader das
DLLs dependentes e `TL_BUILD_RUST=OFF` permanecem no parser C++. No `app run`
nativo sem `--report`, somente a imagem principal usa o resultado Rust; o
`GuestModuleGraph` continua usando C++ para as DLLs. O `PeInfo` Rust é entregue
ao mesmo fluxo C++ de mapeamento e execução, sem alteração de `mmap`,
relocations, imports, ABI ou entry point.

O relatório Rust não mapeia imagem nem executa o entry point;
`execution: not-attempted` continua obrigatório. Falhas Rust não têm fallback
silencioso e encerram o `app run` antes de mapear ou executar.

Na R21.5, a seleção é uma política única do runner: Rust é canônico somente
para a imagem principal dos caminhos promovidos com `TL_BUILD_RUST=ON`. O C++
permanece produção para DLLs dependentes, Proton, instalação, `app run
--report`, execução direta normal e para o build `TL_BUILD_RUST=OFF`, que é a
variante C++ explícita e padrão. Nos caminhos promovidos, o C++ é apenas o
oráculo diferencial; nenhum resultado Rust é substituído silenciosamente.

O mapeamento de saída é `4` para `truncated`/`malformed`, `5` para arquitetura,
formato ou mecanismo não suportados e `70` para falha interna da ABI, limites,
buffer ou wire inválido. Os vetores e testes diferenciais de imports,
delay-imports, exports/forwarders, TLS, unwind V1/V2 e relocations são a
evidência do contrato, não uma declaração de suporte funcional.

## Política de backend MSIX/AppX — R22.1–R22.2

R22.1 implementou o parser Rust e o wire `TLMS` v1.0 para testes de contrato e
comparação diferencial. A partir de R22.2, com `TL_BUILD_RUST=ON`, Rust é
canônico no `--report` direto e no `install` de pacotes. A seleção usa as
extensões `.msix`, `.appx`, `.msixbundle` e `.appxbundle` antes da validação
ZIP, para que entradas truncadas e inválidas recebam o diagnóstico correto.

O relatório de um pacote válido mantém stdout, stderr sem trace e exit code.
Com `--trace`, aparece `package-parse` com `backend="rust"`; falhas incluem
`status`, `code`, `phase`, `input-offset` e `detail-value`. Não existe fallback
de produção para o parser C++ quando a análise Rust falha.

No `install`, Rust valida o pacote e C++ continua responsável por criar o
prefixo, revalidar as condições físicas, extrair os arquivos, verificar o
executável PE32+ AMD64 e salvar o catálogo. A extração usa o executável
principal validado por Rust; falha ou divergência encerra a instalação sem
substituir o resultado Rust e sem cadastro.

Bundles, ZIP64 multipartes, encryption, .NET/Mono, Authenticode, links,
traversal, NUL, colisões normalizadas, DTD e entidades externas continuam fora
do escopo. ZIP64 de disco único é lido dentro dos limites de segurança; o
limite agregado descompactado de 512 MiB continua valendo.
Rust não acessa o filesystem e não valida o PE interno do pacote.

`app run`, `app run --report`, execução direta normal, Proton, DLLs dependentes
e `TL_BUILD_RUST=OFF` continuam usando C++. O build OFF é uma variante C++
explícita e padrão, sem link operacional ou símbolos Rust. A promoção não
altera o nível funcional ou declara suporte a nenhum aplicativo adicional.

O mapeamento de erros Rust para o CLI é `4` para `truncated`/`malformed`, `5`
para `unsupported-format`/`unsupported-mechanism` e `70` para argumentos,
buffers, limites, wire inválido, panic ou falha interna.

## Parser Rust de perfis — R23.1–R23.2

R23.1 implementou o contrato TLPR v1.0 e a comparação diferencial do
`profile.json`. Em R23.2, com `TL_BUILD_RUST=ON`, Rust é canônico dentro de
`load_profile` para todos os consumidores atuais, incluindo `app run` e
`app run --report`. O C++ verifica antes a presença, o tipo regular, a leitura
e o limite do arquivo; depois do TLPR, continua responsável por existência,
tipo regular, symlink, confinamento, colisões físicas, permissões,
materialização e seleção de backend.

O wire tem cabeçalho de 128 bytes, tabelas `info`/`files`/`dlls`/`strings`,
inteiros little-endian, alinhamento de 8 bytes, referências por offset/tamanho
e strings binárias deduplicadas. Entrada acima de 1 MiB, saída acima de 64 MiB,
overflow, referências inválidas ou campos reservados não zerados são
rejeitados. A ABI usa `size`/`fill`, buffers e mensagens caller-owned, e não
expõe layout Rust.

O perfil ausente continua sendo detectado exclusivamente pelo C++ e não chama
Rust. Rejeições de conteúdo — JSON/schema/identidade/caminho lexical inválido,
`unsupported-format`, `input-too-large` ou `output-too-large` — retornam
`ProfileStatus::Invalid` e preservam o fallback genérico atual. Falhas internas
da ABI, argumentos, buffers, decoder TLPR, panic ou status inesperado retornam
`ProfileStatus::InternalError` sem fallback. O evento `compat-profile` recebe
`backend="rust" parser-status="success"` quando há sucesso Rust; rejeições
também carregam `code`, `phase`, `input-offset` e `detail-value`.

`TL_BUILD_RUST=OFF` é a variante C++ explícita e padrão: não compila, liga ou
referencia os símbolos Rust e não recebe campos Rust no trace. A promoção não
altera `Profile`, TLPR, `Cargo.lock`, loader, materializador ou o nível de
compatibilidade declarado para qualquer aplicativo.

## Parser Rust do catálogo — R24.1–R24.3

R24.1 define e testa o contrato TLAC v1.0 para `library.json`; R24.2 adiciona
o adaptador e decoder C++ para o diferencial semântico. Em R24.3, com
`TL_BUILD_RUST=ON`, Rust é o parser canônico de catálogos existentes dentro de
`AppCatalog::load_from_file`. A assinatura pública, `AppEntry`, o formato
persistido e o nível de compatibilidade dos aplicativos não mudam.

O arquivo é lido e limitado em C++, o resultado Rust é validado pelo decoder e
somente então publicado. Conteúdo inválido, limites ou falhas internas deixam
o catálogo vazio e retornam `false`; não há fallback Rust→C++. Arquivo ausente
continua sendo detectado antes da chamada Rust. Escrita do catálogo,
filesystem, permissões, materialização, seleção de backend e execução seguem
em C++. `TL_BUILD_RUST=OFF` é a variante C++ explícita e padrão, sem link ou
referência operacional ao parser Rust.

Com trace, o componente `runtime` emite `catalog-parse` para tentativas Rust,
com `backend`, `parser-status` e, em rejeições, `code`, `phase`,
`input-offset` e `detail-value`. Sem trace o stdout/stderr normal permanece
inalterado; nenhum aplicativo ou nível de compatibilidade é promovido por
esta mudança.

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
| `tl_seh.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!RaiseException`, VEH, `RtlCaptureContext`, `RtlUnwindEx`, `CreateThread`, console; `msvcrt.dll!__C_specific_handler` | **Suportado para exceção explícita:** valida transferência de `RtlUnwindEx`/RAX, registra/remove VEH, lança em thread convidada e seleciona um `__except` por `SCOPE_TABLE_AMD64`; imprime `seh\n`, exit `0`. Não cobre C++, `__finally` nem sinais Linux. | Despacho SEH x64 |
| `tl_seh_v2.exe` | PE32+ AMD64 | Não | Mesmo subconjunto de `tl_seh.exe` | **Suportado fora de epílogo V2:** a mesma busca e transferência SEH usa metadado V2 promovido deterministicamente; imprime `seh\n`, exit `0`. | Despacho SEH x64 |
| `tl_locale_env_fls.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — ambiente W, CP/locale e FLS; thread, console e `ExitProcess` | **Suportado no núcleo determinístico:** altera/expande o ambiente isolado, valida bloco UTF-16, ACP 1252/OEMCP 437, CP437, `en-US`, `LCMapStringW/Ex` e callbacks FLS na thread filha e em `FlsFree`; imprime `locale-env-fls\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Ambiente, locale e FLS |
| `tl_locale_extended.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — validação de locale/code page, enumeração, tipo de caractere e formato de data/hora; console e `ExitProcess` | **Suportado no locale estático:** valida `en-US`/`0x0409`, CP1252/437/UTF-8, enumera o único locale por callback Microsoft x64, classifica `CT_CTYPE1` e formata data/hora en-US; imprime `locale-extended\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Locale determinístico ampliado |
| `tl_process_console.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — startup, handles/tipo, console W, diretório do sistema, recursos do processador, ponteiros e SList | **Suportado no contexto determinístico:** valida `STARTUPINFOW`, troca/restaura stdout, lê UTF-8 como UTF-16, escreve `process-console-é\n`, consulta `C:\Windows\System32`, testa SSE2, encode/decode e SList alinhada; exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Processo e console Win32 |
| `tl_file_metadata.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — enumeração ExW, atributos e metadados por handle | **Suportado no subconjunto de prefixo:** cria dados em `C:\`, enumera com `*`/`?`, alterna `READONLY`, aplica `FileBasicInfo` e testa exclusão no fechamento e POSIX; imprime `file-metadata\n`, exit `0`. Metadata, `--report`, trace e execução são regressões CTest. | Arquivos x64 |
| `tl_security.exe` | PE32+ AMD64 | Não | `ADVAPI32.dll` — token/SID, descritor, DACL e `SetEntriesInAclW`; `KERNEL32.dll` — arquivo/console | **Suportado no subconjunto virtual por prefixo:** cria `C:\tl_security\acl.bin`, consulta `TokenUser` pelo protocolo de tamanho, verifica usuário não elevado, mescla/grava DACL e a lê numa segunda execução; imprime `security-write\n` e depois `security-read\n`. `runtime_tl_security_prefix` prova persistência e isolamento entre prefixos. | Identidade/DACL virtual |
| `tl_dialog.exe` | PE32+ AMD64 | Não | `COMCTL32.dll!InitCommonControlsEx`; `KERNEL32.dll!ExitProcess`, `GetModuleHandleW`, `GetStdHandle`, `WriteFile`; `USER32.dll!DialogBoxParamW`, `EndDialog`, `GetDlgItem`, `SetDlgItemTextW`, `SendDlgItemMessageW`, `GetNextDlgTabItem`, `GetWindowRect`, `Get/SetWindowLongW`, `CopyImage`, `DestroyIcon`, `LoadIconW` | **Suportado no subconjunto modal:** recurso `DIALOG` padrão, `WM_INITDIALOG`, controles lógicos, texto por ID, tabulação, ícone copiado e retorno 42; o cenário `dialog` do `runtime_gui_smoke` envia Tab/Enter e espera `dialog\n`, sem `WM_QUIT` modal | Fase 13.11 |
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
| `tl_crypt32.exe` | PE32+ AMD64 | Não | `CRYPT32.dll!CertGetNameStringW`; `KERNEL32.dll` — console | **Suportado no subconjunto:** lê `CERT_CONTEXT` com blob DER de certificado, nomes simples/issuer/DNS por CN e atributo OID, consulta de tamanho e erro de buffer; saída `crypt32\n`, exit `0`; `--report` resolve 5/5 | Nome de certificado DER |
| `tl_process_parent.exe` / `tl_process_child.exe` | PE32+ AMD64 | Não | `CreateProcessW`, ambiente W, `GetExitCodeProcess`, `TerminateProcess` e `WaitForSingleObject` | Pai cria filhos PE32+ pelo mesmo parser/loader/import resolver e define uma variável que o filho precisa ler, provando a cópia do ambiente Win32; o código de saída real viaja pelo pipe `[flag][exit_code LE32]`. Valida código `7`, encerramento `9` e saída `child\nparent\n`, exit `0`. | Processos filhos |
| `tl_install_setup.exe` / `tl_install_app.exe` | PE32+ AMD64 | Não | arquivos Unicode, ambiente, `GetModuleFileNameW`, `CreateProcessW`, espera e handles | **Fluxo de instalação suportado:** setup externo observa `Z:\\...`, copia a aplicação de `C:\\windows\\temp` para `C:\\Program Files` e a inicia com `CreateProcessW`; a aplicação observa `C:\\...`, diretório herdado e `%LOCALAPPDATA%` do mesmo prefixo. `install → catálogo → app run` é coberto por `integration_install_prefix_catalog_run`; prefixos distintos não compartilham estado. O setup de múltiplos candidatos confirma `InstallPending` (`6`) e a escolha no launcher | Instalação por prefixo |
| `tl_network_loopback.exe` | PE32+ AMD64 | Não | `WS2_32.dll` TCP/UDP, resolução local e `WSAPoll` | Fixture somente loopback, com TCP, UDP e `localhost`; passa com sockets permitidos e é skip controlado em sandbox que retorna `EACCES/EPERM` | WS2_32 |
| `tl_worker_rsl.exe` | PE32+ AMD64 | Não | `WS2_32.dll`/`IPHLPAPI.DLL`/`CRYPT32.dll`/`WTSAPI32.dll` + console | Fluxo combinado do Worker: `WSAStartup`/`getaddrinfo` local, enumeração IPv4 via `getifaddrs`, loja CRYPT32 em memória e sessão WTS local; `ExitProcess 0`, ou skip controlado `77` sem interface IPv4 | Worker RSL |
| `tl_wininet.exe` | PE32+ AMD64 | Não | `WININET.dll` — abertura, conexão HTTPS, requisição, cabeçalhos, resposta, leitura, consulta e fechamento; `KERNEL32.dll` — ambiente/console | **Suportado somente para protocolo HTTPS loopback:** o smoke cria servidor TLS e CA efêmeros em `127.0.0.1`, valida URL, cabeçalho, status `200`, leitura parcial e CA confiável; uma CA diferente falha de forma controlada. Sem Internet, proxy, cookies, credenciais, redirecionamento ou WinTrust. | WinINet HTTPS local |
| `tl_stream.exe` | PE32+ AMD64 | Não | `ole32.dll!CreateStreamOnHGlobal`; vtable `IStream` | **Suportado no subconjunto de stream em memória:** `QueryInterface`, referências, `Read`/`Write`, `Seek`, `SetSize`, `Stat`, `Commit`/`Revert`; saída `ole-stream\n`, exit `0` | OLE stream em memória |
| `tl_trust.exe` | PE32+ AMD64 | Não | `WINTRUST.dll!WinVerifyTrust`; `KERNEL32.dll` — console | **Suportado somente na política de blob TLTC:** cadeia DER explícita folha→raiz, UI desabilitada e sem revogação; rejeita raiz incorreta e política incompatível; saída `trust\n`, exit `0` | Cadeia WinTrust local |
| `tl_wthelper.exe` | PE32+ AMD64 | Não | `WINTRUST.dll!WinVerifyTrust`, `WTHelperProvDataFromStateData`, `WTHelperGetProvSignerFromChain`, `WTHelperGetProvCertFromChain`; `CRYPT32.dll!CertGetNameStringW`; `KERNEL32.dll` — console | **Suportado no subconjunto de estado:** cria/fecha estado WinTrust para cadeia TLTC, percorre signer e folha/raiz, extrai o CN DER e rejeita índices inválidos ou uso após `CLOSE`; saída `wthelper\n`, exit `0` | Travessia WTHelper |
| `tl_registry_unicode.exe` | PE32+ AMD64 | Não | `ADVAPI32.dll` chaves/valores Unicode | Cria, persiste, reabre, consulta e remove chave/valor UTF-16 em armazenamento genérico por escopo; saída `registry\n`, exit `0` | Registro |
| `tl_dynload.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `LoadLibraryA/W/ExA/ExW`, `FreeLibrary`, `GetModuleHandleA/W/ExA/ExW`, `GetProcAddress`, `GetLastError` | Fixture de carregamento dinâmico: `LoadLibrary` com caminho `C:\...`, API Set `api-ms-win-core-file-l1-1-0.dll`, `LoadLibraryEx`, `GetProcAddress` por nome e ordinal (36=`GetTickCount64`), `FreeLibrary`, `GetModuleHandleEx` `PIN`/`FROM_ADDRESS`; saída `dynload\n`, exit `0` | Carregamento dinâmico |
| `tl_version.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `GetVersionExA/W`, `VerifyVersionInfoW`, `VerSetConditionMask`, `GetUserDefaultLocaleName`, `LocaleNameToLCID` | Fixture versão/locale: `GetVersionExA/W` 10.0.19044, `VerifyVersionInfoW`/`VerSetConditionMask` cadeia `VER_MAJOR|MINOR`, `GetUserDefaultLocaleName` → `en-US` (6 com NUL, `122` em buffer curto), `LocaleNameToLCID` `en-US`/`pt-BR`; saída `version\n`, exit `0` | Versão/locale |
| `tl_waitaddr.exe` | PE32+ AMD64 | Não | `KERNEL32.dll`/`api-ms-win-core-synch-l1-2-0.dll` — `WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll`, `CreateThread`/`WaitForSingleObject` | Fixture espera por endereço: timeout 50ms `ERROR_TIMEOUT`, size inválido `87`, `WaitOnAddress` 1/2/4/8, thread waiter `WaitOnAddress`→`WakeByAddressSingle`→`WaitForSingleObject`; via `api-ms-win-core-synch-l1-2-0.dll` forwarder; saída `waitaddr\n`, exit `0` | Sincronização por endereço |
| `tl_fiber.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `ConvertThreadToFiber`/`ConvertThreadToFiberEx`/`ConvertFiberToThread`/`CreateFiber`/`CreateFiberEx`/`SwitchToFiber`/`DeleteFiber`/`GetFiberData` | Fixture fibras: `ConvertThreadToFiberEx` com flags, `CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread`; saída `fiber\n`, exit `0` | Fibras |
| `tl_toolhelp.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `CreateToolhelp32Snapshot`/`Process32FirstW`/`Process32NextW`/`OpenProcess`/`GetCurrentProcessId` | Fixture Toolhelp: `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` enumera `/proc`, `Process32FirstW`/`NextW` com `PROCESSENTRY32W` 568 bytes valida `dwSize`, `OpenProcess` via `/proc/[pid]` e `CloseHandle` para snapshot/process; saída `toolhelp\n`, exit `0` | Processos |
| `tl_shell.exe` | PE32+ AMD64 | Não | `SHELL32.dll` — `SHGetKnownFolderPath`/`SHGetFolderPathW`/`SHGetFolderPathAndSubDirW`/`ShellExecuteW`/`ShellExecuteExW` | Fixture SHELL32: `FOLDERID_RoamingAppData`→`en-US` path, `CSIDL_APPDATA`/`TestSub`, `ShellExecuteW` `42`, `ShellExecuteExW` dummy `hProcess`; saída `shell\n`, exit `0` | Pastas conhecidas |
| `tl_gdiex.exe` | PE32+ AMD64 | Não | `GDI32.dll` — `CreateFontW`/`SetDCBrushColor`/`SetDCPenColor`; `gdiplus.dll` — 8 APIs; `UxTheme.dll` — `SetWindowTheme`; `WINMM.dll` — `timeSetEvent`; `dbghelp.dll` — `SymFromAddr` | Fixture GDI estendido: `CreateFontW` wide, `SetDCBrush/PenColor`, `GdiplusStartup`/`GdipCreateBitmapFromStream`/`Clone`/`HBITMAP`/`Dispose`/`Alloc/Free`, `SetWindowTheme`, `timeSetEvent` `1`, `SymFromAddr` stub; `USER32` `GetDC`; saída `gdiex\n`, exit `0` | GDI estendido |
| `tl_com.exe` | PE32+ AMD64 | Não | `ole32.dll` — `CoInitialize`/`CoInitializeEx`/`CoUninitialize`/`CoCreateInstance`/`CoGetClassObject`/`OleInitialize`/`OleUninitialize`/`CoTaskMemAlloc/Free` | Fixture COM mínimo: `CoInitialize` `S_OK`, `CoCreateInstance` `REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`, `OleInitialize`; saída `com\n`, exit `0` | COM mínimo |
| `tl_7zfm_gui.exe` | PE32+ AMD64 | msvcrt | `KERNEL32`, `USER32`, `ADVAPI32`, `SHELL32`, `COMCTL32`, `MPR`, `msvcrt` | **Suportado:** valida menus, notificações de arquivo, drag & drop, shell folders e conexões de rede MPR; imprime `7ZFM GUI APIS OK\n`, exit `0` | 7-Zip GUI |
| `tl_putty.exe` | PE32+ AMD64 | Não | `KERNEL32`, `WS2_32`, `GDI32`, `USER32`, `comdlg32`, `IMM32`, `SHELL32`, `ADVAPI32` | **Suportado:** sockets assíncronos, eventos WSA, fontes GDI, desenho vetorial, caret, barras de rolagem e registro; imprime `PUTTY SSH APIS OK\n`, exit `0` | PuTTY SSH Client |
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

## Leitor de PE (Fase 1)

O leitor de PE (`include/tradutorlinux/pe/pe_reader.hpp`, `src/pe/pe_reader.cpp`) valida e interpreta:

- DOS header, assinatura PE, COFF header e optional header PE32+ (magic `0x20B`).
- Tabela de seções, com verificação de que headers e dados crus cabem no arquivo.
- Import table por nome (hint) e por ordinal, com limites de DLLs e símbolos.
- Delay import table (`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`) por nome e ordinal,
  quando os descritores usam RVAs (`grAttrs=0x1`).
- Diretório de exceções x64 (`.pdata`/`.xdata`) com `RUNTIME_FUNCTION`,
  `UNWIND_INFO` versões 1 e 2, epílogos V2 normalizados, handlers
  reconhecidos e cadeias validadas.
- Base relocations por bloco e entrada.

Comportamento de rejeição:

| Entrada | Resultado |
|---|---|
| Arquivo truncado no meio de qualquer estrutura | `Truncated` |
| Assinatura DOS/PE ausente, offsets inconsistentes, tamanhos inválidos | `Malformed` |
| Arquitetura diferente de `x86-64` (machine `0x8664`) | `UnsupportedArchitecture` |
| Optional header PE32 (magic `0x10B`) ou outro formato | `UnsupportedFormat` |
| Descriptor delay-import com atributos diferentes de `0x1` | `UnsupportedMechanism` |
| Versão 3+/opcode/flag de mecanismo futuro de `UNWIND_INFO` estruturalmente válido | `UnsupportedMechanism` |
| Tabela `.pdata`/`.xdata`, RVA, código ou cadeia de unwind inválidos | `Malformed` |

O CLI expõe o leitor via `--trace` (eventos do componente `pe`, ver `docs/diagnostico.md`) e via resumo em `stderr`. A saída do leitor é comparada em teste de integração com `llvm-readobj` para as fixtures geradas.

O contrato de desempilhamento e despacho SEH fica em
[`arquitetura/unwinding-x64.md`](arquitetura/unwinding-x64.md). O runtime
suporta apenas exceções explícitas V1/V2 fora de epílogos, não C++/`__finally`
nem sinais Linux.

## Mapeamento de imagem (Fase 2)

O mapeador (`include/tradutorlinux/loader/image_mapper.hpp`, `src/loader/image_mapper.cpp`) reserva a imagem no endereço preferencial quando possível e aplica base relocations quando a base real difere da preferencial. O contrato detalhado (layout de memória, política de permissões, tipos de relocations suportados) está em `docs/arquitetura/mapeamento-imagem.md`.

Comportamento de rejeição:

| Condição | Resultado |
|---|---|
| `SizeOfImage` inválido (0) ou que excede o espaço de endereço do host | `InvalidImage` |
| Seção que excede o tamanho da imagem | `InvalidImage` |
| Seções sobrepostas na imagem | `InvalidImage` |
| Diretório de relocations inválido ou com alvo fora da imagem mapeada | `InvalidImage` |
| Falha do `mmap`/`mprotect` por falta de memória | `OutOfMemory` |

O CLI emite eventos `loader` no trace (ver `docs/diagnostico.md`) ou um resumo em `stderr` quando `--trace` não é usado. O mapa é liberado (`unmap`) ao final do comando.

## Resolução de imports (Fase 3)

O resolvedor (`include/tradutorlinux/loader/import_resolver.hpp`, `src/loader/import_resolver.cpp`) percorre as import tables estática e atrasada do PE, procura cada DLL no registro de módulos internos e grava o endereço resolvido no slot correspondente da IAT da imagem mapeada. Os módulos internos registrados embutidos são declarados em `include/tradutorlinux/loader/module.hpp` e `src/loader/module.cpp`; o contrato (registro, tabela de exports, ordinais internos, ABI) está em `docs/arquitetura/imports.md`.

O contexto mínimo de processo (`include/tradutorlinux/loader/process.hpp`, `src/loader/process.cpp`) mapeia a imagem, resolve imports e prepara a pilha do thread inicial com guard page; o entry point nunca é executado nesta fase.

Comportamento de rejeição:

| Condição | `status` no trace | Resultado |
|---|---|---|
| DLL não registrada | `unknown-dll` | `5` (`Unsupported`) |
| Símbolo não exportado pela DLL | `unknown-symbol` | `5` |
| Ordinal não exportado pela DLL | `unknown-ordinal` | `5` |
| Símbolo conhecido sem implementação | `not-implemented` | `5` |
| Descriptor delay-import com atributos não-RVA | `unsupported-mechanism` | `5` |
| Slot da IAT fora das seções mapeadas | `unsupported-mechanism` | `5` |

Em qualquer falha o entry point não é executado e todas as entradas são reportadas no trace (ver `docs/diagnostico.md`).

**Forwarders e API Sets (inspirado em Wine `dlls/*/*.spec`):** `KERNELBASE.dll` encaminha para `KERNEL32.dll`; `api-ms-win-*` e `ext-ms-win-*` encaminham para o provedor real (`KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `WS2_32`, `SHELL32`, `ole32`, `SHLWAPI`, `version`, `WINMM`, `COMCTL32`, `COMDLG32`, `IMM32`, `PSAPI`, `msvcrt`) — ver `src/loader/module.cpp:52` (`is_api_set_dll`/`is_kernelbase_dll`/`find_export_forwarded`). Falha de símbolo em API Set vira `unknown-symbol`, não `unknown-dll`.

### Extensões de DLL por aplicativo (B14.4)

Perfis v2 podem declarar DLLs PE32+ AMD64 em `compat/dlls/`. A fixture
`tl_compat_dll_app.exe` e o teste `integration_compat_dll_profile` exercitam o
contrato com uma DLL personalizada, uma dependência PE, exports, imports para
`KERNEL32.dll`, TLS callback, `DllMain`, `LoadLibrary`, `GetProcAddress` e
`FreeLibrary`. O mesmo destino é testado com duas variantes em prefixos
independentes; cada execução recebe somente o provider do seu perfil e a fonte
permanece em `compat/dlls/`.

O provider do perfil precede uma DLL PE existente em `drive_c`, que precede o
provider genérico interno. A precedência também vale por export: um símbolo
ausente na extensão pode ser resolvido pelo provider seguinte. Provider
personalizado ausente, inválido, com import não resolvido, dependência ausente,
ciclo ou attach rejeitado é descartado inteiro e usa fallback quando houver.
Uma falha depois do attach é falha do convidado e não vira fallback silencioso.
`compat/` não é pesquisada nem copiada para `drive_c`, e `--report` não carrega
essas DLLs; o diagnóstico contextual ocorre em `app run --trace`.

## APIs de console (Fase 4)

Os exports de `KERNEL32.dll` apontam para funções hospedeiras com a convenção Microsoft x64 (`TL_MSABI`). O runner chama o entry point depois de mapear a imagem e resolver a IAT; `ExitProcess` transfere o controle de volta ao runner e não encerra diretamente o processo Linux.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | Suportado | Mapeia `STD_INPUT_HANDLE`, `STD_OUTPUT_HANDLE` e `STD_ERROR_HANDLE` para tokens opacos; outros valores retornam `NULL` |
| `KERNEL32.dll` | `WriteFile` | Suportado | Escreve em stdout/stderr; exige handle padrão válido, buffer válido e `lpOverlapped == NULL` |
| `KERNEL32.dll` | `ReadFile` | Suportado | Lê stdin; exige handle padrão de entrada válido, buffer válido e `lpOverlapped == NULL` |
| `KERNEL32.dll` | `ExitProcess` | Suportado | Captura o código de saída e retorna o controle ao runner |

Os tokens de handles padrão não são handles de arquivo. A entrada e saída são bytes; nenhuma conversão de encoding é feita. O contrato detalhado está em `docs/arquitetura/console.md`.

## Runtime básico (Fase 5)

`GetLastError`/`SetLastError` usam estado por thread. O subconjunto de arquivos
é `CreateFileA` com `GENERIC_READ`/`GENERIC_WRITE`, `CREATE_ALWAYS` ou
`OPEN_EXISTING`, seguido por `ReadFile`, `WriteFile` e `CloseHandle`. Apenas
caminhos relativos sem drive são aceitos; `\\` é normalizado para `/`.

`VirtualAlloc`, `VirtualFree`, `VirtualProtect` e `VirtualQuery` têm o contrato limitado descrito em
[`runtime-basico.md`](arquitetura/runtime-basico.md).

## GUI mínima (Fase 7)

O protótipo registra um subconjunto de `USER32.dll` e `GDI32.dll` e usa X11
diretamente. Ele é experimental, não altera o subsistema de console e só aceita
`type == 0` em `MessageBoxA`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `USER32.dll` | `MessageBoxA` | Suportado | Caixa modal com `hWnd == NULL` e `uType == 0`; OK retorna `1`, fechar retorna `0` |
| `USER32.dll` | `RegisterClassExA` | Suportado | Classe única por nome (case-insensitive); retorna atom `>= 1` |
| `USER32.dll` | `CreateWindowExA` | Suportado no subconjunto | Cria janela X11 a partir da classe registrada e despacha `WM_CREATE` ao `WNDPROC`; classes próprias usadas como filhos ficam em uma side-table, não viram janelas X11 individuais e participam do hit-test de mouse |
| `USER32.dll` | `ShowWindow` | Suportado | Mostra/esconde a janela X11 |
| `USER32.dll` | `UpdateWindow` | Suportado | Despacha `WM_PAINT` diretamente ao `WNDPROC` |
| `USER32.dll` | `InvalidateRect` | Suportado no subconjunto | Valida o `RECT` opcional, enfileira um `WM_PAINT` por janela até a entrega e faz flush da superfície X11 projetada para filhos lógicos |
| `USER32.dll` | `GetMessageA` | Suportado no subconjunto | Traduz eventos X11 para `WM_PAINT`/`WM_LBUTTONDOWN`/`WM_KEYDOWN`/`WM_KEYUP`/`WM_CLOSE`; o hit-test entrega mouse a filhos lógicos customizados com `HWND` e coordenadas locais, enquanto controles comuns sem `WNDPROC` preservam suas notificações no parent; entrega mensagens pendentes antes dos eventos X11, despacha `WM_TIMER` expirados e retorna `0` com `WM_QUIT` após `PostQuitMessage` |
| `USER32.dll` | `TranslateMessage` | Suportado | Converte o `WM_KEYDOWN` mais recente em `WM_CHAR` com o caractere real (sem `WM_CHAR` para teclas sem caractere) |
| `USER32.dll` | `SetTimer` | Suportado | Timer periódico por janela → `WM_TIMER`; só `lpTimerFunc == NULL` |
| `USER32.dll` | `KillTimer` | Suportado | Remove um timer ativo |
| `USER32.dll` | `DispatchMessageA` | Suportado | Invoca o `WNDPROC` do convidado (`TL_MSABI`, host→convidado) |
| `USER32.dll` | `DefWindowProcA` | Suportado | `WM_CLOSE` → `DestroyWindow`; demais retornam `0` |
| `USER32.dll` | `RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` etc. | Suportado | Wrappers para `A` via `wide_to_utf8`/`utf8_to_wide`; `RegisterClassExW` converte `WNDCLASSEXW` (80 bytes), `CreateWindowExW` converte classe/título, `SetWindowTextW`/`GetWindowTextW`/`GetWindowTextLengthW` convertem, `FindWindowW`/`SendMessageW`/`AppendMenuW` delegam |
| `USER32.dll` | `GetClassInfoW` | Suportado no subconjunto | Consulta a tabela de classes registrada, preenche `WNDCLASSW` quando há saída válida e retorna `ERROR_CLASS_DOES_NOT_EXIST` (`141`) para classe ausente |
| `USER32.dll` | `LoadMenuW`, `GetMenu`, `SetMenu`, `GetSubMenu`, `GetMenuItemCount`, `GetMenuItemInfoW` | Suportado no subconjunto | `LoadMenuW` lê recursos `RT_MENU` MENUEX v1 do módulo convidado, preserva itens/IDs/textos/submenus e associa o menu de classe à janela principal; `GetMenuItemInfoW` valida e preenche o buffer. Para o shell visual específico do 7-Zip, dropdowns aninhados, seleção por mouse/teclado e itens folha encaminham `WM_COMMAND`; templates v0 e mutações continuam fora |
| `USER32.dll` | `DestroyWindow` | Suportado | Destrói a janela e despacha `WM_DESTROY` |
| `USER32.dll` | `PostQuitMessage` | Suportado | Sinaliza `WM_QUIT`; `GetMessageA` retorna `0` |
| `USER32.dll` | `GetDC` / `ReleaseDC` | Suportado | `HDC == HWND` (token opaco da janela); validam o par `hwnd`/`dc`; controles lógicos projetam o desenho na superfície X11 do pai com offsets acumulados |
| `USER32.dll` | `BeginPaint` / `EndPaint` | Suportado | Preenchem o `PAINTSTRUCT` (layout Microsoft x64, 72 bytes) com o tamanho da janela/controle e marcam/desmarcam o estado de pintura; o `HDC` usa a superfície X11 da janela principal |
| `USER32.dll` | `SendMessageA` / `SendMessageW` (controles comuns) | Suportado no subconjunto | Toolbar: `TB_BUTTONSTRUCTSIZE`, `TB_ADDBUTTONSA/W`, `TB_BUTTONCOUNT`, `TB_DELETEBUTTON`, `TB_SETBUTTONSIZE`, `TB_SETBITMAPSIZE`, `TB_AUTOSIZE`, `TB_SETIMAGELIST`, `TB_ENABLEBUTTON`; status bar: `SB_SETTEXTA/W`, `SB_SETPARTS`, `SB_SETMINHEIGHT`, `SB_SIMPLE` |
| `GDI32.dll` | `GetStockObject` | Suportado | Token opaco por stock object (tabela estática, `object` em `0..23`); stock objects não são liberados |
| `GDI32.dll` | `TextOutA` / `TextOut` | Suportado | Desenha texto ANSI com comprimento explícito via `XDrawString`; em controles lógicos soma a posição dos pais ao destino |

### Diálogos e controles (Fase 13.11)

| Módulo | APIs | Estado | Limite publicado |
|---|---|---|---|
| `USER32.dll` | `DialogBoxParamW`, `EndDialog`, `GetDlgItem`, `SetDlgItemTextW`, `SendDlgItemMessageW`, `GetNextDlgTabItem`, `IsDialogMessageW` | Suportado no subconjunto | Somente template numérico `RT_DIALOG` padrão do módulo atual; modal único; controles lógicos `BUTTON`/`EDIT`/`STATIC`/`COMBOBOX`; classes customizadas recebem ciclo básico, mas continuam sem renderer visual genérico; `7-Zip::FM` possui shell visual específico experimental com lista imediata selecionável, navegação visual por pastas via Enter ou duplo clique, árvore lateral limitada a diretórios Linux conhecidos e barra `Address` restrita à raiz visual |
| `USER32.dll` | `GetWindowRect`, `GetWindowLongW`, `SetWindowLongW` | Suportado no subconjunto | Geometria side-table e wrappers limitados de 32 bits sobre `*Ptr` |
| `USER32.dll` | `CopyImage`, `DestroyIcon` | Suportado no subconjunto | Tokens de ícone copiados; não há `LoadImageW` nem desenho de ícones |
| `COMCTL32.dll` | `InitCommonControlsEx` | Suportado no layout de 8 bytes | Valida `cbSize`/classes; ordinais 410/413 continuam `unknown-ordinal` |
| `COMCTL32.dll` | `CreateStatusWindowW` | Suportado no subconjunto | Parent válido; cria uma `msctls_statusbar32` lógica no rodapé, com texto UTF-16 convertido para UTF-8 e desenho na superfície X11 principal |
| `COMCTL32.dll` | `CreateToolbarEx` | Suportado no subconjunto | Parent válido; valida até 128 entradas do vetor `TBBUTTON`, preserva `idCommand`, desenha botões lógicos, mostra o pressionamento, cancela soltura fora do botão e encaminha somente o clique capturado por `WM_COMMAND`; mensagens `TB_ADDBUTTONSA/W` e `TB_AUTOSIZE` também atualizam esse modelo; bitmaps, image lists e estilos avançados permanecem fora |

`tl_dialog.exe` valida o ciclo mínimo sob Xvfb quando o ambiente fornece o
socket X11. O smoke confirma Tab/Enter, `WM_COMMAND`, retorno 42, saída
`dialog\n`, destruição modal e trace; sem X11, o teste é explicitamente
`Skipped`.

`tl_gui.exe` é validado automaticamente quanto a formato e imports; a janela
deve ser validada manualmente numa sessão X11. `tl_win.exe`, `tl_win2.exe`,
`tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe`, `tl_paint.exe` e `tl_dialog.exe`
são executados de ponta a ponta sob `Xvfb` (sempre um servidor próprio, sem
window manager) pelo teste `runtime_gui_smoke`, que cobre o message loop
(autoclose), o fechamento real por `WM_DELETE_WINDOW`, a entrada de teclado
(`KeyPress` sintético → `WM_KEYDOWN`/`WM_CHAR`), a demultiplexação entre duas
janelas simultâneas, o teclado estendido (`KeyPress`+`KeyRelease`, `Shift`,
teclas sem caractere → `WM_KEYDOWN`/`WM_KEYUP`), os timers (`SetTimer` →
`WM_TIMER` → `KillTimer`), a pintura mínima (`BeginPaint`/`TextOut`/`EndPaint`)
e o diálogo modal (`Tab`/`Enter`/`WM_COMMAND`). O `x11_popup_smoke` cobre
Escape, clique externo, destruição externa e timeout. O driver valida também
os traces de contrato do message loop: `GetMessageA ... result="quit"`,
`ExitProcess ... mechanism="guest-transfer"`, `TranslateMessage ...
status="translated"`, `SetTimer`/`GetMessageA(WM_TIMER)`/`KillTimer`,
`BeginPaint`, `TextOut`, `Rectangle` e `FillRect` — ver
[`gui-x11.md`](arquitetura/gui-x11.md).

No preset `sanitize`, o CTest executa `x11_popup_smoke` com LeakSanitizer
habilitado: o cenário inclui 512 desenhos de cores e os quatro caminhos de
cleanup de popup, sem relatório de ASan/LSan na validação sob Xvfb.

## Simple Todo C (Fase 12)

O alvo `Efeckc17/simple-todo-c` é baixado por archive pinado e hash SHA-256 em
`tests/targets/CMakeLists.txt`. O recurso `app.rc` é gerado no diretório de
build com o manifesto e o ícone upstream; `tests/targets/manifests/simple_todo.json`
fixa a lista de 105 imports. O build aplica os overlays
`tests/targets/patches/simple_todo_linux.patch` e
`simple_todo_linux_autorun.patch` e `simple_todo_linux_close.patch`: a tela
ganha layout Linux, a opção de autorun no Windows é removida e o fechamento da
janela destrói o processo. O teste `targetapp_simple_todo_gui_smoke` usa
um Xvfb próprio, `APPDATA=appdata` relativo ao diretório de teste e verifica o
fluxo de adicionar, editar, buscar, concluir, excluir, esconder, mostrar e
sair pelo menu emulado.

| Módulo | APIs adicionais | Estado | Limite publicado |
|---|---|---|---|
| `USER32.dll` | `RegisterClassA`, controles lógicos via `CreateWindowExA`, `SendMessageA`, `Get/SetWindowTextA`, foco, geometria, `WM_COMMAND` e `WM_NOTIFY` | Implementado para o alvo | Não são janelas X11 filhas; EDIT, BUTTON, COMBOBOX, STATIC e SysListView32 são desenhados e roteados por uma side-table |
| `GDI32.dll` | `CreateFontA`, `CreateSolidBrush`, `DeleteObject`, `SetBkColor`, `SetTextColor` | Implementado para o alvo | Tokens de fonte/brush e cores têm efeito limitado; o desenho usa o GC X11 mínimo |
| `SHELL32.dll` | `Shell_NotifyIconA` | Implementado para o alvo | O ícone de bandeja é apenas um contrato lógico; o menu é uma janela popup X11, sem integração com o tray do desktop |
| `msvcrt.dll` | `_acmdln`, `_ismbblead`, `_time64`, `_localtime64`, `strftime`, `_strlwr` | Implementado para o alvo | Locale/DBCS continuam no subconjunto C/ANSI do runtime |

O smoke de integração sob Xvfb é o contrato de regressão do fluxo do alvo e
verifica também o evento de fechamento da janela. Isso não constitui suporte
geral a aplicativos Win32;
permanecem válidas as limitações específicas das APIs listadas acima.

## Aplicativos-alvo reais (Fase 8)

A Fase 8 mede progresso por aplicativos reais, e não apenas por fixtures. Os
primeiros alvos escolhidos são utilitários de console pequenos, de código
aberto e compilados em CI com `mingw-w64`. Cada alvo é fixado por versão,
toolchain e lista de imports; a lista real é capturada por `llvm-readobj` e por
`--report` do runtime e protegida por testes com o label `targetapp`.

As fontes são baixadas com hash SHA-256 verificado pelo módulo
`tests/targets/CMakeLists.txt` (opção `TL_BUILD_TARGET_APPS=ON`, usada no job
`target-apps` do CI). O `--report` lista os imports reais, agrupados por DLL
com contagem de resolução, e classifica o alvo como `result: supported`
(exit code `0`) ou `result: unsupported` (exit code `5`), sem mapear nem
executar a imagem; inclui linha `compatibility:` com porcentagem de imports
resolvidos e `execution-result: not-attempted`. O script
`tests/targets/verify_target_report.cmake` aceita as duas respostas, exige que
todo import do manifest apareça listado sob o grupo `dll:` correspondente e
valida as linhas `compatibility:` e `execution-result:`.

Os scripts de execução e2e (`verify_target_run.cmake`,
`verify_target_run_bytes.cmake` e `verify_target_conversion.cmake`) categorizam
o resultado em:
- `supported` — execução concluída, saída idêntica ao ouro
- `failed` — terminou por sinal, timeout ou exit code inesperado
- `incorrect` — saída diverge do ouro
- `not-attempted` — sem execução (`--report` apenas)

| Aplicativo | Versão / toolchain | Imports (símbolos) | Estado |
|---|---|---|---|
| `xxd.exe` | vim `v9.2.0957` (`src/xxd.c`), `-O2 -s` | `KERNEL32.dll` (16), `msvcrt.dll` (57) | **Executa de ponta a ponta**: saída byte-idêntica ao `xxd` do sistema nos modos padrão e `-p`, exit `0`; arquivo inexistente → exit `2` com erro em stderr. Regressão e2e em CTest (ouro em `tests/targets/golden/xxd/`, 3 testes) |
| `bzip2.exe` | bzip2 `1.0.8`, `-O2 -s` | `KERNEL32.dll` (13), `msvcrt.dll` (69) | **Executa de ponta a ponta**: compressão (`-c`) e descompressão (`-d`) de arquivo; saída válida verificada com `bzip2` nativo nos dois sentidos; exit `0` |
| `dos2unix.exe` | dos2unix `7.5.6`, `-O2 -DD2U_UNIFILE -s` | `KERNEL32.dll` (23), `msvcrt.dll` (67), `SHELL32.dll!CommandLineToArgvW` (1) | **Executa de ponta a ponta no fluxo validado**: `--report` resolve 91/91 imports; regressões convertem CRLF/misto para LF e processam `uni_el_*.txt` com nome UTF-8, usando `CommandLineToArgvW` e enumeração `W`; exit `0` |
| `unix2dos.exe` | dos2unix `7.5.6`, `-O2 -DD2U_UNIFILE -s` | idem `dos2unix.exe` | **Executa de ponta a ponta no fluxo validado**: `--report` resolve 91/91 imports; regressão converte LF para CRLF por stdout; exit `0` |

Os manifests com a lista completa de imports ficam em
`tests/targets/manifests/`. Os binários são produtos de build e ficam em
`build/<dir>/tests/targets/out/`.

## CRT mínimo e KERNEL32 de console (Fase 9)

O subconjunto de `msvcrt.dll` implementado é o núcleo do CRT do mingw-w64 e o
subconjunto de `KERNEL32.dll` exigido pelo `xxd.exe`. A fronteira usa a
convenção Microsoft x64 (`TL_CRT_MSABI`/`TL_MSABI`) e não propaga exceções C++.
Contratos de ABI em `docs/arquitetura/msvcrt.md` e
`docs/arquitetura/console.md`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `msvcrt.dll` | `__getmainargs` | Suportado | Constrói `argc`/`argv`/`envp` a partir da linha de comando do convidado (definida pelo CLI via `msvcrt_set_guest_command_line`); `argv[0]` é o caminho do executável; o final da lista é `NULL` |
| `msvcrt.dll` | `__initterm`, `__set_app_type`, `__setusermatherr`, `_cexit`, `_lock`, `_unlock`, `__C_specific_handler` | Suportado | `__initterm` executa a lista de callbacks (TLS/CTOR); os demais são no-ops. `__C_specific_handler` segue a ABI Microsoft de quatro argumentos e interpreta somente `SCOPE_TABLE_AMD64` de `__try/__except`. |
| `msvcrt.dll` | `_amsg_exit`, `abort`, `exit`, `atexit`, `_onexit` | Suportado | Terminam via `ExitProcess`; `atexit` e `_onexit` acumulam handlers executados no encerramento |
| `msvcrt.dll` | `_errno`, `getenv`, `strerror` | Suportado | Célula `errno` por thread; `getenv` lê o ambiente Win32 isolado do processo convidado |
| `msvcrt.dll` | `fopen`/`fclose`/`fflush`/`ferror`/`fseek`/`ftell`/`rewind`/`fgetc`/`fputc`/`fputs`/`fprintf`/`vfprintf`/`fwrite` | Suportado | I/O em `GuestFile` (layout `_iobuf` de 48 bytes), unbuffered via `::write` com loop `EINTR` |
| `msvcrt.dll` | `_open`/`_fdopen`/`_fileno`/`_isatty`/`_setmode`/`__iob_func` | Suportado | Tradução de flags `_O_*`; modo por fd (`_O_TEXT`/`_O_BINARY`) refletido na flag `_IOSTRG` do `GuestFile` |
| `msvcrt.dll` | `malloc`/`calloc`/`free`, `memcpy`/`memset` | Suportado | Alocação e memória diretas do hospedeiro |
| `msvcrt.dll` | `strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/`strtol`/`strtoul`/`wcslen`/`isalnum`/`isspace`/`toupper` | Suportado | Semântica libc para ASCII/latin-1 |
| `msvcrt.dll` | `fgetc`/`fread`/`ungetc` | Suportado | `fgetc` lê byte e verifica `charbuf` (pushback); `fread` lê `count` elementos de `size` bytes; `ungetc` devolve caractere ao stream via `charbuf` do `GuestFile` |
| `msvcrt.dll` | `memmove`/`remove`/`_stat64` | Suportado | `memmove` com tratamento de overlap; `remove` delega ao host; `_stat64` preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) a partir do `stat()` do host |
| `msvcrt.dll` | `localeconv`, `___lc_codepage_func`, `___mb_cur_max_func` | Suportado | Locale C fixo: `lconv` estático, code page `1252`, `mb_cur_max == 1` |
| `msvcrt.dll` | `signal` | Suportado | Registra handlers em tabela por sinal; nenhuma entrega real ao convidado |
| `KERNEL32.dll` | `VirtualQuery` | Suportado no subconjunto | Preenche `MEMORY_BASIC_INFORMATION` (48 bytes); reservas e commits próprios usam regiões rastreadas pelo runtime, inclusive `AllocationBase`, `AllocationProtect`, `State`, `Protect` e divisão após mudança parcial; os demais mapeamentos usam `/proc/self/maps` |
| `KERNEL32.dll` | `VirtualProtect` | Suportado no subconjunto | `mprotect` sobre a página alinhada dentro de uma alocação commitada; escreve a proteção antiga em `*lpflOldProtect`, atualiza a tabela de regiões e rejeita reserva ou faixa que não contenha `[address, address+size)` |
| `KERNEL32.dll` | `MultiByteToWideChar` / `WideCharToMultiByte` | Suportado | CP `0` (ACP → 1252), `1252`, OEM/`437` e `65001` (UTF-8), incluindo tabela CP437 completa; conversões manuais sem locale e `ERROR_INSUFFICIENT_BUFFER` (122) |
| `KERNEL32.dll` | `Initialize/Enter/Leave/DeleteCriticalSection` | Suportado | No-ops com validação de ponteiro (convidado single-thread → exclusão trivial) |
| `KERNEL32.dll` | `InitializeCriticalSectionAndSpinCount` / `InitializeCriticalSectionEx` | Suportado no subconjunto | Reutilizam a tabela de seções críticas; spin count é ignorado e `InitializeCriticalSectionEx` aceita somente `CRITICAL_SECTION_NO_DEBUG_INFO` ou flags zero |
| `KERNEL32.dll` | `AreFileApisANSI` | Suportado no subconjunto | Retorna `TRUE` para o ACP determinístico `1252` |
| `KERNEL32.dll` | `FormatMessageA` / `FormatMessageW` | Suportado no subconjunto | Mensagens de sistema fixas, buffer fornecido ou `FORMAT_MESSAGE_ALLOCATE_BUFFER`; sem inserts, tabelas externas ou recursos de mensagem |
| `KERNEL32.dll` | `GlobalAlloc` / `GlobalLock` / `GlobalUnlock` / `GlobalFree`, `LocalAlloc` / `LocalFree` | Suportado no subconjunto | Blocos `malloc`/`calloc` rastreados por handle, flags `GMEM_MOVEABLE`/`ZEROINIT`, contagem de locks e rejeição de ponteiros arbitrários; `GlobalAlloc` e `LocalAlloc` usam o mesmo envelope de memória do processo |
| `KERNEL32.dll` | `TlsGetValue` | Suportado | Retorna o valor do slot TLS da thread convidada e `ERROR_SUCCESS` para slot não usado; TLS estático e callback possuem contrato separado na seção de concorrência |
| `KERNEL32.dll` | `GetConsoleMode` / `SetConsoleMode` | Suportado | `GetConsoleMode` devolve `0x3` e `TRUE` só para fd com `isatty`; caso contrário `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `IsDBCSLeadByteEx` | Suportado | Sempre `FALSE` (sem DBCS) |
| `KERNEL32.dll` | `Sleep` | Suportado | `nanosleep` com loop `EINTR` |
| `KERNEL32.dll` | `SetUnhandledExceptionFilter` / `UnhandledExceptionFilter` | Suportado no SEH explícito | Registra/retorna o filtro anterior; o filtro é chamado somente quando VEH e busca por frame não resolvem `RaiseException`. |
| `KERNEL32.dll` | `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler` | Suportado no SEH explícito | Tokens opacos; prioridade `first` e remoção apenas do token correspondente. |
| `KERNEL32.dll` | `RaiseException` / `RtlUnwind` / `RtlUnwindEx` | Suportado no SEH explícito | Captura contexto, busca `.pdata/.xdata` V1/V2 fora de epílogo, chama handlers estáticos e transfere sem retorno ao contexto convidado selecionado. |
| `KERNEL32.dll` | `GetModuleHandleA/W` | Suportado | Retorna handle `0x1000` para módulos registrados (inclui `api-ms-win-*`/`KERNELBASE` via forwarders, extração de filename de caminhos `C:\...`), `NULL` + `ERROR_FILE_NOT_FOUND` caso contrário; `W` converte via `wide_to_utf8` |
| `KERNEL32.dll` | `GetModuleHandleExA/W` | Suportado | Flags `PIN`/`UNCHANGED_REFCOUNT`/`FROM_ADDRESS`; `FROM_ADDRESS` aceita `0x1000` ou endereço dentro da imagem (`g_guest_image_base/size`); valida `phModule` via `mapped_guest_range`; erro `ERROR_INVALID_PARAMETER`/`FILE_NOT_FOUND` |
| `KERNEL32.dll` | `LoadLibraryA/W` / `LoadLibraryExA/W` | Suportado no subconjunto | No `app run`, usa o grafo por execução e a precedência perfil → `drive_c` → genérico; no caminho legado usa módulos internos; normaliza filename case-insensitive e adiciona `.dll`; retorna `ERROR_MOD_NOT_FOUND` (126) quando ausente; `Ex` ignora `hFile`/`flags` |
| `KERNEL32.dll` | `FreeLibrary` | Suportado | Aceita `0x1000` ou base do exe; `NULL`/inválido → `0` + `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetProcAddress` | Suportado no subconjunto | No `app run`, busca no módulo identificado pelo handle e suporta ordinal via `MAKEINTRESOURCE`; aplica fallback por export do grafo. No caminho legado, usa a busca global dos módulos internos; valida `proc_name` e `module`; `ERROR_PROC_NOT_FOUND` (127) ou `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetVersionExA/W` | Suportado | Reporta Windows 10 (10.0.19044, `VER_PLATFORM_WIN32_NT=2`, `szCSDVersion` zero, `wServicePackMajor/Minor=0`, `wSuiteMask=0`, `wProductType=1`); valida `lpVersionInformation` e `dwOSVersionInfoSize` (A:148/156, W:276/284); `ERROR_INVALID_PARAMETER` em ponteiro/size inválido |
| `KERNEL32.dll` | `VerifyVersionInfoW` / `VerSetConditionMask` | Suportado | `VerifyVersionInfoW` valida `lpVersionInfo`/`dwTypeMask` e retorna `TRUE` (versão sempre compatível); `VerSetConditionMask` codifica 3 bits por `TypeMask` (`&0x07`, `shift=i*3`) como no Wine |
| `KERNEL32.dll` | `GetUserDefaultLocaleName` | Suportado | Retorna `en-US` (wide, `0x0409`); `NULL/0` → `6` (inclui NUL); buffer <6 → `0` + `ERROR_INSUFFICIENT_BUFFER` (122); valida `mapped_guest_range` |
| `KERNEL32.dll` | `LocaleNameToLCID` | Suportado | Converte `en-US`→`0x0409`, `pt-BR`→`0x0416`, `en`→`0x09`, `pt`→`0x16` (case-insensitive); `NULL`/vazio/desconhecido → `0` + `ERROR_INVALID_PARAMETER` |
| `KERNEL32.dll` | `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll` | Suportado | `WaitOnAddress` compara `*Address` vs `*CompareAddress` (`size` 1/2/4/8, alinhado, `mapped_guest_range`); se diferente retorna `1`; se igual espera por `Wake*` ou `dwMilliseconds` (`INFINITE`→`wait`, `0`→timeout imediato) via `mutex`+`cv`+`version` por endereço; `WakeSingle`→`notify_one`, `WakeAll`→`notify_all`; timeout → `0` + `ERROR_TIMEOUT` (1460); exposto via `KERNEL32` e `api-ms-win-core-synch-l1-2-0.dll` (forwarder) |
| `KERNEL32.dll` | `GetCommandLineA/W` | Suportado | Retorna linha de comando formatada com aspas a partir do `argv` do convidado |
| `KERNEL32.dll` | `GetEnvironmentVariableA/W`, `SetEnvironmentVariableW`, `Get/FreeEnvironmentStringsW`, `ExpandEnvironmentStringsW` | Suportado | Mapa por processo, case-insensitive, copiado do host e sobreposto pelo prefixo sem mutar o Linux; bloco UTF-16 ordenado/rastreado, expansão `%NOME%`, consultas de tamanho e `ERROR_INSUFFICIENT_BUFFER` |
| `KERNEL32.dll` | `FlsAlloc`, `FlsFree`, `FlsGetValue`, `FlsSetValue` | Suportado no subconjunto por thread | Índices/callbacks por processo, valores por thread; callback MS x64 validado na imagem, uma vez no fim da thread ou em `FlsFree`; fibras reais continuam fora do escopo |
| `KERNEL32.dll` | `GetACP`, `GetOEMCP`, `GetCPInfo`, `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoW/Ex`, `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW`, `GetTimeFormatW`, `LCMapStringW/Ex` | Suportado no subconjunto determinístico | Locale único `en-US`/`0x0409`, ACP 1252 e OEMCP 437; enumeração de um callback, `CT_CTYPE1`, formatos estáticos de data/hora e case mapping ASCII/Latin-1; sort keys, CJK, formatos customizados e locale do host não entram |
| `KERNEL32.dll` | `GetStartupInfoW`, `GetSystemDirectoryW`, `GetFileType`, `SetStdHandle`, `ReadConsoleW`, `WriteConsoleW`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `EncodePointer`, `DecodePointer`, `InitializeSListHead` | Suportado no subconjunto de processo/console | Estado padrão por processo e compartilhado por threads; `STARTUPINFOW` 104 bytes, `C:\Windows\System32`, console UTF-16↔UTF-8, recursos AMD64 fixos, cookie reversível e SList vazia alinhada; sem alocação de console, herança explícita ou operações interlocked de lista |
| `KERNEL32.dll` | `FindFirstFileExW`, `SetFileAttributesW`, `SetFileInformationByHandle` | Suportado no subconjunto de metadados | Enumeração W por `FindExInfoStandard/Basic`, `*`/`?` ASCII case-insensitive e `LARGE_FETCH` como hint; atributos `READONLY`/`NORMAL`/`ARCHIVE`/`DIRECTORY`; classes `FileBasicInfo`, `FileDispositionInfo` e `FileDispositionInfoEx` validadas no prefixo |
| `KERNEL32.dll` | `GetProcessHeap` | Suportado | Retorna token opaco fixo (heap único do processo) |
| `KERNEL32.dll` | `HeapAlloc` | Suportado | `malloc` do hospedeiro; flag `HEAP_ZERO_MEMORY` (0x0008) → `calloc` |
| `KERNEL32.dll` | `HeapFree` | Suportado | `free` do hospedeiro |
| `KERNEL32.dll` | `HeapReAlloc` | Suportado | `realloc` do hospedeiro |
| `KERNEL32.dll` | `GetTickCount64` | Suportado | `steady_clock` em milissegundos |
| `KERNEL32.dll` | `GetSystemTimeAsFileTime` | Suportado | `system_clock` convertido para ticks de 100ns desde 1601 |

O `xxd.exe` tem 42 testes unitários novos (`tests/test_win32.cpp` e
`tests/test_msvcrt.cpp`) cobrindo as conversões de code page (inclusive
surrogate pairs e erro `1113`), `VirtualQuery`/`VirtualProtect`, `TlsGetValue`,
critical sections, `__getmainargs`, stdio em `GuestFile`, `strtol`/`wcslen`,
locale e sinais.

Observações que orientam a próxima etapa (Fase 9/10):

- Todos os alvos compartilham o núcleo de CRT do mingw-w64: `__getmainargs`,
  `__iob_func`, `__initenv`, `_fmode`, `_errno`, `_commode`, `_initterm`,
  `_amsg_exit`, `_lock`/`_unlock`, `malloc`/`free`/`calloc`, `exit`/`atexit`/
  `abort`, `fopen`/`fclose`/`fread`/`fwrite`/`fprintf`/`vfprintf`/`fseek` e o
  grupo de strings (`strlen`/`strcmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/
  `memcpy`/`memmove`/`memset`).
- `bzip2.exe` tem todos os imports de `msvcrt.dll` implementados e sua
  regressão e2e cobre compressão e descompressão byte-idênticas.
- `dos2unix`/`unix2dos` agora exercitam o caminho `W` (`GetCommandLineW`,
  `FindFirstFileW`/`FindNextFileW`/`FindClose`, `GetFileAttributesW`,
  `_wfopen`, `wcs*`) e `SHELL32.dll!CommandLineToArgvW`; os fluxos validados
  usam somente caminhos relativos, UTF-8 e o curinga `*`.
- Nenhum alvo usa `GetStartupInfoA`/`GetEnvironmentStringsA` diretamente: o
  `crt2.o` do mingw delega a linha de comando e o ambiente ao `__getmainargs`
  de `msvcrt.dll`, então essas APIs são dependência interna do CRT mínimo, e
  não do aplicativo.

## Sistema de arquivos (Fase 10)

O subsistema de arquivos expande o `CreateFileA`/`ReadFile`/`WriteFile`/
`CloseHandle` da Fase 5 com APIs de manipulação de diretórios, atributos e
enumeração. A tradução de caminhos Windows (`\\` → `/`) é reutilizável via
`translate_windows_path()`; `CreateFileA` cobre caminhos relativos e
`C:\\...` dentro do prefixo ativo, mas rejeita caminhos absolutos Linux,
enquanto o CRT aceita esses caminhos para os aplicativos que recebem arquivos
do host como argumentos.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `GetFileSize` | Suportado | Retorna tamanho do arquivo aberto via `FileSlot.file_size`; suporta ponteiro `high_size` para arquivos > 4 GiB |
| `KERNEL32.dll` | `SetFilePointer` | Suportado | Seek por `FILE_BEGIN`/`FILE_CURRENT`/`FILE_END`; suporta ponteiro `high_distance`; atualiza `FileSlot.position` |
| `KERNEL32.dll` | `GetFileAttributesA` | Suportado | `stat()` + bits `FILE_ATTRIBUTE_DIRECTORY`/`FILE_ATTRIBUTE_ARCHIVE`/`FILE_ATTRIBUTE_READONLY` |
| `KERNEL32.dll` | `DeleteFileA` | Suportado | `unlink()` com mapeamento de erros |
| `KERNEL32.dll` | `MoveFileA` / `MoveFileExA` | Suportado no subconjunto | `rename()` com mapeamento de erros; `MoveFileExA` aceita `MOVEFILE_REPLACE_EXISTING` e rejeita flags não implementadas |
| `KERNEL32.dll` | `CreateDirectoryA` | Suportado | `mkdir()` com permissão 0777 |
| `KERNEL32.dll` | `FindFirstFileA` | Suportado | Abre `opendir()` + `readdir()` e preenche atributos, tamanho e tempos |
| `KERNEL32.dll` | `FindNextFileA` | Suportado | Continua iteração com o mesmo padrão |
| `KERNEL32.dll` | `FindFirstFileW` | Suportado | Converte UTF-16 para UTF-8 e compartilha a enumeração com a variante A |
| `KERNEL32.dll` | `FindFirstFileExW` | Suportado no subconjunto | Aceita `FindExInfoStandard`/`Basic`, `FindExSearchNameMatch`, filtro nulo e `FIND_FIRST_EX_LARGE_FETCH` como hint; usa os mesmos handles de `FindNextFileW`/`FindClose` |
| `KERNEL32.dll` | `FindNextFileW` | Suportado | Continua enumeração wide e converte o nome encontrado para UTF-16 |
| `KERNEL32.dll` | `FindClose` | Suportado | Fecha `DIR*` e libera slot |
| `KERNEL32.dll` | `GetFileAttributesW` | Suportado | Converte o caminho UTF-16 e delega ao mesmo `stat()` da variante A |
| `KERNEL32.dll` | `SetFileAttributesW` | Suportado no subconjunto | `READONLY` altera bits de escrita Linux; `NORMAL`, `ARCHIVE` e `DIRECTORY` são validados contra o tipo; atributos sem representação retornam `ERROR_INVALID_PARAMETER` |
| `KERNEL32.dll` | `SetFileInformationByHandle` | Suportado no subconjunto | `FileBasicInfo` aplica tempos de acesso/escrita e atributos; `FileDispositionInfo` marca exclusão no fechamento; `FileDispositionInfoEx` cobre `DELETE`, `POSIX_SEMANTICS`, `ON_CLOSE` e `IGNORE_READONLY_ATTRIBUTE` |
| `KERNEL32.dll` | `GetCurrentDirectoryA/W` | Suportado | Retorna o diretório de execução convertido para caminho Windows lógico: `C:\\...` dentro do prefixo, `Z:\\...` para arquivo/diretório externo |
| `KERNEL32.dll` | `GetModuleFileNameA/W` | Suportado | Retorna o módulo definido via `set_guest_module_path()` como caminho Windows lógico: aplicação instalada no prefixo usa `C:\\...`; setup externo usa `Z:\\...` |
| `KERNEL32.dll` | `GetFullPathNameW` | Suportado | Normalização Windows completa (Wine `dlls/kernel32/path.c`): resolve relativo via `GetCurrentDirectory`, colapsa `.`/`..`, trata `C:`, `\` e `\\` (UNC); `file_part` aponta para após último `\`/`:` |
| `KERNEL32.dll` | `GetFullPathNameA` | Suportado | Conversão `A` → `W` com mesma normalização; buffer insuficiente retorna `tamanho+1` e `ERROR_INSUFFICIENT_BUFFER` |
| `SHELL32.dll` | `CommandLineToArgvW` | Suportado | Divide a linha de comando UTF-16 em argumentos, preservando grupos entre aspas; o bloco único retornado é liberado por `LocalFree` |
| `SHELL32.dll` | `SHGetKnownFolderPath` | Suportado | Mapeia `FOLDERID_RoamingAppData`→`XDG_CONFIG_HOME`/`$HOME/.config`, `LocalAppData`→`XDG_DATA_HOME`/`$HOME/.local/share`, `ProgramData`→`/tmp/ProgramData`, `Desktop`/`Documents`/`Downloads`→`$HOME/...`; aloca via `CoTaskMemAlloc` (`malloc`), `ensure_directory_exists` |
| `SHELL32.dll` | `SHGetFolderPathW` | Suportado | `CSIDL_APPDATA`/`LOCAL_APPDATA`/`COMMON_APPDATA`/`DESKTOP`/`PERSONAL`→`$HOME/...`; copia para `pszPath[260]` |
| `SHELL32.dll` | `SHGetFolderPathAndSubDirW` | Suportado | Base `CSIDL` + `pszSubDir` (`\`→`/`) → `base/sub`; garante diretório |
| `SHELL32.dll` | `ShellExecuteW` / `ShellExecuteExW` | Suportado | `ShellExecuteW` valida `lpFile` wide e retorna `42` (>32); `ShellExecuteExW` valida `cbSize>=60` e preenche `hProcess` dummy, retorna `1` |
| `GDI32.dll` | `CreateFontW` | Suportado | Wrapper `wide_to_utf8` → `CreateFontA`; valida `face_name` wide, token estático |
| `GDI32.dll` | `SetDCBrushColor` / `SetDCPenColor` | Suportado | Stub retorna `0`, `ERROR_SUCCESS` |
| `gdiplus.dll` | `GdiplusStartup` / `GdiplusShutdown` / `GdipAlloc` / `GdipFree` / `GdipCreateBitmapFromStream` / `GdipCloneImage` / `GdipDisposeImage` / `GdipCreateHBITMAPFromBitmap` | Suportado | `GdiplusStartup` aloca token `0x1`, `GdipAlloc` `malloc`, `GdipFree` `free`, `GdipCreateBitmapFromStream`/`Clone`/`HBITMAP` retornam dummy `0` |
| `UxTheme.dll` | `SetWindowTheme` | Suportado | Valida `hwnd` e wstrings, retorna `S_OK` (0) |
| `WINMM.dll` | `timeSetEvent` | Suportado | Stub retorna `1` |
| `dbghelp.dll` | `SymFromAddr` | Suportado | Valida `process`/`displacement`/`symbol`, `displacement=0`, retorna `0` (não encontrado) mas sem crash |
| `POWRPROF.dll` | `PowerGetActiveScheme` / `PowerSetActiveScheme` / `CallNtPowerInformation` | Suportado | `PowerGetActiveScheme` aloca GUID `Balanced` via `malloc`, `PowerSetActiveScheme` `S_OK`, `CallNtPowerInformation` `memset` `0` |
| `IPHLPAPI.DLL` | `GetAdaptersInfo` / `GetAdaptersAddresses` / `if_nametoindex` | Suportado no subconjunto | `getifaddrs` do host, contratos de buffer `ERROR_BUFFER_OVERFLOW`/`ERROR_NO_DATA`, registros x64 com strings UTF-16 de largura fixa, interfaces IPv4 e `if_nametoindex` real; IPv6 e campos DNS continuam fora |

### Stubs com contrato explícito

`ExportSupport::Stub` identifica uma resolução deliberadamente limitada; não
significa que a API esteja implementada nem que o aplicativo tenha suporte de
fluxo principal. Os contratos abaixo são protegidos por
`Win32StubTest.*` e pelas suítes de cobertura dos aplicativos:

| Família | Contrato auditado |
|---|---|
| `KERNEL32.dll` / `WINSPOOL.DRV` / `WTSAPI32.dll` | Operações remotas, impressão e consulta detalhada de sessão retornam falha controlada, zeram saídas válidas e definem `ERROR_NOT_SUPPORTED`; os eventos incluem símbolo, mecanismo `stub` e detalhe. |
| `USER32.dll` | Operações de mutação/execução de menu ainda não implementadas retornam falha e `ERROR_NOT_SUPPORTED`; `LoadMenuW`/`GetMenuItemInfoW` cobrem apenas o modelo MENUEX v1 limitado descrito acima. |
| `SensApi.dll` | `IsDestinationReachableW` e `IsNetworkAlive` mantêm o contrato conservador usado pelos alvos atuais; flags válidas são preenchidas com `NETWORK_ALIVE_LAN`. |
| `SETUPAPI.dll` / `CFGMGR32.dll` | O enumerador usa handle sentinela, reporta coleção vazia com `ERROR_NO_MORE_FILES` e zera buffers/contadores de saída. |
| `D3D*.dll` / `DXGI.dll` / `DDRAW.dll` | Fábricas e compiladores não criam objetos: retornam HRESULT de falha/indisponibilidade e limpam ponteiros de saída válidos. Não há suporte DirectX, GPU ou jogos. |
| `WINMM.dll` | `PlaySoundA/W` e `timeSetEvent` mantêm os retornos de compatibilidade históricos; callbacks multimídia não são agendados e `timeKillEvent` não mantém estado de timer. |

### Limitações conhecidas

- `--cpu <segundos>` usa `RLIMIT_CPU` e mede tempo de CPU, não tempo de parede;
  `--memory <MiB>` usa `RLIMIT_AS` e limita o espaço de endereçamento virtual do
  processo. Ambos aceitam `0` como sem limite. Os limites são instalados no
  filho isolado, herdados por processos criados via `CreateProcessA/W` e não
  constituem sandbox.

- `WIN32_FIND_DATAW` tem layout de 592 bytes; enumeração preenche atributos,
  tamanho e tempos. A variante A segue o mesmo estado.
- Enumeração cobre `*`, `?`, `*.*` e correspondência exata case-insensitive
  em ASCII; classes de caracteres, locale de arquivos e case-fold Unicode amplo
  ficam fora.
- `FindFirstFileExW` rejeita níveis, operações, filtros e flags fora do
  subconjunto publicado. `SetFileAttributesW` não representa `HIDDEN`,
  `SYSTEM`, `COMPRESSED`, ADS ou atributos de nuvem; `SetFileInformationByHandle`
  não cobre rename, EOF, allocation, links nem outras classes.
- `CommandLineToArgvW` cobre aspas e separação por espaço usadas pelos alvos;
  as regras completas de escape com barras invertidas antes de aspas ainda não
  fazem parte do subconjunto publicado.
- `CreateFileA` cobre caminhos relativos e caminhos `C:\\...` dentro do
  prefixo ativo; caminhos absolutos Linux continuam rejeitados.
- As APIs wide de arquivo cobrem o subconjunto exercitado por `tl_files_wide`
  e `tl_file_metadata`:
  `CreateFileW`, tamanho/posição, atributos, tempos, cópia/movimentação,
  diretórios e nomes finais; não inventam letras de drive nem aceitam caminhos
  absolutos Windows.
- `FileSlot` rastreia tamanho, posição e exclusão pendente; `ReadFile` e
  `WriteFile` atualizam a posição automaticamente.
- `GetFileAttributesA` para arquivos inexistentes retorna `0xFFFFFFFF` com
  `ERROR_FILE_NOT_FOUND`.
- `GetCurrentDirectoryA/W` reflete apenas o processo convidado isolado. Em
  instalações e entradas de catálogo, o diretório inicial está dentro de
  `drive_c`; a execução não altera o diretório do launcher.
- `GetModuleFileNameA/W` depende de `set_guest_module_path()` chamado antes da
  execução; o loader conserva o caminho Linux internamente, mas a API devolve
  apenas a representação lógica `C:\\...` ou `Z:\\...`.
- `MultiByteToWideChar` e `WideCharToMultiByte` suportam CP_UTF8 (65001) para
  conversão UTF-8/UTF-16; surrogates pair são suportados.
- 15 testes unitários novos em `tests/test_win32.cpp` cobrem `GetFileSize`,
  `SetFilePointer` (seek beginning/end/negative), `GetFileAttributesA`
  (file/directory/nonexistent), `DeleteFileA` (existente/inexistente),
  `MoveFileA` (existente/inexistente), `CreateDirectoryA` (novo/duplicado),
  `FindFirstFileA`/`FindClose`, `GetCurrentDirectoryA/W`,
  `GetModuleFileNameA/W` e conversão UTF-8/UTF-16 com caracteres acentuados.
- Os fluxos dos alvos reais são cobertos por `targetapp_dos2unix_eol`,
  `targetapp_unix2dos_eol` e `targetapp_dos2unix_unicode-glob`; os arquivos de
  entrada CRLF/LF vêm da fonte pinada do dos2unix e o ouro UTF-8 está em
  `tests/targets/golden/dos2unix/`.

## Recursos PE, processos e rede

O loader expõe a faixa do diretório de recursos da imagem corrente somente após
mapear headers/seções. `FindResourceW` percorre diretórios com contagem e
offsets validados; `LoadResource`/`LockResource` devolvem uma visão somente
leitura e `SizeofResource` nunca ultrapassa a imagem. O fixture
`tl_resources.exe` protege esse contrato e o `--report` continua sem mapear ou
executar o entry point.

Handles de eventos, mutexes, semáforos, processos e arquivos são tokens opacos
validados pelo runtime. `WaitForMultipleObjects` aceita até 64 handles e
retorna timeout/índice conforme o subconjunto testado. `CreateProcessW` só
aceita PE32+ x86-64 com caminho relativo; o filho passa por `parse_pe`,
relocations, imports e isolamento antes do entry point. A implementação atual
usa um pipe de resultado, suporta `GetExitCodeProcess` e `TerminateProcess` e
não executa um programa Windows diretamente pelo Linux.
As tabelas internas ainda são separadas por família de recurso; a validação do
tipo ocorre pelo espaço de tokens e a unificação em uma tabela única continua
pendente.

`WS2_32.dll` é um módulo separado. O contrato inicial aceita AF_INET, TCP/UDP,
`getaddrinfo` para `localhost`/loopback, conversões de ordem de bytes e
`WSAPoll`. A fixture nunca acessa Internet; no sandbox sem permissão de socket,
o teste retorna um skip controlado, enquanto a validação com loopback permitido
passa de ponta a ponta.

`WININET.dll` é separado de `WS2_32.dll` e atende somente um cliente HTTPS
direto de loopback: `localhost`/`127.0.0.1`, `INTERNET_FLAG_SECURE`,
`GET`/`HEAD`/`POST`, sem proxy, credenciais, cookies, cache ou
redirecionamento. A CA é fornecida somente pelo host via
`TL_WININET_CA_FILE`, removida do ambiente visível ao convidado e usada em
um teste TLS local; não há loja de certificados, validação de cadeia Windows
nem WinTrust. O contrato completo está em
[`wininet.md`](arquitetura/wininet.md).

## Cadeia WinTrust explícita

`WINTRUST.dll` expõe `WinVerifyTrust` e os três `WTHelper*` no contrato de blob
descrito em [`wintrust.md`](arquitetura/wintrust.md). As fixtures `tl_trust.exe`
e `tl_wthelper.exe` usam dois certificados DER reais (folha e raiz): a primeira
verifica a cadeia e a segunda consulta/fecha o estado. Não há loja de
certificados do sistema, Authenticode, `WTD_CHOICE_FILE`, catálogo ou
revogação.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `WINTRUST.dll` | `WinVerifyTrust` | Suportado no subconjunto | Valida `WINTRUST_ACTION_GENERIC_VERIFY_V2` + `WTD_CHOICE_BLOB` com envelope `TLTC`, cadeia de dois DER, assinatura/validade X.509 e raiz explícita; HRESULT não nulo para política ou cadeia inválida |
| `WINTRUST.dll` | `WTHelperProvDataFromStateData`, `WTHelperGetProvSignerFromChain`, `WTHelperGetProvCertFromChain` | Suportado no subconjunto | Consulta o estado criado por `WTD_STATEACTION_VERIFY`, signer `0` e certificados folha/raiz `0..1`; rejeita contra-assinantes, índices inválidos e ponteiros externos; estado encerra em `CLOSE` |
| `CRYPT32.dll` | `CertGetNameStringW` | Suportado no subconjunto | Valida `CERT_CONTEXT`/estrutura DER e extrai `CERT_NAME_SIMPLE_DISPLAY_TYPE`, `CERT_NAME_FRIENDLY_DISPLAY_TYPE`, `CERT_NAME_DNS_TYPE`, `CERT_NAME_EMAIL_TYPE` ou `CERT_NAME_ATTR_TYPE`; sem verificação criptográfica, loja, SAN, Authenticode ou `Cert*` de cadeia |
| `CRYPT32.dll` | `CertOpenStore` / `CertCloseStore` | Suportado no subconjunto | Loja em memória e wrappers `CertOpenSystemStoreA/W` com nomes validados; provedores não implementados retornam `ERROR_NOT_SUPPORTED`, sem loja Windows ou leitura do trust store Linux |
| `WTSAPI32.dll` | `WTSEnumerateSessionsW` / `WTSFreeMemory` | Suportado no subconjunto | Retorna uma sessão local `Console` com alocação rastreada; `WTSFreeMemory` só libera blocos emitidos pelo runtime |
| `WTSAPI32.dll` | `WTSQuerySessionInformationW` | Stub controlado | Retorna `FALSE` + `ERROR_NOT_SUPPORTED`, zera os parâmetros de saída e emite trace |

## Registro genérico

`ADVAPI32.dll` não possui mais chave ou valor específicos do Todo. O subconjunto
de `RegCreateKeyEx[A/W]`, `RegOpenKeyEx[A/W]`, `RegSetValueEx[A/W]`,
`RegQueryValueEx[A/W]`, `RegDeleteValue[A/W]` e `RegCloseKey` usa chaves/valores
genéricos e persiste bytes, tipo e nomes UTF-8/UTF-16 em um arquivo por escopo
(`APPDATA`, ou `TL_REGISTRY_FILE` para testes). Hive real, COM e as demais
APIs `CRYPT32` continuam fora deste contrato.

## Segurança virtual por prefixo

`ADVAPI32.dll` expõe token não elevado do processo atual, SID virtual
persistente e DACLs para objetos existentes em `C:\` do prefixo. O owner/group
fixo é o SID artificial `S-1-5-21-<a>-<b>-<c>-1000`; um objeto sem metadado
recebe uma ACE allow `GENERIC_ALL` para ele. `GetNamedSecurityInfoW` retorna um
bloco liberável por `LocalFree`; `SetNamedSecurityInfoW` e `SetFileSecurityW`
persistem a DACL. Renomear preserva a DACL, excluir remove o metadado e copiar
restaura o padrão.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `ADVAPI32.dll` | `OpenProcessToken`, `GetTokenInformation` | Suportado no subconjunto | Só `GetCurrentProcess()` + `TOKEN_QUERY`; `TokenUser` usa protocolo de buffer e `TokenElevation` é `0` |
| `ADVAPI32.dll` | Operações de SID e `CheckTokenMembership` | Suportado no subconjunto | SID variável validado; World/Admin conhecidos; usuário virtual pertence apenas ao seu próprio SID |
| `ADVAPI32.dll` | `InitializeSecurityDescriptor`, `SetSecurityDescriptorDacl`, `SetEntriesInAclW` | Suportado no subconjunto | Descritor absoluto e ACE allow/deny; `GRANT`, `SET`, `DENY`, `REVOKE`; somente trustee SID |
| `ADVAPI32.dll` | `GetNamedSecurityInfoW`, `SetNamedSecurityInfoW`, `SetFileSecurityW` | Suportado no subconjunto | Arquivo existente em `C:\` do prefixo; owner/group imutáveis e DACL persistente |

SACL, auditoria, herança complexa, trustees por nome, certificados, privilégios,
elevação, `AccessCheck`, permissões POSIX e a identidade Linux não fazem parte
do contrato. As DACLs não bloqueiam `CreateFile` e não constituem sandbox. Ver
[seguranca-acl.md](arquitetura/seguranca-acl.md).

## COM mínimo e streams em memória (ole32)

`ole32.dll` expõe `CoInitialize`/`CoUninitialize`/`CoTaskMemAlloc` e camada COM mínima para testes de inicialização. `CoCreateInstance`/`CoGetClassObject` validam `rclsid`/`riid`/`ppv` via `mapped_guest_range` e retornam `REGDB_E_CLASSNOTREG` (`0x80040154`) ou `CLASS_E_NOAGGREGATION` (`0x80040110`); `OleInitialize`/`OleUninitialize` são stubs `S_OK`. `CreateStreamOnHGlobal` acrescenta um `IStream` volátil, com vtable Microsoft x64 explícita e somente backing store anônimo do runtime. O contrato detalhado está em [`ole-streams.md`](arquitetura/ole-streams.md).

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `ole32.dll` | `CoInitialize` / `CoInitializeEx` | Suportado | Retorna `S_OK` (0), ignora `reserved`/`coInit` |
| `ole32.dll` | `CoUninitialize` / `OleUninitialize` | Suportado | No-op |
| `ole32.dll` | `OleInitialize` | Suportado | Retorna `S_OK` |
| `ole32.dll` | `CoCreateInstance` / `CoGetClassObject` | Suportado | Valida `rclsid`/`riid`/`ppv`, `unkOuter==nullptr` senão `CLASS_E_NOAGGREGATION`, senão `REGDB_E_CLASSNOTREG`, `*ppv=nullptr` |
| `ole32.dll` | `CoTaskMemAlloc` / `CoTaskMemFree` / `CoTaskMemRealloc` | Suportado | `malloc`/`free`/`realloc` do host |
| `ole32.dll` | `CreateStreamOnHGlobal` | Suportado no subconjunto | Aceita somente `hGlobal=NULL`; cria `IStream` em memória. `Read`/`Write`/`Seek`/`SetSize`/`Stat`, `QueryInterface` e referência são cobertos por `tl_stream.exe`; cópia, clone e lock de região permanecem fora do contrato |

## Concorrência (Fase 11)

O subsistema de concorrência adiciona suporte a threads convidadas, TLS,
sincronização por mutexe e handles de thread. O runtime executa no mesmo
processo filho; cada thread convidada recebe seu próprio TEB/GS, stack e
`thread_local` isolado. Handles de thread são codificados por endereço
(`kThreadHandleBase + índice`).

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `CreateThread` | Suportado | Aloca stack com guard page, TEB, `arch_prctl(GS)`, cria `std::thread` com wrapper que preserva GS; retorna handle de thread |
| `KERNEL32.dll` | `ExitThread` | Suportado | `longjmp` para o `setjmp` do wrapper; thread termina sem encerrar o processo |
| `KERNEL32.dll` | `WaitForSingleObject` | Suportado | Thread, evento, mutex, semáforo, processo e arquivo síncrono; suporta `INFINITE` e timeout |
| `KERNEL32.dll` | `WaitForMultipleObjects` | Suportado | Até 64 handles válidos, espera any/all e retorno por índice; polling controlado para o subconjunto atual |
| `KERNEL32.dll` | `CreateEventA/W`, `SetEvent`, `ResetEvent` | Suportado | Eventos manuais/automáticos com `condition_variable` |
| `KERNEL32.dll` | `CreateMutexA/W`, `ReleaseMutex` | Suportado | Mutex recursivo e ownership pela thread convidada corrente |
| `KERNEL32.dll` | `CreateSemaphoreA/W`, `ReleaseSemaphore` | Suportado | Contagem inicial/máxima e consumo por espera |
| `KERNEL32.dll` | `CloseHandle` | Suportado | Fecha thread/processo/sincronização/arquivo e libera os recursos associados |
| `KERNEL32.dll` | `CreateProcessW` | Suportado no contrato limitado | Cria um filho PE32+ pelo mesmo loader e devolve processo assíncrono; sem drives, WOW64 ou execução nativa direta |
| `KERNEL32.dll` | `GetExitCodeProcess` / `TerminateProcess` | Suportado no contrato limitado | Consulta código e encerra filho isolado via sinal controlado |
| `KERNEL32.dll` | `GetCurrentThreadId` | Suportado | Retorna `thread_local` `g_guest_thread_id` atribuído por `execute_guest_entry` |
| `KERNEL32.dll` | `GetCurrentProcessId` | Suportado | Retorna PID real do processo via `getpid()` |
| `KERNEL32.dll` | `TlsAlloc` | Suportado | Aloca índice de slot `thread_local` (0–63); retorna `0xFFFFFFFF` na exaustão |
| `KERNEL32.dll` | `TlsSetValue` | Suportado | Armazena valor em `g_guest_tls_slots[index]`; rejeita índice inválido |
| `KERNEL32.dll` | `TlsFree` | Suportado | Libera índice para reuso |
| `KERNEL32.dll` | TLS estático do módulo principal | Suportado no subconjunto | Copia o template `IMAGE_TLS_DIRECTORY`, preserva o zero-fill e, para o slot pointer-backed de contrato `0x430`, aloca um bloco zerado de `0x1000` por TEB; a memória é liberada no encerramento da thread |
| `KERNEL32.dll` | `InitializeCriticalSection` | Suportado | Side-table com `pthread_mutex_t` (máximo 32 entradas) |
| `KERNEL32.dll` | `EnterCriticalSection` | Suportado | `pthread_mutex_lock` via side-table |
| `KERNEL32.dll` | `LeaveCriticalSection` | Suportado | `pthread_mutex_unlock` via side-table |
| `KERNEL32.dll` | `DeleteCriticalSection` | Suportado | `pthread_mutex_destroy` + libera entrada na side-table |
| `KERNEL32.dll` | `ConvertThreadToFiber` / `ConvertThreadToFiberEx` / `ConvertFiberToThread` | Suportado | `Convert*` retorna token `0x*` + `g_current_fiber_data`; `Ex` ignora `flags`; `ConvertFiberToThread` limpa `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateFiber` / `CreateFiberEx` / `SwitchToFiber` / `DeleteFiber` / `GetFiberData` | Suportado | Stub retorna token estático; `CreateFiberEx` ignora `stack_commit/reserve/flags`; `SwitchToFiber`/`DeleteFiber` no-op; `GetFiberData` retorna `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateToolhelp32Snapshot` | Suportado | Enumera `/proc` (`TH32CS_SNAPPROCESS` apenas); retorna `&SnapshotSlot` ou `INVALID_HANDLE_VALUE` (`-1`) + `ERROR_INVALID_PARAMETER`/`NOT_ENOUGH_MEMORY`; `CloseHandle` libera slot |
| `KERNEL32.dll` | `Process32FirstW` / `Process32NextW` | Suportado | Valida `hSnapshot` e `dwSize==568`, preenche `PROCESSENTRY32W` via `/proc/[pid]/status` (`PPid`, `Threads`, `Name`→`szExeFile` wide); `Next` avança `next_index`; fim → `0` + `ERROR_NO_MORE_FILES` (18) |
| `KERNEL32.dll` | `OpenProcess` | Suportado | Valida `/proc/[pid]` existe; retorna token `kProcessHandleBase+pid` ou `NULL` + `87`; `CloseHandle` aceita token via range |
| `KERNEL32.dll` | `OutputDebugStringA` / `OutputDebugStringW` | Suportado | Emite diagnóstico `runtime_trace` com a mensagem |
| `KERNEL32.dll` | `SetDllDirectoryW` | Suportado | Define diretório adicional de busca de DLLs no runtime |
| `KERNEL32.dll` | `VirtualQueryEx` | Suportado | Consulta mapeamento do processo via `tl_VirtualQuery` |
| `KERNEL32.dll` | `GetTimeZoneInformation` | Suportado | Retorna fuso horário padrão UTC / `TIME_ZONE_ID_STANDARD` |
| `KERNEL32.dll` | `GetProcessId` | Suportado | Retorna PID do processo convidado ou handle associado |
| `KERNEL32.dll` | `QueryFullProcessImageNameW` | Suportado | Preenche nome e caminho da imagem do processo convidado |
| `KERNEL32.dll` | `FileTimeToLocalFileTime` | Suportado | Converte estrutura de tempo de arquivo |
| `KERNEL32.dll` | `GetLongPathNameW` / `GetShortPathNameW` | Suportado | Converte caminhos entre formatos curto e longo |
| `KERNEL32.dll` | `SetThreadPriority` | Suportado | Retorna sucesso para ajuste de prioridade |
| `KERNEL32.dll` | `GetProcessAffinityMask` | Suportado | Retorna máscara de afinidade do processo e do sistema |
| `KERNEL32.dll` | `CreateHardLinkW` | Suportado | Criação de hard links entre arquivos via chamada `link(2)` |
| `KERNEL32.dll` | `K32GetModuleFileNameExW` | Suportado | Retorna caminho da imagem do módulo executável |
| `GDI32.dll` | `CreateBitmap` | Suportado | Cria e registra handle de bitmap em memória |
| `GDI32.dll` | `StretchBlt` | Suportado | Cópia e redimensionamento de blocos de imagem em DC |
| `GDI32.dll` | `GetObjectW` | Suportado | Consulta informações de dimensões de BITMAP ou LOGFONTW |
| `GDI32.dll` | `CreateDIBSection` | Suportado no subconjunto | Valida `BITMAPINFO`/dimensões, aloca bitmap DIB com ponteiro direto a pixels e limita a superfície a 256 MiB |
| `OLEAUT32.dll` | `SysAllocString` / `SysAllocStringLen` / `SysFreeString` / `SysStringLen` / `SysStringByteLen` | Suportado | Alocação, liberação e consulta de BSTR com cabeçalho de 4 bytes e terminação null |
| `OLEAUT32.dll` | `VariantInit` / `VariantClear` / `VariantCopy` / `VariantCopyInd` / `VariantChangeType` | Suportado | Gerenciamento e clonagem de estruturas VARIANT |
| `OLEAUT32.dll` | `SafeArrayCreate` / `SafeArrayDestroy` / `SafeArrayGetDim` / `SafeArrayAccessData` / etc. | Suportado | Suporte e gerenciamento de contêineres SafeArray multidimensionais |
| `KERNEL32.dll` | `GetTickCount` | Suportado | Retorna tempo de uptime do sistema em milissegundos via `CLOCK_MONOTONIC` |
| `KERNEL32.dll` | `SetCurrentDirectoryW` | Suportado | Altera diretório de trabalho do processo no Linux via `chdir` |
| `KERNEL32.dll` | `DeviceIoControl` | Suportado | Stub de controle de dispositivos e consultas de I/O de disco |
| `KERNEL32.dll` | `FoldStringW` | Suportado | Mapeamento e normalização de strings wide |
| `KERNEL32.dll` | `SetThreadExecutionState` | Suportado | Gerenciamento de energia e estado de suspensão de thread |
| `KERNEL32.dll` | `AllocConsole` / `AttachConsole` / `FreeConsole` | Suportado | Ciclo de vida e alocação de console Win32 |
| `KERNEL32.dll` | `SystemTimeToTzSpecificLocalTime` | Suportado | Conversão de estrutura `SYSTEMTIME` para fuso horário local |
| `KERNEL32.dll` | `IsDBCSLeadByte` | Suportado | Detecção de lead bytes para páginas de código multibyte |
| `KERNEL32.dll` | `GetNumberFormatW` | Suportado | Formatação numérica em buffers wide |
| `USER32.dll` | `SetUserObjectInformationW` | Suportado | Configuração de atributos em objetos de usuário |
| `USER32.dll` | `WaitForInputIdle` | Suportado | Sincronização de prontidão de entrada de processo |
| `USER32.dll` | `FindWindowExW` | Suportado | Busca hierárquica de janelas filhas |
| `USER32.dll` | `SetProcessDefaultLayout` | Suportado | Configuração de layout de renderização de janelas (LTR/RTL) |
| `ADVAPI32.dll` | `LookupPrivilegeValueW` | Suportado | Resolução de LUID para identificadores de privilégios de segurança |
| `ADVAPI32.dll` | `AdjustTokenPrivileges` | Suportado | Ajuste e concessão de privilégios em tokens de processo |
| `SHELL32.dll` | `SHGetFileInfoW` | Suportado | Consulta de atributos, extensões e ícones de arquivos do shell |
| `SHELL32.dll` | `SHGetPathFromIDListW` | Suportado | Conversão de lista de IDs de shell para caminho no sistema de arquivos |
| `SHELL32.dll` | `SHBrowseForFolderW` | Suportado | Diálogo de navegação e seleção de diretórios |
| `SHELL32.dll` | `SHGetMalloc` | Suportado | Obtenção do alocador de memória padrão do Shell |
| `SHELL32.dll` | `SHChangeNotify` | Suportado | Emissão e notificação de eventos do sistema de arquivos para o shell |
| `ole32.dll` | `CLSIDFromString` | Suportado | Conversão de strings de GUID/CLSID para estrutura binária `GUID` |
| `SHLWAPI.dll` | `SHAutoComplete` | Suportado | Retorna `S_OK` para autocompletar em caixas de texto |
| `SHLWAPI.dll` | `PathIsRelativeA` / `PathIsRelativeW` | Suportado | Identifica se um caminho é relativo ou absoluto |

### Limitações conhecidas

- O slot 0 de TLS (`TlsGetValue(0)`) é reservado para o ponteiro ao TEB
  (`NtTib.Self`); o convidado não deve chamar `TlsAlloc` para obter o TEB.
- A side-table de `CRITICAL_SECTION` suporta no máximo 32 seções simultâneas;
  exaustão emite trace de `side-table` com `category="exhaustion"`.
- Handles nomeados não são compartilhados entre processos; o nome é validado,
  mas a tabela é local ao processo host.
- `CreateThread` não suporta `CREATE_SUSPENDED`; `stack_size == 0` usa o
  tamanho padrão (1 MiB).
- `ExitThread` termina somente a thread corrente; não limpa destructors C++.
- O fim de vida de threads convidadas usa trampolim `setjmp`/`longjmp`
  (`thread_local`): `ExitThread` nunca atravessa `pthread_exit`; o `join` real
  acontece em `CloseHandle` (com guarda contra fechamento duplo), que também
  libera a pilha mapeada e invalida o cache de `/proc/self/maps`.
- O fixture `tl_thread.exe` requer mingw-w64 para cross-build; a regressão e2e
  está coberta por `fixture_tl_thread_metadata` e
  `runtime_tl_thread_matches_readobj`.
- O fixture `tl_tls_generic.exe` cobre um template TLS com byte inicializado,
  zero-fill, leitura do slot pointer-backed `0x430` e leitura zero-inicializada
  do bloco associado. O mecanismo continua sendo um subconjunto orientado por
  evidência, não uma implementação de TLS dinâmica universal. A validação
  Debug do unitário e dos quatro testes CTest passou em 2026-09-04.
- Testes unitários em `tests/test_win32.cpp` cobrem `TlsAlloc`,
  `TlsSetValue`, `TlsGetValue`, `TlsFree`, `GetCurrentThreadId`,
  `GetCurrentProcessId`, `CRITICAL_SECTION` (init/enter/leave/delete,
  null check, side-table exhaustion), `CloseHandle` (null/garbage),
  `WaitForSingleObject` (invalid handle, timeout), e `TlsSetGetValue`
  com múltiplos slots.

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
| 1 | `7z_x64.exe` | PE32+ x86-64 | 133/133 (100%) | `supported` | `ExitProcess 0` `7-Zip 24.08 banner` | `src/loader/module.cpp:400` `DosDateTimeToFileTime` já coberto |
| 2 | `7zFM_x64.exe` | PE32+ x86-64 | 298/298 (100%) | `execution-failed` (sem interação) | smoke externo `seven_zip_smoke` seleciona `input.txt`, aciona `Copy` (`idCommand=546`), verifica o arquivo copiado e encerra o runtime com exit `0` | janela X11 abre com shell visual experimental; menu de classe `RT_MENU`/MENUEX real (6 itens de nível superior), dropdowns aninhados e seleção básica por mouse/teclado de itens folha encaminham `WM_COMMAND`; lista imediata do diretório do executável é selecionável, recebe hover e permite navegação visual por pastas com Enter ou duplo clique; árvore lateral recebe hover, retorna à raiz e seleciona diretórios Linux conhecidos; barra `Address` permite navegar somente dentro da raiz visual; toolbar segue os `idCommand` reais, recebe hover, mostra pressão e cancela soltura fora do botão; `Copy` (`idCommand=546`) copia, de forma opt-in, um arquivo selecionado para destino existente dentro da raiz, sem sobrescrever | geometria inválida normalizada para `800x600`; limite da lista em 128 linhas; delay `MPR.dll 6/6`; demais operações ainda não concluídas |
| 3 | `7z.dll` | PE32+ DLL x86-64 | 86/86 (100%) | `imports-resolved` | `not-attempted` (DLL) | **Fase 13.A**: imports resolvidos; a DLL não foi executada como aplicação independente |
| 4 | `putty_x64.exe` | PE32+ x86-64 | 348/348 (100%) | `supported` | `ExitProcess 1` (sem args) | FLS 0/1 ok |
| 5 | `WinRAR_x64.exe` `winrar-x64-723.exe` | PE32+ x86-64 | 251/251 (100%) | `supported` | `ExitProcess 0` `sfxcmd` env | delay `GDI32/ADVAPI32/SHELL32/ole32` |
| 6 | `Rufus_x64.exe` | PE32+ x86-64 | 14/14 (100%) | `supported` | `ExitProcess 56832` | `UPX0` possui 3 seções marcadas `rwx`; o loader aplica W^X e mapeia a combinação como `RW`, sem página `RWX` |
| 7 | `HWiNFO64.exe` | PE32+ x86-64 | — | `malformed` (PE empacotado) | `not-attempted` | `UPX0` tem `SizeOfRawData=0`, enquanto o diretório de exports aponta para RVA sem bytes no arquivo; o desempacotamento permanece fora do escopo |
| 8 | `RobloxPlayerInstaller.exe` | PE32+ x86-64 | 430/430 (100%) | `execution-failed` | `RBXCRASH FatalRuntimeError Worker,28` `ExitProcess 3` (antes `SIGSEGV 0x68 rva 0x39ab exit 71`) | **Fase 13.D**: imports resolvidos, mas o fluxo ainda não conclui com sucesso; o slot TLS específico continua sendo benchmark, não suporte declarado |
| 9 | `Rockstar-Games-Launcher.exe` | PE32+ x86-64 | 338/338 (100%) | `execution-failed` | `ExitProcess 3` | imports resolvidos; fluxo principal ainda não validado como concluído |
| 10 | `Logitech_GHUB_x64.exe` `lghub_installer.exe` | PE32+ x86-64 | 114/114 (100%) | `supported` | `GuestTimeout 72` durante a inicialização | imports resolvidos; o fluxo do instalador não foi concluído e não é suporte funcional |
| 11 | `notepad++.exe` | PE32+ x86-64 | 584/584 (100%) | `execution-failed` | `GuestTimeout 72` (GUI `GetMessageW` bloqueado sem `Xvfb`) | precisa `Xvfb :99` `docs/arquitetura/gui-x11.md` |
| 12 | `RTSSHooks64.dll` | PE32+ DLL x86-64 | 256/256 (100%) | `imports-resolved` | `not-attempted` (DLL) | **Fase 13.RTSS**: imports resolvidos para análise; `CreateRemoteThread` e `WriteProcessMemory` agora falham com `ERROR_NOT_SUPPORTED` (sem fingir execução remota). O restante inclui `GDI32 ...`, `USER32 ...`, `KERNEL32 ...`, `SHLWAPI ...`, `WINMM ...`, `SETUPAPI 7` e `delay DirectX 11`; os stubs DirectX retornam `E_FAIL/S_OK` controlados |
| 13 | `Affinity x64.msix` | Zip/MSIX | — | `package-recognized` | `not-attempted` | `App/Affinity.exe` é `Mono/.Net entry 0x0 0 imports` — `.NET` fora de escopo `PROJETO.md:22`; o inspector lê central directory, manifesto armazenado/DEFLATE e metadados estruturais. O suporte B8 instala somente pacotes com PE32+ x86-64 nativo; este pacote continua sem instalação/execução |
| 14 | `*_x64_Installer.exe` `CapCut/Epic/Creative/Everything/RTSS.exe` | PE32 (x86) | — | `unsupported-architecture` `0x14c` `exit 5` | `parse-failed status="unsupported-architecture"` `src/pe/pe_reader.cpp:685` |

> Detalhe das novas APIs `B`: `GDI32.dll!Arc` `SHLWAPI.dll!PathIsUNCW/PathIsUNCA` `MSIMG32.dll!AlphaBlend/TransparentBlt` `NETAPI32.dll!NetApiBufferFree` `OLEACC.dll!LresultFromObject` `tdh.dll!TdhGetPropertySize` `WINSPOOL.DRV!OpenPrinterW/ClosePrinter` `WTSAPI32.dll!WTSFreeMemory` — todas registradas para resolver imports; `OpenPrinterW` falha com `ERROR_NOT_SUPPORTED` quando a operação é chamada.

## Aplicativos Windows Populares (histórico)

| Aplicativo | Arquitetura | Imports Resolvidos | Compatibilidade | Estado de Execução |
|---|---|---:|---|---|
| **7-Zip File Manager (`7zFM_x64.exe`)** | PE32+ x86-64 | 100% (298/298) | Fluxo principal restrito | Menu de classe MENUEX real, dropdowns aninhados, seleção básica por mouse/teclado de itens folha, toolbar orientada pelos `idCommand` reais com captura de pressão e hover, endereço editável restrito à raiz visual, navegação lateral interativa com hover, lista selecionável com hover e navegação visual por pastas via Enter ou duplo clique e status aparecem; `WM_COMMAND` básico pode ser encaminhado; o smoke externo versionado seleciona um arquivo e conclui `Copy` (`546`) dentro da raiz, sem sobrescrever; demais operações continuam limitadas |
| **7-Zip CLI (`7z_x64.exe`)** | PE32+ x86-64 | 100% (133/133) | Suportado | Executou e imprimiu o banner oficial completo do 7-Zip no terminal |
| **PuTTY SSH Client (`putty_x64.exe`)** | PE32+ x86-64 | 100% (348/348) | Suportado | Executou entry point, inicializou FLS (slots 0 e 1) e loop de eventos de interface e rede |
| **WinRAR (`WinRAR_x64.exe`)** | PE32+ x86-64 | 100% (251/251) | Suportado | Inicializou FLS, subsistema CRT e APIs do Shell/OLE com sucesso |
| **HWiNFO64 (`HWiNFO64.exe`)** | PE32+ x86-64 | — | Análise estrutural rejeitada | `UPX0` não tem dados crus para o RVA do diretório de exports; desempacotamento não é implementado |
| **Roblox Player Installer (`RobloxPlayerInstaller.exe`)** | PE32+ x86-64 | 100% (430/430) | Não suportado como fluxo concluído | Fase 13.D — `RBXCRASH` + `ExitProcess 3` (antes `SIGSEGV`); resolução de imports e correção TLS não equivalem a suporte |
| **Notepad++ (`notepad++.exe`)** | PE32+ x86-64 | 100% (584/584) | Não suportado como fluxo concluído | Histórico de resolução; execução registrada terminou em `GuestTimeout 72` sem Xvfb |
| **Rufus (`Rufus_x64.exe`)** | PE32+ x86-64 | 100% (14/14) | Inicia | UPX marca seções `rwx`; o loader mantém W^X, exec `ExitProcess 56832`; fluxo de uso não validado |
| **7-Zip Installer / Notepad++ Installer / Everything Search** | PE32 (x86) | — | Unsupported | Rejeitados controladamente como arquitetura x86 32-bit (0x14c) |
