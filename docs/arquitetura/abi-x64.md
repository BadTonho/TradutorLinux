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
