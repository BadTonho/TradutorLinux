# Fronteira FFI Rust↔C++

Este documento descreve a B20.1. A biblioteca Rust desta etapa existe somente
como probe opt-in; nenhum parser, loader, runtime Win32 ou API pública de
compatibilidade foi migrado para Rust.

## Build e escopo

O build C++ normal não depende de Rust. `TL_BUILD_RUST` é `OFF` por padrão. Ao
usar `-DTL_BUILD_RUST=ON`, o CMake exige `rustup` e executa o `rustc` de uma
toolchain fixa (`TL_RUST_TOOLCHAIN`, padrão `1.97.1`). A integração completa de
Cargo, lockfile, política de crates e CI pertence à B20.2.

A biblioteca é uma `staticlib` sem crates externos e só é ligada ao
`tl_rust_ffi_probe`. O probe não é instalado nem carregado pelo runtime.

## Ownership e ABI

O header C usa somente tipos de largura explícita, ponteiros e
`extern "C"`. `tl_rust_validator_t` é um handle opaco: o Rust aloca e libera o
objeto por `tl_rust_validator_create`/`tl_rust_validator_destroy`; o C++ não
pode inspecionar, copiar, liberar ou reter seus campos.

As entradas e o buffer de erro pertencem ao chamador. Rust lê exatamente o
tamanho informado e escreve no máximo a capacidade do buffer. Nenhum ponteiro
para memória Rust atravessa a fronteira. `error_required` é obrigatório e
contém o tamanho completo da mensagem incluindo o NUL final; capacidade menor
retorna `TL_RUST_STATUS_BUFFER_TOO_SMALL` e mantém a saída truncada terminada.

Um ponteiro de entrada nulo só é aceito com comprimento zero. Um buffer de
erro nulo só é aceito com capacidade zero; nesse caso uma mensagem não pode
ser devolvida e o status será `BUFFER_TOO_SMALL`. Ponteiros não nulos devem
apontar para a quantidade de bytes indicada pelo chamador; a API não é uma
sandbox para ponteiros inválidos.

## Codificação e status

`tl_rust_validator_validate_utf8` valida bytes UTF-8. A variante UTF-16 recebe
unidades `uint16_t`, valida pares surrogate e interpreta o comprimento em
unidades, não bytes. O handle impõe um limite em bytes; para UTF-16 o limite é
verificado após multiplicação segura por dois.

Os códigos são estáveis e não dependem de `errno` ou de detalhes Rust:

| Código | Significado |
|---:|---|
| `0` | Sucesso |
| `1` | Argumento ou ponteiro inválido |
| `2` | UTF-8 inválido |
| `3` | UTF-16 inválido |
| `4` | Buffer de erro insuficiente |
| `5` | Entrada acima do limite |
| `6` | Falha interna, incluindo panic capturado |

Todas as funções exportadas capturam panics antes de retornar. Nenhuma
exceção ou unwind Rust atravessa a ABI.

## Concorrência

Depois de criado, o handle contém somente configuração imutável. Chamadas de
validação concorrentes no mesmo handle são permitidas; cada chamada deve usar
seus próprios buffers de erro. Não existe estado global, cache mutável ou
serialização implícita. O probe valida chamadas concorrentes de leitura e
handles independentes.

## Evidência

`rust_ffi_probe` cobre criação/destruição, limites, UTF-8, UTF-16, argumentos
nulos, mensagens truncadas, tamanhos necessários, concorrência e destruição
nula. O teste roda somente quando `TL_BUILD_RUST=ON`; os builds padrão com
`TL_BUILD_RUST=OFF` continuam sem requisito Rust. A B20.1 será promovida após
o probe passar em Debug, Sanitize e Release. A capacidade permanece um
contrato experimental: a B20.3 ainda deverá escolher e migrar um componente
de produção por benefício mensurável.
