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

## Flusso di lavoro: un ramo e una pull request per ogni issue

- **Mai commit diretti su `main`.** Ogni modifica passa da un ramo e da una pull request.
- **Una issue = un ramo = una PR.** Il ramo si chiama `issue-<numero>-<descrizione-breve>` (es.
  `issue-2-set-documenti`) e parte da `main` aggiornato. Le modifiche che non appartengono a una
  issue (documentazione, script) vanno in un ramo dedicato (es. `docs/...`) con la sua PR.
- Non mescolare più issue nello stesso ramo. Se una issue dipende da un'altra non ancora unita,
  il suo ramo parte da quello della issue da cui dipende e la PR usa quel ramo come base (PR
  "impilata"); quando la PR di base viene unita e il suo ramo cancellato, GitHub sposta la base
  su `main`.
- Prima di aprire o aggiornare una PR: build e test verdi. La descrizione della PR dice cosa
  cambia, come è stato verificato e cosa resta da provare, e chiude la issue con `Closes #<numero>`.
- **Il merge lo decide l'utente**: si unisce solo quando l'utente dice che la PR è a posto.
- Per un ramo rimasto indietro rispetto a `main`: `git merge main` nel ramo (niente force push).

## Questo file (CLAUDE.md)

- **CLAUDE.md va aggiornato a ogni modifica** che cambia architettura, convenzioni, comandi, regole,
  insidie o prossimi passi, nello stesso ramo e nella stessa PR della modifica.
- Si può aggiornare sui rami delle singole issue; con il merge della PR l'aggiornamento **deve
  rientrare in `main`**. Nei conflitti su questo file si tengono le informazioni di entrambi i rami.
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

## Rilasci

1. In un ramo `release/x.y.z` con PR: aggiornare `project(Tessera VERSION x.y.z)` in `CMakeLists.txt`.
2. Dopo il merge, su `main` aggiornato: `build.cmd Release`, eseguire i test, `deploy.cmd zip`.
3. Tag `vx.y.z` su `main`, push del tag, poi
   `gh release create vx.y.z dist\Tessera-x.y.z-win64.zip -R lucaberto95/Tessera --title ... --notes ...`.

Repository: https://github.com/lucaberto95/Tessera (privato), branch `main`.

## Prossimi passi

Le funzionalità da fare sono le issue GitHub #1–#49 e #51, raccolte nella Roadmap (#50) con area,
priorità e ordine consigliato. Ogni issue descrive comportamento atteso, note di implementazione,
criteri di accettazione e il riferimento alla funzione equivalente di MAXQDA.
