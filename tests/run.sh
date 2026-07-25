#!/bin/sh
# Basic functional tests. Deterministic via --seed.
set -u
cd "$(dirname "$0")/.."
C=./casino
pass=0 fail=0

ok()   { pass=$((pass+1)); }
bad()  { fail=$((fail+1)); echo "FAIL: $1"; }

expect_exit() { # desc expected_code cmd...
    desc=$1 want=$2; shift 2
    "$@" >/dev/null 2>&1
    got=$?
    [ "$got" -eq "$want" ] && ok || bad "$desc (exit $got, wanted $want)"
}

expect_grep() { # desc pattern cmd...
    desc=$1 pat=$2; shift 2
    out=$("$@" 2>&1)
    echo "$out" | grep -q "$pat" && ok || bad "$desc (no '$pat' in: $out)"
}

# --- exit codes: losses are not errors -------------------------------
expect_exit "win exits 0"            0 $C roulette --seed 1 straight:0
expect_exit "loss exits 0"           0 $C roulette --seed 1 straight:36
expect_exit "no bets is fine"        0 $C roulette --seed 1

# --- validation errors exit 2 ----------------------------------------
expect_exit "unknown game"           2 $C nosuchgame red
expect_exit "unknown bet"            2 $C roulette martingale
expect_exit "straight out of range"  2 $C roulette straight:37
expect_exit "bad split adjacency"    2 $C roulette split:1,36
expect_exit "bad street"             2 $C roulette street:1,2,4
expect_exit "bad corner"             2 $C roulette corner:1,2,3,4
expect_exit "dozen out of range"     2 $C roulette dozen:4
expect_exit "red takes no value"     2 $C roulette red:1
expect_exit "bad iterations"         2 $C roulette red --iterations 0
expect_exit "bad seed"               2 $C roulette red --seed abc
expect_exit "trailing comma"         2 $C roulette split:17,
expect_exit "unimplemented game"     2 $C sicbo big

# --- valid layout bets -----------------------------------------------
expect_exit "valid split 0-2"        0 $C roulette --seed 1 split:0,2
expect_exit "valid vertical split"   0 $C roulette --seed 1 split:17,20
expect_exit "valid street"           0 $C roulette --seed 1 street:16,17,18
expect_exit "valid corner"           0 $C roulette --seed 1 corner:16,17,19,20
expect_exit "basket corner"          0 $C roulette --seed 1 corner:0,1,2,3
expect_exit "valid sixline"          0 $C roulette --seed 1 sixline:13,14,15,16,17,18

# --- determinism ------------------------------------------------------
a=$($C roulette --seed 42 red straight:17)
b=$($C roulette --seed 42 red straight:17)
[ "$a" = "$b" ] && ok || bad "seeded runs identical"

# --- output modes -----------------------------------------------------
expect_grep "normal shows table"  "PAYOUT"        $C roulette --seed 42 red
expect_grep "json has game key"   '"game":"roulette"' $C roulette --seed 42 red --json
expect_grep "json spin object"    '"number":'     $C roulette --seed 42 red --json
expect_grep "quiet compact"       "red=[WL]"      $C roulette --seed 42 red --quiet
expect_grep "help works"          "usage:"        $C --help
expect_grep "list-bets works"     "straight:N"    $C roulette --list-bets

# --- simulation / stats ----------------------------------------------
expect_grep "stats table"  "HIT%" $C roulette --seed 7 --iterations 1000 --stats red
n=$($C roulette --seed 7 --iterations 5 red --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "iterations produce 5 lines (got $n)"

# sanity: red hit rate over 100k seeded spins should be near 18/37
hits=$($C roulette --seed 9 --iterations 100000 --stats red --json |
       sed 's/.*"wins":\([0-9]*\).*/\1/')
[ "$hits" -gt 47500 ] && [ "$hits" -lt 49800 ] && ok || \
    bad "red hit count plausible (got $hits)"

# --- symlink invocation ----------------------------------------------
ln -sf casino roulette
expect_grep "symlink invocation" "PAYOUT" ./roulette --seed 42 red
rm -f roulette

# --- other games ------------------------------------------------------
expect_exit "coin flip"        0 $C coin --seed 1 heads
expect_exit "coin bad bet"     2 $C coin edge
expect_exit "dice 2d6"         0 $C dice --seed 1 2d6 total:7
expect_exit "dice bad range"   2 $C dice 2d6 total:13
expect_exit "dice bad spec"    2 $C dice 0d6

# --- blackjack ---------------------------------------------------------
expect_exit "bj scripted h,s"          0 $C blackjack --seed 1 h,s
expect_exit "bj scripted long words"   0 $C blackjack --seed 1 hit,hit,stand
expect_exit "bj space separated"       0 $C blackjack --seed 1 h s
expect_exit "bj double first action"   0 $C blackjack --seed 2 d
expect_exit "bj unknown action"        2 $C blackjack split
expect_exit "bj action takes no value" 2 $C blackjack h:1
expect_exit "bj empty action segment"  2 $C blackjack h,,s
# seed 2: opening hand 9, first hit keeps it < 21, so 'd' is consumed
expect_exit "bj double after hit"      2 $C blackjack --seed 2 h,d,s

a=$($C blackjack --seed 42 h,s --json)
b=$($C blackjack --seed 42 h,s --json)
[ "$a" = "$b" ] && ok || bad "bj seeded runs identical"

expect_grep "bj quiet word"     "^\(WIN\|LOSS\|PUSH\|BLACKJACK\)$" \
    sh -c "$C blackjack --seed 5 s --quiet 2>/dev/null"
expect_grep "bj json game key"  '"game":"blackjack"' $C blackjack --seed 5 s --json
expect_grep "bj json result"    '"result":"'         $C blackjack --seed 5 s --json
expect_grep "bj json actions"   '"actions":\["stand"\]' $C blackjack --seed 5 s --json
expect_grep "bj transcript"     "Dealer:"            $C blackjack --seed 1 h,s
expect_grep "bj list-bets"      "double"             $C blackjack --list-bets

n=$($C blackjack --seed 7 --iterations 5 s --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "bj iterations produce 5 lines (got $n)"

expect_grep "bj stats table" "RESULT" $C blackjack --seed 7 --iterations 200 --stats s
expect_grep "bj stats json"  '"iterations":200' \
    $C blackjack --seed 7 --iterations 200 --stats s --json

# interactive: EOF stands, piped actions are consumed, quiet stdout is clean
expect_exit "bj interactive EOF"  0 sh -c "$C blackjack --seed 1 </dev/null"
expect_exit "bj piped hit"        0 sh -c "echo h | $C blackjack --seed 1"
expect_grep "bj quiet interactive stdout clean" \
    "^\(WIN\|LOSS\|PUSH\|BLACKJACK\)$" \
    sh -c "$C blackjack --seed 1 --quiet </dev/null 2>/dev/null"

ln -sf casino blackjack
expect_grep "bj symlink invocation" '"game":"blackjack"' \
    sh -c "./blackjack --seed 42 s --json"
rm -f blackjack

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
