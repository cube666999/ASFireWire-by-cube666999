# Wytyczne Behawioralne i Zasady Decyzyjne

> Przeniesione z `CLAUDE.md` (odchudzenie kontekstu, 2026-07-01). Czytaj przy: refaktorach,
> wieloetapowych zadaniach, konfliktach danych/hierarchii źródeł.

## Wytyczne Behawioralne
**Kompromis:** Niniejsze wytyczne stawiają ostrożność i precyzję ponad szybkość. Przy trywialnych zadaniach — kieruj się własnym osądem.

### 1. Pomyśl, zanim zaczniesz kodować
Nie zakładaj. Nie ukrywaj dezorientacji. Przed implementacją:
- Jasno określ założenia. Jeśli masz wątpliwości — zapytaj.
- Jeśli istnieje wiele interpretacji — przedstaw je, nie dokonuj wyboru po cichu.
- Jeśli istnieje prostsze podejście — powiedz o tym. Sprzeciwiaj się, gdy jest to uzasadnione.
- Jeśli coś jest niejasne — zatrzymaj się. Nazwij to. Zapytaj.

### 2. Prostota przede wszystkim
Minimalna ilość kodu, która rozwiązuje problem. Żadnych spekulacji:
- Brak funkcji wykraczających poza to, o co proszono
- Brak abstrakcji dla kodu jednorazowego użytku
- Żadnej „elastyczności" ani „konfigurowalności", o którą nie proszono
- Żadnej obsługi błędów dla niemożliwych scenariuszy
- Jeśli napisałeś 200 linii, a wystarczyłoby 50 — napisz od nowa

Zadaj sobie pytanie: „Czy doświadczony inżynier (senior) uznałby to za zbyt skomplikowane?" — jeśli tak, uprość.

### 3. Zmiany chirurgiczne
Dotykaj tylko tego, co musisz:
- Nie „poprawiaj" sąsiedniego kodu bez pytania
- Dopasuj się do istniejącego stylu
- Niepowiązany martwy kod: wspomnij — nie usuwaj

Gdy Twoje zmiany tworzą „osierocone" elementy:
- Usuń importy/zmienne/funkcje, które stały się nieużywane przez **Twoje** zmiany
- Nie usuwaj sam wcześniej istniejącego martwego kodu (Poinformuj o nim wyraźnie), usuń wtedy gdy zostaniesz o to poproszony

**Test:** Każda zmieniona linia powinna bezpośrednio wynikać z prośby użytkownika.

### 4. Wykonanie ukierunkowane na cel
| Zamiast | Zrób |
|---------|------|
| „Napraw bug" | Przeczytaj `[[Docs/context/bug-history.md]]` → napisz test który go odtwarza, potem spraw żeby przechodził |
| „Refaktoryzuj X" | Upewnij się że testy przechodzą przed i po |
| Wieloetapowe | Krótki plan: 1. [Krok] → weryfikacja: [Sprawdzenie] |

## Zasady Decyzyjne
- **Fazy:** Nie zaczynaj USB/komunikacji (Faza 2) dopóki Faza 1.5 nie zamknięta
- **Konflikt danych:** Nigdy nie zgaduj — pokaż rozbieżność z nazwami plików i wartościami
- **Hierarchia źródeł:** → [[Docs/BVERHUE-REFERENCE]]
- **⭐ NAJPIERW sprawdź poprzedni sterownik (main, `../ASFireWire`):** dice dzieli wiele klas z
  działającym sterownikiem main (np. `IsochReceiveContext`, `IsochRxDmaRing`, `OHCIConstants`).
  Zanim zaczniesz debugować bug w warstwie współdzielonej (OHCI/DMA/Isoch/Async/Bus), **porównaj
  odpowiednik w main** — `codegraph_explore` z `projectPath` na `../ASFireWire`, albo czytaj plik
  wprost. main ma `.codegraph` zaindeksowane. Bardzo często problem jest tam już rozwiązany z
  komentarzem wyjaśniającym (regresja w dice = rozbieżność z main). To pierwszy krok, nie ostatni.
  *(Przykład: Bug D/kWake — main `IsochReceiveContext::Start()` miał `kRun|kWake` z komentarzem
  „matching Linux CONTEXT_RUN | CONTEXT_WAKE"; dice miał `kRun|kIsochHeader` → DMA nie startował.)*
