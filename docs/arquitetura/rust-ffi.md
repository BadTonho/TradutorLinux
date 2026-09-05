# Fronteira FFI Rust↔C++

Este documento descreve a B20.1. A biblioteca Rust desta etapa existe somente
como probe opt-in; nenhum parser, loader, runtime Win32 ou API pública de
compatibilidade foi migrado para Rust.

## Build e escopo

O build C++ normal não depende de Rust. `TL_BUILD_RUST` é `OFF` por padrão. Ao
usar `-DTL_BUILD_RUST=ON`, o CMake exige `rustup` e executa o Cargo da
toolchain fixa em `rust-toolchain.toml` (`1.97.1`). O `TL_RUST_TOOLCHAIN` do
CMake mantém o mesmo valor como override explícito para diagnóstico e testes
negativos; os presets e o CI usam a versão fixada.

A biblioteca é uma `staticlib` Cargo sem crates externos e só é ligada ao
`tl_rust_ffi_probe`. O probe não é instalado nem carregado pelo runtime. Cargo
recebe `--locked --offline`: o build não altera o lockfile nem consulta o
registro de crates. Os artefatos e o `target/` Cargo ficam dentro do diretório
de build CMake e não são versionados.

Os presets opt-in são:

```bash
cmake --preset debug-rust
cmake --build --preset debug-rust --target tl_rust_ffi_probe
ctest --preset debug-rust -R '^rust_ffi_probe$' --output-on-failure

cmake --preset sanitize-rust
cmake --build --preset sanitize-rust --target tl_rust_ffi_probe
ctest --preset sanitize-rust -R '^rust_ffi_probe$' --output-on-failure

cmake --preset release-rust
cmake --build --preset release-rust --target tl_rust_ffi_probe
ctest --preset release-rust -R '^rust_ffi_probe$' --output-on-failure
```

O CI instala explicitamente `1.97.1` via rustup antes da matriz dos três
presets. Em uma máquina de desenvolvimento, a toolchain deve ser instalada
com `rustup toolchain install 1.97.1 --profile minimal`; o configure falha
com uma mensagem orientando essa instalação quando ela estiver ausente.

`Cargo.lock` é obrigatório e pertence ao repositório mesmo sem dependências,
para impedir que a entrada de crates altere o grafo silenciosamente. A B20.2
não adiciona crates externas. Uma etapa futura só poderá fazê-lo com versão e
fonte fixadas, lockfile revisado, justificativa técnica, revisão de licença e
segurança e validação offline no CI.

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
`TL_BUILD_RUST=OFF` continuam sem requisito Rust. A B20.2 protege o mesmo
probe nos presets Debug, Sanitize e Release por Cargo. A capacidade permanece
um contrato experimental: a B20.3 ainda deverá escolher e migrar um
componente de produção por benefício mensurável.
