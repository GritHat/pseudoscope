"""What the video tells. `it` is the caption, `en` its translation; keys are
pressed after the caption appears; `spot` is what to look at, in cells:
(x, y, w, h), or (text, dx, dy, w, h) from where a text is."""


def R(n):
    return "<right>" * n


DATA = (57, 2, 66, 21)
CODE = (0, 2, 57, 21)
BOTTOM = (0, 24, 124, 5)
TABS = (1, 2, 75, 1)
SEQ = (0, 3, 124, 21)
LEVELS = (57, 0, 66, 1)

STORY = [
    {"title": "Pseudoscope", "sub": "Pseudocodice e progetti che prendono vita",
     "en": "Pseudocode and designs, brought to life", "seconds": 4.5},

    # ------------------------------------------------------------------
    {"title": "Il primo algoritmo", "tag": "1 · primo anno", "sub": "Ordinare un array, e capire perché funziona",
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
    {"title": "Ricorsione e strutture", "tag": "2 · primo e secondo anno", "sub": "Chi aspetta chi, e dove portano i puntatori",
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
    {"title": "Il tuo pseudocodice", "tag": "3 · ogni scuola ha il suo", "sub": "PSeInt, VisuAlg, Portugol, Cambridge",
     "en": "Each school has its own: PSeInt, VisuAlg, Portugol, Cambridge"},
    {"name": "3 · Altri pseudocodici", "open": "tests/dialects/basico.psc", "start": "<home><down><down><enter>", "steps": [
        {"it": "Un programma scritto per PSeInt, in spagnolo. Non va convertito.",
         "en": "A program written for PSeInt, in Spanish. Nothing to convert.", "spot": (0, 2, 124, 24)},
        {"it": "Si esegue e si esplora come tutti gli altri, e si traduce in C.",
         "en": "It runs and is explored like any other, and translates to C.", "keys": "r<enter>" + R(14), "pace": 0.4},
    ]},

    # ------------------------------------------------------------------
    {"title": "Progettare", "tag": "4 · ingegneria del software", "sub": "Un progetto si può percorrere prima di scriverlo",
     "en": "Software engineering: a design can be walked through before it is written"},
    {"name": "4 · UML", "open": "examples/uml/login_sequence.puml", "start": "<right><right>", "steps": [
        {"it": "Un diagramma di sequenza in PlantUML, percorso un messaggio alla volta.",
         "en": "A PlantUML sequence diagram, walked through one message at a time.", "keys": R(6), "pace": 0.75},
        {"it": "Tab prende l'altro ramo: e se la password fosse sbagliata?",
         "en": "Tab takes the other branch: what if the password is wrong?", "keys": "<tab>", "spot": ("the password is wrong", -2, 0, 105, 4)},
    ]},
    {"name": "4 · UML", "open": "examples/uml/order_state.puml", "steps": [
        {"it": "Un diagramma di stato: si sceglie la freccia, e lo stato cambia dal vivo.",
         "en": "A state diagram: choose an arrow, and the state changes live.", "keys": R(2) + "<down>" + R(1) + R(1) + R(1), "pace": 0.9},
        {"it": "A destra resta il cammino fatto. Valgono anche classi, attività e diagrammi di flusso.",
         "en": "The way so far stays on the right. Classes, activities and flowcharts work too.", "spot": (84, 3, 40, 22)},
    ]},
    {"name": "4 · Pseudoclassi", "open": "examples/design/shop.pscope", "start": "", "steps": [
        {"it": "Oppure pseudoclassi ancora vuote: dove c'è «...» il codice non esiste.",
         "en": "Or pseudo-classes still empty: where there is “...” there is no code yet.", "keys": "<home><down><enter>"},
        {"it": "Il progetto gira lo stesso, e mostra chi chiama chi.",
         "en": "The design runs all the same, and shows who calls whom.", "keys": "<esc><home>r1" + R(9), "pace": 0.65},
    ]},

    # ------------------------------------------------------------------
    {"title": "Un sistema intero", "tag": "5 · end to end", "sub": "Dal clic dell'utente al database, e ritorno",
     "en": "End to end: from the user's click to the database, and back"},
    {"name": "5 · End to end", "open": "examples/systems/login.pscope", "start": "<home><down><down><down><down>", "steps": [
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
    {"title": "Quando le cose vanno storte", "tag": "6 · reti", "sub": "Un protocollo affidabile su un canale che perde pacchetti",
     "en": "Networks: a reliable protocol over a channel that loses packets"},
    {"name": "6 · Protocolli", "open": "examples/systems/stop_and_wait.pscope", "start": "t<enter>", "steps": [
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
    {"title": "Algoritmi avanzati", "tag": "7 · anni successivi", "sub": "Verificare una proprietà, non solo un risultato",
     "en": "Later years: checking a property, not only a result"},
    {"name": "7 · Grafi", "open": "examples/advanced/dijkstra.pscope", "start": "", "steps": [
        {"it": "Dijkstra. Una prova verifica le distanze, l'altra una proprietà che deve valere sempre.",
         "en": "Dijkstra. One test checks distances, the other a property that must always hold.", "keys": "t<down>", "pace": 1.0,
         "spot": (0, 2, 124, 6)},
        {"it": "Se qualcosa non torna, si guarda la prova girare e si scende fino alla causa.",
         "en": "If something is wrong, watch the test run and go down to the cause.", "keys": "<enter>3" + R(8) + "u" + R(14), "pace": 0.25,
         "spot": DATA},
    ]},

    # ------------------------------------------------------------------
    {"title": "Sistemi distribuiti", "tag": "8 · magistrale", "sub": "Eleggere un leader, come fa Raft",
     "en": "Master's: electing a leader, as Raft does"},
    {"name": "8 · Consenso", "open": "examples/systems/election.pscope", "start": "t<enter>", "steps": [
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
    {"title": "E se fosse sbagliato?", "tag": "un esercizio", "color": (255, 135, 120),
     "sub": "La stessa elezione, con una regola dimenticata", "en": "What if it were wrong? The same election, with a rule forgotten"},
    {"name": "8 · Trovare l'errore", "open": "examples/exercises/two_leaders.pscope", "start": "", "steps": [
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
    {"title": "Pseudoscope", "sub": "Dal primo ciclo ai sistemi distribuiti, in qualunque terminale",
     "en": "From the first loop to distributed systems, in any terminal", "seconds": 8,
     "lines": [("make && bin/pseudoscope", "apri la cartella e comincia"),
               ("pseudoscope test file.pscope", "lancia le prove"),
               ("?", "tutti i tasti")]},
]
