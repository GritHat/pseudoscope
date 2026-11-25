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
here=$(cd "$(dirname "$0")" && pwd)
plike=$(cd "$(dirname "${PLIKE:-bin/pseudoscope}")" && pwd)/$(basename "${PLIKE:-bin/pseudoscope}")
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
    "$plike" run --cc="$here/ccheck" $opts "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
    got=$?
    if [ "$got" != "$want" ]; then
        echo "exit status $got, expected $want" >> "$tmp/err"
        fail "programs/$name" "$tmp/err"
    else
        compare "programs/$name" "$name.out" "$tmp/out"
    fi
    # the virtual machine must behave like the compiled program
    # shellcheck disable=SC2086
    "$plike" exec $opts "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
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
        "$plike" $how "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
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
for file in examples/systems/*.plike examples/advanced/*.plike; do
    if "$plike" test "$file" > "$tmp/out" 2>&1; then pass; else fail "$file" "$tmp/out"; fi
done

cd "$here/accept" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    for extra in "" --bounds-check; do
        # shellcheck disable=SC2086
        if "$plike" -q $opts $extra "$file" -o "$tmp/a.c" 2> "$tmp/err" &&
           "$here/ccheck" -std=c11 -Wno-uninitialized -Wno-maybe-uninitialized -c "$tmp/a.c" -o "$tmp/a.o" 2> "$tmp/err"
        then pass; else fail "accept/$name $extra" "$tmp/err"; fi
    done
done

cd "$here/errors" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    # shellcheck disable=SC2086
    "$plike" check $opts "$file" > /dev/null 2> "$tmp/err"
    got=$?
    if [ "$got" != 1 ]; then
        echo "exit status $got, expected 1" >> "$tmp/err"
        fail "errors/$name" "$tmp/err"
    else
        compare "errors/$name" "$name.err" "$tmp/err"
    fi
done

cd "$here/../examples" || exit 1
for file in *.plike; do
    opts= ; grep -q 'plike run --indexing=zero' "$file" && opts=--indexing=zero
    input=/dev/null; [ -f "${file%.plike}.in" ] && input=${file%.plike}.in
    if "$plike" run -b $opts --cc="$here/ccheck" "$file" < "$input" > /dev/null 2> "$tmp/err"
    then pass; else fail "examples/$file" "$tmp/err"; fi
done

# designs: their tests pass, and their diagrams are read as they were
cd "$here/../examples" || exit 1
for file in */*.plike; do
    grep -q '^ *test ' "$file" || continue
    if "$plike" test "$file" > "$tmp/out" 2> "$tmp/err"; then pass; else fail "examples/$file" "$tmp/out"; fi
done
for file in uml/*; do
    "$plike" ast "$file" > "$tmp/out" 2>&1
    compare "examples/$file" "$here/uml/$(basename "$file").out" "$tmp/out"
done

cd "$here/ui" || exit 1
for file in *.ui; do
    name=${file%.ui}
    env=
    { IFS= read -r program; IFS= read -r size; IFS= read -r keys; IFS= read -r opts; IFS= read -r env; } < "$file"
    # shellcheck disable=SC2086
    if env $env "$plike" ui "$program" --size="$size" --keys="$keys" $opts > "$tmp/out" 2> "$tmp/err"
    then compare "ui/$name" "$name.screen" "$tmp/out"
    else fail "ui/$name" "$tmp/err"; fi
done

. "$here/cli.sh"

echo "end to end: $passed passed, $failed failed"
[ "$failed" = 0 ]
