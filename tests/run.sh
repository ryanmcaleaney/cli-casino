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

# --- baccarat -----------------------------------------------------------
expect_exit "bac player bet"        0 $C baccarat player --seed 1
expect_exit "bac banker bet"        0 $C baccarat banker --seed 1
expect_exit "bac tie bet"           0 $C baccarat tie --seed 1
expect_exit "bac unknown bet"       2 $C baccarat foo --seed 1
expect_exit "bac no bet"            2 $C baccarat --seed 1
expect_exit "bac bet takes no value" 2 $C baccarat player:5 --seed 1
expect_exit "bac too many bets"     2 $C baccarat player banker --seed 1

# naturals end the round immediately (no third card either side)
expect_grep "bac natural player 9, banker untouched" "Banker: Jc 2d = 2" \
    $C baccarat player --seed 2
expect_grep "bac natural player 9 shown"  "Player: 2s 7h = 9" \
    $C baccarat player --seed 2
expect_grep "bac natural banker 8, player untouched" "Player: Js 5h = 5" \
    $C baccarat banker --seed 4
expect_grep "bac natural banker 8 shown"  "Banker: 5c 3c = 8" \
    $C baccarat banker --seed 4

# player third-card rule: draws on <=5, stands on 6/7
expect_grep "bac player draws on 5"  "Player: Kd Jc 5c = 5" \
    $C baccarat player --seed 1
expect_grep "bac player stands on 7" "Player: Qs 7s = 7" \
    $C baccarat player --seed 9

# banker third-card table: representative rows/boundaries, verified against
# a reference implementation swept over seeds 1-8000 (0 mismatches)
expect_grep "bac banker 0 draws when player stands on 6" \
    "Banker: Kc Jh Ac = 1" $C baccarat player --seed 84
expect_grep "bac banker 3 draws, p3=5"  "Banker: 7c 6h Js = 3" \
    $C baccarat player --seed 1
expect_grep "bac banker 3 stands, p3=8" "Banker: Ad 2h = 3" \
    $C baccarat player --seed 7
expect_grep "bac banker 4 draws, p3=2"  "Banker: 9h 5c Ac = 5" \
    $C baccarat player --seed 237
expect_grep "bac banker 4 stands, p3=0" "Banker: 4d 10s = 4" \
    $C baccarat player --seed 111
expect_grep "bac banker 5 draws, p3=4"  "Banker: 4s Ac 4h = 9" \
    $C baccarat player --seed 283
expect_grep "bac banker 5 stands, p3=2" "Banker: 8h 7d = 5" \
    $C baccarat player --seed 334
expect_grep "bac banker 6 draws, p3=6"  "Banker: 3h 3d 10s = 6" \
    $C baccarat player --seed 501
expect_grep "bac banker 6 stands, p3=5" "Banker: 6d Jc = 6" \
    $C baccarat player --seed 140
expect_grep "bac banker 7 always stands" "Banker: 3d 4h = 7" \
    $C baccarat player --seed 56
expect_grep "bac banker 6 stands when player stands" "Banker: 7s 9d = 6" \
    $C baccarat player --seed 20

# tie: player/banker bets push, tie bet wins
expect_grep "bac tie pushes player bet" "PUSH" $C baccarat player --seed 109
expect_grep "bac tie pushes banker bet" "PUSH" $C baccarat banker --seed 109
expect_grep "bac tie bet wins on tie"   "WIN"  $C baccarat tie    --seed 109

# output modes
expect_grep "bac quiet compact" "^\(WIN\|LOSS\|PUSH\)$" \
    sh -c "$C baccarat player --seed 1 --quiet"
expect_grep "bac json game key"   '"game":"baccarat"'      $C baccarat banker --seed 3 --json
expect_grep "bac json outcome"    '"outcome":"banker"'      $C baccarat banker --seed 3 --json
expect_grep "bac json result"     '"result":"win"'          $C baccarat banker --seed 3 --json
expect_grep "bac json hands"      '"cards":\["3s","9c","Kc"\]' \
    $C baccarat banker --seed 3 --json
expect_grep "bac help works"      "usage:"     $C baccarat --help
expect_grep "bac list-bets works" "Punto Banco" $C baccarat --list-bets

a=$($C baccarat banker --seed 42 --json)
b=$($C baccarat banker --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "bac seeded runs identical"

n=$($C baccarat player --seed 7 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "bac iterations produce 5 lines (got $n)"

expect_grep "bac stats table" "RESULT" \
    $C baccarat player --seed 7 --iterations 2000 --stats
expect_grep "bac stats json"  '"iterations":2000' \
    $C baccarat player --seed 7 --iterations 2000 --stats --json

# sanity: over many seeded rounds, win/loss/push rates should be near the
# known Punto Banco probabilities (player ~44.6%, banker ~45.8%, tie ~9.5%)
push=$($C baccarat player --seed 99 --iterations 50000 --stats --json |
       sed 's/.*"push":\([0-9]*\).*/\1/')
[ "$push" -gt 4200 ] && [ "$push" -lt 5400 ] && ok || \
    bad "bac push rate plausible (got $push / 50000)"

ln -sf casino baccarat
expect_grep "bac symlink invocation" '"game":"baccarat"' \
    sh -c "./baccarat banker --seed 3 --json"
rm -f baccarat

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
