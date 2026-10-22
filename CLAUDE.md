# CLAUDE.md — guida per riprendere il lavoro su Tessera

Tessera è un'app desktop Windows per l'analisi qualitativa dei dati (QDA): documenti, sistema di
codici gerarchico, codifica di segmenti di testo, memo, variabili, ricerca, matrici, import/export
REFI-QDA (.qdpx), annulla/ripeti. C++20 + Qt 6.10 (Widgets, Sql/SQLite, Pdf, Charts).
L'utente scrive in italiano: rispondi in italiano.

## Regole del progetto

- **Obiettivo: un'alternativa a MAXQDA con lo stesso modo di lavorare.** Funzioni, flussi e
  organizzazione dell'interfaccia ricalcano MAXQDA: ogni issue GitHub ha una sezione "Riferimento
  MAXQDA". Codice, prompt AI, testi, icone e altri asset restano nostri: non si copiano da MAXQDA
  né da altri software proprietari. Interoperabilità tramite REFI-QDA e, in futuro, l'import
  diretto dei progetti `.mqda` (issue #51).
- Testi dell'interfaccia **in italiano** dentro `tr()`; identificatori, commenti e messaggi di
  commit **in inglese**. Stile: quello del codice esistente (Qt naming, `m_` per i membri,
  `QStringLiteral`, commenti brevi che spiegano il perché).
- Ogni modifica deve lasciare verdi i test (`tessera_tests`, `tessera_ui_tests`).

## Questo file (CLAUDE.md)

- **CLAUDE.md va aggiornato a ogni modifica** che cambia architettura, convenzioni, comandi, regole,
  insidie o prossimi passi, nello stesso commit della modifica. Il flusso di lavoro è nella
  sezione "Flusso di lavoro per le issue".
- Se manca, va ricreato con queste sezioni: regole, flusso di lavoro, comandi, architettura,
  concetti chiave, insidie, rilasci.

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
  Model.h        struct valore: DocumentGroup, DocumentSet, Document, Code, Segment, Memo, Variable
  Project        unica fonte di verità: file SQLite .tsra, CRUD, segnali, batch, undo/redo
  Importers      TXT/MD, DOCX, ODT (QZipReader privato + XML), RTF (parser proprio), PDF (QtPdf), HTML
  Retrieval      recupero complesso dei segmenti (intersezione, sovrapposizione, vicinanza...) su segmenti in memoria
  Cooccurrence   matrice codici × codici (sovrapposizione / vicinanza / stesso documento), Jaccard e layout force-directed
  VariableFilter condizioni sulle variabili (E/O) -> documenti; ultimo filtro in meta 'variable_filter'
  Exporters      CSV per Excel (UTF-8 BOM, separatore ';'), tabelle/segmenti HTML, import variabili CSV
  Qdpx           export/import REFI-QDA 1.0 (project.qde + sources/*.txt)
  TextUtils      newline, UTF-16 <-> code point, adattamento segmenti dopo modifiche, frasi/paragrafi
  DemoProject    progetto di esempio (3 interviste originali sul lavoro da remoto)
src/ui/     Qt Widgets (libreria statica tessera_ui; main.cpp crea solo MainWindow)
  MainWindow     menu, toolbar, azioni, collega i segnali del Project ai pannelli
  DocumentTree / CodeTree   alberi con caselle di "attivazione" per il recupero dei segmenti
  DocumentView + CodingStripe   testo con evidenziazioni + barra laterale con le parentesi dei codici
  SegmentsPanel  segmenti recuperati (codici attivi × documenti attivi; nessun documento attivo = tutti);
                 `setRetrieval(Query)` per il recupero complesso, `setPassages(...)` per passaggi già calcolati
  CooccurrenceDialog  matrice + mappa (QGraphicsScene, export PNG e SVG con Qt6::Svg); il doppio clic su una cella
                 emette `passagesRequested` verso il SegmentsPanel
  MemoDialog, MemoManager, VariablesDialog, SearchDialog, MatrixDialog, RetrievalDialog, VariableFilterDialog, UiUtils
tests/      test_core.cpp (QtTest, logica), test_ui.cpp (widget offscreen)
```

### Concetti chiave

- **Salvataggio immediato.** Ogni metodo di `Project` scrive subito nel file; non esiste "Salva".
  "Salva una copia" usa `VACUUM INTO`.
- **Segnali.** Dopo ogni scrittura `Project` emette `groupsChanged`, `documentsChanged`,
  `documentSetsChanged`, `codesChanged`, `segmentsChanged(docId|0)`, `memosChanged`, `variablesChanged`. Le viste si
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
- **Set di documenti** (`document_sets`, `document_set_members`, migrazione v2): riferimenti a
  documenti di qualsiasi gruppo, un documento può stare in più set. Nel `DocumentTree` lo stesso
  documento compare quindi più volte: l'attivazione è per documento e `syncDocumentCheckBoxes`
  allinea tutte le sue caselle; `activeDocumentIds()` legge solo i gruppi.
- **Gruppi e set in QDPX.** REFI-QDA ha solo i `<Set>`: i gruppi si esportano come set con GUID
  derivati (UUID v5 da un namespace fisso + indice, `groupSetGuid()` in `Qdpx.cpp`) e all'import
  tornano gruppi; ogni altro `<Set>` diventa un set di documenti. I file di Tessera 0.1.0 (nessun
  GUID riconoscibile) vengono letti come solo gruppi. I memo di gruppi e set viaggiano nella
  `Description` del set, non come `<Note>`.
- **Posizioni nel testo** = indici UTF-16 (`QString`) nel testo semplice con `\n`. QDPX usa code
  point Unicode: convertire con `TextUtils` (test con emoji). In import QDPX i CRLF vengono
  normalizzati rimappando le posizioni.
- **Modifica del testo.** `DocumentView` in modalità modifica fa diff prefisso/suffisso a ogni
  cambiamento e adatta i segmenti in memoria (`TextUtils::adjustSegmentsForEdit`); al salvataggio
  `Project::updateDocumentText` scrive testo e segmenti insieme (un passo di undo). Durante la
  modifica Ctrl+Z appartiene all'editor e le azioni Annulla/Ripeti sono disattivate.
- **Schema e migrazioni.** `kSchema` in `Project.cpp` è la versione 1 e non si modifica più. Ogni
  cambio di schema è una `Migration { toVersion, statements }` aggiunta a
  `Project::builtInMigrations()`: una sola istruzione SQL per voce, numeri di versione crescenti,
  mai modificare una migrazione già rilasciata. `create()` applica kSchema + tutte le migrazioni;
  `open()` rifiuta file più nuovi, salva una copia `"<nome> (backup vN).tsra"` con `VACUUM INTO` e
  applica le migrazioni mancanti in **una transazione** (con `PRAGMA foreign_key_check` prima del
  commit). Le migrazioni girano con le foreign key **disattivate** (per ricostruire tabelle senza
  cascate) e prima dei trigger di undo. Una tabella nuova va aggiunta a `installUndoTriggers()`
  (che salta le tabelle assenti, per i test che simulano schemi vecchi); le colonne nuove sono già
  coperte. Nei test, `setMigrations()` simula versioni vecchie e future.

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

Si lavora direttamente su `main`: niente rami e niente pull request.

1. Scegliere l'issue (`gh issue list`; di norma priorità alta e dimensione piccola) e leggerla con
   `gh issue view <n>`: obiettivo, comportamento atteso, criteri di accettazione.
2. Implementare, aggiungere o aggiornare i test (i criteri di accettazione della issue diventano test)
   e aggiornare README/CLAUDE.md se cambiano scorciatoie o comportamenti.
3. `build.cmd Release` (da PowerShell: `cmd /c ".\build.cmd Release"`), poi `tessera_tests` e
   `tessera_ui_tests`: devono essere verdi. Un test che fallisce non si aggira, si capisce.
4. Commit su `main` in inglese, con `Closes #<n>` nel corpo, poi `git push`. La issue si chiude da
   sola al push; l'utente ha chiesto di chiuderle subito, senza attendere la prova manuale. Nel
   riepilogo finale elencare comunque all'utente le cose da controllare a mano nell'app vera
   (`--example`): i test automatici non coprono aspetto, focus e scorciatoie reali.
5. **Ogni issue chiusa comporta una nuova release**: dopo il push segui la sezione "Rilasci"
   (versione nuova, `CHANGELOG.md`, tag, zip, `gh release create`). Una release per issue, non
   si accumulano più issue in una sola.

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

Chiuse e già in `main` (provate solo con i test automatici, non ancora a mano nell'app vera):
**#1** migrazioni dello schema, **#2** set di documenti, **#4** import CSV delle variabili con
tipo, **#5** ricerca nel documento (Ctrl+F; Ctrl+Maiusc+F = ricerca lessicale sul progetto),
**#12** recupero complesso dei segmenti (Ctrl+Maiusc+R, logica in `src/core/Retrieval.*`),
**#3** attivazione dei documenti per valori delle variabili (menu Documenti/Analisi ▸ "Attiva per
variabili…", logica in `src/core/VariableFilter.*`; un valore vuoto o non valido soddisfa solo
"è vuoto"; "Attiva" è disabilitato con 0 risultati perché nessun documento attivo = tutti),
**#13** co-occorrenze dei codici (menu Analisi ▸ "Co-occorrenze dei codici…", logica in
`src/core/Cooccurrence.*`; diagonale = frequenze; il Jaccard è `min(coinvolti) / (fa + fb - min)`, esatto per
"stesso documento").
Aggiornare questo elenco quando una issue viene chiusa.

Attenzione: non usare `git stash` per "provare" qualcosa e non resettare `main` con `--hard`.

## Rilasci

`CHANGELOG.md` va aggiornato a ogni release (una voce per versione, con commit e issue).
Ogni issue chiusa fa una release nuova (di norma incremento della versione minore).

1. Aggiornare `project(Tessera VERSION x.y.z)` in `CMakeLists.txt`.
2. `build.cmd Release`, eseguire i test, `deploy.cmd zip`.
3. Commit su `main`, tag `vx.y.z`, push del tag, poi
   `gh release create vx.y.z dist\Tessera-x.y.z-win64.zip -R lucaberto95/Tessera --title ... --notes ...`.

Repository: https://github.com/lucaberto95/Tessera (privato), branch `main`.

## Prossimi passi

Le funzionalità da fare sono le issue GitHub #1–#49 e #51, raccolte nella Roadmap (#50) con area,
priorità e ordine consigliato. Ogni issue descrive comportamento atteso, note di implementazione,
criteri di accettazione e il riferimento alla funzione equivalente di MAXQDA.
