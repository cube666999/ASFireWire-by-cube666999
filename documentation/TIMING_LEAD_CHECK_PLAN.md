# PLAN — zmierzyć REALNY lead NASZEGO strumienia IT na drucie (domena zegara)

> Cel: ustalić, czy nasz dext (Tahoe, v140) nadaje SPH z tym samym leadem co oficjalny sterownik
> (**3 cykle**, `SEQUOIA_SNOOP_RESULT.md`). „Bajt-perfect pakiety, a MOTU misframuje" = klasyczny objaw
> zegarowy → musimy zmierzyć **lead = SPH_cyk − cykl_przybycia** NASZEGO strumienia, nie tylko oficjalnego.

## Dlaczego to ma sens TERAZ

Wyczerpaliśmy zawartość pakietu: nasz `[WIRE16]` (v139) = oficjalny bajt-w-bajt (SID=1=nasz node, DBS=0d,
byte2=04, q1=8222ffff, SPH-rampa). `SID`/`0b1c`/`0b08` = bezskuteczne. Zostaje **timing**. Mierzyliśmy
lead=3 z **oficjalnego** snoopa, ale **nigdy z naszego**. Jeśli nasz lead ≠ 3 (albo dryfuje, albo absolutna
faza jest zła) → to konkretny bug do naprawy (korekta seeda/leadu), zanim sięgniemy po cały SPH-echo.

## Setup (jak oryginalny SPH capture, ale cel = NASZ stream)

| maszyna | rola |
|---|---|
| **M3 / Tahoe** (nasz dext v140) | napędza MOTU, IT na **ch1** (host→device) — to mierzymy |
| **MBP2009 / Linux Mint** | pasywny snoop (`fw_isoch_snoop_cyc` z `ev->cycle`) |

⚠️ **Sekwencja (foreign-IRM):** MB2009 na szynie = obcy IRM → underexposure (PCM się zeruje). **ALE SPH
leci nadal** (packetizer pisze SPH w każdym data-pakiecie niezależnie od payloadu) → **lead da się zmierzyć
mimo underexposure.** Kolejność: **najpierw stream up na Tahoe BEZ Linuksa** (zagraj na MOTU), **potem wepnij
MB2009** i snoopuj. (Tak działał udany snoop 23:10.)

## Narzędzie

`fw_isoch_snoop_cyc` (załatana wersja z `ev->cycle`) — pełny kod w
[`SEQUOIA_SNOOP_HANDOFF.md`](SEQUOIA_SNOOP_HANDOFF.md) §Krok A. Jeśli już zbudowany na MB2009 (`/tmp/fw_isoch_snoop_cyc`)
— użyj. Jeśli nie — zbuduj wg tamtego handoffu.

## Kroki

1. **Tahoe:** zainstaluj v140, zagraj ton/muzykę na MOTU Main Out (stream aktywny, IT na ch1).
2. **MBP2009** (SSH, hasło `<redacted LAN pw>`, wymuś `-o PreferredAuthentications=password -o PubkeyAuthentication=no`):
   ```bash
   sudo modprobe firewire_ohci quirks=0x10
   sudo modprobe -r snd_firewire_motu snd_firewire_lib    # nie przejmuj MOTU
   sudo /tmp/fw_isoch_snoop_cyc /dev/fw0 1 2000 > /tmp/our_it_cyc.txt 2>/tmp/err.txt
   ```
   (arg 2 = **ch1** = nasz IT host→device. Jeśli pusto → spróbuj inne kanały: 0,2,3.)
3. **Policz lead** tym samym parserem co w `SEQUOIA_SNOOP_RESULT.md` §Metoda (regex `cyc=(\d+) len=424 ...`,
   `sph=quadlet[2]`, `sph_cyc=(sph>>12)&0x1FFF`, `lead=((sph_cyc-(cyc&0x1FFF))+8000)%8000`, korekta `>4000→-8000`).
4. Skopiuj `our_it_cyc.txt` do `documentation/raw-captures/` i zapisz wynik (mediana leadu + rozrzut).

## Interpretacja

- **Lead = 3 (jak oficjalny), bez rozrzutu** → nasz timing JEST dobry → timing wykluczony → przejść na
  **SPH-echo** dopiero gdy 0b38 też nie pomoże, albo szukać dalej (routing/0b38).
- **Lead ≠ 3 (np. 2, 4) lub rozrzut** → **konkretny bug**: skoryguj seed/lead w
  [`ASFWAudioDriverZts.cpp:247`](ASFWDriver/Audio/DriverKit/ASFWAudioDriverZts.cpp:247) (`kMotuSphPresentationLeadTicks`)
  o zmierzoną różnicę — tani fix, bez całego SPH-echo.
- **Lead ROŚNIE w czasie (dryf)** → free-run seed-once dryfuje od zegara MOTU → **to jest argument za SPH-echo**
  (`project_motu_linux_sph_echo_fallback`): echo'wać SPH urządzenia zamiast free-run.

> Bottom line: ten pomiar rozstrzyga „czy nasz timing realnie = oficjalny". Jak tak → timing OFF the table.
> Jak nie → albo tani fix leadu, albo (przy dryfie) uzasadnienie dla SPH-echo. Bez tego pomiaru SPH-echo
> byłoby strzałem w ciemno (wcześniej snoop pokazał naszą rampę jako „czystą", ale leadu vs cykl nie liczył).
