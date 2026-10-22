# CLAUDE.md — guida per riprendere il lavoro su Tessera

Tessera: app desktop Windows per l'analisi qualitativa dei dati (QDA), alternativa a MAXQDA.
C++20 + Qt 6.10 (Widgets, Sql/SQLite, Pdf, Charts). L'utente scrive in italiano: rispondi in italiano.

## Regole

- **Stesso modo di lavorare di MAXQDA**: funzioni e organizzazione dell'interfaccia ricalcano MAXQDA
  (ogni issue ha una sezione "Riferimento MAXQDA", che serve solo a capire il comportamento). Clean room:
  codice, prompt, testi, icone e asset non si copiano da MAXQDA né da altro software proprietario.
  Interoperabilità tramite REFI-QDA e, in futuro, import dei `.mqda` (#51).
- Testi dell'interfaccia **in italiano** dentro `tr()`; identificatori, commenti e commit **in inglese**.
  Stile del codice esistente (Qt naming, `m_` per i membri, `QStringLiteral`, commenti brevi sul perché).
- Ogni modifica lascia verdi `tessera_tests` e `tessera_ui_tests`.
- **CLAUDE.md va aggiornato nello stesso commit** di ogni modifica che cambia architettura, convenzioni,
  comandi, regole o insidie. Tenerlo corto: la storia sta in `CHANGELOG.md` e in git, non qui.
  Se manca, va ricreato con: regole, comandi, architettura, concetti, insidie, flusso, rilasci.
- Non usare `git stash` e non resettare `main` con `--hard`.

## Comandi

Toolchain: VS 2022 Build Tools (MSVC 14.44, CMake e Ninja inclusi), Qt 6.10.3 in
`C:\Qt\6.10.3\msvc2022_64` (moduli `qtpdf qtcharts`). cmake/ninja non servono nel PATH.

```bat
build.cmd Release        :: configura + compila in build\Release (app, libreria, test)
deploy.cmd               :: dist\Tessera autonoma (Qt + runtime VC++ app-local)
deploy.cmd zip           :: + dist\Tessera-<versione>-win64.zip per le release
```

Da PowerShell: `cmd /c ".\build.cmd Release"`. Argomenti CMake extra solo via `TESSERA_CMAKE_ARGS`
(es. `-DTESSERA_BUILD_APP=OFF` = solo core + test): `cmd` spezza i `-DX=Y` passati direttamente.

Test (offscreen, nessuna finestra):

```powershell
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'     # senza, il testo offscreen è a quadratini
build\Release\tessera_tests.exe -platform offscreen -o out.txt,txt
build\Release\tessera_ui_tests.exe -platform offscreen -o out_ui.txt,txt
```

Dalla shell di Claude Code i test non stampano nulla: leggere il file di `-o`; l'exit code = numero di
fallimenti. `$env:TESSERA_SHOTS='<cartella>'` salva uno screenshot PNG per vista. L'app accetta
`--example <file.tsra>` (progetto di esempio) e `--screenshot <png>` (salva la finestra dopo 1,5 s ed esce).

## Architettura

```
src/core/   nessuna dipendenza dai widget (libreria statica tessera_core)
  Model.h        struct valore: DocumentGroup, DocumentSet, Document, Code, Segment, DocumentLink, Memo, Variable,
                 Dictionary, DictCategory, DictEntry
  Project        unica fonte di verità: file SQLite .tsra, CRUD, segnali, batch, undo/redo
  Importers      TXT/MD, DOCX, ODT (QZipReader privato + XML), RTF (parser proprio), PDF (QtPdf), HTML
  Retrieval      recupero complesso dei segmenti su segmenti in memoria
  Cooccurrence   matrice codici × codici (sovrapposizione / vicinanza / stesso documento), Jaccard, layout force-directed
  Stats          statistiche delle variabili, gruppi, tabelle incrociate codici × gruppi, chi-quadrato, codici -> variabili
  VariableFilter condizioni sulle variabili (E/O) -> documenti; ultimo filtro in meta 'variable_filter'
  Dictionaries   `tokenize`, `findMatches`, `analyze` (documenti × categorie), `autocode`, `countsToVariables`,
                 import/export (Categoria/Voce in .xlsx/.csv, elenco di parole .txt)
  WordFrequencies  (`Words::`) frequenze con filtri e colonne di confronto, `keywordInContext`, liste di parole
                 escluse (it/en scritte da zero; lista del progetto in meta `word_stoplist`, prefisso `#` = "impostata"),
                 `cloudLayout` (spirale con collisioni). Funzioni pure su testi già letti: girano in un thread
  Exporters      CSV per Excel (UTF-8 BOM, ';'), HTML, import variabili CSV, tabelle/fogli .xlsx (`*Sheet`, `projectSheets`)
  Images         (`Images::`) suffissi/lettura file immagine, `crop` di un'area, miniature, PNG, `areaLabel`
  Pdfs           (`Pdfs::`) `Doc` (PDF da memoria: testo di pagina, `select` tra due punti, `textInArea`, `render`,
                 `cropArea`), `readFile`, `pageTexts/joinPages/pageOfOffset` (pagine separate da `\f`)
  Qdpx           export/import REFI-QDA 1.0 (project.qde + sources/: .txt, .pdf, immagini)
  Xlsx           lettore (fogli, stringhe condivise/inline, date dai formati) e scrittore `writeWorkbook(Sheet...)`
  Survey         import di sondaggi (.xlsx/.csv): ruoli colonna, un documento per rispondente, variabili tipizzate
  TextUtils      newline, UTF-16 <-> code point, adattamento di segmenti/link dopo modifiche, frasi/paragrafi
  DemoProject    progetto di esempio (3 interviste sul lavoro da remoto)
src/ui/     Qt Widgets (libreria statica tessera_ui; main.cpp crea solo MainWindow)
  MainWindow (menu, azioni, collega i segnali del Project ai pannelli), DocumentTree / CodeTree (caselle di
  attivazione), DocumentView + CodingStripe (+ ImageView per le immagini, PdfView per i PDF), LinksDialog, SegmentsPanel (`setRetrieval`, `setPassages`),
  CooccurrenceDialog, StatsDialog, MemoDialog (non modale, `Ui::editMemo/openMemo`, `MemoLinkPicker`, drop di
  codici/documenti), SurveyImportDialog, DictionaryDialog (non modale), MemoManager, VariablesDialog,
  WordFrequencyDialog (3 schede: frequenze, nuvola, parole in contesto; calcolo con `QtConcurrent` + barra di
  avanzamento, `Qt6::Concurrent`; il progetto non è thread-safe: i testi si leggono prima, nel thread della UI),
  SearchDialog (`searchFor`), MatrixDialog, RetrievalDialog, VariableFilterDialog, UiUtils
tests/      test_core.cpp (QtTest, logica), test_ui.cpp (widget offscreen)
```

### Concetti chiave

- **Salvataggio immediato.** Ogni metodo di `Project` scrive subito; non esiste "Salva". "Salva una copia" = `VACUUM INTO`.
- **Segnali.** Dopo ogni scrittura `Project` emette `groupsChanged`, `documentsChanged`, `documentSetsChanged`,
  `codesChanged`, `segmentsChanged(docId|0)`, `memosChanged`, `variablesChanged`, `linksChanged`,
  `dictionariesChanged`. Le viste si ricostruiscono da questi segnali e non cambiano mai il proprio stato al
  posto del Project (il drop nel `CodeTree` imposta `IgnoreAction` e chiama `moveCode`).
- **Batch.** `beginBatch()/commitBatch()` = una transazione, segnali rimandati e **un solo passo di undo**.
  Le operazioni con più scritture stanno in un batch.
- **Undo/redo.** Trigger SQLite TEMP (per connessione) scrivono in `temp.undo_log` l'istruzione inversa di ogni
  riga toccata, cascate comprese; il log si chiude in un passo in `notify()`/`commitBatch()`; il replay gira
  all'indietro con `PRAGMA defer_foreign_keys`. `setUndoLabel(tr(...))` all'inizio di ogni metodo che scrive
  (in un batch vince la prima, impostata dalla UI prima di `beginBatch`). Una tabella nuova va aggiunta a
  `installUndoTriggers()`. Solo di sessione, max 200 passi, azzerata dopo import QDPX e progetto d'esempio.
- **Schema e migrazioni.** `kSchema` è la versione 1 e non si tocca. Ogni cambio è una `Migration { toVersion,
  statements }` in `Project::builtInMigrations()` (una istruzione SQL per voce, versioni crescenti, mai
  modificare una migrazione rilasciata). `open()` rifiuta file più nuovi, salva `"<nome> (backup vN).tsra"` e applica
  le migrazioni in **una transazione** (con `foreign_key_check`), a foreign key **disattivate** e prima dei trigger
  di undo. Nei test `setMigrations()` simula versioni vecchie e future. Versione attuale: 8.
- **Segmenti generalizzati** (v6, #25): `documents.kind` (text|pdf|image|audio|video) e `segments.anchor_kind`
  (text|area|time) + `page, x1, y1, x2, y2` (area; pagina 1-based per i PDF, 0 per le immagini) e
  `t_start, t_end` (ms). I segmenti non testuali salvano `start_pos=0, end_pos=1` per rispettare il CHECK
  originale (nessuna tabella ricostruita). `segmentsForDocument/segments/allSegments` restituiscono **solo i
  segmenti di testo** salvo `includeNonText=true`: il codice basato sul testo (vista, recupero, co-occorrenze,
  export) conta su start/end reali. API: `addMediaDocument`, `addAreaSegment`, `addTimeSegment`. Le query sui
  segmenti richiedono le colonne della v6: i test con migrazioni personalizzate devono includerle (nei test la v6 è
  `builtInMigrations().at(4)`, non più `.last()`). Ancora non fatti (per #28): UI per audio/video e
  tempi, e il loro QDPX (l'export omette audio/video, l'import li conta come saltati: punto d'aggancio in `Qdpx.cpp`).
- **Documenti immagine** (v7, #27): i byte originali del file stanno in `document_data(document_id, data BLOB)`
  (tabella separata: un blob in `documents` finirebbe nel log di undo a ogni rinomina). `documentImage()` è l'unico
  punto che decodifica e applica l'orientamento EXIF (`setAutoTransform`): visualizzazione, coordinate delle aree e
  `Images::crop` usano quell'immagine raddrizzata. Aree = pixel dell'immagine, `page = 0`. `DocumentView` ha una
  modalità immagine (`isImage()`): `hasSelection/codeSelection/selectRange` lavorano sul rettangolo di `ImageView`;
  collegamenti, ricerca nel documento e modifica del testo non si applicano. `SegmentsPanel` nel recupero semplice
  include le aree (miniatura nella colonna Segmento, `segments()` restituisce solo i testi); i recuperi complessi
  e le co-occorrenze restano di solo testo. Chi mostra o esporta segmenti con `allSegments(true)` deve gestire
  `SegmentKind::Area` (niente `documentText().mid(start, ...)`). TIFF/WebP richiedono i plugin `qtiff`/`qwebp`
  (modulo Qt `qtimageformats`, assente nel Qt di questo PC: `aqt install-qt ... -m qtimageformats`); `windeployqt`
  li copia da solo se ci sono.
- **Documenti PDF** (v8, #26): come le immagini, i byte stanno in `document_data`; `documents.content` tiene il testo
  estratto all'import (pagine unite da `\f`, `Project::addPdfDocument`) e fa da cache per ricerca lessicale, frequenze e
  dizionari (la codifica automatica salta i PDF: `addSegment` rifiuta i documenti non testuali). Aree in **punti PDF**,
  origine in alto a sinistra, pagina 1-based, indipendenti dallo zoom. `segments.area_text` (v8) è il testo con cui l'area è
  stata codificata (selezione o testo nel rettangolo): lista segmenti, export e memo lo mostrano senza aprire il PDF; senza
  testo si mostra una miniatura (`Pdfs::Doc::cropArea`). `PdfView` (`QAbstractScrollArea`, rendering con `QPdfDocument`,
  cache di 12 pagine) disegna le pagine, le aree e le barre dei codici nel margine di ogni pagina; trascinare seleziona il testo
  (glifo più vicino al punto, quindi si può partire dal bianco), Alt+trascinare un rettangolo. `DocumentView` ha una modalità PDF
  (`isPdf()`, come `isImage()`): `selectRange(start,end,0)` con un offset del testo estratto porta alla pagina (hit della
  ricerca). Ctrl+F nel documento, collegamenti e modifica non valgono per i PDF. QPdfDocument: `load(QIODevice*)` è `void`
  (guardare `status()`), `getSelection` vuole che il punto di partenza sia su un glifo.
- **Set di documenti** (`document_sets`, v2): riferimenti a documenti di qualsiasi gruppo. Nel `DocumentTree`
  lo stesso documento compare più volte: l'attivazione è per documento (`syncDocumentCheckBoxes`);
  `activeDocumentIds()` legge solo i gruppi.
- **Gruppi e set in QDPX.** REFI-QDA ha solo `<Set>`: i gruppi sono set con GUID derivati (UUID v5, `groupSetGuid()`
  in `Qdpx.cpp`), ogni altro `<Set>` è un set di documenti; i file 0.1.0 si leggono come solo gruppi. I memo di
  gruppi e set sono `<Note>` elencate dal set come `<MemberNote>` (i vecchi file Tessera usavano la `Description`,
  ancora letta); i gruppi, anche vuoti, tornano tutti all'import.
- **PDF e immagini in QDPX** (#46): `PDFSource` (`path` = `sources/<guid>.pdf`, `Representation` col testo a pagine
  separate da `\f`, `PDFSelection`) e `PictureSource` (`sources/<guid>.<ext>`, `PictureSelection`). Le coordinate
  sono interi nello schema: punti PDF / pixel, origine in alto a sinistra, come in Tessera (le frazioni delle aree PDF
  si arrotondano; non verificato con file di altri programmi). Il testo di un'area PDF viaggia nel `name`
  della selezione, letto solo se `origin` è Tessera. Un PDF senza file ma col testo si importa come testo; file
  mancanti o illeggibili sono "fonti ignorate" nel rapporto, come audio/video, `Cases` e `Graphs`. Non coperti: audio/video
  e trascrizioni (#28/#29: aggiungere `AudioSource`/`VideoSource` accanto ai rami PDF/Picture), grafi (#15), casi, barra
  di avanzamento per file molto grandi, memo liberi con collegamenti (tornano come memo del primo elemento collegato).
- **Collegamenti** (`links`, v3): estremità `from`/`to` (documento + intervallo), cascata sui documenti.
  `updateDocumentText(id, testo, segmenti, link)` sposta le estremità e cancella i link assenti dalla lista.
  In QDPX: `PlainTextSelection` senza `Coding` (`linkEndGuid()`) + `<Link>` bidirezionale, commento in `name`;
  i `<Link>` tra oggetti non-testo sono ignorati e contati.
- **Memo e collegamenti** (`memo_links`, v4): bersaglio principale in `memos.target_type/target_id` (elemento,
  `project` o `free`) più altri elementi in `memo_links`. `memoFor`/`memoIdsFor` considerano entrambi;
  `setMemoLinks` li sostituisce; `removeOrphanMemos()` promuove il primo collegamento se sparisce il principale e
  cancella solo i memo senza elementi (mai liberi né del progetto). QDPX: un `<NoteRef>` per elemento verso la
  stessa `<Note>`; nota senza riferimenti = memo libero.
- **Dizionari** (v5): categorie ad albero, voci con `*` come jolly. Confronto per parole (`tokenize`: lettere,
  cifre, segni combinanti; apostrofi e trattini separano), NFC, senza maiuscole salvo opzione. Per categoria le
  occorrenze non si sovrappongono (vince la voce più lunga) e contano anche le sottocategorie. `autocode` crea i
  codici mancanti sotto un codice col nome del dizionario, in un batch. Non vanno in QDPX.
- **Posizioni nel testo** = indici UTF-16 nel testo semplice con `\n`. QDPX usa code point Unicode: convertire con
  `TextUtils` (test con emoji); in import i CRLF si normalizzano rimappando le posizioni.
- **Modifica del testo.** `DocumentView` in modifica fa diff prefisso/suffisso a ogni cambiamento e adatta
  segmenti e link in memoria; al salvataggio `updateDocumentText` scrive tutto insieme (un passo di undo).
  Durante la modifica Ctrl+Z appartiene all'editor e Annulla/Ripeti sono disattivate.

### Insidie già incontrate

- Una `QString` nulla va in SQL come NULL: `bindAll()` la converte in `''` (colonne NOT NULL).
- `QTextEdit::toPlainText()` trasforma gli spazi non separabili: in DocumentView usare `rawText()`.
- Nel corpo di un trigger SQLite i nomi di tabella non si qualificano (`temp.x`).
- `DocumentView` deve restare iscritto da solo ai segnali del Project (segmenti e link): con dati non aggiornati
  il salvataggio di una modifica al testo cancellerebbe codifiche. `addSegment`/`addLink` rifiutano posizioni fuori testo.
- Le colonne della `CodingStripe` si assegnano sui pixel (righe visive) e si ricalcolano quando il testo va a capo.
- `findChild<QLineEdit*>()` in `DocumentView` deve trovare il campo di ricerca: i widget con un QLineEdit interno (`m_pageSpin`)
  si creano dopo `buildFindBar()`.
- Nei test un PDF con testo vero si fa con `QPdfWriter` (`setResolution(72)`: unità = punti) e `QPainter::drawText`.
- `findChild<QTableWidget*>()` in un dialogo con più tabelle restituisce quella sbagliata: nei test `findChildren`.
- `QZipReader/QZipWriter` sono header privati di QtCore (`Qt6::CorePrivate`). `QZipWriter::close()` chiude il
  dispositivo: niente `QSaveFile` diretto (fatale), scrivere in un `QBuffer` e poi nel `QSaveFile`.
- **Testo dell'utente e rich text.** Tooltip, `QLabel` e `QMessageBox` interpretano come HTML un testo che
  sembra HTML (e `<img src>` può leggere `\\host\share`). Usare `Ui::plainToolTip()`, `setTextFormat(Qt::PlainText)`,
  `Ui::showInfo/showError/confirm`, `toHtmlEscaped()` per etichette e titoli di Qt Charts.
- `QString::arg()` a catena con testo dell'utente sostituisce anche i `%N` scritti dall'utente: un solo `arg()`.
- Percorsi esterni nei `.qdpx` solo con `Qdpx::isReadableSourcePath`; le strutture annidate importate hanno un
  limite di profondità (codici QDPX: 200).
- CSV: apostrofo davanti alle celle che Excel eseguirebbe come formule (`= + - @`, non i numeri); `readCsv` lo toglie.
- ODF: gli attributi hanno namespace (`attributes().value(ns, "c")`).
- XLSX lettura: una data è un numero con stile di data (`cellXfs`, non `cellStyleXfs`); nei formati le parti tra
  virgolette e `[...]` non contano, `m` accanto a `h`/`s` = minuti; seriale 60 del sistema 1900 = 29/02/1900 inesistente.
- XLSX scrittura: numero solo se C-number senza zeri iniziali ("007" resta testo), data solo se ISO dal
  01/03/1900; colonne di nomi/testo libero in `Sheet::textColumns`; tabelle con `isoDates = true` per date vere.
  `_xHHHH_` protetto con `_x005F_`; celle > 32.767 caratteri troncate (e contate per l'avviso); ordine degli
  elementi di `worksheet`/`styleSheet` imposto dallo schema (altrimenti Excel "ripara"). Verifica: aprire in Excel via COM.
- `QList::removeIf` passa un riferimento const: per modificare mentre si filtra, ciclo esplicito.
- Seguire un link può aprire un altro documento e sostituire `m_links`: copiare il link prima di `openDocument`.
- Un widget riceve un `QDropEvent` solo dopo aver accettato il `QDragEnterEvent` (nei test mandare prima l'enter).
  I campi di testo accettano già i drop come testo: serve un event filter (vedi `MemoDialog::eventFilter`).
- In un `QDialog` i `QPushButton` sono `autoDefault`: Invio in un `QLineEdit` preme il primo pulsante, e se apre
  un dialogo modale il test si appende. Togliere `autoDefault` (vedi `DictionaryDialog`).
- `QRect(topLeft, bottomRight)` è inclusivo: per un'area usare `QRect(x, y, w, h)` (vedi `Images::crop`).
- Nei test dei widget, `grab()` subito dopo un `setText` può salvare un layout vecchio: `QTest::qWait(50)` prima dello screenshot.
- moc rifiuta struct e funzioni di supporto dentro `private slots:` dei test: metterle in `private:`.
- PowerShell 5.1: `Get-Content` legge come ANSI i UTF-8 senza BOM e `Set-Content -Encoding utf8` aggiunge il BOM:
  per riscrivere un sorgente usare Edit o `[IO.File]::ReadAllText/WriteAllText`.

## Flusso di lavoro per le issue

Si lavora su `main`: niente rami né pull request.

1. Leggere la issue (`gh issue view <n>`): obiettivo, comportamento, prerequisiti, criteri di accettazione.
2. Implementare; i criteri di accettazione diventano test; aggiornare README/CLAUDE.md se cambiano
   scorciatoie, comportamenti o convenzioni.
3. Build e test verdi (vedi Comandi). Un test che fallisce non si aggira, si capisce.
4. Commit in inglese con `Closes #<n>` nel corpo, `git push`. Le issue si chiudono subito, senza attendere la
   prova manuale; nel riepilogo elencare le cose da controllare a mano nell'app vera (`--example`).
5. **Ogni issue chiusa fa una release** (sezione Rilasci), una per issue.

Roadmap = issue **#50** (area, priorità, dimensione S/M/L/XL). Etichette: `area: core | organizzazione |
analisi | collaborazione | tipi di dati | import | ai | export | app`, `priorità: alta | media | bassa`.
Di norma priorità alta, poi dimensione piccola, rispettando i prerequisiti.

### Stato delle issue

Chiuse in `main` (provate solo con test automatici, non a mano): #1, #2, #3, #4, #5, #8, #9, #12, #13, #19,
#21, #25 (modello e API), #26 (PDF a pagine e aree), #27 (immagini e aree), #46 (QDPX con PDF e immagini; audio/video e grafi no), #33, #44, #18 (senza lemmatizzazione). Il dettaglio di cosa fanno è nel `CHANGELOG.md`. Rimandato da
#19: le regole di disambiguazione.
Aggiornare questo elenco quando una issue viene chiusa.

## Rilasci

`CHANGELOG.md` si aggiorna a ogni release (una voce per versione, con commit e issue); di norma +1 versione minore.

1. `project(Tessera VERSION x.y.z)` in `CMakeLists.txt` (prima del tag: il tag deve contenere il cambio).
2. `build.cmd Release`, test, `deploy.cmd zip`.
3. Commit su `main`, tag `vx.y.z`, push del tag, poi
   `gh release create vx.y.z dist\Tessera-x.y.z-win64.zip -R lucaberto95/Tessera --title ... --notes ...`.
4. `gh release view vx.y.z -R lucaberto95/Tessera --json assets`: lo zip deve esserci.

Repository: https://github.com/lucaberto95/Tessera (privato), branch `main`.
