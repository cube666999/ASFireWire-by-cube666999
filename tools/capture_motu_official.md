# Przechwytywanie oficjalnego sterownika MOTU — ground truth

Cel: ustalić co **oficjalny sterownik FireWire MOTU** zapisuje do 828mk3, czego nasz
dext DriverKit **nie** robi — podejrzana przyczyna:
- kierowania całego dźwięku na **Analog 7** zamiast na Main Out, oraz
- **wysokiego pisku** (najpewniej niezgodność parsowania/formatu/konfiguracji).

## Zalecana platforma

**MacBook Pro 2009 · macOS El Capitan · natywny port FireWire 800 · oficjalny sterownik MOTU.**

Dlaczego nie Sequoia / Apple Silicon:
- DTrace na El Capitan pozwala czytać pamięć jądra; nowszy macOS to blokuje.
- Natywny kontroler OHCI — bez mostkowania Thunderbolt→FireWire w ścieżce.
- Ta sama wersja IOFireWireFamily co nasze referencyjne źródło `docs/IOFireWireFamily/`.
- Kext MOTU (`com.motu.driver.FireWireAudio`) jest natywny dla tej epoki.

## Sposób ŁATWY — klikalny skrypt z pulpitu (zalecany)

Plik `capture_motu_official.command` ma **wbudowany** skrypt DTrace i zapisuje log
**na pulpit**. Wystarczy ten jeden plik.

1. **Wyłącz SIP** (raz): Recovery (⌘R) → Terminal → `csrutil disable` → reboot.
2. Skopiuj `capture_motu_official.command` na **Pulpit** maszyny z El Capitan.
3. Raz nadaj prawa (Terminal): `chmod +x ~/Desktop/capture_motu_official.command`
4. **Kliknij dwukrotnie** plik na Pulpicie → otworzy Terminal, poprosi o hasło sudo.
5. Gdy pojawi się „GRAJ dźwięk do MOTU teraz" — włącz dźwięk na ~15 s.
6. Wciśnij **ENTER** w Terminalu, żeby zakończyć.
7. Log zapisze się jako `~/Desktop/motu_official_<data>.txt` — odeślij mi go.

## Sposób ręczny (alternatywa, z Terminala)

1. Wyłącz SIP jak wyżej.
2. Skopiuj `capture_motu_official.d` na maszynę z El Capitan.
3. Uruchom, włącz dźwięk, zatrzymaj po ~15 s:
   ```bash
   sudo dtrace -w -s capture_motu_official.d | tee ~/Desktop/motu_official.txt
   # ... włącz iTunes / dowolny dźwięk do MOTU, niech gra ~15 s ...
   # Ctrl-C
   ```
4. Odeślij `~/Desktop/motu_official.txt`.

## Co odczytamy z wyniku

- **`[WR]` / `[ATREQ]`** — każdy async write wykonany przez sterownik. Wyciągniemy
  48-bitowy **offset rejestru** i **dane** dla każdego. Porównamy z tym co pisze nasz
  `MOTUAudioBackend` (tylko CLOCK_STATUS 0x0b14, packet-format, ISOC_CTRL).
  Wszystko dodatkowe — zwłaszcza zapisy Command-DSP / routingu — to czego nam brakuje.
- **`[ISOCH]`** — kanał, prędkość, rozmiar pakietu, wskaźnik programu DCL. Potwierdza
  DBS / układ pakietu oficjalnego sterownika (porównaj z naszym DBS=13 / 424 bajty).

## Jeśli sonda nie strzela / złe argumenty

To jest skrypt **odkrywczy** (szerokie dopasowania `strstr`). Jeśli `[WR]`/`[ATREQ]`
nic nie pokazują, wylistuj kandydujące symbole na maszynie i doprecyzujemy:
```bash
sudo dtrace -ln 'fbt:com.apple.iokit.IOFireWireFamily::entry' | grep -iE 'rite|async|req'
```
Odeślij też tę listę.
