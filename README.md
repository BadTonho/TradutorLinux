# TradutorLinux

Runtime educacional de compatibilidade Win32 para Linux. O projeto executará, de forma gradual e documentada, um subconjunto de executáveis PE32+ x86-64 de console no Linux x86-64.

O estado atual é a **Fase 0**: a CLI, o build, os testes e as fixtures estão prontos, mas o parser e o loader de PE ainda não foram implementados. Por isso, ao receber um arquivo legível, o comando retorna uma mensagem de recurso não suportado.

Consulte [PROJETO.md](PROJETO.md) para visão e arquitetura e [ROADMAP.md](ROADMAP.md) para os marcos.

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
tradutorlinux [--trace] <arquivo.exe>
tradutorlinux --help
tradutorlinux --version
```

`--trace` escreve diagnósticos somente em `stderr`. A saída padrão será reservada à futura saída do programa Windows. O contrato completo de trace e códigos de saída está em [docs/diagnostico.md](docs/diagnostico.md).

## Qualidade

O projeto usa C++20, CMake, Ninja, GoogleTest, CTest, clang-tidy, cppcheck e sanitizers. O GitHub Actions executa build, testes e análise estática para pushes e pull requests.
