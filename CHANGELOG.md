# Changelog

Le modifiche rilevanti di Tessera, dalla più recente. Ogni voce rimanda alla release e al commit.

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
