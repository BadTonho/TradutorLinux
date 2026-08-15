# TradutorLinux

Runtime educacional de compatibilidade Win32 para Linux. O projeto executa, de forma gradual e documentada, um subconjunto de executáveis PE32+ x86-64 de console e GUI experimental no Linux x86-64.

O estado atual é a **Fase 7**: as fases de parser, mapeamento, imports, console,
runtime básico, diagnóstico, relatório de cobertura e GUI Win32 experimental
estão concluídas. O suporte continua restrito às aplicações e limitações
publicadas na matriz de compatibilidade.

Consulte [PROJETO.md](PROJETO.md) para visão e arquitetura e [ROADMAP.md](ROADMAP.md) para os marcos.

Para testar visualmente as aplicações GUI em uma sessão X11 real, consulte o
[guia de teste visual](docs/guia-visual.md). O smoke test automático usa
`Xvfb` e valida comportamento sem abrir uma janela visível.

## Ambiente de desenvolvimento

O ambiente suportado inicialmente é Ubuntu 24.04 LTS x86-64, instalado nativamente ou em uma máquina virtual.

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git gdb llvm clang-tidy clang-format cppcheck gcc-mingw-w64-x86-64-win32
```

O pacote MinGW gera fixtures PE32+ para Windows; ele não é usado para compilar o runtime Linux.

## Build e testes

Na raiz do repositório:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Para validar memória indefinida e acessos inválidos durante os testes:

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

As fixtures são geradas em `build/<preset>/tests/samples/generated/`; não devem ser adicionadas ao Git.

## CLI atual

```text
tradutorlinux [--trace] [--report] <arquivo.exe>
tradutorlinux --help
tradutorlinux --version
```

`--trace` escreve diagnósticos somente em `stderr`. A saída padrão será reservada à futura saída do programa Windows. O contrato completo de trace e códigos de saída está em [docs/diagnostico.md](docs/diagnostico.md).

`--report` lista os imports e o estado de suporte sem mapear nem executar o
entry point. Retorna `0` quando todas as dependências pertencem ao subconjunto
suportado e `5` quando há uma limitação conhecida.

## Qualidade

O projeto usa C++20, CMake, Ninja, GoogleTest, CTest, clang-tidy, cppcheck e sanitizers. O GitHub Actions executa build, testes e análise estática para pushes e pull requests.
