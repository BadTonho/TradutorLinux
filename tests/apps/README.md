# Testes específicos por aplicativo

Smokes, fixtures e cenários de interação de aplicativos reais ficam nesta
árvore. Eles exercitam o runtime geral, mas não fazem parte da implementação
de produção nem criam DLLs específicas.

Cada cenário deve declarar o aplicativo, a ação segura, o critério de término,
os limites externos e a evidência ON/OFF quando aplicável.

O `7zip/7z_cli_smoke.cpp` é executado somente quando recebe explicitamente o
runtime e o `7z_x64.exe` do corpus. O cenário cobre arquivos `stored`,
`DEFLATE`, `7z/LZMA2` normal e protegido por senha, sobrescrita controlada com
`-aoa`, rejeição de senha incorreta sem payload válido, caminhos relativos e o
filtro stdin/stdout (`-si`/`-so`).
O CMake registra o teste automaticamente quando `TL_POPULAR_APPS_DIR` aponta
para `Aplicativos_Windows_Populares/`.

O mesmo parâmetro registra `popular_apps_report_matrix`, que analisa os 26 PE
selecionados e o MSIX do corpus e verifica os exit codes esperados sem mapear
ou executar os arquivos. O caso é repetido no build Rust ON e no baseline C++
OFF.

`popular_apps_recursive_report_matrix` complementa essa seleção com todos os
64 PE/DLL/MSIX encontrados recursivamente no corpus atual, incluindo cópias
extraídas de 7-Zip e Notepad++. Ela continua sendo somente análise: DLLs,
pacotes e instaladores rejeitados nunca são iniciados, e cada exit esperado é
fixado no catálogo do teste para que uma mudança de formato ou arquitetura
falhe de forma visível.

`popular_apps_native_matrix` executa somente os seis casos PE32+ com ação direta
ou rejeição pré-entry já documentados: 7-Zip, os dois binários WinRAR, Rockstar,
Rufus e GUP do Notepad++. Cada caso usa
prefixo temporário, ambiente headless, `--timeout 3`, limites de CPU/memória e
verifica o exit code e o diagnóstico esperado; instaladores, DLLs e cenários
interativos de GUI permanecem nos testes específicos.

`popular_apps_install_matrix` cobre oito casos controlados: Roblox, G HUB, o
alias byte-a-byte, Affinity e as rejeições pré-extração de CPU-Z, GPU-Z,
HWMonitor e HWiNFO. Cada caso usa `HOME`, `XDG_CONFIG_HOME`, `APPDATA` e
prefixo temporários, verifica o estágio/erro esperado e exige ausência de
arquivos, extração e cadastro após a rejeição. Instaladores PE32/x86 permanecem
fora da execução funcional.

Com o mesmo parâmetro e Xvfb disponível, os smokes GUI reais de 7-Zip File
Manager, PuTTY, WinRAR SFX e Notepad++ também são registrados separadamente no
CTest. O smoke do 7-Zip prepara catálogo, prefixo e
`compat/profile.json` schema 4 temporários, cadastra o aplicativo com ID
`7zip` e executa somente por `app run`; o trace exige `compat-profile` carregado
e `compat-extension` selecionada. Cada cenário cria seu próprio Xvfb/staging e
mantém seu resultado limitado ao comportamento documentado na matriz de
compatibilidade.
