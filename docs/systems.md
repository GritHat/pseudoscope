# Systems

Classes, components that exchange messages, and tests. The words used here are only
special where they are expected: `class`, `state` or `test` are still good names for
variables.

## Classes

```pascal
class Cart
    var total: real
    procedure add(in price: real)
    begin
        total := total + price
    end add
end Cart
```

A method is called with a dot, `cart.add(9.9)`. Inside, the fields are used by name, and
`self` is the object.

A class can extend others, be abstract, or be an interface:

```pascal
abstract class Shape
    abstract function area(): real
end Shape

class Circle extends Shape
    var r: real
    function area(): real
    begin
        area := 3.14 * r * r
    end area
end Circle

procedure show(inout s: Shape)      // any shape: the object chooses its area
begin
    print(s.area())
end show
```

`extends A, B` takes from several classes; a class that two of them extend is there
once. What is inherited twice under the same name must be written again in the class.
`super.m()` (or `A.m()`) calls the inherited method. An `interface` only says what its
classes must be able to do. Components do not extend.

Classes are translated to every language; each takes of inheritance what it has: see
[translation.md](translation.md).

`...` is a statement: what is still to be written. The program runs all the same, and
shows who calls whom: a design can be tried before it has any code
([examples/design](../examples/design)).

## Components

A component is a class that lives on its own and reacts to messages.

```pascal
component Server
    states Closed, Open
    var sessions: integer

    on start                                  // when the program begins
    begin
        become Open
    end start

    on Login(in user: integer) when Open      // a message, in these states only
    begin
        sessions := sessions + 1
        send Welcome(sessions) to sender
        send Expire to self after 30
    end Login

    on Expire when Open
    begin
        become Closed
    end Expire
end Server

component Client
    var session: integer

    on Click(in user: integer)
    begin
        send Login(user) to server
    end Click

    on Welcome(in number: integer)
    begin
        session := number
    end Welcome
end Client

var server: Server
var client: Client
link client <-> server delay 2..5 loss 10     // 10 messages in 100 get lost
seed 1                                        // the same run every time
```

| | |
|---|---|
| `send M(values) to x` | `x`: a component, `self`, `sender`, `all Class` |
| `... after n` | later: a timer, if sent to `self` |
| `cancel M` | forgets the messages `M` to itself not yet arrived |
| `become S` | changes state; `self.state` (or `server.state`) reads it |
| `stop` | the component no longer reacts |
| `now`, `random(a, b)` | the time; a number from a to b |

`a -> b` is a link in one direction. Without a link a message takes 1. A message that
arrives in a state without a handler for it is ignored, and shown as such. Time is
simulated: the run is the same every time, and one can go back in it.

Components are run by `exec`, `test` and the interface, and translated to every language
([translation.md](translation.md)).

## Tests

```pascal
test "the right password logs in"
begin
    step "the user types the password"
    send Click(2) to client
    run 20                               // "run" alone: until nothing is left to do
    step "and is in"
    expect client -> server: Login       // it happened, after the previous expect
    expect server -> client: Welcome
    check client.session = 1, "the first session"
    check server.state = Open
end test
```

`test` is whoever is outside. Each test starts from
the beginning.

```sh
pseudoscope test program.pscope
```

The exit status is 1 if any fails. In the interface, `t` runs them all and Enter shows
one while it runs.

## Watching

At the System level (`1`) a step is a message. Tab changes the view:

| View | UML diagram | Shows |
|---|---|---|
| Sequence | sequence | who says what to whom, what gets lost, what is on its way |
| Components | component | fields and state of each, and the messages travelling |
| States | state machine | where each one is, how it got there, where it can go |
| Timing | timing | the state of each, along time |
| Communication | communication | the same messages, by who talks to whom, numbered |
| Objects | object | the objects there are, and their values |
| Checks | | the steps of the test, and what has been verified |

Only those that make sense for the program are there. `m` is the next component in
States, PgUp and PgDn scroll the sequence. At the Function level `f` turns the code into
an activity diagram, with the current statement in yellow.

Every program has a sequence: without classes, each subprogram is a participant.
