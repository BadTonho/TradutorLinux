# Fronteira FFI Rust↔C++

Este documento descreve as B20.1–B20.3. A biblioteca Rust continua opt-in:
além do probe, a B20.3 usa Rust somente para a validação lexical dos caminhos
de perfis e materialização. Nenhum parser, loader, runtime Win32 ou API pública
de compatibilidade foi migrado para Rust.

## Build e escopo

O build C++ normal não depende de Rust. `TL_BUILD_RUST` é `OFF` por padrão. Ao
usar `-DTL_BUILD_RUST=ON`, o CMake exige `rustup` e executa o Cargo da
toolchain fixa em `rust-toolchain.toml` (`1.97.1`). O `TL_RUST_TOOLCHAIN` do
CMake mantém o mesmo valor como override explícito para diagnóstico e testes
negativos; os presets e o CI usam a versão fixada.

A biblioteca é uma `staticlib` Cargo sem crates externos. Com
`TL_BUILD_RUST=ON`, ela é ligada ao `tradutorlinux_core` e habilita o adaptador
de validação lexical; o `tl_rust_ffi_probe` também é construído. Com
`TL_BUILD_RUST=OFF`, o runtime não referencia Rust e mantém o caminho C++.
Cargo recebe `--locked --offline`: o build não altera o lockfile nem consulta o
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

Para validar também o componente integrado, os presets Rust constroem
`tradutorlinux_unit_tests`; os testes `RustPathValidationTest.*` comparam o
resultado Rust com as regras lexicais C++ e verificam limites e rejeição de
NUL:

```bash
cmake --build --preset debug-rust --target tradutorlinux_unit_tests
ctest --preset debug-rust -R '^(rust_ffi_probe|RustPathValidationTest\.)' \
  --output-on-failure
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
| `7` | Caminho lexical inválido |

Todas as funções exportadas capturam panics antes de retornar. Nenhuma
exceção ou unwind Rust atravessa a ABI.

## Concorrência

Depois de criado, o handle contém somente configuração imutável. Chamadas de
validação concorrentes no mesmo handle são permitidas; cada chamada deve usar
seus próprios buffers de erro. Não existe estado global, cache mutável ou
serialização implícita. O probe valida chamadas concorrentes de leitura e
handles independentes.

## Validação lexical de caminhos

A B20.3 adiciona duas funções ao mesmo contrato FFI:
`tl_rust_validator_validate_relative_path` para fontes em `compat/files` e
`compat/dlls`, e `tl_rust_validator_validate_c_drive_path` para destinos
`C:\\...`. Ambas recebem bytes e aplicam o limite de 1 MiB do handle; não
exigem UTF-8, mas rejeitam NUL.

A validação relativa replica as regras C++ atuais: caminho não vazio, sem raiz
ou caminho absoluto, sem componentes `.`/`..` e sem barra invertida. A
validação de `C:` exige a unidade C, separador inicial, nome de arquivo e
impede que componentes `..` escapem da raiz. Os separadores `/` e `\\` são
aceitos conforme o contrato existente.

O Rust é uma pré-validação lexical sem acesso ao filesystem. O C++ continua
verificando existência, tipo regular, symlinks, colisões, permissões e
confinamento físico em `drive_c`. O Rust é chamado durante o carregamento do
perfil e novamente pelo materializador para proteger `Profile` construído
diretamente. Qualquer erro do Rust ou do adaptador falha fechado e impede
criação de diretórios ou cópia.

O teste diferencial usa um corpus fixo e exige equivalência com as regras C++
para os casos existentes; a rejeição adicional de NUL é intencional e coberta
separadamente. Não há estado global: os handles são locais à operação e podem
ser usados por chamadas concorrentes somente com buffers de erro próprios.

## Evidência

`rust_ffi_probe` cobre criação/destruição, limites, UTF-8, UTF-16, argumentos
nulos, mensagens truncadas, tamanhos necessários, concorrência, caminhos
relativos, destinos `C:` e rejeição de NUL. Os testes de perfil e materializador
protegem a integração opt-in. Todos esses testes rodam somente quando
`TL_BUILD_RUST=ON`; os builds padrão com `TL_BUILD_RUST=OFF` continuam sem
requisito Rust.
