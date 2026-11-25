<img src="docs/logo.svg" alt="" width="96" align="right">

# Pseudoscope

*[English](README.md) · [Español](README.es.md) · [Português](README.pt.md)*

Pseudocodice e progetti che prendono vita nel terminale. Si scrive un algoritmo, delle
classi, un sistema distribuito o un diagramma; lo si guarda girare passo per passo,
avanti e indietro, dall'esterno o in ogni dettaglio; lo si mette alla prova; lo si traduce
in C, C++, Java, Rust, TypeScript, JavaScript o Python.

![Pseudoscope in quarantacinque secondi](docs/demo/preview.gif)

## Che cosa fa

- **Esegue lo pseudocodice passo per passo**, avanti e indietro, a cinque livelli: dal
  diagramma di sequenza di un intero sistema fino ai byte in memoria.
- **Legge lo pseudocodice che c'è già**: il P-like in tutte le sue grafie, PSeInt,
  VisuAlg, Portugol Studio, Cambridge e il libro di Cormen, con le loro librerie per testi, numeri,
  file e disegni.
- **Classi ed ereditarietà**: classi astratte, interfacce, più classi base; il metodo lo
  sceglie l'oggetto.
- **Sistemi**: componenti che si scambiano messaggi su collegamenti che li ritardano e
  li perdono, e prove che dicono che cosa deve succedere da un capo all'altro.
- **Diagrammi da percorrere**: UML (sequenza, stati, attività, classi, tempi), Gantt,
  entità e relazioni, mappe mentali, WBS, linee del tempo, in PlantUML, Mermaid e
  Graphviz.
- **Documenti**: i programmi e i diagrammi dentro appunti in Markdown e articoli in
  LaTeX si aprono lì dove sono.
- **Sette linguaggi**: C, C++, Java, Rust, TypeScript, JavaScript e Python, prove e
  simulazione comprese; ciò che un linguaggio non sa dire viene rifiutato spiegando
  perché.
- **Nella tua lingua**: interfaccia, errori e aiuto in inglese, italiano, spagnolo e
  portoghese.

È nato come un progetto del primo anno per eseguire il P-like, lo pseudocodice usato a
lezione. Quello pseudocodice non ha mai avuto una sintassi fissa, quindi si
accettano tutte le grafie: `A[i,j]`, `A[i][j]` o `A(i,j)`, `and`, `.AND.` o `&&`, `end f`
o `endfunction`, e così via.

## Compilare

Servono un compilatore C11, make e un sistema POSIX. Per i test anche Python 3.

```sh
git clone https://github.com/GritHat/pseudoscope
cd pseudoscope
make                 # bin/pseudoscope
make install         # facoltativo, PREFIX=/usr/local
```

## Cominciare

```sh
bin/pseudoscope                           # la cartella corrente
bin/pseudoscope ui examples/sorting.pscope
tools/pseudoscope-window examples         # in una finestra nuova, con qualsiasi terminale
```

I programmi finiscono in `.pscope` (`.plike` funziona ancora), o in niente: un file è ciò
che dicono le sue prime righe.

Invio scende nel dettaglio, Esc risale, `r` esegue, `t` lancia le prove, `?` elenca i
tasti. Funziona in kitty, GNOME Terminal, Ptyxis, Konsole, xterm e simili, in tmux e
screen, sulla console di Linux, e anche senza colori o senza Unicode: vedi
[docs/interface.md](docs/interface.md).

Parla inglese, italiano, spagnolo e portoghese: la lingua del sistema, oppure `--lang=it`, oppure
`PSEUDOSCOPE_LANG=es`. Per aggiungerne un'altra basta un file: vedi
[docs/localization.md](docs/localization.md).

## Un programma, cinque livelli

Il livello decide che cosa si vede e quanto è lungo un passo.

| Livello | Si vede | Un passo è |
|---|---|---|
| Sistema | diagrammi di sequenza, di stato, dei tempi e altri, dal vivo | un messaggio |
| Programma | le chiamate, e chi aspetta chi | una chiamata o un ritorno |
| Funzione | il codice, o il suo diagramma di attività, e i dati: array, liste, alberi | un'istruzione |
| Istruzione | un'espressione che diventa il suo valore | un'operazione |
| Memoria | il codice C, gli indirizzi, i byte | una lettura o una scrittura |

Ogni passo si può annullare. Ogni sottoprogramma si può provare da solo, con valori
scritti a mano o generati. Ogni accesso è controllato, e gli errori dicono che cosa è
successo.

## Dal primo programma ai sistemi distribuiti

| | Prova | Guarda |
|---|---|---|
| Ordinamento | `examples/sorting.pscope` | livello Funzione, `v` per le barre, ← per tornare indietro |
| Ricorsione | `examples/fib.pscope` | livello Programma |
| Liste e alberi | `examples/list.pscope`, `tree.pscope` | i puntatori come frecce |
| Grafi | `examples/advanced/dijkstra.pscope` | le prove (`t`), che verificano anche una proprietà |
| Classi | `examples/advanced/stack_class.pscope` | Oggetti |
| Ereditarietà | `examples/advanced/shapes.pscope` | `T`: le stesse classi in ogni linguaggio |
| Un progetto senza codice | `examples/design/shop.pscope` | Sequenza |
| Un protocollo | `examples/systems/stop_and_wait.pscope` | che cosa succede quando i pacchetti si perdono |
| End to end | `examples/systems/login.pscope` | una prova, mentre gira |
| Consenso | `examples/systems/election.pscope` | Tempi: chi comanda, e quando |
| Trova l'errore | `examples/exercises/two_leaders.pscope` | quale regola manca? Le prove lo mostrano |
| UML | `examples/uml` | diagrammi da percorrere |
| Piani, entità, mappe mentali | `examples/diagrams` | un Gantt giorno per giorno |
| Appunti e articoli | `examples/documents` | Markdown e LaTeX, aperti come cartelle |
| File e disegni | `examples/libraries` | `d`: ciò che disegna un programma Portugol |
| Come nel libro di Cormen | `examples/cormen` | `r` su una procedura: non si dichiara niente |

I componenti si scambiano messaggi su collegamenti che li ritardano e li perdono; le prove
dicono che cosa deve succedere da un capo all'altro:

```pascal
test "the right password logs in"
begin
    step "the user types the password"
    send Click(2, 2222) to client
    run
    expect client -> server: Login
    expect server -> client: Welcome
    check client.state = LoggedIn
end test
```

## Sette linguaggi

```sh
pseudoscope translate --to=java program.pscope     # oppure -o Program.java
pseudoscope run --to=python program.pscope
pseudoscope test --to=rust system.pscope           # le prove, tradotte
```

| | Le classi estendono | |
|---|---|---|
| C | niente | `float`, puntatori, controllo degli indici con `-b` |
| C++ | più classi | riferimenti, `std::vector` |
| Java | una classe, e interfacce | ciò che un sottoprogramma modifica torna in un `record` |
| Rust | interfacce (trait) | tuple; `unsafe` solo con variabili globali e puntatori |
| TypeScript, JavaScript | una classe, e interfacce | supera `tsc --strict` |
| Python | più classi | annotazioni di tipo, `abc` |

Ciò che un linguaggio non sa dire viene rifiutato spiegando perché, non scritto
sbagliato. Si traducono anche componenti e prove, simulazione compresa: una prova dà lo
stesso esito in tutti e sette. `make targets` lo verifica, programma per programma.
Vedi [docs/translation.md](docs/translation.md).

## Riga di comando

```sh
pseudoscope exec program.pscope            # esegue, senza compilatore C
pseudoscope test program.pscope            # lancia le sue prove
pseudoscope program.pscope -o program.c    # traduce in C (o .cpp .java .rs .ts .js .py)
pseudoscope run program.pscope             # compila ed esegue
pseudoscope build program.pscope -o program
pseudoscope check program.psc             # segnala solo gli errori
pseudoscope ast diagram.puml              # che cosa ha capito (--dot per Graphviz)
```

`exec`, `run` e `build` partono da una procedura chiamata `main`, senza parametri. `-`
legge lo standard input. `pseudoscope --help` elenca le opzioni; queste scelgono la grafia di P-like:

| Opzione | Valori | Predefinito |
|---|---|---|
| `-a`, `--assignment` | `colon-equals`, `equals` | `colon-equals` |
| `-i`, `--indexing` | `one`, `zero` | `one` |
| `-p`, `--params` | `decl`, `body`, `mixed` | `mixed` |
| `--operators` | `standard`, `dotted`, `mixed` | `mixed` |
| `-m`, `--mixed-arrays` | `true`, `false` | `true` |

`-b` aggiunge il controllo degli indici al programma generato.

## Limiti

File, disegni e le altre funzioni di libreria degli pseudocodici si eseguono qui e non si
traducono. I puntatori a valori semplici esistono solo in C, C++ e Rust. Non si leggono
`algorithm2e` né tastiera, mouse e rete di Portugol Studio. Ogni documento dice che
cos'altro manca.

## Documentazione

In inglese:

- [L'interfaccia](docs/interface.md)
- [Il linguaggio](docs/language.md)
- [Sistemi: classi, componenti, prove](docs/systems.md)
- [Tradurre: sette linguaggi](docs/translation.md)
- [Altri pseudocodici](docs/dialects.md)
- [Diagrammi e documenti](docs/diagrams.md)
- [Le lingue: inglese, italiano, spagnolo, portoghese, e come aggiungerne una](docs/localization.md)
- [Come funziona](docs/design.md)

## Test

```sh
make test       # unità, end to end, schermate, cataloghi, ingombri, sintassi
make sanitize   # gli stessi, con il programma sotto ASan e UBSan
make targets    # ogni programma in ogni linguaggio di cui ci sono gli strumenti
make fuzz       # programmi e diagrammi alterati, tasti a caso
```

## Il video

`tools/demo/make.py` riprende la vera interfaccia, guidata dai tasti scritti in
`tools/demo/story.py`, e i veri comandi: `--preview` fa quello breve qui sopra. La musica
la scrive nota per nota `tools/demo/music.py`, quindi non è di nessun altro, e
`tools/demo/logo.py` disegna il simbolo. Il video intero non sta nel repository.

## Licenza

MIT
