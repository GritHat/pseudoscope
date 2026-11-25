#!/bin/sh
# End-to-end tests.
#   programs/  translated, compiled, run; output compared with NAME.out
#              (NAME.in: input, NAME.opts: options, NAME.status: exit status)
#   dialects/  programs in other pseudocodes (FILE.in, FILE.out)
#   accept/    syntax that must be accepted, and give C that compiles
#   errors/    programs to refuse; messages compared with NAME.err
#   uml/       what is understood of the diagrams in examples/uml
#   ui/        NAME.ui: program, size, keys, options, environment; compared
#              with NAME.screen
# UPDATE=1 rewrites the expected files.
# Everything is expected in English, whatever the language of who runs the
# tests; those of the other languages ask for theirs with --lang.
PSEUDOSCOPE_LANG=en
export PSEUDOSCOPE_LANG
here=$(cd "$(dirname "$0")" && pwd)
pseudoscope=$(cd "$(dirname "${PSEUDOSCOPE:-bin/pseudoscope}")" && pwd)/$(basename "${PSEUDOSCOPE:-bin/pseudoscope}")
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
passed=0 failed=0

pass() { passed=$((passed + 1)); }
fail() { failed=$((failed + 1)); echo "FAIL $1"; [ -s "${2:-}" ] && sed 's/^/    /' "$2" | head -20; }
compare() { # name expected actual
    [ -n "${UPDATE:-}" ] && cp "$3" "$2"
    if diff -u "$2" "$3" > "$tmp/diff" 2>&1; then pass; else fail "$1" "$tmp/diff"; fi
}

cd "$here/programs" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    input=/dev/null; [ -f "$name.in" ] && input=$name.in
    want=0; [ -f "$name.status" ] && want=$(cat "$name.status")
    # shellcheck disable=SC2086
    "$pseudoscope" run --cc="$here/ccheck" $opts "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
    got=$?
    if [ "$got" != "$want" ]; then
        echo "exit status $got, expected $want" >> "$tmp/err"
        fail "programs/$name" "$tmp/err"
    else
        compare "programs/$name" "$name.out" "$tmp/out"
    fi
    # the virtual machine must behave like the compiled program
    # shellcheck disable=SC2086
    "$pseudoscope" exec $opts "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
    got=$?
    if [ "$got" != "$want" ]; then
        echo "exit status $got, expected $want" >> "$tmp/err"
        fail "programs/$name (exec)" "$tmp/err"
    else
        compare "programs/$name (exec)" "$name.out" "$tmp/out"
    fi
done

cd "$here/dialects" || exit 1
for file in *.psc *.alg *.por *.pse; do
    input=/dev/null; [ -f "$file.in" ] && input=$file.in
    for how in exec "run --cc=$here/ccheck"; do
        # shellcheck disable=SC2086
        "$pseudoscope" $how "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
        got=$?
        if [ "$got" != 0 ]; then
            echo "exit status $got, expected 0" >> "$tmp/err"
            fail "dialects/$file ($how)" "$tmp/err"
        else
            compare "dialects/$file ($how)" "$file.out" "$tmp/out"
        fi
    done
done

# systems, classes, designs: their own tests, and the examples
cd "$here/.." || exit 1
for file in examples/systems/*.pscope examples/advanced/*.pscope; do
    if "$pseudoscope" test "$file" > "$tmp/out" 2>&1; then pass; else fail "$file" "$tmp/out"; fi
done

# the same tests, translated to C: they must say the same
for file in examples/systems/*.pscope examples/exercises/*.pscope examples/advanced/dijkstra.pscope; do
    "$pseudoscope" test "$file" > "$tmp/here" 2>&1
    "$pseudoscope" test --to=c --cc="$here/ccheck" "$file" > "$tmp/out" 2>&1
    compare "$file (tests in C)" "$tmp/here" "$tmp/out"
done

cd "$here/accept" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    for extra in "" --bounds-check; do
        # shellcheck disable=SC2086
        if "$pseudoscope" -q $opts $extra "$file" -o "$tmp/a.c" 2> "$tmp/err" &&
           "$here/ccheck" -std=c11 -Wno-uninitialized -Wno-maybe-uninitialized -c "$tmp/a.c" -o "$tmp/a.o" 2> "$tmp/err"
        then pass; else fail "accept/$name $extra" "$tmp/err"; fi
    done
done

cd "$here/errors" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    # shellcheck disable=SC2086
    "$pseudoscope" check $opts "$file" > /dev/null 2> "$tmp/err"
    got=$?
    if [ "$got" != 1 ]; then
        echo "exit status $got, expected 1" >> "$tmp/err"
        fail "errors/$name" "$tmp/err"
    else
        compare "errors/$name" "$name.err" "$tmp/err"
    fi
done

cd "$here/../examples" || exit 1
for file in *.pscope; do
    opts= ; grep -q 'run --indexing=zero' "$file" && opts=--indexing=zero
    input=/dev/null; [ -f "${file%.pscope}.in" ] && input=${file%.pscope}.in
    if "$pseudoscope" run -b $opts --cc="$here/ccheck" "$file" < "$input" > /dev/null 2> "$tmp/err"
    then pass; else fail "examples/$file" "$tmp/err"; fi
done

# designs: their tests pass, and their diagrams are read as they were
cd "$here/../examples" || exit 1
for file in */*.pscope; do
    grep -q '^ *test ' "$file" || continue
    "$pseudoscope" test "$file" > "$tmp/out" 2> "$tmp/err"
    got=$? want=0
    case $file in exercises/*) want=1 ;; esac # what is wrong in them is found
    if [ "$got" = "$want" ]; then pass; else fail "examples/$file" "$tmp/out"; fi
done
for file in uml/* diagrams/* documents/*; do
    "$pseudoscope" ast "$file" > "$tmp/out" 2>&1
    compare "examples/$file" "$here/uml/$(basename "$file").out" "$tmp/out"
done
# documents: everything they hold is right, and their programs run
for file in documents/*; do
    if "$pseudoscope" check -q "$file" > /dev/null 2> "$tmp/err"; then pass; else fail "examples/$file" "$tmp/err"; fi
done
"$pseudoscope" exec documents/lesson.md#2 > "$tmp/out" 2>&1
compare "examples/documents/lesson.md#2" "$here/uml/lesson.md.2.out" "$tmp/out"
"$pseudoscope" exec documents/paper.tex > "$tmp/out" 2>&1
compare "examples/documents/paper.tex (exec)" "$here/uml/paper.tex.1.out" "$tmp/out"

# libraries: files are written where the program is run, so in a place of their own
mkdir "$tmp/libraries" && cp libraries/* "$tmp/libraries" && cd "$tmp/libraries" || exit 1
for file in *.por *.pse *.psc *.alg; do
    "$pseudoscope" exec "$file" --canvas="$tmp/canvas.ppm" > "$tmp/out" 2>&1
    compare "examples/libraries/$file" "$here/libraries/$file.out" "$tmp/out"
done
"$pseudoscope" exec desenho.por --canvas="$tmp/canvas.ppm" > /dev/null 2>&1
cksum < "$tmp/canvas.ppm" > "$tmp/out"
compare "examples/libraries/desenho.por (drawing)" "$here/libraries/desenho.por.cksum" "$tmp/out"
compare "examples/libraries/files.pse (file)" "$here/libraries/marks.txt" "$tmp/libraries/marks.txt"

# classes that extend others, and what else C cannot say: run here
cd "$here/targets" || exit 1
for file in *.plike; do
    "$pseudoscope" exec "$file" > "$tmp/out" 2>&1
    compare "targets/$file (exec)" "${file%.plike}.out" "$tmp/out"
done

# a file is what its text says, whatever its name
cp "$here/../examples/fib.pscope" "$tmp/noname" && cp "$here/dialects/basico.psc" "$tmp/pseint.txt" && cp "$here/../examples/uml/payment.mmd" "$tmp/drawing"
"$pseudoscope" exec "$tmp/noname" > "$tmp/out" 2>&1
compare "a program without an extension" "$here/uml/noname.out" "$tmp/out"
"$pseudoscope" exec "$tmp/pseint.txt" < "$here/dialects/basico.psc.in" > "$tmp/out" 2>&1
compare "PSeInt without its extension" "$here/dialects/basico.psc.out" "$tmp/out"
"$pseudoscope" ast "$tmp/drawing" > "$tmp/out" 2>&1
compare "a diagram without an extension" "$here/uml/payment.mmd.out" "$tmp/out"
cd "$here/../examples" || exit 1

# a path under the home is shown from there: let it be this folder, wherever it has been put
root=$(cd "$here/.." && pwd)
cd "$here/ui" || exit 1
for file in *.ui; do
    name=${file%.ui}
    env=
    { IFS= read -r program; IFS= read -r size; IFS= read -r keys; IFS= read -r opts; IFS= read -r env; } < "$file"
    # shellcheck disable=SC2086
    if env HOME="$root" $env "$pseudoscope" ui "$program" --size="$size" --keys="$keys" $opts > "$tmp/out" 2> "$tmp/err"
    then compare "ui/$name" "$name.screen" "$tmp/out"
    else fail "ui/$name" "$tmp/err"; fi
done

. "$here/cli.sh"

echo "end to end: $passed passed, $failed failed"
[ "$failed" = 0 ]
