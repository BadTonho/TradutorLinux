# TradutorLinux

Runtime de compatibilidade Win32 para Linux. O projeto executa, de forma gradual
e documentada, um subconjunto de executáveis PE32+ x86-64 de console e GUI
experimental no Linux x86-64.

O objetivo de longo prazo é ampliar esse subconjunto para classes cada vez mais abrangentes de aplicativos Windows, sempre com testes, limitações publicadas e regressões reproduzíveis.

O estado atual é a **Fase 13**. A subetapa **13.14 (TLS genérico e Worker RSL)**
foi concluída para as fixtures reutilizáveis; o caso comercial do Roblox
continua como benchmark com execução não concluída. O ciclo de reconciliação,
portfólio, limites de recursos, instalação MSIX/AppX, validação X11 e
expectativas de fixtures (`B1`, `B4`, `B5`, `B7`, `B8`, `B10` e `B16` no
`ROADMAP.md`) já tem entregas validadas. As próximas etapas são condicionadas:
B2 exige decisão de produto, B6 exige a amostra comercial Worker/RSL, B9 exige
evidência adicional de unwind e B11–B19 exigem alvo ou benefício medido. Parser,
mapeamento, imports, console, runtime básico, diagnóstico, relatório de
cobertura, instaladores em prefixos e GUI Win32 experimental já têm entregas
validadas. O suporte continua restrito às aplicações, APIs e limitações
publicadas na matriz de compatibilidade.

Consulte [PROJETO.md](PROJETO.md) para visão e arquitetura e [ROADMAP.md](ROADMAP.md) para os marcos.

Para testar visualmente as aplicações GUI em uma sessão X11 real, consulte o
[guia de teste visual](docs/guia-visual.md). O smoke test automático usa
`Xvfb` e valida comportamento sem abrir uma janela visível.

A tela principal pode ser aberta com:

```bash
./build/debug/src/tradutorlinux_gui
```

O pacote Debian pode ser gerado com `cpack --config build/debug/CPackConfig.cmake -G DEB`
e instalado com `sudo apt install ./tradutorlinux_0.0.0_amd64.deb`. Depois da instalação,
o launcher aparece no menu de aplicativos como **TradutorLinux**.

Ela permite informar um `.exe`, analisar imports, executar o convidado e
acompanhar o diagnóstico diretamente na janela.

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

`--report` lista os imports e o estado de resolução sem mapear nem executar o
entry point. Retorna `0` quando todas as dependências estáticas foram resolvidas
e `5` quando há uma limitação de resolução conhecida. Isso não prova que o
aplicativo executa um fluxo funcional; essa conclusão exige execução e teste
registrados em [docs/compatibilidade.md](docs/compatibilidade.md).

## Qualidade

O projeto usa C++20, CMake, Ninja, GoogleTest, CTest, clang-tidy, cppcheck e sanitizers. O GitHub Actions executa build, testes e análise estática para pushes e pull requests.
