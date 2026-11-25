"""What the video tells. `it` is the caption, `en` its translation; keys are
pressed after the caption appears; `spot` is what to look at, in cells:
(x, y, w, h), or (text, dx, dy, w, h) from where a text is. A scene without
`open` happens in a terminal: each step types the command in `run`.

TRAILER, at the end, is the short one that plays by itself in the README."""


def R(n):
    return "<right>" * n


def D(n):
    return "<down>" * n


DATA = (57, 2, 66, 21)
CODE = (0, 2, 57, 21)
BOTTOM = (0, 24, 124, 5)
TABS = (1, 2, 75, 1)
SEQ = (0, 3, 124, 21)
LEVELS = (57, 0, 66, 1)
OTHER = (62, 2, 62, 26)   # the translation, beside the pseudocode
CALLS = (0, 2, 74, 10)

STORY = [
    {"title": "Pseudoscope", "logo": True, "sub": "Pseudocodice e progetti che prendono vita",
     "en": "Pseudocode and designs, brought to life", "seconds": 4.5},

    # ------------------------------------------------------------------
    {"title": "Il primo algoritmo", "title_en": "The first algorithm", "tag": "1 · primo anno",
     "sub": "Ordinare un array, e capire perché funziona",
     "en": "First year: sorting an array, and understanding why it works"},
    {"name": "1 · Il primo algoritmo", "open": "examples", "start": "", "steps": [
        {"it": "Si parte dai file: a destra c'è l'anteprima di quello scelto.",
         "en": "It starts from the files, with a preview of the chosen one.", "spot": (40, 2, 84, 24)},
    ]},
    {"name": "1 · Il primo algoritmo", "open": "examples/basic.pscope", "start": "<home>", "steps": [
        {"it": "La mappa del programma: ogni sottoprogramma è una scatola, con ciò che entra e ciò che esce.",
         "en": "The map: each subprogram is a box, with what goes in and what comes out.", "keys": "<down>",
         "spot": (39, 2, 85, 24)},
        {"it": "Invio scende nel dettaglio: prima il codice…", "en": "Enter goes into detail: first the code…", "keys": "<enter>"},
        {"it": "…poi la sua traduzione in C, riga per riga.", "en": "…then its translation to C, line by line.", "keys": "<enter>"},
        {"it": "Con T la stessa cosa in C++, Java, Rust, TypeScript, JavaScript e Python.",
         "en": "T shows the same in C++, Java, Rust, TypeScript, JavaScript and Python.", "keys": "TTTTTT", "pace": 1.15,
         "spot": OTHER},
        {"it": "Con r si prova questo sottoprogramma da solo: i valori si scrivono, o si generano.",
         "en": "r tries this subprogram by itself, with values typed or generated.", "keys": "r",
         "spot": (1, 5, 120, 7)},
        {"it": "Ogni → è un'istruzione. A sinistra il codice, a destra i dati mentre cambiano.",
         "en": "Each → is a statement: the code on the left, the data changing on the right.",
         "keys": "<enter>" + R(8)},
        {"it": "In basso, a parole, cosa è appena successo.", "en": "At the bottom, in words, what has just happened.",
         "keys": R(5), "spot": BOTTOM},
        {"it": "Non è chiaro? Con ← si torna indietro, quanto si vuole.",
         "en": "Not clear? ← goes back, as far as you want.", "keys": "<left>" * 5, "pace": 0.7, "spot": DATA},
        {"it": "Con v l'array diventa un grafico: si vedono i valori grandi salire in fondo.",
         "en": "v turns the array into bars: the large values rise to the end.", "keys": "v" + R(26), "pace": 0.22, "spot": DATA},
        {"it": "E con f il codice diventa il suo diagramma di attività, con il punto in cui siamo.",
         "en": "And f turns the code into its activity diagram, showing where we are.", "keys": "f" + R(6), "spot": CODE},
    ]},
    {"name": "1 · Il primo algoritmo", "open": "examples/basic.pscope", "start": "<home><down>r<enter>" + R(5), "steps": [
        {"it": "Cinque livelli, dall'esterno al dettaglio: il livello decide cosa si vede e quanto è lungo un passo.",
         "en": "Five levels: each decides what you see, and how long a step is.", "spot": LEVELS},
        {"it": "Istruzione: un'espressione diventa il suo valore, un pezzo alla volta.",
         "en": "Statement: an expression becomes its value, one piece at a time.", "keys": "<enter>" + R(5), "pace": 1.0},
        {"it": "Memoria: il C, gli indirizzi, i byte. Per chi vuole arrivare fino in fondo.",
         "en": "Memory: the C code, addresses, bytes. For those who want to go all the way.", "keys": "<enter>" + R(4), "pace": 0.8,
         "spot": (58, 2, 66, 21)},
        {"it": "Esc risale: l'astrazione si sceglie in ogni momento.", "en": "Esc comes back up: choose the abstraction at any time.",
         "keys": "<esc><esc>", "pace": 0.9},
    ]},

    # ------------------------------------------------------------------
    {"title": "Ricorsione e strutture", "title_en": "Recursion and data structures", "tag": "2 · primo e secondo anno",
     "sub": "Chi aspetta chi, e dove portano i puntatori",
     "en": "Recursion and data structures: who waits for whom, and where pointers lead"},
    {"name": "2 · Ricorsione", "open": "examples/fib.pscope", "start": "r2", "steps": [
        {"it": "Livello Programma: un passo è una chiamata o un ritorno.", "en": "Program level: a step is a call or a return.",
         "keys": R(9), "pace": 0.6},
        {"it": "A destra la pila: ogni chiamata aspetta che finisca quella sopra di lei.",
         "en": "On the right, the stack: each call waits for the one above it.", "spot": (74, 2, 50, 21)},
        {"it": "A sinistra l'albero: fib(2) viene calcolato due volte. Ecco perché è lenta.",
         "en": "On the left, the tree: fib(2) is computed twice. That is why it is slow.", "keys": R(12), "pace": 0.4,
         "spot": (0, 2, 74, 21)},
    ]},
    {"name": "2 · Strutture dati", "open": "examples/tree.pscope", "start": "r<right><right><right>3<enter>", "steps": [
        {"it": "Un albero binario di ricerca costruito dal programma: i puntatori diventano un disegno.",
         "en": "A binary search tree built by the program: pointers become a drawing.", "keys": R(60), "pace": 0.1,
         "spot": ("root  Node*", -2, 0, 64, 9)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Il tuo pseudocodice", "title_en": "Your own pseudocode", "tag": "3 · ogni scuola ha il suo",
     "sub": "PSeInt, VisuAlg, Portugol, Cambridge, con le loro librerie",
     "en": "Each school has its own: PSeInt, VisuAlg, Portugol, Cambridge, with their libraries"},
    {"name": "3 · Altri pseudocodici", "open": "tests/dialects/basico.psc", "start": "<home><down><down><enter>", "steps": [
        {"it": "Un programma scritto per PSeInt, in spagnolo. Non va convertito.",
         "en": "A program written for PSeInt, in Spanish. Nothing to convert.", "spot": (0, 2, 124, 24)},
        {"it": "Si esegue, si esplora e si traduce come tutti gli altri.",
         "en": "It runs, is explored and is translated like any other.", "keys": "r<enter>" + R(14), "pace": 0.4},
    ]},
    {"name": "3 · Le loro librerie", "open": "examples/libraries/files.pse", "start": "", "steps": [
        {"it": "Ognuno ha le sue librerie. Lo pseudocodice di Cambridge scrive un file, poi lo rilegge riga per riga.",
         "en": "Each has its libraries. Cambridge pseudocode writes a file, then reads it back.", "keys": "r" + R(16), "pace": 0.3,
         "spot": BOTTOM},
    ]},
    {"name": "3 · Le loro librerie", "open": "examples/libraries/desenho.por", "start": "r" + R(9), "steps": [
        {"it": "Portugol Studio ha una libreria grafica: con d si guarda il disegno crescere, un'istruzione alla volta.",
         "en": "Portugol Studio has a graphics library: d shows the drawing grow, statement by statement.",
         "keys": "d" + R(38), "pace": 0.13},
    ]},

    # ------------------------------------------------------------------
    {"title": "Classi ed ereditarietà", "title_en": "Classes and inheritance", "tag": "4 · programmazione a oggetti",
     "sub": "Chi estende chi, e quale metodo parte", "en": "Object-oriented programming: who extends whom, and which method runs"},
    {"name": "4 · Ereditarietà", "open": "examples/advanced/shapes.pscope", "start": "<home>", "steps": [
        {"it": "Una classe astratta, un'interfaccia, e le classi che le estendono.",
         "en": "An abstract class, an interface, and the classes that extend them.", "keys": D(8), "pace": 0.35,
         "spot": (39, 2, 85, 16)},
        {"it": "Il C non ha classi astratte. Invece di tradurre male, lo dice.",
         "en": "C has no abstract classes. Rather than translate wrongly, it says so.", "keys": "<enter><enter>", "pace": 0.8,
         "spot": OTHER},
        {"it": "Ogni linguaggio a modo suo: in C++ una classe ne estende quante vuole…",
         "en": "Each language in its own way: in C++ a class extends as many as it likes…", "keys": "T", "spot": OTHER},
        {"it": "…in Java una sola, più le interfacce…", "en": "…in Java only one, plus interfaces…", "keys": "T", "spot": OTHER},
        {"it": "…e in Rust nessuna: ci sono solo i trait, e qui non bastano.",
         "en": "…and in Rust none: there are only traits, and here they are not enough.", "keys": "T", "spot": OTHER},
        {"it": "Poi TypeScript, JavaScript e Python, con le sue classi multiple.",
         "en": "Then TypeScript, JavaScript and Python, with its multiple classes.", "keys": "TTT", "pace": 1.3, "spot": OTHER},
    ]},
    {"name": "4 · Ereditarietà", "open": "examples/advanced/shapes.pscope", "start": "r2", "steps": [
        {"it": "Quando gira, il metodo lo sceglie l'oggetto: twice() è di Shape, ma area() è quella di Rectangle.",
         "en": "At run time the object chooses: twice() is Shape's, but area() is Rectangle's.", "keys": R(11), "pace": 0.5,
         "spot": CALLS},
    ]},

    # ------------------------------------------------------------------
    {"title": "Progettare", "title_en": "Designing", "tag": "5 · ingegneria del software",
     "sub": "Un progetto si può percorrere prima di scriverlo",
     "en": "Software engineering: a design can be walked through before it is written"},
    {"name": "5 · UML", "open": "examples/uml/login_sequence.puml", "start": "<right><right>", "steps": [
        {"it": "Un diagramma di sequenza in PlantUML, percorso un messaggio alla volta.",
         "en": "A PlantUML sequence diagram, walked through one message at a time.", "keys": R(6), "pace": 0.75},
        {"it": "Tab prende l'altro ramo: e se la password fosse sbagliata?",
         "en": "Tab takes the other branch: what if the password is wrong?", "keys": "<tab>", "spot": ("the password is wrong", -2, 0, 105, 4)},
    ]},
    {"name": "5 · UML", "open": "examples/uml/order_state.puml", "steps": [
        {"it": "Un diagramma di stato: si sceglie la freccia, e lo stato cambia dal vivo.",
         "en": "A state diagram: choose an arrow, and the state changes live.", "keys": R(2) + "<down>" + R(1) + R(1) + R(1), "pace": 0.9},
        {"it": "A destra resta il cammino fatto. Valgono anche classi, attività e diagrammi di flusso.",
         "en": "The way so far stays on the right. Classes, activities and flowcharts work too.", "spot": (84, 3, 40, 22)},
    ]},
    {"name": "5 · Pseudoclassi", "open": "examples/design/shop.pscope", "start": "", "steps": [
        {"it": "Oppure pseudoclassi ancora vuote: dove c'è «...» il codice non esiste.",
         "en": "Or pseudo-classes still empty: where there is “...” there is no code yet.", "keys": "<home><down><enter>"},
        {"it": "Il progetto gira lo stesso, e mostra chi chiama chi.",
         "en": "The design runs all the same, and shows who calls whom.", "keys": "<esc><home>r1" + R(9), "pace": 0.65},
    ]},
    {"name": "5 · Piani, dati, idee", "open": "examples/diagrams/thesis_plan.puml", "steps": [
        {"it": "Un piano scritto in PlantUML è un Gantt da percorrere nel tempo: cosa è finito, cosa è in corso.",
         "en": "A plan written in PlantUML is a Gantt chart to walk through time: what is done, what is under way.",
         "keys": R(4), "pace": 1.0},
    ]},
    {"name": "5 · Piani, dati, idee", "open": "examples/diagrams/library_er.mmd", "steps": [
        {"it": "Entità e relazioni, in Mermaid: ogni legame si legge come una frase.",
         "en": "Entities and relationships, in Mermaid: each link reads as a sentence.", "keys": D(2), "pace": 1.0,
         "spot": (83, 3, 40, 9)},
    ]},
    {"name": "5 · Piani, dati, idee", "open": "examples/diagrams/algorithms_mindmap.mmd", "steps": [
        {"it": "E mappe mentali, WBS, linee del tempo, grafi di Graphviz: un ramo alla volta.",
         "en": "And mind maps, WBS, timelines, Graphviz graphs: one branch at a time.", "keys": D(10), "pace": 0.3},
    ]},

    # ------------------------------------------------------------------
    {"title": "Appunti e articoli", "title_en": "Lecture notes and papers", "tag": "6 · Markdown e LaTeX",
     "sub": "Ciò che è già scritto si apre così com'è", "en": "Markdown and LaTeX: what is already written opens as it is"},
    {"name": "6 · Markdown", "open": "examples/documents", "start": "", "steps": [
        {"it": "Gli appunti in Markdown si aprono come una cartella: dentro, i loro programmi e i loro diagrammi.",
         "en": "Markdown notes open like a folder: inside, their programs and their diagrams.", "keys": "<enter><down>",
         "pace": 0.9, "spot": (0, 2, 41, 9)},
        {"it": "Lo pseudocodice della lezione si esegue lì dov'è, senza copiarlo altrove.",
         "en": "The pseudocode of the lesson runs where it is, with no copying elsewhere.", "keys": "<enter>r" + R(14),
         "pace": 0.3},
    ]},
    {"name": "6 · LaTeX", "open": "examples/documents", "start": "<down>", "steps": [
        {"it": "Un articolo in LaTeX: l'ambiente algorithmic diventa un programma, pgfgantt un piano.",
         "en": "A LaTeX paper: the algorithmic environment becomes a program, pgfgantt a plan.", "keys": "<enter>",
         "pace": 1.0, "spot": (0, 2, 41, 6)},
        {"it": "L'algoritmo di Euclide, così com'è nell'articolo, eseguito: Gcd(48, 18) fa 6.",
         "en": "Euclid's algorithm, as it is written in the paper, executed: Gcd(48, 18) is 6.", "keys": "<enter>r2" + R(5),
         "pace": 0.6, "spot": CALLS},
    ]},

    # ------------------------------------------------------------------
    {"title": "Un sistema intero", "title_en": "A whole system", "tag": "7 · end to end",
     "sub": "Dal clic dell'utente al database, e ritorno",
     "en": "End to end: from the user's click to the database, and back"},
    {"name": "7 · End to end", "open": "examples/systems/login.pscope", "start": "<home><down><down><down><down>", "steps": [
        {"it": "Tre componenti: ognuno vive per conto suo e reagisce ai messaggi.",
         "en": "Three components: each lives on its own and reacts to messages.", "spot": (39, 2, 85, 12)},
        {"it": "Con t si lanciano tutte le prove. Invio ne mostra una mentre gira.",
         "en": "t runs all the tests. Enter shows one while it runs.", "keys": "t<enter>", "pace": 1.6},
        {"it": "Livello Sistema: il diagramma di sequenza si disegna da solo, un messaggio per passo.",
         "en": "System level: the sequence diagram draws itself, one message per step.", "keys": R(10), "pace": 0.8, "spot": SEQ, "poster": True},
        {"it": "Invio entra in ciò che il server sta facendo adesso, fino alla singola istruzione…",
         "en": "Enter goes into what the server is doing right now, down to a statement…", "keys": "<enter><enter>" + R(2), "pace": 0.9,
         "spot": CODE},
        {"it": "…e Esc riporta all'esterno, senza perdere il filo.", "en": "…and Esc goes back outside, without losing the thread.",
         "keys": "<esc><esc>", "pace": 0.8},
        {"it": "Le stesse cose, da altri punti di vista: Tab cambia diagramma.",
         "en": "The same things from other points of view: Tab changes diagram.", "keys": "<tab><tab><tab><tab>", "pace": 1.7, "spot": TABS},
        {"it": "E le verifiche: cosa doveva succedere, da un capo all'altro, e cosa è successo.",
         "en": "And the checks: what had to happen, end to end, and what did.", "keys": "<tab>g", "pace": 0.8, "spot": (1, 4, 80, 14)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Quando le cose vanno storte", "title_en": "When things go wrong", "tag": "8 · reti",
     "sub": "Un protocollo affidabile su un canale che perde pacchetti",
     "en": "Networks: a reliable protocol over a channel that loses packets"},
    {"name": "8 · Protocolli", "open": "examples/systems/stop_and_wait.pscope", "start": "t<enter>", "steps": [
        {"it": "Stop-and-wait: il canale perde un messaggio su quattro.", "en": "Stop-and-wait: the channel loses one message in four.",
         "keys": R(7), "pace": 0.6},
        {"it": "Ecco: la conferma si è persa.", "en": "There: the acknowledgement got lost.", "keys": R(2), "pace": 0.8,
         "spot": ("✗", -12, 0, 62, 1)},
        {"it": "Scade il timer, e il mittente manda di nuovo lo stesso pacchetto.",
         "en": "The timer expires, and the sender sends the same packet again.", "keys": R(4), "pace": 0.8,
         "spot": ("Timeout(1)", -24, 0, 80, 2)},
        {"it": "Componenti: i dati di ciascuno, e i messaggi mentre viaggiano.",
         "en": "Components: the data of each, and the messages as they travel.", "keys": "<tab>" + R(8), "pace": 0.6},
        {"it": "Stati: dov'è il mittente, come ci è arrivato, dove può andare.",
         "en": "States: where the sender is, how it got there, where it can go.", "keys": "<tab>" + R(8), "pace": 0.6,
         "spot": (1, 4, 60, 12)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Algoritmi avanzati", "title_en": "Advanced algorithms", "tag": "9 · anni successivi",
     "sub": "Verificare una proprietà, non solo un risultato",
     "en": "Later years: checking a property, not only a result"},
    {"name": "9 · Grafi", "open": "examples/advanced/dijkstra.pscope", "start": "", "steps": [
        {"it": "Dijkstra. Una prova verifica le distanze, l'altra una proprietà che deve valere sempre.",
         "en": "Dijkstra. One test checks distances, the other a property that must always hold.", "keys": "t<down>", "pace": 1.0,
         "spot": (0, 2, 124, 6)},
        {"it": "Se qualcosa non torna, si guarda la prova girare e si scende fino alla causa.",
         "en": "If something is wrong, watch the test run and go down to the cause.", "keys": "<enter>3" + R(8) + "u" + R(14), "pace": 0.25,
         "spot": DATA},
    ]},

    # ------------------------------------------------------------------
    {"title": "Sistemi distribuiti", "title_en": "Distributed systems", "tag": "10 · magistrale",
     "sub": "Eleggere un leader, come fa Raft",
     "en": "Master's: electing a leader, as Raft does"},
    {"name": "10 · Consenso", "open": "examples/systems/election.pscope", "start": "t<enter>", "steps": [
        {"it": "Cinque nodi. Chi non sente un leader per un po' si candida e chiede i voti.",
         "en": "Five nodes. Whoever hears no leader for a while asks for votes.", "keys": R(14), "pace": 0.25},
        {"it": "Due candidati nello stesso istante!", "en": "Two candidates at the same moment!", "keys": R(4), "pace": 0.4,
         "spot": ("◆ Candidate", -18, 0, 100, 3)},
        {"it": "Ma ogni nodo vota una volta sola per mandato: solo uno arriva alla maggioranza.",
         "en": "But each node votes once per term: only one gets a majority.", "keys": R(24), "pace": 0.22, "spot": ("◆ Leader", -18, 0, 100, 1)},
        {"it": "Il diagramma degli stati di ogni nodo, dal vivo.", "en": "The state chart of each node, live.",
         "keys": "<tab><tab>", "pace": 0.8},
        {"it": "Il diagramma dei tempi: il leader cade, e gli altri ne eleggono uno nuovo.",
         "en": "The timing diagram: the leader crashes, and the others elect a new one.", "keys": "<tab>g", "pace": 0.8,
         "spot": (0, 4, 90, 12)},
    ]},
    {"title": "E se fosse sbagliato?", "title_en": "What if it were wrong?", "tag": "un esercizio", "color": (255, 135, 120),
     "sub": "La stessa elezione, con una regola dimenticata", "en": "What if it were wrong? The same election, with a rule forgotten"},
    {"name": "10 · Trovare l'errore", "open": "examples/exercises/two_leaders.pscope", "start": "", "steps": [
        {"it": "Le prove falliscono: in un mandato ci sono due leader.", "en": "The tests fail: there are two leaders in the same term.",
         "keys": "t<down>", "pace": 1.0, "spot": (0, 2, 124, 6)},
        {"it": "Si guarda dove succede, sul diagramma dei tempi…", "en": "Let us see where it happens, on the timing diagram…",
         "keys": "<enter>" + R(53) + "<tab><tab><tab>", "pace": 0.07, "hold": 2.2, "spot": ("nodes[1]", -2, 0, 100, 9)},
        {"it": "…e poi nella sequenza: gli stessi nodi votano per tutti e due.",
         "en": "…and then in the sequence: the same nodes vote for both.", "keys": "<tab><tab><tab>", "pace": 0.6, "hold": 2.5, "spot": SEQ},
        {"it": "Manca il controllo su chi si è già votato: è la regola che rende sicuro l'algoritmo.",
         "en": "The check on past votes is missing: the rule that makes it safe.", "keys": "x<esc><home>" + "<down>" * 5 + "<enter>", "pace": 0.35,
         "spot": ("if t = term then", -10, 0, 60, 1)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Sette linguaggi", "title_en": "Seven languages", "tag": "11 · dal progetto al codice",
     "sub": "Le stesse prove, tradotte e compilate", "en": "From the design to the code: the same tests, translated and compiled"},
    {"name": "11 · Sette linguaggi", "steps": [
        {"run": "pseudoscope test examples/systems/election.pscope",
         "it": "Le prove dell'elezione, eseguite qui: non serve nessun altro strumento.",
         "en": "The tests of the election, run here: no other tool is needed."},
        {"run": "pseudoscope test --to=rust examples/systems/election.pscope",
         "it": "Le stesse, tradotte in Rust e compilate. Anche la simulazione è tradotta: l'esito è identico.",
         "en": "The same, translated to Rust and compiled. The simulation is translated too: same result."},
        {"run": "pseudoscope test --to=java examples/exercises/two_leaders.pscope",
         "it": "Anche gli errori sono gli stessi: in Java l'esercizio fallisce nello stesso punto.",
         "en": "The mistakes are the same too: in Java the exercise fails at the same point.", "hold": 2.4},
        {"run": "pseudoscope translate --to=python examples/fib.pscope", "clear": True,
         "it": "E il codice si porta via: C, C++, Java, Rust, TypeScript, JavaScript, Python.",
         "en": "And the code is yours to take away: C, C++, Java, Rust, TypeScript, JavaScript, Python.", "hold": 2.4},
    ]},

    # ------------------------------------------------------------------
    {"title": "Pseudoscope", "logo": True, "chapter": "Per cominciare", "title_en": "Getting started", "sub": "Dal primo ciclo ai sistemi distribuiti, in qualunque terminale",
     "en": "From the first loop to distributed systems, in any terminal", "seconds": 9,
     "lines": [("make && bin/pseudoscope", "apri la cartella e comincia"),
               ("pseudoscope test file.pscope", "lancia le prove"),
               ("pseudoscope run --to=java file.pscope", "tradotto, in sette linguaggi"),
               ("?", "tutti i tasti")]},
]

# Forty seconds, for those who have not decided to watch ten minutes yet.
TRAILER = [
    {"name": "Passo per passo", "open": "examples/basic.pscope", "start": "<home><down>r<enter>", "steps": [
        {"it": "Pseudocodice che gira: ogni → è un'istruzione, e con ← si torna indietro.",
         "en": "Pseudocode that runs: each → is a statement, and ← goes back.", "keys": R(12) + "<left><left><left>", "pace": 0.3,
         "seconds": 5.5, "hold": 0.6},
        {"it": "I dati mentre cambiano: qui l'array, visto come grafico.",
         "en": "The data while they change: here the array, seen as bars.", "keys": "v" + R(26), "pace": 0.14, "seconds": 5.5,
         "hold": 0.8},
    ]},
    {"name": "Sette linguaggi", "open": "examples/basic.pscope", "start": "<home><down><enter><enter>", "steps": [
        {"it": "La traduzione riga per riga: C, C++, Java, Rust, TypeScript, JavaScript, Python.",
         "en": "The translation line by line: C, C++, Java, Rust, TypeScript, JavaScript, Python.", "keys": "TTTTTT", "pace": 0.7,
         "seconds": 6.0, "hold": 0.8},
    ]},
    {"name": "Sistemi", "open": "examples/systems/election.pscope", "start": "t<enter>", "steps": [
        {"it": "Un sistema distribuito: il diagramma di sequenza si disegna da solo.",
         "en": "A distributed system: the sequence diagram draws itself.", "keys": R(30), "pace": 0.13, "seconds": 6.0, "hold": 1.0},
    ]},
    {"name": "Diagrammi", "open": "examples/diagrams/thesis_plan.puml", "steps": [
        {"it": "Diagrammi da percorrere: UML, Gantt, entità e relazioni, mappe mentali.",
         "en": "Diagrams to walk through: UML, Gantt, entities and relationships, mind maps.", "keys": R(4), "pace": 0.6,
         "seconds": 4.5, "hold": 0.8},
    ]},
    {"name": "Altri pseudocodici", "open": "examples/libraries/desenho.por", "start": "r" + R(9), "steps": [
        {"it": "Gli pseudocodici delle scuole, con le loro librerie: qui Portugol disegna.",
         "en": "The pseudocodes of schools, with their libraries: here Portugol draws.", "keys": "d" + R(38), "pace": 0.09,
         "seconds": 5.5, "hold": 1.0},
    ]},
    {"name": "Prove tradotte", "steps": [
        {"run": "pseudoscope test --to=rust examples/systems/election.pscope",
         "it": "E le prove, tradotte e compilate: stesso esito in ogni linguaggio.",
         "en": "And the tests, translated and compiled: the same result in every language.", "seconds": 5.5, "hold": 2.0},
    ]},
]
