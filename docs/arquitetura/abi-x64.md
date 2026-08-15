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

## Fronteira implementada na Fase 3 (stubs `ms_abi`)

A primeira fronteira concreta usa o atributo `ms_abi` de GCC/Clang: as funções hospedeiras de `KERNEL32.dll` são declaradas `TL_MSABI` (`__attribute__((ms_abi))`), `extern "C"` e `noexcept` em `include/tradutorlinux/runtime/winapi.hpp`. O compilador gera a transição System V → Microsoft x64 na chamada, incluindo shadow space e alinhamento de stack.

Contrato vigente:

- Tipos mínimos Win32 (`Handle`, `Bool`, `Dword`, `Uint` e as constantes de handle padrão) ficam em `tradutorlinux::abi` no mesmo cabeçalho.
- Nenhuma exceção C++ atravessa a fronteira: os stubs não alocam e não lançam.
- Todo stub registra um aviso `[tl][runtime][warning] stub` no trace com `dll`, `symbol` e `detail`, sinalizando que a semântica real chega na Fase 4.
- Os stubs são exercitados diretamente em `tests/test_abi.cpp` por ponteiros de função tipados `ms_abi`; a Fase 4 substitui o corpo deles pela semântica real sem mudar a fronteira.

A lista completa de stubs, ordinais internos e comportamento placeholder está em `docs/arquitetura/imports.md`.
