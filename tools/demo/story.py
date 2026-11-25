"""What the video tells, in English: story_it.py says the same in Italian.
`say` is the caption; keys are pressed after the caption appears; `spot` is what to look at, in cells:
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
    {"title": "Pseudoscope", "logo": True, "sub": "Pseudocode and designs, brought to life", "seconds": 4.5},

    # ------------------------------------------------------------------
    {"title": "The first algorithm", "tag": "1 · first year",
     "sub": "Sorting an array, and understanding why it works"},
    {"name": "1 · The first algorithm", "open": "examples", "start": "", "steps": [
        {"say": "It starts from the files, with a preview of the chosen one.", "spot": (40, 2, 84, 24)},
    ]},
    {"name": "1 · The first algorithm", "open": "examples/basic.pscope", "start": "<home>", "steps": [
        {"say": "The map: each subprogram is a box, with what goes in and what comes out.", "keys": "<down>",
         "spot": (39, 2, 85, 24)},
        {"say": "Enter goes into detail: first the code…", "keys": "<enter>"},
        {"say": "…then its translation to C, line by line.", "keys": "<enter>"},
        {"say": "T shows the same in C++, Java, Rust, TypeScript, JavaScript and Python.", "keys": "TTTTTT", "pace": 1.15,
         "spot": OTHER},
        {"say": "r tries this subprogram by itself, with values typed or generated.", "keys": "r",
         "spot": (1, 5, 120, 7)},
        {"say": "Each → is a statement: the code on the left, the data changing on the right.",
         "keys": "<enter>" + R(8)},
        {"say": "At the bottom, in words, what has just happened.",
         "keys": R(5), "spot": BOTTOM},
        {"say": "Not clear? ← goes back, as far as you want.", "keys": "<left>" * 5, "pace": 0.7, "spot": DATA},
        {"say": "v turns the array into bars: the large values rise to the end.", "keys": "v" + R(26), "pace": 0.22, "spot": DATA},
        {"say": "And f turns the code into its activity diagram, showing where we are.", "keys": "f" + R(6), "spot": CODE},
    ]},
    {"name": "1 · The first algorithm", "open": "examples/basic.pscope", "start": "<home><down>r<enter>" + R(5), "steps": [
        {"say": "Five levels: each decides what you see, and how long a step is.", "spot": LEVELS},
        {"say": "Statement: an expression becomes its value, one piece at a time.", "keys": "<enter>" + R(5), "pace": 1.0},
        {"say": "Memory: the C code, addresses, bytes. For those who want to go all the way.", "keys": "<enter>" + R(4), "pace": 0.8,
         "spot": (58, 2, 66, 21)},
        {"say": "Esc comes back up: choose the abstraction at any time.",
         "keys": "<esc><esc>", "pace": 0.9},
    ]},

    # ------------------------------------------------------------------
    {"title": "Recursion and data structures", "tag": "2 · first and second year",
     "sub": "Who waits for whom, and where pointers lead"},
    {"name": "2 · Recursion", "open": "examples/fib.pscope", "start": "r2", "steps": [
        {"say": "Program level: a step is a call or a return.",
         "keys": R(9), "pace": 0.6},
        {"say": "On the right, the stack: each call waits for the one above it.", "spot": (74, 2, 50, 21)},
        {"say": "On the left, the tree: fib(2) is computed twice. That is why it is slow.", "keys": R(12), "pace": 0.4,
         "spot": (0, 2, 74, 21)},
    ]},
    {"name": "2 · Data structures", "open": "examples/tree.pscope", "start": "r<right><right><right>3<enter>", "steps": [
        {"say": "A binary search tree built by the program: pointers become a drawing.", "keys": R(60), "pace": 0.1,
         "spot": ("root  Node*", -2, 0, 64, 9)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Your own pseudocode", "tag": "3 · each school has its own",
     "sub": "PSeInt, VisuAlg, Portugol, Cambridge, with their libraries"},
    {"name": "3 · Other pseudocodes", "open": "tests/dialects/basico.psc", "start": "<home><down><down><enter>", "steps": [
        {"say": "A program written for PSeInt, in Spanish. Nothing to convert.", "spot": (0, 2, 124, 24)},
        {"say": "It runs, is explored and is translated like any other.", "keys": "r<enter>" + R(14), "pace": 0.4},
    ]},
    {"name": "3 · Their libraries", "open": "examples/libraries/files.pse", "start": "", "steps": [
        {"say": "Each has its libraries. Cambridge pseudocode writes a file, then reads it back.", "keys": "r" + R(16), "pace": 0.3,
         "spot": BOTTOM},
    ]},
    {"name": "3 · Their libraries", "open": "examples/libraries/desenho.por", "start": "r" + R(9), "steps": [
        {"say": "Portugol Studio has a graphics library: d shows the drawing grow, statement by statement.",
         "keys": "d" + R(38), "pace": 0.13},
    ]},

    # ------------------------------------------------------------------
    {"title": "Classes and inheritance", "tag": "4 · object-oriented programming",
     "sub": "Who extends whom, and which method runs"},
    {"name": "4 · Inheritance", "open": "examples/advanced/shapes.pscope", "start": "<home>", "steps": [
        {"say": "An abstract class, an interface, and the classes that extend them.", "keys": D(8), "pace": 0.35,
         "spot": (39, 2, 85, 16)},
        {"say": "C has no abstract classes. Rather than translate wrongly, it says so.", "keys": "<enter><enter>", "pace": 0.8,
         "spot": OTHER},
        {"say": "Each language in its own way: in C++ a class extends as many as it likes…", "keys": "T", "spot": OTHER},
        {"say": "…in Java only one, plus interfaces…", "keys": "T", "spot": OTHER},
        {"say": "…and in Rust none: there are only traits, and here they are not enough.", "keys": "T", "spot": OTHER},
        {"say": "Then TypeScript, JavaScript and Python, with its multiple classes.", "keys": "TTT", "pace": 1.3, "spot": OTHER},
    ]},
    {"name": "4 · Inheritance", "open": "examples/advanced/shapes.pscope", "start": "r2", "steps": [
        {"say": "At run time the object chooses: twice() is Shape's, but area() is Rectangle's.", "keys": R(11), "pace": 0.5,
         "spot": CALLS},
    ]},

    # ------------------------------------------------------------------
    {"title": "Designing", "tag": "5 · software engineering",
     "sub": "A design can be walked through before it is written"},
    {"name": "5 · UML", "open": "examples/uml/login_sequence.puml", "start": "<right><right>", "steps": [
        {"say": "A PlantUML sequence diagram, walked through one message at a time.", "keys": R(6), "pace": 0.75},
        {"say": "Tab takes the other branch: what if the password is wrong?", "keys": "<tab>", "spot": ("the password is wrong", -2, 0, 105, 4)},
    ]},
    {"name": "5 · UML", "open": "examples/uml/order_state.puml", "steps": [
        {"say": "A state diagram: choose an arrow, and the state changes live.", "keys": R(2) + "<down>" + R(1) + R(1) + R(1), "pace": 0.9},
        {"say": "The way so far stays on the right. Classes, activities and flowcharts work too.", "spot": (84, 3, 40, 22)},
    ]},
    {"name": "5 · Pseudo-classes", "open": "examples/design/shop.pscope", "start": "", "steps": [
        {"say": "Or pseudo-classes still empty: where there is “...” there is no code yet.", "keys": "<home><down><enter>"},
        {"say": "The design runs all the same, and shows who calls whom.", "keys": "<esc><home>r1" + R(9), "pace": 0.65},
    ]},
    {"name": "5 · Plans, data, ideas", "open": "examples/diagrams/thesis_plan.puml", "steps": [
        {"say": "A plan written in PlantUML is a Gantt chart to walk through time: what is done, what is under way.",
         "keys": R(4), "pace": 1.0},
    ]},
    {"name": "5 · Plans, data, ideas", "open": "examples/diagrams/library_er.mmd", "steps": [
        {"say": "Entities and relationships, in Mermaid: each link reads as a sentence.", "keys": D(2), "pace": 1.0,
         "spot": (83, 3, 40, 9)},
    ]},
    {"name": "5 · Plans, data, ideas", "open": "examples/diagrams/algorithms_mindmap.mmd", "steps": [
        {"say": "And mind maps, WBS, timelines, Graphviz graphs: one branch at a time.", "keys": D(10), "pace": 0.3},
    ]},

    # ------------------------------------------------------------------
    {"title": "Lecture notes and papers", "tag": "6 · Markdown and LaTeX",
     "sub": "What is already written opens as it is"},
    {"name": "6 · Markdown", "open": "examples/documents", "start": "", "steps": [
        {"say": "Markdown notes open like a folder: inside, their programs and their diagrams.", "keys": "<enter><down>",
         "pace": 0.9, "spot": (0, 2, 41, 9)},
        {"say": "The pseudocode of the lesson runs where it is, with no copying elsewhere.", "keys": "<enter>r" + R(14),
         "pace": 0.3},
    ]},
    {"name": "6 · LaTeX", "open": "examples/documents", "start": "<down>", "steps": [
        {"say": "A LaTeX paper: the algorithmic environment becomes a program, pgfgantt a plan.", "keys": "<enter>",
         "pace": 1.0, "spot": (0, 2, 41, 6)},
        {"say": "Euclid's algorithm, as it is written in the paper, executed: Gcd(48, 18) is 6.", "keys": "<enter>r2" + R(5),
         "pace": 0.6, "spot": CALLS},
    ]},

    # ------------------------------------------------------------------
    {"title": 'A whole system', "tag": '7 · end to end',
     "sub": "From the user's click to the database, and back"},
    {"name": "7 · End to end", "open": "examples/systems/login.pscope", "start": "<home><down><down><down><down>", "steps": [
        {"say": "Three components: each lives on its own and reacts to messages.", "spot": (39, 2, 85, 12)},
        {"say": "t runs all the tests. Enter shows one while it runs.", "keys": "t<enter>", "pace": 1.6},
        {"say": "System level: the sequence diagram draws itself, one message per step.", "keys": R(10), "pace": 0.8, "spot": SEQ, "poster": True},
        {"say": "Enter goes into what the server is doing right now, down to a statement…", "keys": "<enter><enter>" + R(2), "pace": 0.9,
         "spot": CODE},
        {"say": "…and Esc goes back outside, without losing the thread.",
         "keys": "<esc><esc>", "pace": 0.8},
        {"say": "The same things from other points of view: Tab changes diagram.", "keys": "<tab><tab><tab><tab>", "pace": 1.7, "spot": TABS},
        {"say": "And the checks: what had to happen, end to end, and what did.", "keys": "<tab>g", "pace": 0.8, "spot": (1, 4, 80, 14)},
    ]},

    # ------------------------------------------------------------------
    {"title": "When things go wrong", "tag": "8 · networks",
     "sub": "A reliable protocol over a channel that loses packets"},
    {"name": "8 · Protocols", "open": "examples/systems/stop_and_wait.pscope", "start": "t<enter>", "steps": [
        {"say": "Stop-and-wait: the channel loses one message in four.",
         "keys": R(7), "pace": 0.6},
        {"say": "There: the acknowledgement got lost.", "keys": R(2), "pace": 0.8,
         "spot": ("✗", -12, 0, 62, 1)},
        {"say": "The timer expires, and the sender sends the same packet again.", "keys": R(4), "pace": 0.8,
         "spot": ("Timeout(1)", -24, 0, 80, 2)},
        {"say": "Components: the data of each, and the messages as they travel.", "keys": "<tab>" + R(8), "pace": 0.6},
        {"say": "States: where the sender is, how it got there, where it can go.", "keys": "<tab>" + R(8), "pace": 0.6,
         "spot": (1, 4, 60, 12)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Advanced algorithms", "tag": "9 · later years",
     "sub": "Checking a property, not only a result"},
    {"name": "9 · Graphs", "open": "examples/advanced/dijkstra.pscope", "start": "", "steps": [
        {"say": "Dijkstra. One test checks distances, the other a property that must always hold.", "keys": "t<down>", "pace": 1.0,
         "spot": (0, 2, 124, 6)},
        {"say": "If something is wrong, watch the test run and go down to the cause.", "keys": "<enter>3" + R(8) + "u" + R(14), "pace": 0.25,
         "spot": DATA},
    ]},

    # ------------------------------------------------------------------
    {"title": 'Distributed systems', "tag": "10 · master's",
     "sub": 'Electing a leader, as Raft does'},
    {"name": "10 · Consensus", "open": "examples/systems/election.pscope", "start": "t<enter>", "steps": [
        {"say": "Five nodes. Whoever hears no leader for a while asks for votes.", "keys": R(14), "pace": 0.25},
        {"say": "Two candidates at the same moment!", "keys": R(4), "pace": 0.4,
         "spot": ("◆ Candidate", -18, 0, 100, 3)},
        {"say": "But each node votes once per term: only one gets a majority.", "keys": R(24), "pace": 0.22, "spot": ("◆ Leader", -18, 0, 100, 1)},
        {"say": "The state chart of each node, live.",
         "keys": "<tab><tab>", "pace": 0.8},
        {"say": "The timing diagram: the leader crashes, and the others elect a new one.", "keys": "<tab>g", "pace": 0.8,
         "spot": (0, 4, 90, 12)},
    ]},
    {"title": "What if it were wrong?", "tag": "an exercise", "color": (255, 135, 120),
     "sub": "The same election, with a rule forgotten"},
    {"name": "10 · Finding the mistake", "open": "examples/exercises/two_leaders.pscope", "start": "", "steps": [
        {"say": "The tests fail: there are two leaders in the same term.",
         "keys": "t<down>", "pace": 1.0, "spot": (0, 2, 124, 6)},
        {"say": "Let us see where it happens, on the timing diagram…",
         "keys": "<enter>" + R(53) + "<tab><tab><tab>", "pace": 0.07, "hold": 2.2, "spot": ("nodes[1]", -2, 0, 100, 9)},
        {"say": "…and then in the sequence: the same nodes vote for both.", "keys": "<tab><tab><tab>", "pace": 0.6, "hold": 2.5, "spot": SEQ},
        {"say": "The check on past votes is missing: the rule that makes it safe.", "keys": "x<esc><home>" + "<down>" * 5 + "<enter>", "pace": 0.35,
         "spot": ("if t = term then", -10, 0, 60, 1)},
    ]},

    # ------------------------------------------------------------------
    {"title": "Seven languages", "tag": "11 · from the design to the code",
     "sub": "The same tests, translated and compiled"},
    {"name": "11 · Seven languages", "steps": [
        {"run": "pseudoscope test examples/systems/election.pscope",
         "say": "The tests of the election, run here: no other tool is needed."},
        {"run": "pseudoscope test --to=rust examples/systems/election.pscope",
         "say": "The same, translated to Rust and compiled. The simulation is translated too: same result."},
        {"run": "pseudoscope test --to=java examples/exercises/two_leaders.pscope",
         "say": "The mistakes are the same too: in Java the exercise fails at the same point.", "hold": 2.4},
        {"run": "pseudoscope translate --to=python examples/fib.pscope", "clear": True,
         "say": "And the code is yours to take away: C, C++, Java, Rust, TypeScript, JavaScript, Python.", "hold": 2.4},
    ]},

    # ------------------------------------------------------------------
    {"title": "Pseudoscope", "logo": True, "chapter": "Getting started",
     "sub": "From the first loop to distributed systems, in any terminal", "seconds": 9,
     "lines": [("make && bin/pseudoscope", "open the folder and start"),
               ("pseudoscope test file.pscope", "run the tests"),
               ("pseudoscope run --to=java file.pscope", "translated, in seven languages"),
               ("?", "all the keys")]},
]

# Forty seconds, for those who have not decided to watch ten minutes yet.
TRAILER = [
    {"name": "Step by step", "open": "examples/basic.pscope", "start": "<home><down>r<enter>", "steps": [
        {"say": "Pseudocode that runs: each → is a statement, and ← goes back.", "keys": R(12) + "<left><left><left>", "pace": 0.3,
         "seconds": 5.5, "hold": 0.6},
        {"say": "The data while they change: here the array, seen as bars.", "keys": "v" + R(26), "pace": 0.14, "seconds": 5.5,
         "hold": 0.8},
    ]},
    {"name": "Seven languages", "open": "examples/basic.pscope", "start": "<home><down><enter><enter>", "steps": [
        {"say": "The translation line by line: C, C++, Java, Rust, TypeScript, JavaScript, Python.", "keys": "TTTTTT", "pace": 0.7,
         "seconds": 6.0, "hold": 0.8},
    ]},
    {"name": "Systems", "open": "examples/systems/election.pscope", "start": "t<enter>", "steps": [
        {"say": "A distributed system: the sequence diagram draws itself.", "keys": R(30), "pace": 0.13, "seconds": 6.0, "hold": 1.0},
    ]},
    {"name": "Diagrams", "open": "examples/diagrams/thesis_plan.puml", "steps": [
        {"say": "Diagrams to walk through: UML, Gantt, entities and relationships, mind maps.", "keys": R(4), "pace": 0.6,
         "seconds": 4.5, "hold": 0.8},
    ]},
    {"name": "Other pseudocodes", "open": "examples/libraries/desenho.por", "start": "r" + R(9), "steps": [
        {"say": "The pseudocodes of schools, with their libraries: here Portugol draws.", "keys": "d" + R(38), "pace": 0.09,
         "seconds": 5.5, "hold": 1.0},
    ]},
    {"name": "Translated tests", "steps": [
        {"run": "pseudoscope test --to=rust examples/systems/election.pscope",
         "say": "And the tests, translated and compiled: the same result in every language.", "seconds": 5.5, "hold": 2.0},
    ]},
]
