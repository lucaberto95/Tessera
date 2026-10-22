# Tessera

**Tessera** è un software desktop per Windows dedicato all'analisi qualitativa dei dati:
importi interviste e documenti, costruisci un sistema di codici, codifichi i brani di testo,
scrivi memo e confronti i risultati tra documenti.

È scritto in C++20 con Qt 6. Lo scambio con altri software QDA avviene tramite lo standard aperto
**REFI-QDA (.qdpx)**.

![Tessera](docs/screenshot.png)

## Download

Scarica `Tessera-x.y.z-win64.zip` dalla pagina
[Releases](https://github.com/lucaberto95/Tessera/releases), estrai la cartella e avvia
`Tessera.exe`. Non serve installare nulla: Qt e il runtime di Visual C++ sono già inclusi.

Per provarlo subito, usa **Progetto → Crea progetto di esempio…**: crea tre interviste sul lavoro
da remoto, già codificate, con variabili e memo.

## Funzionalità

| Area | Cosa fa |
|---|---|
| **Progetti** | Ogni progetto è un singolo file (`.tsra`, SQLite) salvato automaticamente a ogni modifica. Ci sono i progetti recenti e "Salva una copia come…". |
| **Annulla / Ripeti** | `Ctrl+Z` / `Ctrl+Y` funzionano su ogni operazione della sessione (fino a 200 passi), anche su eliminazioni, importazioni e codifica automatica. |
| **Documenti** | I documenti si organizzano in gruppi e si importano da TXT/MD, DOCX, ODT, RTF, PDF (con testo) e HTML. I **set di documenti** raccolgono documenti di gruppi diversi (trascinandoli su un set o dai documenti attivi) e si attivano con un clic. Si possono creare documenti incollando del testo e modificarne il testo: le codifiche si adattano da sole. |
| **Codici** | Sistema di codici gerarchico con colori, descrizioni e memo, riorganizzabile con il trascinamento e con le frequenze sempre visibili. |
| **Codifica** | Seleziona il testo e trascinaci sopra un codice, oppure usa `Ctrl+E` (codice selezionato), `Ctrl+Shift+E` (nuovo codice) o il menu contestuale. Il testo codificato è evidenziato con il colore del codice e una barra laterale mostra le parentesi dei codici. Ogni segmento può avere commento e peso. |
| **Recupero segmenti** | Attiva documenti e codici con le caselle di spunta: il pannello in basso raccoglie i segmenti corrispondenti, con filtro ed esportazione in HTML o Excel (CSV). |
| **Memo** | Memo su progetto, gruppi, documenti, codici e segmenti, con una finestra che li raccoglie tutti ed esportazione. |
| **Variabili** | Variabili dei documenti tipizzate (testo, intero, decimale, sì/no, data), modificabili in una tabella, con import/export CSV (all'import il tipo di ogni colonna viene riconosciuto e si può correggere in un'anteprima). «Attiva per variabili…» attiva i documenti che soddisfano condizioni sulle variabili (es. Genere = F E Età ≥ 40). |
| **Analisi** | Ricerca lessicale (più termini con OR, parole intere, espressioni regolari) con codifica automatica della parola, della frase o del paragrafo. Matrice codici × documenti con heatmap, frequenze dei codici e grafico. |
| **Interoperabilità** | Importazione ed esportazione QDPX (REFI-QDA) di documenti testuali, codici, codifiche, memo, variabili e gruppi. |

### Scorciatoie

| Tasti | Azione |
|---|---|
| `Ctrl+N` / `Ctrl+O` | Nuovo progetto / apri progetto |
| `Ctrl+I` | Importa documenti |
| `Ctrl+K` | Nuovo codice |
| `Ctrl+E` | Codifica la selezione con il codice selezionato |
| `Ctrl+Shift+E` | Codifica la selezione con un nuovo codice |
| `Ctrl+Z` / `Ctrl+Y` | Annulla / ripeti |
| `Ctrl+F` | Cerca nel documento aperto (barra di ricerca; `F3` / `Maiusc+F3` per scorrere, `Esc` per chiudere) |
| `Ctrl+Shift+F` | Ricerca lessicale su tutto il progetto |
| `Ctrl+M` | Matrice codici × documenti |
| `Ctrl+Shift+M` | Tutti i memo |
| `Ctrl+rotella` | Zoom del testo |

## Compilare dai sorgenti

Requisiti:
- Visual Studio 2022 o Build Tools con il carico di lavoro C++, che include già CMake e Ninja.
- Qt 6.8 o successivo per MSVC 2022 64 bit, con i moduli `qtpdf` e `qtcharts`. Gli script cercano
  Qt in `C:\Qt\6.10.3\msvc2022_64`; se è altrove, imposta la variabile `QTDIR`.

```bat
build.cmd Release
deploy.cmd zip
```

`build.cmd` compila nella cartella `build\Release`. `deploy.cmd zip` prepara la cartella
autonoma `dist\Tessera` e lo zip per la release.

Test (senza aprire finestre):

```bat
set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
set QT_QPA_FONTDIR=C:\Windows\Fonts
build\Release\tessera_tests.exe -platform offscreen
build\Release\tessera_ui_tests.exe -platform offscreen
```

## Struttura del codice

```
src/core/   modello dati, persistenza SQLite, undo/redo, import/export, QDPX (nessun widget)
src/ui/     interfaccia Qt Widgets
tests/      test della logica (test_core) e dell'interfaccia in modalità offscreen (test_ui)
```

I dettagli di architettura, le convenzioni e le insidie già incontrate sono in [CLAUDE.md](CLAUDE.md).

## Limiti attuali

- Lavora solo su fonti testuali: niente codifica di aree di immagini o PDF, né di audio e video.
- La cronologia di annullamento vale per la sessione e si azzera chiudendo il progetto.
- Non ci sono il lavoro di gruppo né il confronto tra codificatori.
