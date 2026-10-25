# Changelog

Le modifiche rilevanti di Tessera, dalla più recente. Ogni voce rimanda alla release e al commit.

## [Unreleased]

- Cronologia di annulla (Modifica > Cronologia di annulla...): finestra non modale con le azioni annullabili e ripetibili della sessione, aggiornata dai segnali del progetto; scegliendo un punto si annulla o si ripete fino a lì in un solo gesto. Nuove API di `Project`: `undoLabels/redoLabels`, `undoSteps/redoSteps`. Nessun cambio di schema. Non chiude #11 (cronologia salvata nel progetto).
- Mappe concettuali (#15): menu Analisi > Mappe concettuali. Un canvas con nodi (testo libero, codici, documenti, memo, segmenti) e frecce etichettate con colore, spessore e tratto; si trascinano codici e documenti dagli alberi, doppio clic apre l'elemento, i nodi di elementi eliminati restano segnati come eliminati. Modelli automatici (codice e sottocodici, codice e documenti, co-occorrenze), griglia con aggancio, allineamento, zoom, esportazione in PNG, SVG e PDF. Salvate nel progetto (schema 12, con annullamento), unite dall'unione di progetti e scambiate in QDPX come `<Graphs>` (colore sì, spessore e tratto no).
- Foglio di lavoro per le domande di ricerca (#17): menu Analisi > Domande di ricerca…, con elenco di domande (stato, descrizione, temi, conclusioni, note di lavoro) e evidenze collegate per riferimento a segmenti, codici e memo (i codici si trascinano dall'albero). Esportazione dell'intero foglio in Word, PDF o HTML come bozza dei risultati, con i testi dei segmenti citati. Schema 12 (tabelle `research_questions` e `research_evidence`, nell'annulla); l'unione di progetti porta con sé le domande e aggiunge alle domande già presenti le evidenze mancanti. Non entra nel QDPX (REFI-QDA non ha un equivalente). Nota: altri rami usano lo schema 12, da rinumerare all'integrazione.

- Analisi del sentiment (#20): menu Analisi > Analisi del sentiment. Stima la polarità (positiva, negativa, neutra) dei segmenti recuperati, delle frasi o dei documenti con un piccolo lessico originale italiano/inglese, negazioni e intensificatori; tabella ordinabile con punteggio, barra di distribuzione, filtro per polarità, invio dei passaggi al pannello segmenti e codifica automatica con i sottocodici Positivo/Negativo/Neutro (un solo passo di annullamento). Avviso: stima esplorativa da verificare. Variante AI non inclusa. Nessuna migrazione dello schema.
- Sincronizzazione del lavoro di gruppo senza server (#24, opzione 2), da Lavoro di gruppo > Sincronizzazione continua: ognuno lavora sul proprio .tsra (che non va mai messo nella cartella condivisa) e pubblica le proprie modifiche in 	essera-<id progetto>.sync/<membro>.tsrlog (JSON Lines versionato, un solo scrittore per file, scrittura atomica, righe finali di controllo). Sincronizza legge i file degli altri e applica codifiche, codici, memo, collegamenti e sintesi con un solo passo di annullamento; ricorda cosa ha già letto (idempotente, le eliminazioni locali non tornano), segnala i conflitti di testo tenendo entrambe le versioni, ignora con avviso file parziali, danneggiati, di altri progetti o di formato più recente e non applica le codifiche su documenti dal contenuto diverso. Anteprima, rilettura completa e sincronizzazione opzionale all'apertura. Non sincronizza documenti nuovi o modificati, né eliminazioni; nessuna migrazione dello schema.
- Lavoro di gruppo a cicli su cartella condivisa (#24, opzione 1): Analisi > Lavoro di gruppo. Il responsabile avvia un ciclo e Tessera scrive una copia del progetto per ogni membro (`<progetto>_ciclo<N>_<membro>.tsra`) con membro e ciclo registrati nel file; aprendo la copia l'autore delle codifiche è sempre il membro. Le copie rientrate si importano con l'unione (#22) limitata al lavoro del membro, con anteprima e annullamento; la cronologia dei cicli sta nella tabella `team_cycles` (schema 12) e il confronto tra i codificatori riusa l'accordo (#23). Nessun server.
- Trascrizioni con time stamp (#29, parte senza lettore): import di .srt/.vtt e di testi con marcatori `[hh:mm:ss]` come documenti di testo con time stamp (tabella `timestamps`, schema 12) che seguono le modifiche del testo; finestra Documenti > Time stamp della trascrizione (inserimento al cursore, modifica del tempo, spostamento di tutti, vai al testo, esporta in SRT/WebVTT/testo con marcatori); registrazione associata al documento (percorso). Il lettore integrato con salto al punto richiede Qt Multimedia e il supporto audio/video (#28), non ancora presenti.
- Topic modeling esplorativo (#38): Analisi > Topic modeling… trova i temi ricorrenti con LDA (campionamento di Gibbs, implementazione propria in `core/Topics`, seme fisso = risultato riproducibile). Unità: documenti, paragrafi o segmenti recuperati; numero di temi scelto o automatico (euristica sulla coerenza). Per ogni tema: parole caratteristiche, quota, passaggi più rappresentativi; assegnazione di ogni unità; "Crea codici dai temi" (opzionalmente codifica le unità sopra una soglia, un solo passo di annulla); esportazione xlsx/csv/html. Nessuna AI.
- Trascrizioni con time stamp (#29, parte senza lettore): import di .srt/.vtt e di testi con marcatori `[hh:mm:ss]` come documenti di testo con time stamp (tabella `timestamps`, schema 12) che seguono le modifiche del testo; finestra Documenti > Time stamp della trascrizione (inserimento al cursore, modifica del tempo, spostamento di tutti, vai al testo, esporta in SRT/WebVTT/testo con marcatori); registrazione associata al documento (percorso). Il lettore integrato è descritto nella voce seguente.

- Audio e video con lettore integrato (#28, #29): nuovo modulo Qt Multimedia (backend FFmpeg di Qt; il deploy aggiunge circa 20 MB tra `Qt6Multimedia`, FFmpeg e plugin). I file audio/video (MP3, WAV, M4A, AAC, OGG, FLAC, MP4, MOV, MKV, AVI, WebM...) si importano come documenti Audio/Video **collegati per percorso** (non incorporati: possono essere enormi); il percorso è relativo alla cartella del `.tsra` quando la registrazione sta sotto di essa, così una cartella di progetto spostata continua a funzionare, e un file spostato accanto al progetto si ritrova dal nome. Se il file manca il lettore dice "Ricollega il file…". Lettore con play/pausa (F9), salto indietro (F7) e avanti di N secondi (configurabile), velocità 0,5×-2×, linea del tempo con le codifiche come bande colorate; un intervallo si sceglie trascinando sulla linea o con "Inizio qui"/"Fine qui" e si codifica come segmento `time` (Ctrl+E, trascinando un codice sul lettore o dal pulsante). Nel pannello Segmenti gli intervalli compaiono come "00:12:30–00:13:05" con il pulsante "Riproduci"; lo stesso vale per i passaggi di una trascrizione tra due time stamp. Nelle trascrizioni con registrazione il lettore compatto sta sopra il testo: Ctrl+clic sul testo riproduce dal time stamp che lo governa, F8 inserisce un time stamp al tempo corrente nella posizione del cursore (anche mentre si modifica il testo), il testo del time stamp corrente si evidenzia durante la riproduzione e la finestra dei time stamp ha "Riproduci da qui". Esportazioni dei segmenti (tabelle e HTML) e unione di progetti gestiscono audio e video. Non ancora: forma d'onda e fotogrammi del video nella linea del tempo, export QDPX di audio/video (#46).

- Interfaccia in inglese (#49): le stringhe di `tr()` hanno la traduzione in `translations/tessera_en.ts` (lingua sorgente: italiano), compilata da `lrelease` e incorporata nell'eseguibile. La lingua si sceglie da Impostazioni > Lingua dell'interfaccia (sistema, italiano, inglese; si applica al riavvio). Anche il progetto di esempio ha una versione inglese. Per aggiornare le traduzioni: target CMake `update_translations`.
- Import da MAXQDA e NVivo tramite lo standard REFI-QDA: oltre ai progetti .qdpx si importano i libri dei codici .qdc. I file nativi (.mqda/.mx24, .nvp/.nvpx) sono riconosciuti e rifiutati con le istruzioni per esportare in REFI-QDA (lettura diretta non verificabile in clean room, vedi docs/foreign-formats.md). Rif. #51.

## [0.29.3] - 2026-10-23 — Riavvio dopo l'aggiornamento

- Lo script di aggiornamento ora individua la chiusura di Tessera filtrando per PID e nome del processo, riavvia l'app anche se l'attesa scade e la lancia dalla sua cartella. Gli errori di copia sono registrati in `update.log` nella cartella temporanea `tessera-update`.

## [0.29.2] - 2026-10-23 — Correzione dell'aggiornamento automatico

- L'aggiornamento estraeva file vuoti: lo zip creato da `Compress-Archive` (PowerShell 5.1) usa le barre rovesciate nei nomi e il lettore non trovava i dati. L'estrazione prova entrambe le grafie e rifiuta qualunque file la cui dimensione non corrisponde all'archivio.
- `deploy.cmd zip` ora usa `tools/makezip.ps1` (nomi con `/`), così anche la 0.29.0/0.29.1 già installate riescono ad aggiornarsi a questa versione. Chi ha provato l'aggiornamento da 0.29.0/0.29.1 ha un'installazione danneggiata e deve riscaricare lo zip.

## [0.29.1] - 2026-10-23 — Icona e barre

- L'icona dell'applicazione ora compare nella finestra e nella barra delle applicazioni (la finestra principale la sovrascriveva con un quadratino colorato).
- Menu «Progetto» rinominato «File», con sottomenu «Importa» ed «Esporta». Barra degli strumenti con icone di pari dimensione per tutti i pulsanti.

## [0.29.0] - 2026-10-23 — Aggiornamento automatico

- Aiuto › «Verifica aggiornamenti…» e, all'avvio, un controllo silenzioso (una richiesta all'API delle release su GitHub, con il solo User-Agent `Tessera/versione`; si spegne da Aiuto › «Cerca aggiornamenti all'avvio»). Se esiste una versione più recente compare il riepilogo con le note; «Aggiorna ora» scarica lo zip con barra di avanzamento e annullabile.
- `src/core/Updater`: confronto numerico delle versioni, scelta dell'asset `Tessera-x.y.z-win64.zip` solo se servito da github.com, controllo di dimensione e SHA-256 (se GitHub fornisce il digest), estrazione che rifiuta percorsi fuori dalla cartella, script `update.cmd` che attende la chiusura di Tessera, copia i file sopra la cartella dell'applicazione e la riavvia. Se la cartella non è scrivibile si apre la pagina della release per l'installazione manuale.
- Crediti e licenza: autore Luca Bertocchi. README: nota sul vibecoding e sugli aggiornamenti.
- Test con il server HTTP finto (controllo, download, file corrotto, errore del server) e con lo script di sostituzione. Non incluso: aggiornamento tramite installer (richiede Inno Setup), canale di anteprime.
## [0.28.0] - 2026-10-23 — Codici, sottocodici e raggruppamento con l'AI (#41)

- Menu «AI» e menu contestuali (documento, selezione, codice, albero dei codici): «Suggerisci codici» per il documento aperto o la selezione, «Suggerisci sottocodici» per un codice (guarda i suoi segmenti), «Raggruppa i codici» (primo livello o sottocodici di un codice).
- Pannello `AiCodesDialog`: mostra destinatario e cosa verrà inviato, invia solo su «Genera» (annullabile). Il risultato è una proposta: casella di spunta, nome e descrizione modificabili, per sottocodici e categorie l'elenco di cosa verrebbe spostato (ogni elemento escludibile). «Applica» crea i codici spuntati e sposta segmenti o codici in **un solo passo di annulla**; la descrizione dei nuovi codici riceve «Proposto da AI (modello X)» (disattivabile).
- `src/core/ai/AiCodes`: output strutturato (JSON Schema, `strict`), segmenti e codici numerati 1, 2, 3… nel prompt e rimappati agli id del database, numeri inventati dal modello scartati e contati, nomi già esistenti o vuoti scartati, un elemento in una sola proposta; limite di elementi e di token per richiesta con unione dei risultati per nome. Prompt in `resources/ai/`. `src/ui/AiUi`: conferma cloud e client condivisi con `AiAssistDialog`.
- Test con il server HTTP finto. Non incluso: scelta dell'AI su quali segmenti restano al codice padre (restano dove sono), colori scelti dal modello.
## [0.27.0] - 2026-10-22 — Riassunti, spiegazioni e traduzioni con l'AI (#40)
Commit `6a0680a`

- Menu «AI» e voci nei menu contestuali (documento, codice, selezione, segmento): riassunto del documento, riassunto dei segmenti di un codice (con i sottocodici; citazioni come [Intervista 3]), «Spiega» e «Traduci» per un brano o un segmento.
- Pannello del risultato: lunghezza (breve, media, lunga), lingua di output, istruzioni aggiuntive; mostra destinatario (locale o cloud) e cosa verrà inviato; la richiesta parte solo con «Genera», è annullabile e il risultato è modificabile. Niente è salvato senza un clic: «Salva come memo» (memo del documento, del codice o del segmento, autore «AI (modello X)»; se il memo esiste il testo firmato si accoda) o «Salva come sintesi» (segmenti di un solo documento); un solo passo di annulla. «Copia» e «Rigenera».
- `src/core/ai/AiAssist`: prompt scritti da zero in `resources/ai/*.txt` (lingua di output sempre esplicita; il testo è trattato come dato, non come istruzioni), testi lunghi divisi in blocchi (la citazione resta col suo testo), riassunti parziali poi fusi, traduzioni concatenate; ogni esecuzione va nel registro AI se attivo.
- Test con il server HTTP finto (nessuna rete, nessuna chiave). Non incluso: parafrasi e semplificazione, riferimenti cliccabili nei riassunti, prova con un modello locale reale.
## [0.26.0] - 2026-10-22 — Infrastruttura AI (#39)
Commit `75f1ab4`

- «Impostazioni › AI…»: provider Nessuno (predefinito), server locale compatibile OpenAI (Ollama, LM Studio, llama.cpp, vLLM) o cloud (OpenAI, Anthropic, Mistral, Google, tutti dall'endpoint compatibile OpenAI). Indirizzo, modello (elenco da `GET /models` o nome libero), chiave API, timeout, «Prova connessione» con errori chiari (server spento, indirizzo errato, chiave rifiutata, modello mancante, timeout).
- Tutela dei dati: nessuna chiamata senza un'azione dell'utente; un provider cloud si usa solo dopo la conferma, una volta per provider, di quali dati escono e verso chi; la chiave API sta solo nell'archivio credenziali di Windows (mai nel progetto, nelle impostazioni o nei messaggi); i redirect HTTP non si seguono. Blocco per amministratori: `tessera.ini` accanto all'eseguibile (`[AI] Locked=true`, `Disabled=true`, `Provider/Url/Model/Timeout`).
- `src/core/ai/`: client asincrono e annullabile (`Ai::Client`), output strutturato con JSON Schema (ripiego sul prompt se il server non supporta `response_format`, nuovo tentativo su JSON non valido), stima dei token e suddivisione in blocchi, descrizione di ciò che viene inviato, autore «AI (modello X)».
- Registro opzionale delle richieste nel progetto (`ai_log`, schema v11 con backup automatico; spento di default, non annullabile, non unito né esportato).
- Test con un server HTTP finto locale (nessuna rete, nessuna chiave reale). Non incluso: client Anthropic Messages dedicato, funzioni AI concrete (#40-#43), interfaccia per consultare il registro.
## [0.25.0] - 2026-10-22 — Icona e installer (#47)
Commit `a1b0b9f`

- Icona originale (mosaico di tessere) in SVG, con `.ico` 16-256 px per l'applicazione e per i file `.tsra`; incorporate in `Tessera.exe` (`src/tessera.rc`) e usate per la finestra e la barra delle applicazioni. `tessera_makeicon` rigenera gli `.ico` dagli SVG.
- `installer/tessera.iss` (Inno Setup): installazione per utente senza amministratore, menu Start, disinstallazione da App installate, associazione `.tsra` (doppio clic apre il progetto); `deploy.cmd installer` lo compila. La disinstallazione non tocca i progetti.
- Non incluso: firma del codice. L'installer non è stato compilato in questo ambiente (Inno Setup non installato): la release contiene solo lo ZIP.
## [0.24.0] - 2026-10-22 — Report in Word, PDF e HTML (#45)
Commit `0dc26d6`

- «Crea report…» (menu Progetto): si scelgono titolo, formato (Word `.docx`, PDF, HTML) e contenuti: indice, codebook (gerarchia, colore, descrizione, memo, frequenze e N esempi di segmenti per codice), segmenti codificati raggruppati per codice o per documento (con o senza commenti), memo, tabella delle variabili e tabelle riassuntive.
- I tre formati nascono da uno stesso elenco di blocchi (`Report::build`), quindi restano coerenti. Il DOCX usa stili Word veri (Titolo, Titolo 1-3, Citazione, Sommario) e un campo indice che Word compila all'apertura, più tabelle Word con intestazione ripetuta; il PDF è prodotto da `QTextDocument` + `QPdfWriter`.
- Non incluso: matrice codici × documenti e miniature delle aree immagine/PDF nel report.
## [0.23.0] - 2026-10-22 — Documenti con formattazione (#32)
Commit `86ffd70`

- DOCX, ODT, RTF e HTML conservano la formattazione principale: titoli (stili "Titolo/Heading", `text:h`, `<h1>`..), grassetto, corsivo, sottolineato, barrato, elenchi puntati e numerati e tabelle (righe a bande, intestazione evidenziata). `DocumentView` la mostra sopra il testo semplice.
- Vincolo rispettato: il testo semplice resta la base delle posizioni; la formattazione sta in `documents.rich_content` (schema v10, migrazione con backup) come intervalli, senza inserire caratteri nel testo, quindi segmenti, collegamenti e recupero sono identici alla versione solo testo. Le tabelle restano righe del testo (non `QTextTable`).
- La modifica del testo funziona anche sui documenti formattati: gli intervalli seguono la modifica; annulla ripristina testo e formattazione insieme. L'unione di progetti copia la formattazione.
- Corretto: i tab stop dei paragrafi DOCX (`w:tabs/w:tab`) non producono più una tabulazione nel testo.
- Non incluso: immagini incorporate, formattazione per MD/PDF, formattazione nell'export QDPX, editor rich text.

## [0.22.0] - 2026-10-22 — Accordo tra codificatori (#23)
Commit `0132262`

- «Accordo tra codificatori…» (menu Analisi): si confrontano due autori delle codifiche nello stesso progetto oppure questo progetto e un altro `.tsra` con gli stessi documenti (abbinati per uuid, poi per nome). Documenti e codici attivi restringono il confronto.
- Tre livelli: presenza del codice nel documento, frequenza nel documento, segmenti (un segmento concorda se l'altro codificatore ha un segmento dello stesso codice con sovrapposizione/unione almeno pari alla soglia, 90% di default).
- Tabella per codice e totale con accordo %, kappa di Cohen e kappa di Brennan-Prediger (`n.d.` quando indefinito, per esempio senza variazione o al livello segmenti per Cohen); elenco dei disaccordi con il testo, doppio clic per aprire il documento sul punto; esportazione in Excel, CSV e HTML.
- Non incluso: alfa di Krippendorff per più codificatori; il livello segmenti confronta solo i segmenti di testo (non aree PDF/immagine né intervalli di tempo).

## [0.21.0] - 2026-10-22 — Unione di progetti (#22)
Commit `06012e9`

- «Unisci un altro progetto…» (menu Progetto): si sceglie un `.tsra` o un `.qdpx` (letto da una copia, l'originale non si tocca nemmeno se di uno schema più vecchio) e un riepilogo a secco mostra documenti, codici, codifiche, memo, collegamenti, sintesi, variabili, set e dizionari nuovi, già presenti o in conflitto. Tutta l'unione è un solo passo di annulla.
- Riconoscimento: documenti per `uuid` e poi nome + impronta del contenuto (testo e byte di PDF/immagini), codici per `uuid` e poi percorso, segmenti per documento, codice e posizione (testo, area o tempo). Rifare l'unione non aggiunge nulla.
- Conflitti: un documento con testo diverso si importa come copia «nome (copia di file)» con le sue codifiche, oppure si salta; memo, commenti di codifica e sintesi diversi sullo stesso elemento si concatenano con un separatore con autore e data. Opzione «Importa le codifiche di» un solo autore (con i documenti e i codici necessari).
- Autori e date originali di codifiche, memo, collegamenti e sintesi si conservano (`Project::setProvenance`). Il nome utente si chiede al primo avvio.
- Non incluso: scelta elemento per elemento nel riepilogo; le variabili con valori diversi tengono quello del proprio progetto.## [0.20.0] - 2026-10-22 — Sintesi per documento × codice e tabelle riassuntive (#14)
Commit `d1efc95`

- «Griglia delle sintesi…» (menu Analisi): finestra a tre pannelli con la matrice documenti × codici (numero di segmenti, colore del codice, icona dove c'è una sintesi), i segmenti della cella selezionata (anche aree PDF/immagine) e l'editor della sintesi, che si salva da solo dopo una pausa, al cambio di cella, alla chiusura e quando la finestra perde il fuoco. Scrivere una sintesi è un solo passo di annulla (le modifiche consecutive della stessa sintesi si fondono).
- «Tabelle riassuntive…»: tabelle salvate nel progetto con nome e configurazione (documenti scelti o un set, codici, variabili); documenti come righe, variabili e codici come colonne, ogni cella con la sintesi. L'anteprima si rigenera quando le sintesi cambiano; esportazione in Excel (.xlsx), CSV e HTML.
- Schema v9 (migrazione con backup): tabelle `summaries` (UNIQUE documento × codice, CASCADE) e `summary_tables`. Eliminare un codice o un documento elimina le sue sintesi (annullabile) e la conferma dice quante sono.
- Non incluso: esportazione DOCX (#45) e QDPX (REFI-QDA non ha un elemento adatto: le sintesi non viaggiano nel file, si esportano dalle tabelle); sintesi in testo semplice, non formattato.
## [0.19.0] - 2026-10-22 — QDPX con PDF e immagini (#46)
Commit `88f5cd8`

- L'export QDPX include i PDF (`PDFSource`: file originale in `sources/<guid>.pdf`, testo a pagine come `Representation`, aree come `PDFSelection`) e le immagini (`PictureSource`, file in `sources/<guid>.<ext>`, aree come `PictureSelection`), con codici, commenti, variabili e memo. L'import li ricostruisce: round trip senza perdite, tranne le frazioni di punto delle aree PDF (lo schema vuole interi). Un PDF di un altro programma senza testo viene estratto dal file.
- I memo di gruppi e set (anche più di uno, e quelli collegati a un gruppo o a un set) sono `<Note>` elencate dal set come `<MemberNote>`; prima solo il primo viaggiava nella `Description`. I gruppi vuoti non si perdono più.
- Il rapporto d'import elenca le fonti ignorate per tipo (audio, video, immagini o PDF mancanti o illeggibili), i casi (`Cases`) e le mappe (`Graphs`).
- I collegamenti con un'estremità in un'immagine non si esportano più (avrebbero puntato a un oggetto inesistente).
- Non incluso: audio/video e trascrizioni (#28, #29, col punto d'aggancio indicato in `Qdpx.cpp`), grafi (#15), barra di avanzamento per file molto grandi, memo liberi con collegamenti (tornano come memo del primo elemento collegato). Coordinate di PDF e immagini scritte come punti/pixel con origine in alto a sinistra, non verificate con file di altri programmi.
## [0.18.0] - 2026-10-22 — PDF a pagine e codifica di aree (#26)
Commit `9254cf8`

- All'import di un PDF si sceglie «Come PDF (consigliato)» o «Solo testo». Come PDF il file originale sta nel progetto (`document_data`) e il documento si vede com'era: nuovo `PdfView` con scorrimento continuo, zoom (`Ctrl`+rotella, «Larghezza», «100%»), casella «Pagina» e rendering con QtPdf.
- Trascinando si seleziona del testo (si parte anche dal bianco: vale il glifo più vicino), con `Alt` + trascinamento un rettangolo. La selezione si codifica come il testo (`Ctrl+E`, menu contestuale, codice trascinato) e diventa un'area in punti PDF, quindi resta dov'era a ogni zoom e dopo la riapertura del progetto. Le aree si colorano col codice, il margine di ogni pagina ha le barre dei codici e un clic su un'area mostra codici e commento.
- Schema v8 (migrazione con backup): `segments.area_text`, il testo con cui è stata codificata un'area PDF. Il testo estratto all'import resta in `documents.content` (pagine separate da `\f`): ricerca lessicale, frequenze di parole e dizionari leggono i PDF, e un risultato della ricerca apre la pagina giusta; la codifica automatica salta i PDF.
- Segmenti recuperati, HTML, CSV, Excel ed etichette dei memo mostrano il testo dell'area, o il ritaglio della pagina (miniatura, PNG incorporato nell'HTML, «Copia immagine») se non c'è testo.
- Non incluso: `Ctrl+F` dentro un PDF (c'è la ricerca lessicale del progetto, che porta alla pagina); export/import QDPX dei PDF e delle loro aree (#46).

## [0.17.0] - 2026-10-22 — Documenti immagine e codifica di aree (#27)
Commit `37f5cc8`

- Si importano immagini PNG, JPG, GIF, BMP, TIFF e WebP (`Ctrl+I`): i byte originali stanno nel progetto, in una tabella `document_data` (schema v7, migrazione con backup), separata da `documents` per non copiare il blob nel log di annulla a ogni rinomina. API: `Project::addImageDocument`, `setDocumentData`/`documentData`, `documentImage`.
- L'orientamento EXIF si applica in un solo punto (`documentImage`, `QImageReader::setAutoTransform`): visualizzazione, coordinate delle aree e ritagli usano la stessa immagine raddrizzata.
- Nuovo visualizzatore (`ImageView`, `QGraphicsView`): rotella = zoom, tasto centrale = sposta, «Adatta» e «100%». Si trascina un rettangolo per selezionare un'area, che si codifica come un testo (`Ctrl+E`, `Ctrl+Maiusc+E`, menu contestuale «Codifica con», trascinando un codice dall'albero). Le aree si colorano col codice; un clic su un'area la seleziona e mostra codici, peso e commento (intestazione e barra di stato); un codice trascinato su un'area già codificata ne aggiunge un altro allo stesso rettangolo.
- Segmenti recuperati: l'area è una miniatura del ritaglio (con «Copia immagine»); i recuperi complessi restano di solo testo. Esportazioni: HTML con il ritaglio incorporato come PNG, CSV e Excel con la descrizione dell'area; il memo di un'area ha un'etichetta leggibile.
- Le immagini hanno memo e variabili come i testi e un'icona nell'albero dei documenti. Collegamenti e modifica del testo non si applicano alle immagini.
- TIFF e WebP si leggono solo se Qt ha i plugin `qtiff`/`qwebp` (modulo `qtimageformats`): l'installazione di Qt di questo PC non li ha, quindi lo zip non li contiene e importando quei file compare un messaggio chiaro. PNG, JPG, GIF e BMP funzionano. `deploy.cmd` li include da solo se i plugin sono installati.
- Non incluso: l'export/import QDPX delle immagini e delle loro aree (resta nella #46); PDF, audio e video (#26, #28).

## [0.16.0] - 2026-10-22 — Modello di segmento generalizzato (#25)
Commit `8854204`

- Schema v6 (migrazione con backup e transazione, nessuna tabella ricostruita): `documents.kind` (testo, PDF, immagine, audio, video) e ancore dei segmenti `anchor_kind` (testo, area, tempo) con `page`, `x1..y2` e `t_start`/`t_end` in ms.
- Nuova API di `Project`: `addMediaDocument`, `addAreaSegment` (PDF con pagina, immagini senza) e `addTimeSegment` (audio e video), con validazione per tipo di documento, annulla/ripeti e rimozione dei duplicati.
- I segmenti non testuali salvano `start_pos=0, end_pos=1` (il CHECK originale resta); le liste `segmentsForDocument`/`segments`/`allSegments` restituiscono solo segmenti di testo salvo `includeNonText`, quindi vista, recupero e co-occorrenze non cambiano.
- L'export QDPX omette per ora i documenti non testuali.
- Rimandato: contenuto binario dei documenti (blob o collegamento), interfaccia per aree e tempi, selezioni QDPX (#26, #27, #28, #46).

## [0.15.0] - 2026-10-22 — Frequenze di parole, nuvola di parole e parole in contesto (#18)
Commit `9d1c2cf`

- Nuova finestra «Frequenze di parole, nuvola e parole in contesto…» (menu Analisi), sui documenti, sui documenti attivi o sui segmenti recuperati.
- Frequenze: tabella con parola, frequenza, percentuale e numero di documenti, ordinabile e filtrabile per testo; opzioni di lunghezza minima, maiuscole/minuscole ed esclusione dei numeri; confronto con una colonna per gruppo di documenti, per set o per valore di una variabile; esportazione in Excel, CSV o HTML.
- Lista delle parole da escludere, salvata nel progetto e modificabile (liste di partenza in italiano e in inglese, scritte per Tessera); «Escludi dalla lista» aggiunge le parole selezionate.
- Le parole sono quelle di `Dictionaries::tokenize`: apostrofi e trattini separano le parole («dell'azienda» = «dell» + «azienda»), le emoji non sono parole.
- Nuvola di parole con le prime N parole, dimensione e colore secondo la frequenza, disposte a spirale senza sovrapposizioni; esportazione PNG e SVG.
- Parole in contesto (KWIC): per una parola o una sequenza di parole, contesto sinistro e destro allineati attorno alla parola, con il documento; il doppio clic apre il documento sull'occorrenza; esportabile.
- Da una parola della tabella: «Parole in contesto», «Cerca e codifica…» (apre la ricerca già compilata) e «Aggiungi al dizionario…» (voci di una categoria, un solo passo di annulla): era la parte rimandata dalla #19.
- Il calcolo gira in un thread (`QtConcurrent`) con barra di avanzamento; la tabella mostra al massimo 20.000 parole (l'esportazione le contiene tutte).
- Non incluso: la lemmatizzazione (la issue ammette una prima versione senza), il confronto con le parole combinate e l'albero delle parole.

## [0.14.0] - 2026-10-22 — Dizionari e codifica automatica con dizionario (#19)
Commit `5407a87`

- Nuova finestra «Dizionari e analisi del contenuto…» (menu Analisi). Un dizionario ha categorie e sottocategorie, ognuna con parole o espressioni di più parole; `*` fa da jolly dentro la parola (`lavor*` trova «lavoro», «lavoratori», «lavorare»).
- Il confronto è per parole e non distingue maiuscole e minuscole (salvo l'opzione): le espressioni ignorano punteggiatura e a capo tra le parole, e a ogni punto del testo vince la voce più lunga, senza contare due volte la stessa parola. Una categoria conta anche le voci delle sue sottocategorie.
- Le voci si aggiungono a mano (anche più voci separate da virgole), si importano da Excel/CSV (colonne Categoria e Voce, con le sottocategorie scritte come «Tema > Sottotema») o da un elenco di parole in un file di testo, e il dizionario si esporta in Excel o CSV.
- Scheda «Analisi»: tabella documenti × categorie con il numero di parole, i conteggi o la percentuale sulle parole del documento, il totale e, a scelta, le variabili dei documenti; tutti i documenti o solo quelli attivi; esportazione in Excel, CSV o HTML; doppio clic su una riga per aprire il documento.
- «Codifica automatica…»: per ogni categoria si sceglie un codice esistente o se ne crea uno con il suo nome (sotto un codice col nome del dizionario, con la stessa struttura delle categorie) e si codifica la parola, la frase o il paragrafo; tutta la codifica è un solo passo di annulla.
- «Salva come variabili»: i conteggi delle categorie selezionate (o di tutte) diventano variabili intere dei documenti.
- Nuove tabelle `dictionaries`, `dict_categories` e `dict_entries` (migrazione dello schema alla versione 5, con copia di sicurezza del file all'apertura). Il progetto di esempio contiene un piccolo dizionario.
- Non incluso: aggiungere parole dalla tabella delle frequenze (dipende dalla issue #18, ancora aperta) e le regole di disambiguazione; i dizionari non fanno parte dell'export QDPX.

## [0.13.0] - 2026-10-22 — Export Excel nativo (.xlsx) (#44)
Commit `7575a73`

- Le esportazioni in Excel producono un vero file `.xlsx`, scritto senza librerie esterne: intestazioni in grassetto con filtro automatico e prima riga bloccata, larghezze delle colonne adattate al contenuto, testo a capo per segmenti, commenti, descrizioni e memo.
- Numeri e date sono valori veri (le date si vedono nel formato del sistema); nomi e variabili di tipo testo restano testo, così «007» o un documento chiamato «2024» non cambiano.
- Il colore del codice fa da sfondo alla cella del codice (segmenti, codebook, matrici); la matrice codici × documenti e le co-occorrenze mantengono la heatmap come riempimento delle celle.
- Pannello dei segmenti: nuovo pulsante «Esporta Excel (.xlsx)» accanto al CSV, e nel menu Analisi «Esporta segmenti recuperati (Excel .xlsx)…». Codebook, variabili, memo, matrice, co-occorrenze e statistiche propongono `.xlsx` nella finestra di salvataggio; CSV e HTML restano disponibili.
- Nuovo comando «Esporta tutto il progetto in Excel…» (menu Progetto): una cartella di lavoro con i fogli Documenti, Codici, Segmenti, Variabili e Memo.
- Le celle oltre il limite di Excel di 32.767 caratteri vengono troncate con un avviso; i caratteri non ammessi da XML vengono tolti.
- Il lettore XLSX legge anche fogli diversi dal primo e decodifica le sequenze `_xHHHH_` di Excel.
- Non incluso: la heatmap come formattazione condizionale di Excel (è un riempimento statico, uguale a quello a schermo).

## [0.12.0] - 2026-10-22 — Import di dati di sondaggio da Excel/CSV (#33)
Commit `92fa2f0`

- Nuovo comando «Importa dati di sondaggio (Excel/CSV)…» nel menu Documenti: legge un questionario da `.xlsx` (primo foglio) o `.csv`, una riga per rispondente.
- Anteprima delle colonne: ognuna si segna come ID rispondente, variabile (con il tipo proposto, modificabile; le variabili già esistenti tengono il loro tipo), risposta aperta o da ignorare; i ruoli sono proposti in base a intestazioni e contenuto.
- Ogni rispondente diventa un documento in un nuovo gruppo: ogni risposta aperta è un paragrafo preceduto dalla domanda ed è codificata con un codice che ha il nome della domanda (riusato se esiste già). Le altre colonne diventano variabili tipizzate del documento.
- Le risposte vuote si saltano, i rispondenti senza risposte aperte non creano documenti; i valori non compatibili con il tipo sono segnalati. Tutto l'import è un solo passo di annulla.
- Nuovo lettore XLSX senza librerie esterne: stringhe condivise e inline, numeri, valori sì/no, date e ore dai formati numerici (anche il sistema di date 1904).
- Non incluso: la modalità «un documento per domanda» proposta nella issue come alternativa.

## [0.11.0] - 2026-10-22 — Memo collegati a più elementi (#9)
Commit `fad39e0`

- Un memo si può collegare a più codici, documenti, segmenti, gruppi e set: la finestra del memo ha la sezione «Collegato a» con «Aggiungi…» (selettore con filtro) e «Rimuovi»; codici e documenti trascinati dagli alberi sulla finestra vengono collegati.
- La finestra del memo non è più modale: si può continuare a lavorare (e trascinare) mentre è aperta; testo e collegamenti si salvano insieme, con un solo passo di annulla.
- L'icona del memo compare su tutti gli elementi collegati, e «Memo…» su ognuno di essi apre lo stesso memo.
- Eliminare un elemento collegato non elimina il memo finché ne resta almeno un altro.
- «Nuovo memo libero…» in *Tutti i memo* crea un memo senza bersaglio principale, distinto dal memo del progetto; la colonna «Collegato a» elenca tutti gli elementi del memo.
- Nuova tabella `memo_links` (migrazione dello schema alla versione 4, con copia di sicurezza del file all'apertura).
- QDPX: un memo collegato a più oggetti diventa una nota con un `<NoteRef>` per ogni oggetto, e all'import più riferimenti alla stessa nota danno un solo memo collegato; le note senza riferimenti diventano memo liberi.
- Nel progetto di esempio il memo di «Isolamento» è collegato anche all'intervista di Marco.

## [0.10.0] - 2026-10-22 — Collegamenti tra brani dei documenti (#8)
Commit `e0199f8`

- Collegamenti ipertestuali tra due brani, nello stesso documento o in documenti diversi: «Inizia collegamento» (`Ctrl+L`) sulla selezione, poi «Completa collegamento» (`Ctrl+Shift+L`) sulla destinazione, anche dal menu contestuale.
- Il testo collegato è blu e sottolineato; `Ctrl+clic` (o il menu contestuale) apre l'altra estremità, in entrambe le direzioni. Il tooltip mostra la destinazione e il commento.
- Finestra *Collegamenti…* (menu Documenti): tutti i link con le due estremità, commento, autore e data; doppio clic per aprire un'estremità, commento modificabile, eliminazione (annullabile).
- Quando si modifica il testo di un documento le estremità si spostano come le codifiche; un link il cui testo viene cancellato del tutto sparisce.
- Nuova tabella `links` (migrazione dello schema alla versione 3, con copia di sicurezza del file all'apertura).
- QDPX: i collegamenti vengono esportati (`<Links>` tra selezioni di testo senza codifica) e reimportati; i collegamenti tra altri oggetti di altri programmi sono ignorati e segnalati.
- Il progetto di esempio contiene un collegamento.

## [0.9.1] - 2026-10-22 — Correzioni di sicurezza e bachi
Commit `a918403`

Sicurezza:
- Import QDPX: rifiutati i percorsi di rete (`\\server\...`) delle fonti esterne fuori dalla cartella del file, che su Windows inviano l'hash NTLM dell'utente; limite di 200 livelli per i codici annidati.
- Nomi, testi e valori importati sono mostrati come testo semplice in tooltip, etichette e messaggi, ed escapati nei grafici: come HTML potevano far caricare file da `<img src>`.
- Export CSV: le celle che Excel eseguirebbe come formule (`= + - @`) hanno un apostrofo davanti.
- I trigger di annulla/ripeti quotano i nomi delle colonne letti dal file di progetto.
- Import ODT: limite al numero di spazi ripetuti.

Bachi:
- Salvando il testo modificato di un documento non si perdono più le codifiche aggiunte nel frattempo da altre parti (codifica automatica, pannello Segmenti).
- I segmenti fuori dal testo sono rifiutati; la codifica automatica salta i risultati di una ricerca diventati vecchi.
- Import ODT: gli spazi ripetuti (`text:c`) non sono più ridotti a uno.
- Export QDPX atomico (il file esistente si perde solo a scrittura riuscita); import con GUID ripetuti o posizioni negative gestito.
- I nomi che contengono `%1`, `%2`… non vengono più alterati nell'export HTML, nei messaggi e in *Codici in variabili*.

## [0.9.0] - 2026-10-22 — Statistiche e tabelle incrociate (#21)
Commit `2b9fcb9`, `5ae8955`

- Nuovo modulo `Stats`: statistiche descrittive delle variabili (frequenze, n, media, mediana, deviazione standard, minimo e massimo; vuoti e non validi contati a parte).
- Tabella incrociata codici × valori di una variabile (anche in classi numeriche), gruppi o set di documenti; segmenti o documenti con il codice, in assoluto o in % di colonna/riga; riga *Totale documenti*.
- Chi-quadrato con p-value sotto la tabella, con avvisi sulle frequenze attese basse e sui gruppi sovrapposti.
- Grafico a barre (variabili categoriali) o istogramma (numeriche) per la variabile scelta.
- *Codici in variabili*: una variabile intera per codice con il numero di segmenti (o 0/1) per documento, in un solo passo di annulla.
- Finestra *Statistiche e tabelle incrociate…* (menu Analisi) con esportazione CSV/HTML.

## [0.8.0] - 2026-10-22 — Co-occorrenze dei codici (#13)
Commit `4ea4eba`

- Nuovo modulo `Cooccurrence`: matrice codici × codici con criterio di sovrapposizione dei segmenti, vicinanza (paragrafi o caratteri) o stesso documento; valori come numero di co-occorrenze o indice di Jaccard; diagonale = frequenze.
- Finestra *Co-occorrenze dei codici…* (menu Analisi) con heatmap ed esportazione CSV/HTML. Il doppio clic su una cella mostra i passaggi nel pannello Segmenti.
- Mappa dei codici (layout force-directed riproducibile) con soglia sugli archi ed esportazione PNG/SVG.

## [0.7.0] - 2026-10-22 — Attivazione per valori delle variabili (#3)
Commit `71fb2ed`

- Nuovo `VariableFilter`: condizioni tipizzate sulle variabili dei documenti, combinabili con E / O.
- Finestra *Attiva per variabili…* (menu Documenti/Analisi) con anteprima dei documenti corrispondenti; permette di attivarli o salvarli come set di documenti.
- L'ultimo filtro è ricordato nel progetto e non è un passo di annulla.

## [0.6.0] - 2026-10-22 — Set di documenti (#2)
Commit `78cce94`

- Set di documenti con nome, con riferimenti a documenti di qualsiasi gruppo (un documento può stare in più set). Migrazione dello schema v2.
- Mostrati sotto i gruppi nell'albero dei documenti, con attivazione sincronizzata, trascinamento, menu contestuale e memo.
- QDPX: i gruppi sono esportati come `<Set>` con GUID derivati e tornano gruppi all'import; gli altri set diventano set di documenti. I memo dei gruppi non sono più duplicati come note di progetto.

## [0.5.0] - 2026-10-22 — Recupero complesso dei segmenti (#12)
Commit `3a1de24`

- Nuovo modulo `Retrieval` con nove modalità su due liste di codici (A, B): intersezione (tutti / almeno n), sovrapposizione, solo un codice, solo questo codice, se dentro, se fuori, vicino a, seguito da; distanze in paragrafi o caratteri.
- Finestra *Recupero complesso* (menu Analisi, Ctrl+Maiusc+R) con conteggio dei risultati in tempo reale.
- Il pannello Segmenti mostra i risultati, permette di tornare al recupero semplice e di codificare tutti i risultati con un nuovo codice in un solo passo di annulla.

## [0.4.0] - 2026-10-22 — Import variabili da CSV con tipo (#4)
Commit `7defbd0`

- Le colonne del CSV delle variabili sono tipizzate (intero, decimale, sì/no, data) invece di essere sempre testo.
- Anteprima con il tipo proposto per colonna (modificabile per le variabili nuove), righe senza documento corrispondente e valori incompatibili con una variabile esistente.
- Valori normalizzati al salvataggio; l'intero import è un solo passo di annulla/ripeti.

## [0.3.0] - 2026-10-22 — Ricerca nel documento (#5)
Commit `660c568`

- Barra di ricerca per il documento aperto (Ctrl+F), con F3 / Maiusc+F3, maiuscole/minuscole e parola intera; posizioni corrette anche con emoji.
- **Cambio scorciatoia:** la ricerca lessicale sul progetto passa a Ctrl+Maiusc+F.
- Corretto: `MainWindow` scollega i segnali dei figli alla distruzione.

## [0.2.0] - 2026-10-22 — Migrazioni dello schema (#1)
Commit `daf1514`

- Lo schema del progetto evolve tramite migrazioni ordinate (`kSchema` resta la versione 1).
- Aprendo un progetto più vecchio viene salvata una copia `<nome> (backup vN).tsra` e le migrazioni mancanti sono applicate in un'unica transazione, con controllo delle chiavi esterne.
- I progetti creati da una versione più recente vengono rifiutati.

## [0.1.0] - 2026-10-21 — Prima versione
Commit `d0e5340`

- App desktop Windows per l'analisi qualitativa dei dati: documenti, codici gerarchici, codifica, memo, variabili, ricerca, matrici, import/export REFI-QDA, annulla/ripeti.

[0.7.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.7.0
[0.6.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.6.0
[0.5.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.5.0
[0.4.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.4.0
[0.3.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.3.0
[0.2.0]: https://github.com/lucaberto95/Tessera/releases/tag/v0.2.0
