# Fronteira de ABI x86-64

O TradutorLinux hospedará código PE32+ compilado para a ABI Microsoft x64 dentro de um processo Linux que usa a ABI System V AMD64. Essas ABIs não podem ser misturadas sem um adaptador explícito.

## Convenção Microsoft x64

| Item | Regra |
|---|---|
| Inteiros e ponteiros 1–4 | `RCX`, `RDX`, `R8`, `R9` |
| Pontos flutuantes 1–4 | `XMM0`–`XMM3` |
| Argumentos adicionais | Stack, depois de 32 bytes de shadow space reservados pelo chamador |
| Retorno inteiro/ponteiro | `RAX` |
| Retorno de ponto flutuante | `XMM0` |
| Voláteis | `RAX`, `RCX`, `RDX`, `R8`–`R11`, `XMM0`–`XMM5` |
| Não voláteis | `RBX`, `RBP`, `RDI`, `RSI`, `R12`–`R15`, `XMM6`–`XMM15` |

Antes de uma instrução `call`, `RSP` deve estar alinhado a 16 bytes. A instrução de chamada empilha o retorno, então a função chamada observa `RSP mod 16 = 8` na entrada. O shadow space de 32 bytes é obrigatório mesmo quando a função não recebe quatro argumentos.

## Convenção System V AMD64

| Item | Regra |
|---|---|
| Inteiros e ponteiros 1–6 | `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9` |
| Pontos flutuantes 1–8 | `XMM0`–`XMM7` |
| Argumentos adicionais | Stack |
| Retorno inteiro/ponteiro | `RAX` |
| Retorno de ponto flutuante | `XMM0` |
| Não voláteis | `RBX`, `RBP`, `R12`–`R15` |
| Red zone | 128 bytes abaixo de `RSP`; não existe na ABI Microsoft x64 |

O alinhamento de stack antes de `call` também é de 16 bytes. Registros XMM não são preservados pelo chamado nessa ABI.

## Regras obrigatórias para trampolins futuros

- Toda entrada chamada por código Windows terá convenção Microsoft x64 explícita.
- Toda chamada do runtime ao Linux obedecerá System V AMD64 explícita.
- Um trampolim deve preservar todos os registros não voláteis exigidos pela ABI de origem.
- O trampolim deve reservar/remover shadow space conforme a ABI Microsoft antes de chamar código Windows.
- Nenhuma exceção C++ pode atravessar uma fronteira de ABI. Interfaces de trampolim serão `extern "C"` e `noexcept`; erros serão convertidos em resultado explícito e trace.
- A primeira implementação deve preferir atributos `ms_abi`/`sysv_abi` de GCC ou Clang. Assembly entra apenas quando uma exigência não puder ser expressa e testada pelo compilador.

## Fronteira implementada (`ms_abi`)

A primeira fronteira concreta usa o atributo `ms_abi` de GCC/Clang: as funções hospedeiras de `KERNEL32.dll` são declaradas `TL_MSABI` (`__attribute__((ms_abi))`), `extern "C"` e `noexcept` em `include/tradutorlinux/runtime/winapi.hpp`. O compilador gera a transição System V → Microsoft x64 na chamada, incluindo shadow space e alinhamento de stack.

Contrato vigente:

- Tipos mínimos Win32 (`Handle`, `Bool`, `Dword`, `Uint` e as constantes de handle padrão) ficam em `tradutorlinux::abi` no mesmo cabeçalho.
- Nenhuma exceção C++ atravessa a fronteira: as APIs exportadas são `noexcept` e convertem falhas em retorno, `GetLastError` e trace.
- As APIs de console, arquivos, memória e GUI são exercitadas por fixtures PE e testes de integração.
- As chamadas host→guest de `WNDPROC` também usam ponteiros tipados `ms_abi` e são cobertas por testes de layout e execução.

A lista de módulos, exports, ordinais internos e comportamentos suportados está em `docs/arquitetura/imports.md` e `docs/compatibilidade.md`.

## Subconjunto KERNEL32 para inicialização de instaladores

As APIs `InitializeCriticalSectionAndSpinCount`, `InitializeCriticalSectionEx`,
`AreFileApisANSI` e `FormatMessageA` usam a mesma fronteira Microsoft x64:
ponteiros e escalares entram em `RCX`, `RDX` e `R8`, e o retorno `BOOL`/`DWORD`
volta em `RAX`. A `CRITICAL_SECTION` é uma estrutura opaca para o convidado;
o runtime valida somente o endereço e mantém a exclusão em uma tabela lateral.

O spin count é aceito como hint e não altera a implementação single-thread. A
variante `Ex` aceita `dwFlags == 0` ou `CRITICAL_SECTION_NO_DEBUG_INFO`
(`0x01000000`); outras flags retornam `ERROR_INVALID_PARAMETER`. `AreFileApisANSI`
retorna `TRUE` para o ACP determinístico `1252`. `FormatMessageA/W` cobre as
mensagens de sistema fixas do runtime, buffer fornecido e
`FORMAT_MESSAGE_ALLOCATE_BUFFER`; inserts, tabelas externas e recursos de
mensagem permanecem fora do contrato. A fixture `tl_k32_gap.exe` cobre os
retornos, buffers e falhas de argumento.

`GlobalAlloc`/`GlobalLock`/`GlobalUnlock`/`GlobalFree` e
`LocalAlloc`/`LocalFree` usam `RCX`/`RDX` e retornam o handle ou ponteiro em
`RAX`. O subconjunto aceita `GMEM_MOVEABLE` e `GMEM_ZEROINIT`; os blocos são
`malloc`/`calloc` rastreados por uma tabela lateral, e o endereço do bloco é o
handle usado por `GlobalLock`. A contagem de locks é validada, ponteiros
arbitrários não são liberados e a fixture `tl_globalmem.exe` cobre memória
móvel, fixa, inicialização zero, unlock final e `LocalFree`.

`CRYPT32.dll!CertGetNameStringW` recebe `PCCERT_CONTEXT` em `RCX`, tipo em
`RDX`, flags em `R8`, parâmetro opcional em `R9`, buffer e capacidade na pilha
Microsoft x64. O layout convidado de `GuestCertContext` tem 40 bytes; somente
`X509_ASN_ENCODING` e um blob DER estruturalmente validado são aceitos. O subconjunto extrai
atributos de subject/issuer para os tipos simple, friendly, DNS, email e OID,
retornando a capacidade necessária quando o buffer é nulo ou zero. Loja de
certificados, SAN, Authenticode e cadeia permanecem fora do contrato; a fixture
`tl_crypt32.exe` cobre a consulta, o nome do emissor e buffer insuficiente.

## Captura de contexto para unwinding

`RtlCaptureContext` não pode ser expresso como uma chamada C++ comum: ela
precisa observar RIP/RSP e os registradores no ponto exato da chamada do
convidado. Por isso `src/runtime/unwind_capture.S` é uma entrada Microsoft x64
sem prólogo; ela recebe `CONTEXT*` em `RCX`, preserva a fotografia dos
registradores do chamador e grava RIP/RSP, flags, MXCSR e XMM0–XMM15 no layout
de `ContextAmd64`. Os offsets e o tamanho (1232 bytes) têm `static_assert` no
cabeçalho. O restante do núcleo de unwinding volta imediatamente ao C++
`noexcept`; nenhum mecanismo C++ de exceção atravessa essa fronteira. Ver
também [unwinding-x64.md](unwinding-x64.md).

`src/runtime/seh.S` aplica a mesma regra para `RaiseException` e `RtlUnwind`:
captura o frame Microsoft x64 do chamador antes de entrar em C++ e entrega a
fotografia ao despachante. O trampolim inverso restaura GPRs, XMM0–XMM15,
MXCSR, RSP, RIP e RAX a partir de `CONTEXT` e nunca retorna ao hospedeiro.

Callbacks FLS também são ponteiros Microsoft x64: o runtime aceita apenas um
endereço executável da imagem ativa, limpa o valor FLS antes da invocação e não
permite que uma exceção C++ atravesse a chamada. O contrato de ciclo de vida
fica em [ambiente-locale-fls.md](ambiente-locale-fls.md).

`EnumSystemLocalesW` segue a mesma regra: o callback `LocaleEnumProcW` só é
chamado quando aponta para memória executável da imagem PE ativa e recebe a
string UTF-16 estática `0409` na ABI Microsoft x64. Nesta fase a enumeração
possui um único item e não consulta o locale do Linux.

O contexto da Fase 13.8 usa o layout AMD64 de `STARTUPINFOW` com 104 bytes e
`SLIST_HEADER` com 16 bytes/alinhamento 16. As APIs de console recebem contagens
em unidades UTF-16 e convertem no limite hospedado; nenhum ponteiro para as
estruturas do Linux é exposto ao convidado.

A Fase 13.9 acrescenta `FILE_BASIC_INFO` (40 bytes),
`FILE_DISPOSITION_INFO` (1 byte) e `FILE_DISPOSITION_INFO_EX` (4 bytes). As
estruturas de enumeração `WIN32_FIND_DATAW` possuem 592 bytes; todos os
ponteiros e tamanhos dessas estruturas são validados antes do acesso.

## Identidade e descritores de segurança (Fase 13.10)

Os layouts de segurança publicados usam ponteiros de 64 bits e o alinhamento
Microsoft x64: `SID` começa com `GuestSidHeader` de 8 bytes e contém de 0 a 15
subautoridades de 32 bits; `SID_AND_ATTRIBUTES` e `TOKEN_USER` têm 16 bytes;
`TOKEN_ELEVATION`, 4; `SECURITY_DESCRIPTOR` absoluto, 40; `ACL`, 8;
`TRUSTEE_W`, 32; e `EXPLICIT_ACCESS_W`, 48 bytes. O descritor é absoluto: os
campos de owner, group, SACL e DACL são ponteiros, nunca offsets relativos.

`ACL` e `SID` têm tamanho variável. Antes de usar uma ACE, o runtime valida
o cabeçalho, o tamanho total, a quantidade de ACEs, cada `AceSize`, o tipo,
flags e o SID embutido. Antes de escrever `TOKEN_USER`, descritor ou saída de
ponteiro, valida a faixa completa da memória convidada. Os detalhes funcionais
e os limites de ACL ficam em [seguranca-acl.md](seguranca-acl.md).

## Diálogos padrão (Fase 13.11)

O recurso `RT_DIALOG` aceito por `DialogBoxParamW` usa somente o layout padrão:
`GuestDialogTemplate` e `GuestDialogItemTemplate` têm 18 bytes cada, com campos
de 32 bits seguidos por palavras/coordenadas de 16 bits. Cada item começa em
offset alinhado a DWORD; o parser copia os campos com validação de tamanho e
nunca desreferencia uma estrutura não alinhada. `DIALOGEX`, fontes, menus ou
classes customizados e classes de controle fora de `BUTTON`, `EDIT`, `STATIC` e
`COMBOBOX` são rejeitados de forma controlada.

`INITCOMMONCONTROLSEX` é validado como dois `DWORD` (8 bytes): `cbSize` precisa
ser 8 e `dwICC` deve conter apenas classes comuns no conjunto de 16 bits. A
validação não cria janelas X11 filhas e não torna os ordinais desconhecidos 410
e 413 de `COMCTL32` resolvíveis.

## WinTrust e cadeia de certificados (Fase 13.12)

`WinVerifyTrust` recebe `HWND` em `RCX`, a ação em `RDX` e `WINTRUST_DATA*`
em `R8`, todos pela fronteira `ms_abi`. O layout publicado em
`include/tradutorlinux/runtime/wintrust.hpp` mantém `GuestWintrustBlobInfo` em
40 bytes e `GuestWintrustData` em 88 bytes; os campos de ponteiro seguem o
alinhamento Microsoft x64 e são validados antes da leitura.

O contrato de confiança usa somente `WTD_CHOICE_BLOB` com a ação
`WINTRUST_ACTION_GENERIC_VERIFY_V2` e o envelope DER `TLTC` documentado em
[`wintrust.md`](wintrust.md). A saída é um HRESULT explícito; nenhuma exceção,
ponteiro OpenSSL ou estrutura Linux atravessa a fronteira do convidado.
`WTD_STATEACTION_VERIFY` devolve um estado opaco validado pelo runtime, e os
três `WTHelper*` atravessam somente os registros Microsoft x64 publicados no
header: um signer e dois certificados (`GuestWintrustProviderData` 224 bytes,
`GuestWintrustSigner` 64 bytes e `GuestWintrustProviderCert` 88 bytes). O
estado permanece válido até `WTD_STATEACTION_CLOSE`; índices externos ou
contra-assinantes retornam nulo.
