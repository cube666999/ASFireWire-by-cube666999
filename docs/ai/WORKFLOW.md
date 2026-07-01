# Git, dokumentacja, compact — pełne instrukcje

> Przeniesione z `CLAUDE.md` (odchudzenie kontekstu, 2026-07-01). Krytyczny skrót (branch, remote,
> `git push cube666 dice-motu`) zostaje w `CLAUDE.md` na stałe — to jest rozwinięcie.

## Git — zasady push

To repo (dice) ma dwa remote:
- `origin` = `mrmidi/ASFireWire.git` — **NIE należy do użytkownika**, brak uprawnień do push
- `cube666` = `cube666999/ASFireWire-by-cube666999.git` — **fork użytkownika**, tu pushujemy

**Branch roboczy dice = `dice-motu`** (NIE `main` — `main`/`dice-motu` to różne branche; main branch zero-copy
żyje w osobnym repo `../ASFireWire`).

**ZAWSZE przed pushem:**
1. Sprawdź `git remote -v` i `git branch` (potwierdź że jesteś na `dice-motu`)
2. Używaj jawnie: `git push cube666 dice-motu`
3. Nigdy nie używaj samego `git push` (domyślnie trafia na `origin` = brak uprawnień)

> ℹ️ **Historia:** wcześniej remote `cube666` używał HTTPS+token GitHub. Token został skompromitowany
> i usunięty (patrz DevLog 2026-06-XX). Aktualnie remote to **SSH** (`git@github.com:...`) — brak tokena
> w `.git/config`. Sprawdź `git remote -v`; jeśli zobaczysz `ghp_…` → coś się cofnęło, natychmiast zgłoś.

## System pracy — kiedy notować, commitować, pushować

Prowadź dokumentację i historię **sam, bez przypominania** — to część zadania, nie dodatek.

**Notuj na bieżąco (`Focus.md` / `DevLog.md`):**
- Po każdym fixie: numer wersji (CFBundleVersion), co zmienia, w którym pliku, wynik testu hardware.
- `Focus.md` = ZAWSZE aktualny stan + następny krok. `DevLog.md` = archiwum rozwiązanych bugów.
- **System Focus → Ukończone:** gdy element z „AKTUALNY STAN" zostanie rozwiązany i zweryfikowany,
  **przenieś go** do sekcji `## ✅ Ukończone` w `Focus.md` (jednolinijkowo: wersja + plik), a pełny
  opis (root cause + fix + dowód z logów) zapisz w `DevLog.md`. Focus.md NIE rośnie historią —
  trzyma tylko aktywny stan i następny krok. Nie zostawiaj nieaktualnych „problemów otwartych".
- Potwierdzone fakty sprzętowe → **NIE kopiuj liczb**, linkuj kanon `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`.
- Aktualizuj memory (`MEMORY.md` + plik faktu) gdy ustalisz coś nieoczywistego i trwałego.

**Commituj gdy:**
- Fix jest kompletny i zweryfikowany (build przeszedł / test hardware potwierdził), LUB
- Kończysz spójny etap dokumentacji/refaktoru. Nie commituj w połowie niedziałającej zmiany.
- Commit message po **angielsku** (mrmidi czyta commity dice). Format `fix(motu-v3): …`, `docs: …`, `chore: …`.

**Pushuj gdy:** commit reprezentuje stabilny punkt który warto mieć w zdalnym repo (działający fix,
ukończona dokumentacja). `git push cube666 dice-motu`. Nie pushuj eksperymentalnych WIP bez powodu.

**Czego NIE robić (lekcje z pierwszego sterownika):**
- ❌ NIE buduj hardware-test bez `--clean` ani z `--no-bump` (iCloud mtime → stary `.o` / pominięty upgrade dextu).
- ❌ NIE używaj `grep`/`find`/`Bash` do szukania kodu (CodeGraph first).
- ❌ NIE `log stream` bez `/usr/bin/` (zsh builtin) ani z predykatem po procesie (użyj `senderImagePath`).
- ❌ NIE ufaj samemu `systemextensionsctl list` jako dowodowi świeżego kodu — weryfikuj markerem w logach dextu.

## Compact Instructions

Przy kompaktowaniu konwersacji **zachowaj:**
- Aktualny numer wersji dextu (CFBundleVersion) i co ostatni fix zmienia + w którym pliku.
- Stan ZTS debug: czy `DrainCompleted()` > 0, czy ZTS publikowany, gdzie IR DMA się zatrzymuje.
- Cel bieżącej sesji — co naprawiamy i dlaczego.
- Potwierdzone fakty sprzętowe → linkuj kanon `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`, NIE kopiuj liczb.
- Zasady git: `git push cube666 dice-motu`, branch `dice-motu`.
- Wnioski z logów/disassembly które potwierdziły działanie fixa.

**Odrzuć:** surowe logi systemowe, wielokrotnie czytane duże pliki (project.pbxproj, build.sh — są w repo),
stare iteracje debugowania już rozwiązane, długie outputy bash bez nowych informacji.

## Snapshot branch dla upstream (motu-v3-showcase)

Do prezentowania stanu MOTU wsparcia dla mrmidi (lub innego reviewera) istnieje **osobna gałąź prezentacyjna**
`motu-v3-showcase` na forku `cube666`. To **snapshot**, nie rozwijana ręka:
- Jeden squash-commit od `origin/DICE` z kurowanym opisem (highlights, key finding, measurement corpus)
- Zero szumu bumpów, zero WIP
- Kod identyczny jak na `integrate-dice-c2bdf11` w chwili tworzenia

### Kiedy odświeżyć
- Zebrałeś istotny merytoryczny fix / diagnostykę i chcesz to pokazać mrmidiemu / w Discord
- Wysyłasz link do fork'a komuś nowemu i nie chcesz „obciachu" widoku `git log`
- Zamierzasz otworzyć realny PR w przyszłości (wtedy showcase będzie dobrym draftem)

### Jak odświeżyć (10 minut, bezpieczna sekwencja)

⚠️ **Sequence-critical.** Nie skracaj, nie zmieniaj kolejności.

```bash
# 0. Working tree ma być CZYSTY. Jak nie jest — zestashuj albo scommituj.
git status --short   # ma nic nie pokazać
git fetch origin

# 1. Skasuj stary showcase LOKALNIE i ZDALNIE.
git branch -D motu-v3-showcase 2>/dev/null || true
git push cube666 --delete motu-v3-showcase 2>/dev/null || true

# 2. Zbuduj nowy od najnowszego origin/DICE.
git checkout -b motu-v3-showcase origin/DICE
git merge --squash integrate-dice-c2bdf11
# ↑ jak konflikt → STOP, wróć na integrate-dice-c2bdf11 (git checkout ...), zapytaj Claude co robić.

# 3. PRZETŁUMACZ Focus.md i CLAUDE.md na angielski W TYM drzewie (showcase) TYLKO.
#    Nie ruszać oryginałów na integrate-dice-c2bdf11 — Claude edytuje kopie w
#    bieżącym (showcase) working tree, nadpisuje treść, NIE tłumaczy DevLog.md
#    ani docs/ai/* (te zostają PL — patrz „Strategia B" niżej).
#    (Ten krok robi Claude ręcznie, nie ma automatycznego skryptu.)

# 4. Curated commit — użyj poprzedniego jako template.
# Zobacz poprzedni: git log --format=%B -1 (na starym showcase zanim skasujesz)
# Zaktualizuj sekcje: STATE, HIGHLIGHTS, WHAT WE ARE STUCK ON
git commit   # edytor otworzy się na message; wklej curated tekst

# 5. Push i wróć na roboczy branch (drzewo robocze — polskie oryginały nietknięte).
git push cube666 motu-v3-showcase
git checkout integrate-dice-c2bdf11
```

**Nic nie zniknie** przy tej operacji. `integrate-dice-c2bdf11` pozostaje nietknięty ze wszystkimi ~90 commitami
włącznie z bumpami, i **zawsze po polsku**. Showcase to **dodatkowa** gałąź na forku, obok — nie zastępuje historii.

### Strategia B — język na showcase (ustalone 2026-07-01)

Decyzja: mrmidi nie zna polskiego, ale tłumaczenie WSZYSTKIEGO kosztuje zbyt dużo tokenów
przy każdym odświeżeniu. Podział:
- **`Focus.md`, `CLAUDE.md` na showcase → PO ANGIELSKU.** To jedyne pliki które mrmidi realnie
  otworzy z linku. Tłumaczone w locie przez Claude przy każdym odświeżeniu (koszt ~10k tokenów).
- **`DevLog.md`, `docs/ai/*` na showcase → zostają PO POLSKU**, identycznie jak na roboczym branchu.
  To nasza wewnętrzna historia/instrukcje — mrmidi tam nie zagląda, nie warto tłumaczyć.
- **Working tree (`integrate-dice-c2bdf11`) NIGDY nie jest tłumaczony** — user pracuje po polsku,
  zawsze. Tłumaczenie istnieje wyłącznie jako osobna kopia treści w commicie na `motu-v3-showcase`.

### Konwencja nazwy
Jak w przyszłości będzie kilka snapshotów w różnym stanie: `motu-v3-showcase` = aktualny „najlepszy widok".
Dla starych: `motu-v3-showcase-2026-07-01` (data). Nie robimy tego dopóki jeden branch wystarcza.
