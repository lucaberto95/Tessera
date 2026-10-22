# CLAUDE.md — guida per riprendere il lavoro su Tessera

Tessera è un'app desktop Windows per l'analisi qualitativa dei dati (QDA): documenti, sistema di
codici gerarchico, codifica di segmenti di testo, memo, variabili, ricerca, matrici, import/export
REFI-QDA (.qdpx), annulla/ripeti. C++20 + Qt 6.10 (Widgets, Sql/SQLite, Pdf, Charts).
L'utente scrive in italiano: rispondi in italiano.

## Regole del progetto

- **Progetto originale (clean room).** Non copiare codice, prompt, testi, icone, schemi o asset da
  software QDA proprietari. Le funzionalità si progettano da zero; l'interoperabilità passa solo
  dallo standard aperto REFI-QDA.
- Testi dell'interfaccia **in italiano** dentro `tr()`; identificatori, commenti e messaggi di
  commit **in inglese**. Stile: quello del codice esistente (Qt naming, `m_` per i membri,
  `QStringLiteral`, commenti brevi che spiegano il perché).
- Ogni modifica deve lasciare verdi i test (`tessera_tests`, `tessera_ui_tests`).

## Comandi

Toolchain sul PC dell'utente: VS 2022 Build Tools (MSVC 14.44, CMake e Ninja inclusi),
Qt 6.10.3 in `C:\Qt\6.10.3\msvc2022_64` (installato con aqtinstall, moduli `qtpdf qtcharts`).
Non servono cmake/ninja nel PATH: li trovano gli script.

```bat
build.cmd Release        :: configura + compila in build\Release (app, libreria, test)
deploy.cmd               :: dist\Tessera autonoma (Qt + runtime VC++ app-local)
deploy.cmd zip           :: + dist\Tessera-<versione>-win64.zip per le release
```

Da PowerShell: `cmd /c "build.cmd Release"`. Argomenti CMake extra solo tramite la variabile
`TESSERA_CMAKE_ARGS` (es. `-DTESSERA_BUILD_APP=OFF` per compilare solo core + test): `cmd` spezza
gli argomenti `-DX=Y` passati direttamente.

Test (offscreen, senza finestre):

```powershell
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'     # senza, il testo offscreen è a quadratini
build\Release\tessera_tests.exe -platform offscreen
build\Release\tessera_ui_tests.exe -platform offscreen
```

`$env:TESSERA_SHOTS='<cartella>'` fa salvare a `tessera_ui_tests` uno screenshot PNG di ogni vista.
L'app accetta `--example <file.tsra>` (crea il progetto di esempio) e `--screenshot <png>`
(salva la finestra dopo 1,5 s ed esce): utili per verificare l'interfaccia vera.
Con `-platform offscreen` i test girano senza aprire finestre sullo schermo dell'utente.

## Architettura

```
src/core/   nessuna dipendenza dai widget (libreria statica tessera_core, usata da app e test)
  Model.h        struct valore: DocumentGroup, Document, Code, Segment, Memo, Variable
  Project        unica fonte di verità: file SQLite .tsra, CRUD, segnali, batch, undo/redo
  Importers      TXT/MD, DOCX, ODT (QZipReader privato + XML), RTF (parser proprio), PDF (QtPdf), HTML
  Exporters      CSV per Excel (UTF-8 BOM, separatore ';'), tabelle/segmenti HTML, import variabili CSV
  Qdpx           export/import REFI-QDA 1.0 (project.qde + sources/*.txt)
  TextUtils      newline, UTF-16 <-> code point, adattamento segmenti dopo modifiche, frasi/paragrafi
  DemoProject    progetto di esempio (3 interviste originali sul lavoro da remoto)
src/ui/     Qt Widgets (libreria statica tessera_ui; main.cpp crea solo MainWindow)
  MainWindow     menu, toolbar, azioni, collega i segnali del Project ai pannelli
  DocumentTree / CodeTree   alberi con caselle di "attivazione" per il recupero dei segmenti
  DocumentView + CodingStripe   testo con evidenziazioni + barra laterale con le parentesi dei codici
  SegmentsPanel  segmenti recuperati (codici attivi × documenti attivi; nessun documento attivo = tutti)
  MemoDialog, MemoManager, VariablesDialog, SearchDialog, MatrixDialog, UiUtils
tests/      test_core.cpp (QtTest, logica), test_ui.cpp (widget offscreen)
```

### Concetti chiave

- **Salvataggio immediato.** Ogni metodo di `Project` scrive subito nel file; non esiste "Salva".
  "Salva una copia" usa `VACUUM INTO`.
- **Segnali.** Dopo ogni scrittura `Project` emette `groupsChanged`, `documentsChanged`,
  `codesChanged`, `segmentsChanged(docId|0)`, `memosChanged`, `variablesChanged`. Le viste si
  ricostruiscono da questi segnali; non modificano mai il proprio stato al posto del Project
  (es. il drop nel `CodeTree` imposta `IgnoreAction` e chiama `moveCode`).
- **Batch.** `beginBatch()/commitBatch()` = una transazione, segnali rimandati al commit e **un solo
  passo di undo**. Le operazioni con più scritture devono stare in un batch.
- **Undo/redo** (`Project::undo/redo`). Trigger SQLite TEMP (per connessione, non salvati nel file)
  scrivono in `temp.undo_log` l'istruzione inversa di ogni riga inserita/modificata/cancellata,
  cascate comprese. Il log viene chiuso in un passo in `notify()`/`commitBatch()`. Il replay gira
  all'indietro con `PRAGMA defer_foreign_keys`; ciò che i trigger registrano durante il replay
  diventa il passo opposto. Etichette: `setUndoLabel(tr(...))` all'inizio di ogni metodo che
  scrive; dentro un batch vince la prima (la UI la imposta prima di `beginBatch`). Una nuova
  tabella va aggiunta all'elenco in `installUndoTriggers()`. Cronologia solo di sessione, max 200
  passi; azzerata dopo import QDPX e progetto d'esempio.
- **Posizioni nel testo** = indici UTF-16 (`QString`) nel testo semplice con `\n`. QDPX usa code
  point Unicode: convertire con `TextUtils` (test con emoji). In import QDPX i CRLF vengono
  normalizzati rimappando le posizioni.
- **Modifica del testo.** `DocumentView` in modalità modifica fa diff prefisso/suffisso a ogni
  cambiamento e adatta i segmenti in memoria (`TextUtils::adjustSegmentsForEdit`); al salvataggio
  `Project::updateDocumentText` scrive testo e segmenti insieme (un passo di undo). Durante la
  modifica Ctrl+Z appartiene all'editor e le azioni Annulla/Ripeti sono disattivate.
- **Schema** in `Project.cpp` (`kSchema`), versione in `meta.schema_version` (`SchemaVersion = 1`).
  Cambiando lo schema: alzare la versione e aggiungere una migrazione in `open()`.

### Insidie già incontrate

- Una `QString` nulla viene salvata come SQL NULL: `bindAll()` la converte in `''` (colonne NOT NULL).
- `QTextEdit::toPlainText()` trasforma gli spazi non separabili: usare `rawText()` in DocumentView.
- Nel corpo di un trigger SQLite i nomi di tabella non possono essere qualificati (`temp.x`).
- `DocumentView` deve restare iscritto ai segnali del Project da solo: con segmenti non aggiornati
  il salvataggio di una modifica al testo cancellerebbe codifiche.
- Le colonne della `CodingStripe` si assegnano sui pixel (righe visive), non sugli offset di testo,
  e vanno ricalcolate quando il testo va a capo diversamente.
- `findChild<QTableWidget*>()` in un dialogo con più tabelle restituisce quella sbagliata: nei test
  filtrare con `findChildren`.
- `QZipReader/QZipWriter` sono header privati di QtCore (`Qt6::CorePrivate`): possono cambiare tra
  versioni di Qt.

## Flusso di lavoro per le issue

Il codice di ogni issue si sviluppa su un branch e arriva su `main` solo con una pull request.
**Eccezione: CLAUDE.md** (e le sole note di processo) si modifica e si committa direttamente su
`main`, perché è l'unica versione che l'AI legge: un branch lo renderebbe invisibile alle sessioni
successive. Mantenerlo aggiornato a ogni sessione, nello stesso momento in cui cambia qualcosa.

1. Scegliere l'issue (`gh issue list`; di norma priorità alta e dimensione piccola) e leggerla con
   `gh issue view <n>`: obiettivo, comportamento atteso, criteri di accettazione.
2. Creare il branch da `main` aggiornato: `git switch -c feature/<nome-breve> main`
   (`fix/…` per i bug, `docs/…` per la sola documentazione).
3. Implementare, aggiungere o aggiornare i test (i criteri di accettazione della issue diventano test)
   e aggiornare README/CLAUDE.md se cambiano scorciatoie o comportamenti.
4. `build.cmd Release` (da PowerShell: `cmd /c ".\build.cmd Release"`), poi `tessera_tests` e
   `tessera_ui_tests`: devono essere verdi. Un test che fallisce non si aggira, si capisce.
5. Commit in inglese, con `Closes #<n>` nel corpo. Push del branch (`git push -u origin <branch>`).
6. Aprire la PR contro `main` con `gh pr create --body-file <file>` (il corpo multilinea passato
   inline si rompe in PowerShell). Il corpo inizia con `Closes #<n>` e riassume: cosa cambia, test
   eseguiti, cosa resta da provare a mano. Poi `get_status`/`bind_pr` dei tool ccd_pr.
7. **Non fare il merge e non chiudere la issue prima della prova manuale** nell'app vera
   (`--example`); i test automatici non coprono aspetto, focus e scorciatoie reali. Dare all'utente
   l'elenco delle cose da controllare. Il merge della PR chiude la issue da solo.
8. Dopo il merge: `git switch main`, `git pull`, eliminare il branch.

### Issue, etichette e roadmap

- La issue **#50** è la roadmap: elenca tutte le issue per area con priorità e dimensione (S/M/L/XL).
  Ordine consigliato per iniziare: #1, #2, #3, #4, #5, #12, #13. Ogni issue ha obiettivo, comportamento
  atteso, note di implementazione, prerequisiti, criteri di accettazione e dimensione.
- Etichette: `area: core | organizzazione | analisi | collaborazione | tipi di dati | import | ai |
  export | app`, `priorità: alta | media | bassa`, più `enhancement`, `bug`, `good first issue`.
- Le sezioni "Riferimento MAXQDA" delle issue indicano solo quale funzione è equivalente: servono a
  capire il comportamento, non a copiare. Vale la regola clean room sopra.
- Scegliere di norma `priorità: alta`, poi dimensione piccola, rispettando i prerequisiti scritti
  nella issue.

### Stato delle issue

- **#5** (ricerca nel documento aperto, Ctrl+F): implementata, in attesa di prova manuale e merge
  nella [PR 55](https://github.com/lucaberto95/Tessera/pull/55). Ctrl+F = ricerca nel documento,
  Ctrl+Shift+F = ricerca lessicale sul progetto.
- **#4** (import CSV delle variabili con riconoscimento del tipo): implementata, in attesa di prova manuale e merge
  nella [PR 57](https://github.com/lucaberto95/Tessera/pull/57).
- Aggiornare questo elenco quando una issue passa in PR o viene chiusa.

Attenzione: non usare `git stash` per "provare" qualcosa e non resettare `main` con `--hard`;
per riportare `main` allineato a `origin/main` senza perdere commit usare `git branch -f main origin/main`
dopo aver spostato il lavoro su un branch.

## Rilasci

1. Aggiornare `project(Tessera VERSION x.y.z)` in `CMakeLists.txt`.
2. `build.cmd Release`, eseguire i test, `deploy.cmd zip`.
3. Commit + tag `vx.y.z`, push, poi
   `gh release create vx.y.z dist\Tessera-x.y.z-win64.zip -R lucaberto95/Tessera --title ... --notes ...`.

Repository: https://github.com/lucaberto95/Tessera (privato), branch `main`.

## Idee per i prossimi passi

Codifica di aree in PDF/immagini; audio/video con trascrizione e timestamp; confronto tra
codificatori (kappa); set di documenti e filtri per variabile nel recupero; ricerca dentro il
documento aperto; importazione variabili CSV con tipi; installer (MSIX/Inno Setup) e icona dell'app.
