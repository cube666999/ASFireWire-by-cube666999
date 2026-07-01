# CodeGraph MCP — pełne instrukcje

> Przeniesione z `CLAUDE.md` (odchudzenie kontekstu, 2026-07-01). Skrócona wersja zakazu
> (grep/find/Bash) zostaje w `CLAUDE.md` na stałe — to jest rozwinięcie + instalacja.

## ⛔ BEZWZGLĘDNY ZAKAZ: grep / find / Bash do szukania kodu

**NIGDY nie używaj `grep`, `find`, ani Bash do eksploracji kodu.** To jest bezwzględna reguła bez wyjątków.

Jedyne dozwolone narzędzia do lokalizacji symboli i plików:
- `codegraph_search` — znajdź klasy, metody, pliki po nazwie
- `codegraph_context` — kontekst dla zadania (entry points + related symbols)
- `codegraph_callers` — kto wywołuje dany symbol
- `codegraph_callees` — co wywołuje dany symbol
- `codegraph_node` — pełny kod konkretnego węzła
- `codegraph_impact` — co zostanie dotknięte zmianą symbolu

`grep`/`find`/Bash są dopuszczalne **wyłącznie** gdy CodeGraph nie zwróci wyniku po 2 próbach, i tylko z wyraźną adnotacją `# fallback: CodeGraph nie znalazł`.

**ZAWSZE CodeGraph jako pierwszy krok.** Czytanie pliku bez wcześniejszego `codegraph_search` / `codegraph_context` = błąd procesu.

**WAŻNE — zawsze przekazuj `projectPath`:** MCP serwer jest skonfigurowany w `ASFireWire-dice/.mcp.json`, ale jego CWD może być inne. Bez explicit `projectPath` CodeGraph może zwrócić "not initialized". Każde wywołanie musi mieć:
```
projectPath: "/Users/cube666/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire-dice"
```

## Instalacja na świeżej maszynie

Index dice żyje w `.codegraph/`. Serwer skonfigurowany w `ASFireWire-dice/.mcp.json`
(`--path` wskazuje na dice). Komenda to `serve --mcp`, NIE `mcp`.

```bash
# Build/refresh indeksu dice:
export PATH="/opt/homebrew/bin:/opt/homebrew/opt/node@22/bin:$PATH"
cd "/Users/cube666/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire-dice"
NODE_OPTIONS="--max-old-space-size=4096" codegraph index .

# Zatwierdź MCP raz po instalacji: uruchom `claude` z katalogu dice →
#   "New MCP server found: codegraph" → opcja 2 (Use this and all future) →
#   /mcp powinien pokazać "codegraph · ✓ connected"
```

⚠️ **`.mcp.json` jest per-katalog:** uruchamiając `claude` z `ASFireWire-dice/` dostajesz indeks dice;
z `ASFireWire/` — indeks main. Każde wywołanie CodeGraph przekazuj `projectPath` (patrz zakaz wyżej).
