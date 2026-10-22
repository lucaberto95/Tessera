# Changelog

Le modifiche rilevanti di Tessera, dalla più recente. Ogni voce rimanda alla release e al commit.

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
