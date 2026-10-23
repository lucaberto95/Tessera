# Tessera

**Tessera** è un software per Windows dedicato all'analisi qualitativa dei dati: importi interviste,
documenti, PDF e immagini, costruisci un sistema di codici, codifichi i brani, scrivi memo e confronti
i risultati tra documenti, gruppi e persone. Lo scambio con altri software di analisi qualitativa
avviene tramite lo standard aperto **REFI-QDA (.qdpx)**.

![Tessera](docs/screenshot.png)

## Download

Scarica `Tessera-x.y.z-win64.zip` dalla pagina [Releases](https://github.com/lucaberto95/Tessera/releases),
estrai la cartella e avvia `Tessera.exe`: non richiede installazione. Se la release lo include, c'è anche
l'installer `Tessera-x.y.z-setup.exe`, che installa per l'utente corrente senza diritti di amministratore,
crea il collegamento nel menu Start e associa i file `.tsra` (doppio clic = apre il progetto).

Per provarlo subito: **Progetto → Crea progetto di esempio…** crea tre interviste sul lavoro da remoto,
già codificate, con variabili e memo.

## Come si lavora

Ogni progetto è un singolo file `.tsra`, salvato automaticamente a ogni modifica: non c'è «Salva».
`Ctrl+Z` / `Ctrl+Y` annullano e ripetono qualsiasi operazione della sessione, anche eliminazioni,
importazioni e codifiche automatiche. «Salva una copia come…» duplica il progetto.

### Documenti e dati

- **Testi**: importa TXT, Markdown, DOCX, ODT, RTF, PDF e HTML, oppure incolla il testo. DOCX, ODT, RTF e HTML
  mantengono titoli, grassetto, corsivo, sottolineato, barrato, elenchi e tabelle. Il testo si può modificare:
  le codifiche si adattano da sole.
- **Gruppi e set**: i documenti si organizzano in gruppi; i set raccolgono documenti di gruppi diversi e si
  attivano con un clic.
- **PDF**: si leggono con le pagine originali; trascinando si seleziona del testo, con `Alt` + trascinamento un
  rettangolo (figure, tabelle). La selezione si codifica come un testo.
- **Immagini**: PNG, JPG, GIF, BMP (e TIFF, WebP se disponibili). Si selezionano e si codificano le aree.
- **Sondaggi**: da Excel o CSV, una riga per rispondente. Ogni colonna diventa ID, variabile, risposta aperta
  o viene ignorata; ogni rispondente diventa un documento.
- **Variabili**: tipizzate (testo, intero, decimale, sì/no, data), modificabili in tabella, con import CSV.
  «Attiva per variabili…» attiva i documenti che soddisfano condizioni (es. Genere = F E Età ≥ 40).
- **Collegamenti**: collegano un brano a un altro, anche tra documenti diversi (`Ctrl+L`, poi `Ctrl+Shift+L`
  sulla destinazione; `Ctrl+clic` per seguirli).

### Codifica

- **Codici** gerarchici con colore, descrizione e memo, riorganizzabili con il trascinamento; le frequenze
  sono sempre visibili.
- **Codificare**: seleziona il testo (o l'area) e trascinaci sopra un codice, oppure `Ctrl+E` (codice
  selezionato), `Ctrl+Shift+E` (nuovo codice) o il menu contestuale. Una barra laterale mostra le
  parentesi dei codici; ogni segmento può avere commento e peso.
- **Memo** su progetto, gruppi, documenti, codici e segmenti, oppure liberi. Un memo si può collegare a più
  elementi e compare su ognuno.
- **Recupero dei segmenti**: attiva documenti e codici con le caselle di spunta e il pannello in basso
  raccoglie i segmenti corrispondenti, con filtro ed esportazione.

### Analisi

- **Ricerca lessicale** (più termini, parole intere, espressioni regolari) con codifica automatica di parola,
  frase o paragrafo.
- **Matrici e statistiche**: codici × documenti con heatmap, co-occorrenze, frequenze, tabelle incrociate
  per gruppo o variabile.
- **Parole**: frequenze con lista di parole escluse e confronto tra gruppi, nuvola di parole, parole in
  contesto.
- **Dizionari e analisi del contenuto**: categorie con parole, espressioni e jolly (`lavor*`); conteggi per
  documento, codifica automatica per categoria, conteggi salvabili come variabili.
- **Sintesi**: griglia documenti × codici con il testo di ogni cella accanto all'editor della sintesi, e
  tabelle riassuntive sempre aggiornate.
- **Accordo tra codificatori**: confronto fra due autori o fra due progetti, a livello di presenza, frequenza e
  segmenti, con kappa di Cohen, kappa di Brennan-Prediger e l'elenco dei disaccordi.

### Lavoro di gruppo

«Unisci un altro progetto…» (menu Progetto) aggiunge il lavoro di un'altra copia (`.tsra` o `.qdpx`) senza
duplicare nulla. Prima di confermare c'è un riepilogo; si può importare solo il lavoro di un autore, i documenti
in conflitto si importano come copia o si saltano, e i memo in conflitto si concatenano. Si annulla con un solo
`Ctrl+Z`.

### Esportazione

- **Report** per una tesi: copertina, indice, codebook, segmenti codificati, memo, variabili e tabelle
  riassuntive, in Word (.docx, con stili veri e indice automatico), PDF o HTML.
- **Excel**: segmenti, codebook, variabili, memo, matrici e statistiche in `.xlsx` nativo (anche CSV);
  «Esporta tutto il progetto in Excel…» crea un foglio per ciascuna parte.
- **REFI-QDA (.qdpx)**: testi, PDF, immagini, codici, codifiche, memo, variabili, gruppi e collegamenti.

### Intelligenza artificiale (facoltativa)

Spenta di default. In **Impostazioni › AI…** si sceglie un server locale (Ollama, LM Studio e simili: i dati
restano sul computer) oppure un provider cloud (OpenAI, Anthropic, Mistral, Google), solo dopo una conferma
esplicita su quali dati escono. La chiave API sta nell'archivio credenziali di Windows, mai nel progetto.

- **Riassunti, spiegazioni, traduzioni** di documenti, segmenti e brani.
- **Suggerimenti di codici**: codici per un documento o una selezione, sottocodici per un codice troppo ampio,
  raggruppamento dei codici in categorie.

Nulla parte senza un'azione dell'utente: ogni risultato è una proposta da rivedere, e i codici o i memo
creati portano l'indicazione «AI (modello X)» e si annullano con `Ctrl+Z`.

## Scorciatoie

| Tasti | Azione |
|---|---|
| `Ctrl+N` / `Ctrl+O` | Nuovo progetto / apri progetto |
| `Ctrl+I` | Importa documenti |
| `Ctrl+K` | Nuovo codice |
| `Ctrl+E` | Codifica la selezione con il codice selezionato |
| `Ctrl+Shift+E` | Codifica la selezione con un nuovo codice |
| `Ctrl+L` / `Ctrl+Shift+L` | Inizia / completa un collegamento dalla selezione |
| `Ctrl+clic` | Su un testo collegato: apre l'altra estremità |
| `Ctrl+Z` / `Ctrl+Y` | Annulla / ripeti |
| `Ctrl+F` | Cerca nel documento aperto (`F3` / `Maiusc+F3` per scorrere, `Esc` per chiudere) |
| `Ctrl+Shift+F` | Ricerca lessicale su tutto il progetto |
| `Ctrl+M` | Matrice codici × documenti |
| `Ctrl+Shift+M` | Tutti i memo |
| `Ctrl+rotella` | Zoom del testo e dei PDF |
| `Alt` + trascinamento | In un PDF: seleziona un rettangolo da codificare |

## Limiti attuali

- Si codificano testi e aree di immagini e PDF; audio e video non sono ancora supportati.
- Nei PDF `Ctrl+F` non è ancora disponibile: si usa la ricerca lessicale del progetto, che porta alla pagina.
- La cronologia di annullamento vale per la sessione e si azzera chiudendo il progetto.
