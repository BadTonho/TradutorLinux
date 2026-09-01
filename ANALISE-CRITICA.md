# Análise completa do projeto TradutorLinux

Data: 2026-09-01

## Resumo executivo

O projeto está **muito saudável estruturalmente** (44 mil linhas, 417+ testes, 4 presets, arquitetura em camadas clara, roadmap detalhado). Encontrei **erros reais** concentrados em três áreas: (1) segurança do loader/memória, (2) bugs de comportamento em APIs "suportadas" e (3) falhas no backend gráfico X11/Wayland. Os achados foram verificados diretamente no código.

---

## 🔴 CRÍTICOS (corrupção de memória / segurança)

### C1. Seção exec+write vira memória RWX (viola a política documentada)
`src/loader/image_mapper.cpp:48-50` + `:63-74` + `:112-114`
O header `image_mapper.hpp:79` promete: *"A section requesting execute+write is downgraded to read-write"* e o AGENTS.md proíbe memória executável gravável. **O código não implementa o downgrade** — `section_permissions()` retorna `ReadWriteExecute` e `permissions_for_page()` também (`:112-114`), então `mprotect` aplica `PROT_READ|WRITE|EXEC` em qualquer seção marcada exec+write. Um PE hostil ou até linker real com `MEM_WRITE|MEM_EXECUTE` obtém página W+X, permitindo automodificação de código — exatamente o que a política do projeto proíbe.

### C2. Callbacks TLS invocados com endereços não-relocados
`src/pe/pe_reader.cpp:721` + `src/runtime/winapi.cpp:656`
`callback_vas` são lidos dos **bytes do arquivo** (VAs absolutos na base preferencial), passados crus e invocados diretamente como ponteiro de função. Quando a imagem é mapeada fora da base preferencial (`delta != 0`, comum pois `0x140000000` costuma estar ocupado), cada callback aponta para endereço errado → **crash**. O relocations aplica a correção na cópia em memória, mas a lista invocada não é ajustada pelo `delta`.

### C3. Backend X11: out-of-bounds read em `fill_rectangle`
`src/gui/x11.cpp:450`
```cpp
XSetForeground(dpy, fill.gc, fill.pixels[static_cast<std::size_t>(brush_index)]);
```
Só rejeita `brush_index == 5`. Valor negativo vira `size_t` gigante; valor >5 lê além do array de 6 elementos. O backend Wayland (`wayland.cpp:407`) valida corretamente; o X11 não.

### C4. Backend Wayland: overflow assinado em `fill()`
`src/gui/wayland.cpp:296`
```cpp
const int right = std::min(w.width, x + width), bottom = std::min(w.height, y + height);
```
`x + width` pode estourar `int` (UB), produzindo range inválida para `std::fill` → corrupção de heap.

### C5. Buffer de DIB fixo de 4096 bytes entregue ao convidado
`src/runtime/gdi32.cpp:360-373`
`tl_CreateDIBSection` devolve um `static char g_dib_buffer[4096]` **sem relação com o `BITMAPINFO` fornecido**. Um convidado pedindo um surface de 1024×1024×32bpp (4 MB) estoura o buffer estático do host.

### C6. Escritas do convidado sem validação de endereço
- `tl_GetSystemTimeAsFileTime` — `kernel32.cpp:1499-1506`: escreve `*file_time` sem `mapped_guest_range`.
- `tl_GetTextExtentPoint32W` — `gdi32.cpp:375-389`: `memcpy(size,...)` com só `!= nullptr`.

---

## 🟠 MAJORES (comportamento errado em caminho suportado)

### M1. `tl_MoveFileExA` é no-op silencioso
`kernel32.cpp:6069-6077` — retorna sucesso (1) e **não move nada**. Apps que fazem rename/temp (updaters, instaladores) perdem dados silenciosamente.

### M2. `tl_GetTempPathA` / `tl_GetSystemDirectoryA` retornam caminhos Windows literais
`kernel32.cpp:6061`, `:6184` — `"C:\\Temp\\"`, `"C:\\Windows\\System32"` sem tradução para o caminho Linux real (que não existem em `drive_c`). Apps que abrem o arquivo de temp falham com "não encontrado".

### M3. `tl_GetWindowTextA` sempre retorna 0 mesmo em sucesso
`user32.cpp:1036` — copia o texto mas retorna 0 (a variante W retorna o comprimento corretamente em `:1068`). Apps usam o retorno para saber quantos chars foram lidos → texto truncado.

### M4. Stubs que "mentem sucesso"
`winapi.cpp` — `tl_OpenPrinterW` devolve token `'PRNT'`, `tl_CreateRemoteThread` devolve `'RTND'`, `tl_WriteProcessMemory` retorna sucesso sem escrever, entre outros. Violam a regra do projeto ("falhe de forma controlada") e corrompem o estado do chamador downstream.

### M5. Estado modal de diálogo sem sincronização
`user32.cpp` — `g_modal_*` são globals lidas/escritas pelo loop sem lock, enquanto threads convidadas podem postar/entrar diálogos → data race.

### M6. Import IAT no cabeçalho DOS é validado mas nunca pode ser aplicado
`pe_reader.cpp:479-488` + `image_mapper.cpp:493` — IAT em `RVA < SizeOfHeaders` passa na validação mas `write_image_bytes` exige um `MapRegion` que cobre (headers não são região) → `InvalidAddress`. PE válido mas atípico falha de forma silenciosa.

### M7. Delay imports resolvidos agressivamente no startup
`import_resolver.cpp:101,107-115` — resolvem e vinculam todos os delay imports antes do entry point. Windows resolve sob demanda; aqui uma DLL delay-import ausente **impede o programa de iniciar** quando deveria iniciar de qualquer forma.

### M8. Cadeias de forwarder com apenas 1 salto
`module.cpp:75-102` — `find_export_forwarded` não segue forwarder→forwarder; pode retornar `NotImpl`/endereço 0 onde existe implementação.

### M9. MSIX parser: DoS de alocação e validação de campos ZIP
`src/package/msix.cpp:179` — `std::string(uncompressed_size, '\0')` com `uncompressed_size` até ~4 GB → `bad_alloc` não tratado. `:132-195` — `filename_len`/`compressed_size` sem checagem contra tamanho real do arquivo.

### M10. Path traversal em desktop entry
`src/catalog/app_catalog.cpp:370` — `app.id` não validado como componente de nome; um ID com `../` escreve fora do diretório de aplicativos.

### M11. `track_popup_menu` bloqueia para sempre em `XNextEvent`
`src/gui/x11.cpp:556-584` — sem timeout/`XPending`/Escape → deadlock do loop se o ponteiro sair do popup e não chegar mais evento.

---

## 🟡 MENORES
- `test_win32.cpp` com testes monolíticos de "cobertura por aplicativo" (200+ asserts) — falha única esconde qual API regrediu.
- Magic numbers sem comentário (`259U`, `0x40100`, `0xC002U`) em `test_win32.cpp:2620,2653,2754`.
- `maps_permissions_for` duplicado verbatim em 3 arquivos de teste.
- `version.cpp` listado 2× em `src/CMakeLists.txt:38,52` — **CMake deduplica** (só há 1 `.o`), então não quebra build, mas é código morto/confuso.
- `g_modal_*`/estado estático de GUI sem atomicidade (`x11.cpp:74-117` `ready` flag) — data race sob threads.
- `XAllocColor` sem `XFreeColors` → vazamento de colormap.
- Parser XML do MSIX ingênuo (substring, sem CDATA/comentários/entidades).
- `unescape_json_string` não decodifica `\uXXXX`.
- Testes de stubs testam "retorna algo" em vez de comportamento correto (`tl_GetMenu(nullptr)`).

---

## ❌ Correções a alegações dos subagentes (verifiquei)
- **Não** há falta de CI — existe `.github/workflows/ci.yml`.
- A duplicação de `version.cpp` **não** quebra o build (CMake deduplica) — é só confusão no arquivo.

---

## Prioridade recomendada

| Ordem | Correção | Impacto |
|---|---|---|
| 1 | **C1** — implementar o downgrade RWX→RW prometido | Segurança/isolamento |
| 2 | **C2** — relocar VAs de TLS callbacks por `delta` | Crash real com PEs comuns |
| 3 | **C5/C6** — validar buffers/sizes antes de escrever | Corrupção de memória host |
| 4 | **C3/C4** — bounds check em X11/Wayland | OOB read/write |
| 5 | **M1/M2/M3** — implementar MoveFileEx/TempPath/GetWindowTextA de verdade | Comportamento errado em apps suportados |
| 6 | **M4** — stubs devem falhar, não fingir sucesso | Corrupção downstream |
