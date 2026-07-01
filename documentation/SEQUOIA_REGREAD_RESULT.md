# SEQUOIA_REGREAD_RESULT — odczyt rejestrów init MOTU (wynik)

> Wynik zadania z [`SEQUOIA_REGREAD_HANDOFF.md`](SEQUOIA_REGREAD_HANDOFF.md). Sesja Claude na
> **macOS Sequoia 15.7.4 (build 24G517)**, 2026-06-27. Oficjalny sterownik MOTU załadowany i grający.

## ⭐ AKTUALIZACJA 2026-06-27 — WARTOŚCI ZDOBYTE (DTrace deref, El Capitan)

> Read-back był ślepy (write-only), ale **DTrace na El Capitanie wyciągnął realne wartości u źródła.**
> Sesja zdalna (Sequoia → SSH → MBP2009 El Capitan 10.11.6, SIP off). Pełny przebieg:
> [`SEQUOIA_REGREAD_SESSION_LOG.md`](SEQUOIA_REGREAD_SESSION_LOG.md). Surowe logi:
> `raw-captures/2026-06-27_elcap_dtrace_deref_values.txt` + `..._iomd_dump.txt`.

**Mechanizm:** oficjalny sterownik pisze przez `IOFireWireController::asyncWrite(... IOMemoryDescriptor* ...)`;
dane wyciągnięte łańcuchem `*(u32*)(*(u64*)(*(u64*)(arg7+96)))` (deskryptor → `_ranges` → inline `IOVirtualRange.address`).
**Walidacja łańcucha** (zgodna z read-backiem): `0b10→0x00000002` ✓, `0b14→0x0a000100` @48k ✓, `0b04→0xffc20001` ✓.

**Wartości (host-order / „swap", jak w dumpie 020402):**

| rejestr | wartość | size | uwagi |
|---|---|:---:|---|
| **0x0b08** | `0xffffffff` → `0x00000000` | 4 | command/doorbell: set→clear (read-back zawsze 0). Pisany przy zimnym starcie. |
| **0x0b1c** | **`0x00120000`** (@48 kHz) | 4 | rate-zależny (`0x00000a00` przy innym rate; koreluje z `0b14` rate-idx). |
| **0x0b38** | **`{0xffc20002, 0x00000000}`** (clear=`ffffffff/ffffffff`) | **8** | V3 stream-control stream-2 (paralela do `0b04=0xffc10001`). **2. quadlet ZMIERZONY (cap_run4, replug 2026-06-27): `0x00000000`.** Też wariant 1.quad `0xffc10002` (rzadziej). |

**Triggery (potwierdzone):** `0b1c`/`0b00`/`0b10`/`0b14` padają przy **re-starcie streamu** (zmiana sample rate
lub przełączenie wyjścia). **`0b38`/`0b08` TYLKO przy zimnym starcie** (przepięcie kabla FireWire MOTU).

**➡️ DO ZROBIENIA przed wdrożeniem na Tahoe:**
1. **Dobrać 2. quadlet `0x0b38`** (size=8) — rozszerzyć deref o `d1 = *(u32*)(buf+4)` (`cap_run3.d`).
2. Skorelować pełną tabelę `0b1c` ↔ rate, jeśli celujemy w multi-rate (dla 48 k wystarczy `0x00120000`).
3. Dodać zapisy do `MOTUVendorProtocol::PrepareDuplex` z tymi wartościami (NIE zgadywane). Kolejność jak w trace.

> ⚠️ **Korekta TL;DR poniżej:** sekcja „wynik NEGATYWNY" dotyczy WYŁĄCZNIE read-backu. DTrace-deck u źródła
> wartości **zdobył** — poniższy opis read-backu zostaje jako kontekst, ale wartości masz wyżej.

---

## TL;DR dla sesji Tahoe (read-back — kontekst historyczny)

1. **Read-back jest ślepy na te rejestry — to twarde.** Rejestry **0x0b1c** i
   **0x0b38** są **write-only command registers**: nie odpowiadają na async read ANI w idle, ANI w
   trakcie aktywnego streamu. Read-back fizycznie nie może podać ich wartości. Handoff to przewidział
   („read-back steady-state może go NIE złapać").
2. **`dataBE=0x80a5211c` z traca `_v2.txt` to ARTEFAKT — NIE wpisuj tej wartości do `PrepareDuplex`.**
   Ta sama „magiczna" stała pojawia się dla **wszystkich 40 zapisów** i wszystkich offsetów (0b00, 0b04,
   0b14…), a my znamy realne wartości tych offsetów z read-backu (0b00=`0x61620000`, 0b04=`0xffc10001`,
   0b14=`0x0a000100`) — żadna nie jest `0x80a5211c`. Tracer dereferencjował zły/stały wskaźnik. Dane są
   śmieciowe.
3. **Co JEST pewne (z trace'ów):** 0x0b1c / 0x0b38 / 0x0b08 to **zapisy stream-start**, fired razem z
   `createDCLProgram` (jeden zapis 0b1c na każdy (re)build DCL = na każdy start streamu, **NIE periodyczny
   heartbeat** — pozorna periodyczność to były restarty wyjścia audio). Czyli należą do sekwencji init —
   tylko nie znamy ich **wartości**.
4. **Następny krok, żeby zdobyć wartości: snoop realnego payloadu zapisu** (poprawić kernelowy tracer,
   żeby deref `buf=` DMA-bufora, albo łapać dane z write-requesta). Read-back tej ścieżki nie domknie.

---

## Środowisko (porównywalne z dumpem `2026-06-08_020402`)

| Parametr | Wartość |
|---|---|
| Host OS | macOS Sequoia **15.7.4** (24G517), MacBook Pro M3 Max (wewn. SSD) |
| Sterownik | **oficjalny MOTU** (HAL: 16 in / 14 out, FireWire) |
| MOTU | 828mk3, Vendor 0x1F2, Model 0x106800, GUID 0x1F20000087236, Unit SW ver **0x15** |
| nodeID / gen | 0xffc0 / gen rośnie z bus-resetami (2→4→7 podczas startu streamu) |
| Sample Rate | **48000** |
| Clock Source | **Internal** (MOTU master — patrz Focus „zegar wykluczony") |
| Stream | ton 440 Hz / 48k / stereo, przez domyślne wyjście (afplay → MOTU out 1/2) |

Narzędzie: `../ASFireWire/tools/read_motu_regs.c` (async `ReadQuadlet` przez `IOFireWireLib`, zakres
0x0b00–0x0c98 co 4 B). **Na Sequoia `Open(fw)` NIE koliduje z grającym sterownikiem** — czyta się
swobodnie nawet w trakcie streamu (inaczej niż ostrzega nagłówek narzędzia z czasów El Capitan).

---

## Tabela — IDLE vs STREAMING (host-order „swapped", jak w dumpie 020402)

> „IDLE" = MOTU domyślnym urządzeniem, brak aktywnego IO. „STREAMING" = ton aktywnie leci przez MOTU
> (read w trakcie odtwarzania, gen=4, czysty — bez wyścigu bus-reset). **Kolumny są IDENTYCZNE** dla
> wszystkich czytelnych rejestrów → start streamu nie zmienił ŻADNEGO czytelnego rejestru w tym zakresie.
> Cała konfiguracja startu siedzi w **write-only** 0b1c/0b38 (poniżej).

| offset | IDLE (swapped) | STREAMING (swapped) | uwaga |
|---|---|---|---|
| 0x0b00 | 0x61620000 | 0x61620000 | |
| 0x0b04 | 0xffc10001 | 0xffc10001 | V3 stream control |
| **0x0b08** | **0x00000000** | **0x00000000** | pisany przy starcie, read-back = 0 (command/doorbell?) |
| 0x0b10 | 0x00000002 | 0x00000002 | PACKET_FORMAT |
| 0x0b14 | 0x0a000100 | 0x0a000100 | CLOCK_STATUS (rate idx) |
| **0x0b1c** | **— (brak odpowiedzi)** | **— (brak odpowiedzi)** | ⭐ **write-only, NIEczytelny** |
| 0x0b28 | 0x00101800 | 0x00101800 | |
| **0x0b38** | **— (brak odpowiedzi)** | **— (brak odpowiedzi)** | ⭐ **write-only, NIEczytelny** |
| 0x0b68 | 0x00080061 | 0x00080061 | |
| 0x0c04 | 0x00000100 | 0x00000100 | ROUTE_PORT_CONF |
| 0x0c0c | 0x00000080 | 0x00000080 | |
| 0x0c10 | 0x00000067 | 0x00000067 | licznik/status (020402 miał 0x63 — drift, nieistotne) |
| 0x0c40–0x0c4c | „Pres et 1 " (ASCII) | — || nazwa portu |
| 0x0c60–0x0c6c | „Internal " (ASCII) | — || nazwa clock source |

> **Potwierdzenie #2 (2026-06-27, Spotify):** powtórzony odczyt podczas grania prawdziwej muzyki ze
> Spotify (nie syntetyczny ton, nodeID=0xffc0 gen=12) dał **identyczną** tabelę — 0b08=0, 0b1c/0b38 nadal
> nieobecne. Wynik niezależny od źródła audio i od tego, czy łapie się moment startu.

Offsety **nieobecne w tabeli** (0b0c, 0b18, 0b1c, 0b20, 0b24, 0b2c, 0b30, 0b34, **0b38**, 0b3c) zwróciły
błąd read (brak odpowiedzi) i są pomijane przez narzędzie — tak samo w obu stanach i tak samo w starym
dumpie 020402. To NIE jest „zero" — to „urządzenie nie odpowiada na read tego adresu".

---

## Dowód, że 0x0b1c / 0x0b38 są write-only (a nie „nie złapaliśmy momentu")

- Read w **idle**, read **w trakcie aktywnego streamu**, oraz historyczny dump `020402` — **we wszystkich
  trzech** 0b1c i 0b38 są nieobecne. Handoff sugerował, że może pojawią się „dopiero gdy gra" — **nie
  pojawiają się ani wtedy**.
- W trace zapisów (`_v2.txt`) 0b1c/0b38 są **zapisywane** wielokrotnie, więc urządzenie je przyjmuje —
  ale ich nie zwraca na read. Klasyczny write-only command register (zgodne z `buf=` = bufor danych w v1).

## Dowód, że `dataBE=0x80a5211c` to artefakt (NIE używać)

`grep -oE "dataBE=0x[0-9a-f]+" ..._v2.txt | sort | uniq -c` → **40× ta sama wartość `0x80a5211c`**, dla
offsetów 0b00/0b04/0b08/0b10/0b14/0b1c/0b38. Skoro 0b00/0b04/0b14 mają realnie `0x61620000`/`0xffc10001`/
`0x0a000100` (read-back), stała `0x80a5211c` nie może być ich danymi → tracer v2 logował zły/stały
wskaźnik. **Wartości 0b1c/0b38 pozostają nieznane.**

## Wzorzec czasowy zapisów (to JEST pewne i użyteczne)

Z `_004331_...` i `_010315_..._v2.txt`, korelacja z `createDCLProgram`:

- **0x0b1c** — 1 zapis na każdy `createDCLProgram` (talking=1, czyli IT/output DCL). 4 zapisy w v2
  odpowiadają 4 rebuildom DCL (restarty wyjścia) — **per-stream-start, nie heartbeat**.
- **0x0b38** — zapis przy starcie streamu (tuż obok createDCL), 1–2×.
- **0x0b08** — kilka zapisów ciasno zgrupowanych przy starcie; read-back = 0 (command/doorbell).

Wniosek: to sekwencja **inicjalizacji streamu**. Właściwe miejsce u nas = `MOTUVendorProtocol::PrepareDuplex`
(obok istniejących 0b04/0b10/0b14/0c04), odpalane raz przy starcie duplexu. Brakuje wyłącznie **wartości**.

---

## Rekomendacja dla Tahoe

1. **NIE dodawaj zgadywanych wartości** do `PrepareDuplex` (zwłaszcza NIE `0x80a5211c`). Bez realnych
   danych ten krok jest ślepy.
2. **Zdobądź wartości przez snoop payloadu zapisu** (jedyna droga — read-back wyczerpany):
   - poprawić kernelowy tracer, żeby logował **zawartość** `buf=` (deref DMA-bufora write-requesta), nie
     wskaźnik/stałą — wtedy 0b1c/0b38/0b08 dadzą realne quadlety;
   - lub zrzucić write-request data z DTrace na `IOFireWireUserClient`/AVC write path.
3. **Równolegle rozważ, że to może nie być statyczny config.** 0b08 read-back=0 sugeruje command/doorbell,
   a nie wartość trzymaną. Jeśli po zdobyciu wartości i wpisaniu ich misframing zostanie → przejść na
   **SPH-echo** (fallback z `Focus.md` / pamięci `project_motu_linux_sph_echo_fallback`).

> Bottom line: read-back zrobiony, środowisko 48k/Internal/Main potwierdzone, mechanizm działa na Sequoia
> bez kolizji ze sterownikiem. Ale **0b1c/0b38 są write-only i ich wartości nie istnieją w żadnym dotąd
> zebranym materiale** (v1 = wskaźnik, v2 = stała-artefakt). Następny krok to snoop danych zapisu, nie
> kolejny read.
