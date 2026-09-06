# Fronteira FFI Rust↔C++

Este documento descreve as B20.1–B20.6 e a adoção seletiva do parser PE nas
R21.3–R21.5. A biblioteca Rust continua opt-in:
além do probe, a B20.3 usa Rust somente para a validação lexical dos caminhos
de perfis e materialização. A B20.4 endurece essa fronteira com testes e
tratamento de limites; a B20.5 integra o mesmo validador ao fluxo operacional
de `app run`; a B20.6 promove essa adoção seletiva após evidência reproduzível.
R21.3–R21.5 acrescentam a análise Rust do PE na execução direta de `--report` e
na imagem principal do `app run` nativo; nenhum loader, runtime Win32, DLL
dependente ou API pública de compatibilidade foi migrado para Rust.

## Build e escopo

O build C++ normal não depende de Rust. `TL_BUILD_RUST` é `OFF` por padrão. Ao
usar `-DTL_BUILD_RUST=ON`, o CMake exige `rustup` e executa o Cargo da
toolchain fixa em `rust-toolchain.toml` (`1.97.1`). O `TL_RUST_TOOLCHAIN` do
CMake mantém o mesmo valor como override explícito para diagnóstico e testes
negativos; os presets e o CI usam a versão fixada.

A biblioteca é uma `staticlib` Cargo sem crates externos. Com
`TL_BUILD_RUST=ON`, ela é ligada ao `tradutorlinux_core` e habilita os
adaptadores de validação lexical e de análise PE; o `tl_rust_ffi_probe` também
é construído. Com
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
ctest --preset debug-rust -R '^(rust_ffi_probe|rust_cargo_tests|rust_cargo_clippy|RustPathValidationTest\.)' \
  --output-on-failure

ctest --preset sanitize-rust -R \
  '^(rust_ffi_probe|rust_cargo_tests|rust_cargo_clippy|RustPathValidationTest\.)' \
  --output-on-failure

ctest --preset release-rust -R \
  '^(rust_ffi_probe|rust_cargo_tests|rust_cargo_clippy|RustPathValidationTest\.)' \
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

## Testes de robustez

A B20.4 usa testes property-based determinísticos sem crates externas. O
gerador possui uma semente fixa e produz entradas bounded, permitindo que uma
falha seja reproduzida localmente e no CI sem ferramenta adicional. Os testes
verificam invariantes de caminhos e o corpus C++ amplia a comparação direta
com `path_rules`, mantendo a equivalência obrigatória; NUL continua sendo a
única rejeição adicional intencional do validador Rust.

Os casos cobertos incluem UTF-8 truncado, overlong, surrogate e continuations;
UTF-16 com surrogate isolado ou pares invertidos; raízes, separadores e
traversal; limites exatos de 1 MiB; `u64::MAX`; overflow da contagem UTF-16;
ponteiros nulos; e todas as capacidades de buffer de erro ao redor de
`error_required`. O probe C++ usa regiões sentinela para confirmar que Rust
não escreve antes ou depois do buffer e que toda mensagem permanece terminada
em NUL quando há capacidade.

A criação do handle usa alocação fallible e converte falha em
`TL_RUST_STATUS_INTERNAL` sem publicar um ponteiro parcial. Um failpoint
thread-local existe somente nos testes Rust para reproduzir esse caminho; ele
não é uma API de produção nem simula uma sandbox. As funções de validação não
alocam durante a análise UTF-8, UTF-16 ou de caminhos. Um teste unitário também
força um panic interno e confirma a conversão para erro interno, sem unwind
atravessando a ABI.

Os testes de Cargo e Clippy são registrados no CTest somente quando
`TL_BUILD_RUST=ON`:

```bash
ctest --preset debug-rust -R '^(rust_cargo_tests|rust_cargo_clippy)$' \
  --output-on-failure

rustup run 1.97.1 cargo test --target-dir build/debug-rust/rust_ffi/cargo-target \
  --locked --offline
rustup run 1.97.1 cargo clippy --target-dir build/debug-rust/rust_ffi/cargo-target \
  --locked --offline --all-targets -- -D warnings
```

Os presets `sanitize-rust` executam o probe e os testes C++ com ASan/UBSan/LSan
conforme o ambiente do projeto. O Cargo continua sendo executado com
`--locked --offline`; a validação de memória do Rust usa os testes da própria
toolchain e a fronteira linkada aos probes sanitizados. Builds com
`TL_BUILD_RUST=OFF` não criam os testes Rust, não linkam a staticlib e não
exigem toolchain Rust.

## Integração seletiva do parser PE — R21.3–R21.5

O header público `include/tradutorlinux/ffi/rust_pe_parser.h` e o wire format
TLPE v1.0 continuam congelados. O adaptador C++ é proprietário dos buffers,
valida o resultado e converte-o para `PeInfo`; nenhum ponteiro, `String`, `Vec`,
exceção ou panic atravessa a ABI.

Com Rust habilitado, a ABI é usada em dois caminhos bem delimitados:

- `--report` direto, sem mapear nem executar;
- `app run` nativo sem `--report`, somente para a imagem principal antes do
  fluxo C++ de `prepare_process`.

Execução direta normal, `app run --report`, instalação, Proton e o parsing das
DLLs do `GuestModuleGraph` permanecem em C++. `TL_BUILD_RUST=OFF` não liga nem
referencia o adaptador. Uma falha Rust não faz fallback para C++: no `app run`
ela termina antes de mapear, resolver imports ou executar o entry point.

O CLI retorna `4` para `truncated`/`malformed`, `5` para arquitetura, formato
ou mecanismo não suportados e `70` para erro FFI, wire inválido, limites,
panic, status inesperado ou falha interna. Eventos PE do caminho Rust têm
`backend="rust"`; `parse-failed` também preserva `code`, `phase`,
`input-offset` e `detail-value`. O backend Proton pode emitir métricas Rust de
validação de perfil/arquivos, mas isso não significa que seu parser PE tenha
sido trocado.

### Política final de R21.5

A seleção é centralizada no runner e não depende de decisões duplicadas em
cada fluxo. Rust é o backend canônico da imagem principal apenas em
`--report` direto e em `app run` nativo sem `--report` e sem Proton, quando
`TL_BUILD_RUST=ON`. O adaptador retorna `PeInfo` ao fluxo C++ existente; não há
fallback silencioso para `parse_pe` C++ se a análise Rust falhar.

O parser C++ continua sendo produção nos caminhos excluídos — DLLs do
`GuestModuleGraph`, Proton, instalação, `app run --report`, execução direta
normal e builds `TL_BUILD_RUST=OFF` — e é oráculo diferencial somente nos
caminhos promovidos. O build OFF é a variante C++ explícita e padrão: não liga
a staticlib Rust e não contém os símbolos do parser Rust.

Os testes de promoção cobrem a matriz nativa completa, o corpus diferencial,
falhas antes de mapeamento/execução, trace `backend="rust"`, ausência desse
campo nos eventos C++, equivalência ON/OFF e contratos C/C++. O estado de
compatibilidade dos aplicativos não muda com a promoção.

## Integração operacional e promoção da B20.5/B20.6

No fluxo `app run`, o adaptador cria uma sessão RAII Rust por fase: uma durante
`load_profile` e outra durante `FileExposure::materialize` (ou sua variante
`materialize_into` no Proton). Cada sessão cria um único handle, reutiliza-o
para todos os caminhos da fase e destrói-o ao sair do escopo. Não há handle
global, cache compartilhado ou estado entre prefixos. A execução Proton usa a
mesma regra para a fase de materialização do prefixo Proton.

Os resultados distinguem três situações:

- entrada aceita: a validação lexical passou e o C++ continua as verificações
  físicas de existência, tipo, symlink, colisão e confinamento;
- entrada lexical inválida: o perfil ou a exposição é rejeitado e o fluxo
  nativo preserva o fallback genérico já existente;
- falha interna do adaptador: sessão não criada, status inesperado ou erro de
  infraestrutura; o runtime falha fechado, não executa o convidado e retorna
  `70` (`InternalError`).

As métricas vivem nos resultados C++ de carregamento e materialização, sem
alterar a ABI C. Elas registram backend, handles criados, verificações,
rejeições e duração acumulada em microssegundos medida com `steady_clock`.
Com Rust desligado, os resultados permanecem no caminho C++ e nenhum evento
Rust é produzido.

Quando `--trace` inclui `runtime`, uma sessão Rust com verificações produz um
evento por fase. Rejeições usam `status="invalid-input"`; falhas do adaptador
usam `status="internal-error"` e nível `error`:

```text
[tl][runtime][info] path-validation phase="profile" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
[tl][runtime][info] path-validation phase="files" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
```

No backend Proton, a sessão de arquivos usa o componente `proton` e conserva
o contexto `[tl][proton]`:

```text
[tl][proton][info] path-validation phase="files" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
```

O trace registra a falha contextualizada com fase, status e detalhe. O perfil
inválido não chega ao materializador; se a materialização nativa rejeitar um
mapeamento, nenhuma criação ou cópia parcial é promovida ao convidado. Em
timeout, o hospedeiro espera o filho ser encerrado e executa a limpeza RAII dos
arquivos expostos antes de retornar `72` (`GuestTimeout`); a fonte em
`compat/files/` permanece preservada.

O CTest `integration_rust_operational` executa o mesmo cenário no catálogo:
duas execuções de `tl_compat_file.exe`, um perfil lexicalmente inválido de
`tl_hello.exe`, um timeout de `tl_hang.exe` com arquivo auxiliar e o mock do
backend Proton com `tl_proton_probe.exe`. Ele verifica stdout, exit codes,
trace, isolamento, fonte preservada, destino removido, invisibilidade de
`compat/`, repetição sem vazamento de estado e ambiente Proton. O teste é
registrado também em `TL_BUILD_RUST=OFF`; nesse modo ele serve como baseline e
exige a ausência de `path-validation` Rust.

Para comparação local, o cenário completo levou aproximadamente `1,42 s` no
Debug com Rust e `1,41 s` no Debug sem Rust neste ambiente. Esses valores são
apenas baseline da máquina e do build atual, não são um limite de desempenho;
os campos `duration-us` devem ser acompanhados ao comparar builds equivalentes.
Uma medição futura deve repetir o mesmo teste, fixture, prefixo limpo e
configuração de otimização nos dois modos.

## Status da promoção B20.6

A B20.6 promove somente o uso opt-in do validador lexical Rust no fluxo real de
`app run`. O backend nativo C++ continua sendo o padrão quando
`TL_BUILD_RUST=OFF`, e falhas internas do adaptador continuam sendo tratadas
com falha fechada (`70`), sem aceitar uma entrada não validada. A promoção não
adiciona APIs Win32, não muda o schema de perfis e não declara suporte a
aplicativos reais.

O gate de promoção exige os três presets Rust, o baseline C++ sem Rust, Cargo e
Clippy offline, a integração operacional, regressões completas, equivalência
de comportamento e worktree limpo. Os testes de rejeição de
`tl_missing_dll.exe` preservam a distinção entre DLL ausente (`unknown-dll`) e
símbolo ausente em DLL conhecida (`unknown-symbol`).

## Evidência

`rust_ffi_probe` cobre criação/destruição, limites, UTF-8, UTF-16, argumentos
nulos, mensagens truncadas, tamanhos necessários, sentinelas de memória,
concorrência, caminhos relativos, destinos `C:` e rejeição de NUL. Os testes
de perfil e materializador protegem a integração opt-in. Na promoção B20.6,
Debug Rust e Release Rust passaram na suíte completa sem os cinco testes
opcionais de Proton real: 0 falhas entre 668 testes em cada perfil (quatro
testes ambientais foram `skipped`). O gate específico passou 7/7 em Debug,
Sanitize e Release. Cargo com
`--locked --offline` e Clippy com `-D warnings` passaram nos três perfis. O
baseline C++ Debug com `TL_BUILD_RUST=OFF` passou sem falhas entre 659 testes,
sem staticlib Rust e sem eventos `path-validation`; o piloto real opcional do
Proton passou 5/5 em Debug. A suíte completa Sanitize foi executada, mas mantém
dez falhas
históricas ou ambientais fora do gate B20 (ASan/UBSan em helpers/fixtures,
`RLIMIT_AS`, imagens sem relocations e Xvfb/LSan). Essas falhas não foram
introduzidas pela adoção seletiva e nenhuma falha nova apareceu no escopo
promovido.

O helper `write_le_u32` não utilizado foi removido de `image_mapper.cpp`, os
retornos de `write()` foram tratados e o salto cross-stack intencional passou a
usar `_longjmp` não fortificado; assim o Release voltou a linkar sem mudar o
mapper. A distinção `unknown-symbol`/`unknown-dll` também possui regressão no
grafo de módulos. Esta etapa não declara suporte a aplicativos reais nem
migração ampla da produção para Rust.
