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

# --- blackjack (6-deck shoe, S17, 3:2, splits, surrender, insurance) -----
# rule profile / shoe
expect_grep "bj 6-deck shoe is 312" '"shoe_size":312' \
    $C blackjack s --seed 1 --json
# every exposed and hidden card leaves the shoe: seed 1 deals 2 + 5 cards
expect_grep "bj shoe counts hole card too" '"shoe_remaining":305' \
    $C blackjack s --seed 1 --json
# the shoe persists between rounds rather than restarting at 312
n=$($C blackjack s --seed 4 --iterations 3 --json |
    sed -n '3p' | sed 's/.*"shoe_remaining":\([0-9]*\).*/\1/')
[ "$n" -lt 300 ] && ok || bad "bj shoe persists across rounds (got $n)"
# a reshuffle only happens between rounds, once past the cut card
sh_before=$($C blackjack s --seed 4 --iterations 120 --json |
    awk -F'"shoe_remaining":' '{split($2,a,","); r=a[1];
        if (prev != "" && $0 ~ /"shuffled":true/) print prev; prev=r}' |
    head -1)
[ -n "$sh_before" ] && [ "$sh_before" -le 78 ] && ok || \
    bad "bj reshuffles at the cut card (had $sh_before left)"
# card accounting is exact every round (no mid-hand reshuffle, no leaks)
$C blackjack s --seed 4 --iterations 60 --json | awk '
/"shuffled":true/ { split($0, x, "\"shoe_remaining\":"); split(x[2], y, ",");
                    prev = y[1]; next }
{
  n = gsub(/"[0-9AJQK][0-9a-z]?[cdhs]"/, "&");
  split($0, x, "\"shoe_remaining\":"); split(x[2], y, ","); rem = y[1];
  if (prev != "" && rem != prev - n) bad = 1;
  prev = rem
}
END { print (bad ? "LEAK" : "EXACT") }' | grep -q EXACT && ok || \
    bad "bj shoe accounting is exact every round"

a=$($C blackjack bet:50 h,s --seed 42 --json)
b=$($C blackjack bet:50 h,s --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "bj seeded runs identical"

# totals: hard vs soft, dealer stands on soft 17 (S17)
expect_grep "bj soft ace counts 11" '"cards":\["Ac","Ks"\],"total":21' \
    $C blackjack s --seed 13 --json
expect_grep "bj dealer stands on soft 17" '"dealer":\["6s","As"\],"dealer_total":17' \
    $C blackjack s --seed 13 --json

# natural blackjack pays 3:2 and is exact in half credits
expect_grep "bj natural detected" '"result":"BLACKJACK"' \
    $C blackjack s --seed 13 --json
expect_grep "bj natural pays 3:2" "^BLACKJACK net=37.5 bankroll=1037.5$" \
    $C blackjack s --seed 13 --quiet
# push returns the wager
expect_grep "bj push returns wager" "^PUSH net=0 bankroll=1000$" \
    $C blackjack s --seed 16 --quiet
# an ordinary win pays 1:1
expect_grep "bj win pays 1:1" "^WIN net=25 bankroll=1025$" \
    $C blackjack s --seed 1 --quiet

# double: wager doubles, exactly one card, then the hand stands
expect_grep "bj double doubles the wager" '"total":20,"wager":50.0,"doubled":true' \
    $C blackjack d --seed 1 --json
expect_grep "bj double takes one card only" '"cards":\["Qh","5s","5h"\]' \
    $C blackjack d --seed 1 --json

# surrender returns exactly half the wager
expect_grep "bj surrender pays half" "^SURRENDER net=-12.5 bankroll=987.5$" \
    $C blackjack r --seed 1 --quiet
expect_exit "bj surrender only on first two cards" 0 $C blackjack h,r --seed 1

# splits
expect_grep "bj split creates two hands" \
    '"cards":\["Qc","8s"\],"total":18,"wager":25.0,"doubled":false,"split":true' \
    $C blackjack p,s,s --seed 39 --json
expect_grep "bj split settles each hand" "^LOSS,LOSS net=-50 bankroll=950$" \
    $C blackjack p,s,s --seed 39 --quiet
expect_grep "bj resplit up to four hands" \
    '"cards":\["4d","As"\]' $C blackjack p,p,p,s,s,s,s --seed 5879 --json
n=$($C blackjack p,p,p,p,s,s,s,s --seed 5879 --json | grep -o '"cards"' | wc -l)
[ "$n" -eq 4 ] && ok || bad "bj never exceeds four hands (got $n)"
# double after split is allowed
expect_grep "bj DAS allowed" '"cards":\["4s","3s","4c"\],"total":11,"wager":50.0,"doubled":true' \
    $C blackjack p,d,s,s --seed 164 --json
# split aces take exactly one card each and stand
expect_grep "bj split aces one card" \
    '"cards":\["Ac","6h"\],"total":17,.*"cards":\["Ad","Qs"\],"total":21' \
    $C blackjack p --seed 393 --json
expect_grep "bj split aces cannot double" '"cards":\["Ac","6h"\],"total":17,"wager":25.0,"doubled":false' \
    $C blackjack p,d,d --seed 393 --json
# 21 on a split hand is an ordinary 21, never a natural
expect_grep "bj 21 after split is not blackjack" \
    '"cards":\["Ad","Qs"\],"total":21,"wager":25.0,"doubled":false,"split":true,"result":"WIN"' \
    $C blackjack p --seed 393 --json

# dealer peeks: a dealer natural ends the round before any action
expect_grep "bj dealer peek ends round" '"actions":\[\]' \
    $C blackjack p,s,s --seed 267 --json
expect_grep "bj dealer natural beats 12" '"dealer":\["Qh","Ah"\],"dealer_total":21' \
    $C blackjack s --seed 267 --json

# insurance: offered only against an ace, pays 2:1, settled independently
expect_grep "bj insurance wins on dealer natural" '"insurance":{"taken":true,"won":true}' \
    $C blackjack insurance,s --seed 25 --json
expect_grep "bj insurance hedges the loss" '"net":0.0' \
    $C blackjack insurance,s --seed 25 --json
expect_grep "bj insurance loses otherwise" '"insurance":{"taken":true,"won":false}' \
    $C blackjack insurance,s --seed 5 --json
expect_grep "bj insurance costs half the wager" "^LOSS net=-37.5" \
    $C blackjack insurance,s --seed 5 --quiet
expect_grep "bj declining insurance" '"insurance":{"taken":false,"won":false}' \
    $C blackjack noinsurance,s --seed 5 --json
expect_grep "bj no insurance without an ace" '"insurance":{"taken":false' \
    $C blackjack insurance,s --seed 1 --json

# bankroll: wagers are debited and settlements credited across rounds
expect_grep "bj bankroll tracks a loss" "bankroll=975$" \
    $C blackjack s --seed 5 --quiet
expect_grep "bj bet:N sets the wager" '"bet":100.0' \
    $C blackjack bet:100 s --seed 1 --json
expect_exit "bj bet below minimum"  2 $C blackjack bet:1 s
expect_exit "bj bet above maximum"  2 $C blackjack bet:5000 s
# an unaffordable double is refused and the hand simply stands
expect_grep "bj cannot afford double" '"doubled":false,"split":false,"result":"WIN"' \
    sh -c "$C blackjack bet:500 d,d --seed 8 --iterations 2 --json | tail -1"

# scripted actions
expect_exit "bj scripted h,s"          0 $C blackjack --seed 1 h,s
expect_exit "bj scripted long words"   0 $C blackjack --seed 1 hit,hit,stand
expect_exit "bj space separated"       0 $C blackjack --seed 1 h s
expect_exit "bj double first action"   0 $C blackjack --seed 2 d
expect_exit "bj unknown action"        2 $C blackjack banana
expect_exit "bj action takes no value" 2 $C blackjack h:1
expect_exit "bj empty action segment"  2 $C blackjack h,,s
# an illegal scripted action falls back to standing, so one script can
# drive a whole simulation without dying on an unsuitable hand
expect_grep "bj illegal action stands" '"actions":\["hit","stand"\]' \
    $C blackjack --seed 1 h,d,s --json

expect_grep "bj quiet line"     "^\(WIN\|LOSS\|PUSH\|BLACKJACK\|SURRENDER\).* net=.* bankroll=" \
    sh -c "$C blackjack --seed 5 s --quiet 2>/dev/null"
expect_grep "bj json game key"  '"game":"blackjack"' $C blackjack --seed 5 s --json
expect_grep "bj json result"    '"result":"'         $C blackjack --seed 5 s --json
expect_grep "bj json actions"   '"actions":\["stand"\]' $C blackjack --seed 1 s --json
expect_grep "bj transcript"     "Dealer:"            $C blackjack --seed 1 h,s
expect_grep "bj list-bets"      "6-deck"             $C blackjack --list-bets
expect_grep "bj list-bets surrender" "surrender"     $C blackjack --list-bets
expect_grep "bj description"    "6-deck shoe"        $C --help

n=$($C blackjack --seed 7 --iterations 5 s --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "bj iterations produce 5 lines (got $n)"

# stats
expect_grep "bj stats table" "RESULT" $C blackjack --seed 7 --iterations 200 --stats s
expect_grep "bj stats json"  '"iterations":200' \
    $C blackjack --seed 7 --iterations 200 --stats s --json
expect_grep "bj stats money" '"wagered":' \
    $C blackjack --seed 7 --iterations 200 --stats s --json
expect_grep "bj stats splits"  '"splits":' \
    $C blackjack --seed 7 --runs 200 p,s,s,s --json
# not every round reaches a surrender decision: a dealer natural ends
# some rounds at the peek
expect_grep "bj stats surrenders" '"surrenders":182' \
    $C blackjack --seed 7 --runs 200 r --json
# wagered/returned/net must agree
$C blackjack --seed 3 --runs 5000 h,s --json | sed 's/.*"wagered":\([0-9.]*\),"returned":\([0-9.]*\),"net":\([-0-9.]*\).*/\1 \2 \3/' |
awk '{ printf "%s\n", ($1 - $2 + $3 < 0.001 && $2 - $1 - $3 < 0.001) ? "OK" : "BAD" }' |
grep -q OK && ok || bad "bj stats net equals returned minus wagered"
# always-stand return is well below break even but not absurd
r=$($C blackjack --seed 11 --runs 20000 s --json |
    sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/')
case "$r" in 0.9*|0.8*) ok ;; *) bad "bj always-stand return plausible (got $r)" ;; esac

# simulation needs a script or --basic; interactive play still works
expect_exit "runs bj needs script"  2 $C blackjack --runs 10
expect_exit "bj interactive EOF"    0 sh -c "$C blackjack --seed 1 </dev/null"
expect_exit "bj piped hit"          0 sh -c "echo h | $C blackjack --seed 1"

# --- blackjack --basic (automatic basic strategy) ------------------------
# a single round plays itself: no stdin is read at all
expect_exit "bj basic single round"  0 \
    sh -c "$C blackjack --basic --seed 1 </dev/null"
expect_exit "bj basic runs"          0 $C blackjack --basic --runs 100 --seed 1
expect_exit "bj basic iterations"    0 \
    $C blackjack --basic --iterations 100 --stats --seed 1
expect_exit "bj basic with wager"    0 \
    $C blackjack bet:50 --basic --runs 100 --seed 1
expect_exit "bj basic quiet"         0 $C blackjack --basic --quiet --seed 1
expect_exit "bj basic json"          0 $C blackjack --basic --json --seed 1

# the action log records the automatic decisions exactly like scripted ones
expect_grep "bj basic json actions" '"actions":\["stand"\]' \
    $C blackjack --basic --seed 1 --json
expect_grep "bj basic transcript" "^> stand" $C blackjack --basic --seed 1
expect_grep "bj basic stats heading" "Strategy: basic" \
    $C blackjack --basic --runs 100 --seed 1

# a fixed seed is reproducible, and matches nothing about the scripted path
a=$($C blackjack --basic --seed 42 --json)
b=$($C blackjack --basic --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "bj basic seed is deterministic"

# basic strategy declines every insurance offer it is shown
$C blackjack --basic --runs 2000 --seed 3 --json |
    grep -q '"insurance_bets":0,"insurance_wins":0' &&
    ok || bad "bj basic never takes insurance"
n=$($C blackjack --basic --iterations 2000 --seed 3 --json |
    grep -c '"insurance":{"taken":true')
[ "$n" -eq 0 ] && ok || bad "bj basic declines insurance per round (got $n)"
n=$($C blackjack --basic --iterations 2000 --seed 3 --json |
    grep -c '"noinsurance"')
[ "$n" -gt 0 ] && ok || bad "bj basic logs noinsurance when offered (got $n)"

# automatic play reaches settlement on every round: no PENDING result
n=$($C blackjack --basic --iterations 2000 --seed 5 --json |
    grep -c '"result":"PENDING"')
[ "$n" -eq 0 ] && ok || bad "bj basic always settles (got $n pending)"

# the doubles, splits and multi-hand splits the chart calls for happen
$C blackjack --basic --runs 5000 --seed 5 --json | awk '
    { match($0, /"doubles":[0-9]+/); d = substr($0, RSTART + 10, RLENGTH - 10)
      match($0, /"splits":[0-9]+/);  s = substr($0, RSTART + 9, RLENGTH - 9)
      match($0, /"surrenders":[0-9]+/); r = substr($0, RSTART + 13, RLENGTH - 13)
      print (d > 0 && s > 0 && r > 0) ? "OK" : "BAD" }' |
    grep -q OK && ok || bad "bj basic doubles, splits and surrenders"
n=$($C blackjack --basic --iterations 4000 --seed 8 --json |
    grep -c '"split":true.*"split":true.*"split":true')
[ "$n" -gt 0 ] && ok || bad "bj basic plays multiple split hands (got $n)"

# a basic-strategy run beats always-stand and lands near break even
r=$($C blackjack --basic --runs 20000 --seed 11 --json |
    sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/')
case "$r" in 0.9[5-9]*|1.0*) ok ;; *) bad "bj basic return near break even (got $r)" ;; esac

# --basic replaces the decision source, so a script cannot come with it
expect_exit "bj basic with script"      2 $C blackjack h,s --basic --runs 100
expect_exit "bj basic with one action"  2 $C blackjack bet:25 s --basic
expect_exit "bj basic with long action" 2 $C blackjack stand --basic --seed 1
# ...but a wager is not an action
expect_exit "bj basic bet is not action" 0 $C blackjack bet:50 --basic --seed 1

# --basic is count-independent, so it is not a counting mode
expect_exit "bj basic with counting" 2 $C blackjack --basic --counting
expect_exit "bj basic with gui"      2 $C blackjack --gui --basic
# and it is blackjack only
expect_exit "basic rejected roulette" 2 $C roulette red --basic
expect_exit "basic rejected videopoker" 2 $C videopoker --basic
expect_grep "basic rejected message" "only available for blackjack" \
    sh -c "$C roulette red --basic 2>&1"

expect_grep "basic in global help" "basic.*blackjack only" $C --help
expect_grep "basic in bj help" "S17, DAS, late" $C blackjack --list-bets

# --- blackjack --count-bet (Hi-Lo true-count bet ramp) -------------------
expect_exit "bj count-bet single round" 0 \
    sh -c "$C blackjack --basic --count-bet --seed 123 </dev/null"
expect_exit "bj count-bet runs"       0 \
    $C blackjack --basic --count-bet --runs 100 --seed 1
expect_exit "bj count-bet iterations" 0 \
    $C blackjack --basic --count-bet --iterations 100 --stats --seed 1
expect_exit "bj count-bet quiet"      0 \
    $C blackjack --basic --count-bet --quiet --runs 100 --seed 1
expect_exit "bj count-bet json"       0 \
    $C blackjack --basic --count-bet --json --runs 100 --seed 1

# a visible round shows the count and the wager it chose, before the cards
expect_grep "bj count-bet shows count" "Running count: +0" \
    $C blackjack --basic --count-bet --seed 1
expect_grep "bj count-bet shows true count" "True count: +0.0" \
    $C blackjack --basic --count-bet --seed 1
expect_grep "bj count-bet shows ramp" "Bet ramp: 1 unit" \
    $C blackjack --basic --count-bet --seed 1
expect_grep "bj count-bet shows wager" "^Wager: 25" \
    $C blackjack --basic --count-bet --seed 1
# a fresh shoe is a zero count, so the opening wager is one unit
expect_grep "bj count-bet opens at one unit" "^Wager: 10" \
    $C blackjack bet:10 --basic --count-bet --seed 1

# stats output in all three formats
expect_grep "bj count-bet stats heading" "Betting: Hi-Lo true-count 1-8 spread" \
    $C blackjack --basic --count-bet --runs 200 --seed 7
expect_grep "bj count-bet stats table" "OPENING BET" \
    $C blackjack --basic --count-bet --runs 200 --seed 7
expect_grep "bj count-bet stats bands" "+4 or more" \
    $C blackjack --basic --count-bet --runs 200 --seed 7
expect_grep "bj count-bet quiet keys" "strategy=basic betting=count" \
    $C blackjack --basic --count-bet --runs 200 --seed 7 --quiet
expect_grep "bj count-bet quiet buckets" "bet_1u=[0-9]* bet_2u=[0-9]*" \
    $C blackjack --basic --count-bet --runs 200 --seed 7 --quiet
expect_grep "bj count-bet json strategy" '"strategy":"basic"' \
    $C blackjack --basic --count-bet --runs 200 --seed 7 --json
expect_grep "bj count-bet json betting" \
    '"betting":{"mode":"hilo_true_count","spread":"1-8"' \
    $C blackjack --basic --count-bet --runs 200 --seed 7 --json
# a flat game says so too, and grows no betting object
expect_grep "bj flat json strategy" '"strategy":"basic"}' \
    $C blackjack --basic --runs 200 --seed 7 --json

# a fixed seed is reproducible
a=$($C blackjack --basic --count-bet --runs 200 --seed 42 --json)
b=$($C blackjack --basic --count-bet --runs 200 --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "bj count-bet seed is deterministic"

# every round is bet from exactly one ramp step and one true-count band
n=$($C blackjack --basic --count-bet --runs 2000 --seed 3 --json |
    sed 's/.*"multipliers":{\([^}]*\)}.*/\1/' | tr ',' '\n' |
    sed 's/.*://' | awk '{ s += $1 } END { print s }')
[ "$n" -eq 2000 ] && ok || bad "bj count-bet multiplier buckets sum (got $n)"
n=$($C blackjack --basic --count-bet --runs 2000 --seed 3 --json |
    sed 's/.*"true_count":{\([^}]*\)}.*/\1/' | tr ',' '\n' |
    sed 's/.*://' | awk '{ s += $1 } END { print s }')
[ "$n" -eq 2000 ] && ok || bad "bj count-bet true-count buckets sum (got $n)"

# the spread is really used: the ramp reaches its top and its bottom
$C blackjack --basic --count-bet --runs 2000 --seed 3 --json | awk '
    { match($0, /"1":[0-9]+/); one = substr($0, RSTART + 4, RLENGTH - 4)
      match($0, /"8":[0-9]+/); top = substr($0, RSTART + 4, RLENGTH - 4)
      print (one + 0 > 0 && top + 0 > 0 && one + 0 < 2000) ? "OK" : "BAD" }' |
    grep -q OK && ok || bad "bj count-bet spreads its wagers"

# the opening wager stays inside the ramp: 25 up to 8 x 25, average between
$C blackjack --basic --count-bet --runs 2000 --seed 3 --json |
    sed 's/.*"average_initial_bet":\([0-9.]*\),"minimum_initial_bet":\([0-9.]*\),"maximum_initial_bet":\([0-9.]*\).*/\1 \2 \3/' |
    awk '{ print ($2 <= $1 && $1 <= $3 && $2 >= 5 && $3 == 200) ? "OK" : "BAD" }' |
    grep -q OK && ok || bad "bj count-bet opening wager average, min and max"

# bet:N is the unit, not a fixed wager, and the table maximum caps the top
expect_grep "bj count-bet unit from bet" '"base_unit":10.0' \
    $C blackjack bet:10 --basic --count-bet --runs 200 --seed 3 --json
expect_grep "bj count-bet unit ramps" '"maximum_initial_bet":80.0' \
    $C blackjack bet:10 --basic --count-bet --runs 2000 --seed 3 --json
$C blackjack bet:100 --basic --count-bet --runs 2000 --seed 3 --json | awk '
    { match($0, /"maximum_initial_bet":[0-9.]+/)
      m = substr($0, RSTART + 22, RLENGTH - 22)
      match($0, /"capped_table":[0-9]+/); c = substr($0, RSTART + 15, RLENGTH - 15)
      print (m + 0 == 500 && c + 0 > 0) ? "OK" : "BAD" }' |
    grep -q OK && ok || bad "bj count-bet table maximum caps the spread"

# it is bet sizing only: insurance is still declined every time
$C blackjack --basic --count-bet --runs 2000 --seed 3 --json |
    grep -q '"insurance_bets":0,"insurance_wins":0' &&
    ok || bad "bj count-bet never takes insurance"
# and the ramp is worth something: it beats a flat basic-strategy game
$C blackjack --basic --count-bet --runs 20000 --seed 11 --json |
    sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/' |
    awk '{ print ($1 > 0.99 && $1 < 1.05) ? "OK" : "BAD" }' |
    grep -q OK && ok || bad "bj count-bet return above a flat game"

# --count-bet sizes wagers for a hand the strategy engine plays
expect_exit "bj count-bet needs basic"   2 $C blackjack --count-bet --runs 1000
expect_exit "bj count-bet not scripted"  2 $C blackjack s --count-bet --runs 1000
expect_grep "bj count-bet needs basic message" "requires --basic" \
    sh -c "$C blackjack --count-bet --runs 1000 2>&1"
expect_exit "bj count-bet with gui"      2 $C blackjack --gui --count-bet
expect_exit "bj count-bet with counting" 2 $C blackjack --counting --count-bet
expect_exit "count-bet rejected roulette"   2 $C roulette red --count-bet
expect_exit "count-bet rejected videopoker" 2 $C videopoker --count-bet
expect_grep "count-bet rejected message" "only available for blackjack" \
    sh -c "$C roulette red --count-bet 2>&1"

expect_grep "count-bet in global help" "count-bet.*blackjack --basic only" \
    $C --help
expect_grep "count-bet in bj help" "TC +4+ *8 units" $C blackjack --list-bets

ln -sf casino blackjack
expect_grep "bj symlink invocation" '"game":"blackjack"' \
    sh -c "./blackjack --seed 42 s --json"
rm -f blackjack

# between-round phases: the GUI deals, re-bets and re-buys from a settled
# table, which the CLI never does (it plays one round per iteration), so
# the engine API is exercised directly.  See tests/bj_phase.c.
if ${CC:-cc} -std=c11 -Isrc -o build/bj_phase_test tests/bj_phase.c \
        src/games/bj_strategy.c src/games/blackjack.c src/cardart.c \
        src/cards.c src/cli.c src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "bj settled is a between-round phase" 0 ./build/bj_phase_test
else
    bad "bj phase test did not build"
fi

# Hi-Lo counting: the tag table, counting each card exactly once, the
# face-down hole card and the reset on a reshuffle.  See tests/bj_count.c.
if ${CC:-cc} -std=c11 -Isrc -o build/bj_count_test tests/bj_count.c \
        src/games/bj_strategy.c src/games/blackjack.c src/cardart.c \
        src/cards.c src/cli.c src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "bj hi-lo counting checks" 0 ./build/bj_count_test
else
    bad "bj counting test did not build"
fi

# basic strategy for the engine's own rules (6 deck, S17, DAS, late
# surrender, peek): the chart itself, the legal fallbacks and count
# independence.  See tests/bj_strategy.c.
if ${CC:-cc} -std=c11 -Isrc -o build/bj_strategy_test tests/bj_strategy.c \
        src/games/bj_strategy.c src/games/blackjack.c src/cardart.c \
        src/cards.c src/cli.c src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "bj basic strategy checks" 0 ./build/bj_strategy_test
else
    bad "bj strategy test did not build"
fi

# --basic drives the engine one decision at a time: every recommendation
# legal, every round settled, insurance declined, and the bankroll
# fallbacks reached.  See tests/bj_basic.c.
if ${CC:-cc} -std=c11 -Isrc -o build/bj_basic_test tests/bj_basic.c \
        src/games/bj_strategy.c src/games/blackjack.c src/cardart.c \
        src/cards.c src/cli.c src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "bj automatic basic play checks" 0 ./build/bj_basic_test
else
    bad "bj automatic basic play test did not build"
fi

# the Hi-Lo bet ramp: its boundaries, the table and bankroll limits, and
# the sequencing around a round (count settled and shoe decided before the
# wager is chosen).  See tests/bj_countbet.c.
if ${CC:-cc} -std=c11 -Isrc -o build/bj_countbet_test tests/bj_countbet.c \
        src/games/bj_strategy.c src/games/blackjack.c src/cardart.c \
        src/cards.c src/cli.c src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "bj true-count bet ramp checks" 0 ./build/bj_countbet_test
else
    bad "bj true-count bet ramp test did not build"
fi

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

# --- slots --------------------------------------------------------------
expect_exit "slots spin exits 0"     0 $C slots --seed 1
expect_exit "slots takes no bets"    2 $C slots red
expect_exit "slots bet with value"   2 $C slots line:1

# every payline payout category, pinned to deterministic seeds
expect_grep "slots jackpot 3xSEVEN" "^JACKPOT$"   $C slots --seed 253 --quiet
expect_grep "slots big win 3xBAR"   "^BIG_WIN$"   $C slots --seed 84  --quiet
expect_grep "slots 3xBELL win"      "^WIN$"       $C slots --seed 29  --quiet
expect_grep "slots 3xCHERRY win"    "^WIN$"       $C slots --seed 476 --quiet
expect_grep "slots two-cherry"      "^SMALL_WIN$" $C slots --seed 3   --quiet
expect_grep "slots loss"            "^LOSS$"      $C slots --seed 1   --quiet
# three of a non-paying symbol on the payline is still a loss
expect_grep "slots 3xLEMON loses"   "^LOSS$"      $C slots --seed 7   --quiet

a=$($C slots --seed 123 --json)
b=$($C slots --seed 123 --json)
[ "$a" = "$b" ] && ok || bad "slots seeded runs identical"

# 3x3 window: 3 rows rendered, middle row marked as the payline
expect_grep "slots window top border" "┌──────────┬──────────┬──────────┐" \
    $C slots --seed 1
expect_grep "slots window payline marker" "<- PAYLINE" $C slots --seed 1
n=$($C slots --seed 1 | grep -c "│")
[ "$n" -eq 3 ] && ok || bad "slots window has 3 symbol rows (got $n)"

# columns are adjacent circular strip entries: seed 8 stops reel 1 at the
# last strip index (bottom wraps to index 0), seed 9 at index 0 (top wraps
# to the last index).  Windows verified against the REEL_A strip.
expect_grep "slots wraparound at last index" \
    '"window":\[\["SEVEN","LEMON","CHERRY"\],\["LEMON","CHERRY","LEMON"\],\["CHERRY","ORANGE","BAR"\]\]' \
    $C slots --seed 8 --json
expect_grep "slots wraparound at index 0" \
    '"window":\[\["LEMON","LEMON","ORANGE"\],\["CHERRY","BAR","SEVEN"\],\["LEMON","ORANGE","LEMON"\]\]' \
    $C slots --seed 9 --json
# seed 8's full window holds three CHERRYs but the payline has only one:
# top/bottom rows must not pay
expect_grep "slots top/bottom rows never pay" "^LOSS$" $C slots --seed 8 --quiet

expect_grep "slots payout line"   "Payout: 100:1" $C slots --seed 253
expect_grep "slots json game key" '"game":"slots"'  $C slots --seed 253 --json
expect_grep "slots json reels kept" '"reels":\["SEVEN","SEVEN","SEVEN"\]' \
    $C slots --seed 253 --json
expect_grep "slots json payline"  '"payline":\["SEVEN","SEVEN","SEVEN"\]' \
    $C slots --seed 253 --json
expect_grep "slots json window middle is payline" \
    '\],\["SEVEN","SEVEN","SEVEN"\],\[' $C slots --seed 253 --json
expect_grep "slots json result"   '"result":"jackpot"' $C slots --seed 253 --json
expect_grep "slots json payout"   '"payout":"100:1"'   $C slots --seed 253 --json
expect_grep "slots help works"    "usage:"    $C slots --help
expect_grep "slots list-bets"     "JACKPOT"   $C slots --list-bets

n=$($C slots --seed 7 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "slots iterations produce 5 lines (got $n)"

expect_grep "slots stats table" "RESULT" $C slots --seed 3 --iterations 200 --stats
expect_grep "slots stats json"  '"iterations":200' \
    $C slots --seed 3 --iterations 200 --stats --json

# sanity: payline jackpot over 100k seeded spins near (2/20)^3 = 0.1%
jp=$($C slots --seed 3 --iterations 100000 --stats --json |
     sed 's/.*"jackpot":\([0-9]*\).*/\1/')
[ "$jp" -gt 40 ] && [ "$jp" -lt 200 ] && ok || \
    bad "slots jackpot rate plausible (got $jp / 100000)"

ln -sf casino slots
expect_grep "slots symlink invocation" '"game":"slots"' \
    sh -c "./slots --seed 1 --json"
rm -f slots

# --- craps --------------------------------------------------------------
# validation
expect_exit "craps no bet"          2 $C craps --seed 1
expect_exit "craps unknown bet"     2 $C craps banana
expect_exit "craps hard:5"          2 $C craps hard:5
expect_exit "craps hard no value"   2 $C craps hard
expect_exit "craps hard bad value"  2 $C craps hard:x
expect_exit "craps pass with value" 2 $C craps pass:1
expect_exit "craps multi bets ok"   0 $C craps pass field hard:8 --seed 3

# pass line branches (comeout: seed 1 = 7, 13 = 3, 20 = 2, 21 = 12)
expect_grep "craps pass natural 7 win" "^WIN$"  $C craps pass --seed 1 --quiet
expect_grep "craps pass one-roll round" '"rolls":\[\[2,5\]\]' \
    $C craps pass --seed 1 --json
expect_grep "craps pass craps-3 loss"  "^LOSS$" $C craps pass --seed 13 --quiet
expect_grep "craps pass craps-2 loss"  "^LOSS$" $C craps pass --seed 20 --quiet
expect_grep "craps pass craps-12 loss" "^LOSS$" $C craps pass --seed 21 --quiet
expect_grep "craps point established"  '"point":8' $C craps pass --seed 3 --json
expect_grep "craps seven-out loses"    "^LOSS$" $C craps pass --seed 3 --quiet
expect_grep "craps point made wins"    "^WIN$"  $C craps pass --seed 8 --quiet
expect_grep "craps transcript" "Come-out: " $C craps pass --seed 3

# don't pass branches (same dice as pass, mirrored)
expect_grep "craps dp 3 wins"     "^WIN$"  $C craps dont-pass --seed 13 --quiet
expect_grep "craps dp 2 wins"     "^WIN$"  $C craps dont-pass --seed 20 --quiet
expect_grep "craps dp 7 loses"    "^LOSS$" $C craps dont-pass --seed 1 --quiet
expect_grep "craps dp 12 pushes"  "^PUSH$" $C craps dont-pass --seed 21 --quiet
expect_grep "craps dp seven-out wins" "^WIN$" $C craps dont-pass --seed 3 --quiet
expect_grep "craps dontpass alias ok" "^WIN$" $C craps dontpass --seed 3 --quiet

# field (come-out roll only; 2 pays 2:1, 12 pays 3:1)
expect_grep "craps field 2 wins 2:1"  '"payout":"2:1"' $C craps field --seed 20 --json
expect_grep "craps field 12 wins 3:1" '"payout":"3:1"' $C craps field --seed 21 --json
expect_grep "craps field 8 loses"     "^LOSS$" $C craps field --seed 3 --quiet

# hardways: every win, loss by easy way, loss by 7, unresolved push
expect_grep "craps hard:4 win 7:1"  '"result":"win","payout":"7:1"' \
    $C craps hard:4 --seed 12 --json
expect_grep "craps hard:6 win 9:1"  '"result":"win","payout":"9:1"' \
    $C craps hard:6 --seed 3 --json
expect_grep "craps hard:8 win"      "^WIN$" $C craps hard:8 --seed 8 --quiet
expect_grep "craps hard:10 win"     "^WIN$" $C craps hard:10 --seed 7 --quiet
# seed 3 come-out is 3+5=8: hard:8 loses the easy way immediately
expect_grep "craps hard:8 easy-way loss" "^LOSS$" $C craps hard:8 --seed 3 --quiet
# seed 3 never rolls a 4 before the seven-out: hard:4 loses to the 7
expect_grep "craps hard:4 seven loss"    "^LOSS$" $C craps hard:4 --seed 3 --quiet
# seed 2 resolves on the come-out (11): hard:10 is never decided
expect_grep "craps hard:10 push"         "^PUSH$" $C craps hard:10 --seed 2 --quiet

# output modes and determinism
expect_grep "craps json game key" '"game":"craps"' $C craps pass --seed 3 --json
expect_grep "craps json rolls"    '"rolls":\[\[3,5\]' $C craps pass --seed 3 --json
expect_grep "craps quiet multi"   "^pass=L field=L hard:8=L$" \
    $C craps pass field hard:8 --seed 3 --quiet
expect_grep "craps help works"    "usage:"    $C craps --help
expect_grep "craps list-bets"     "come-out"  $C craps --list-bets

a=$($C craps pass field hard:8 --seed 42 --json)
b=$($C craps pass field hard:8 --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "craps seeded runs identical"

n=$($C craps pass --seed 7 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "craps iterations produce 5 lines (got $n)"

expect_grep "craps stats table" "PUSHES" \
    $C craps pass --seed 7 --iterations 200 --stats
expect_grep "craps stats json"  '"iterations":200' \
    $C craps pass --seed 7 --iterations 200 --stats --json

# sanity: pass wins ~49.3% of rounds over 50k seeded rounds
pw=$($C craps pass --seed 11 --iterations 50000 --stats --json |
     sed 's/.*"wins":\([0-9]*\).*/\1/')
[ "$pw" -gt 23800 ] && [ "$pw" -lt 25400 ] && ok || \
    bad "craps pass win rate plausible (got $pw / 50000)"

ln -sf casino craps
expect_grep "craps symlink invocation" '"game":"craps"' \
    sh -c "./craps pass --seed 3 --json"
rm -f craps

# --- videopoker ---------------------------------------------------------
# every hand category, deterministic via the deal: evaluation hook
expect_grep "vp royal flush"      "^ROYAL_FLUSH$"     $C videopoker deal:10h,jh,qh,kh,ah --quiet
expect_grep "vp straight flush"   "^STRAIGHT_FLUSH$"  $C videopoker deal:5s,6s,7s,8s,9s --quiet
expect_grep "vp four of a kind"   "^FOUR_OF_A_KIND$"  $C videopoker deal:9c,9d,9h,9s,2c --quiet
expect_grep "vp full house"       "^FULL_HOUSE$"      $C videopoker deal:kc,kd,kh,2s,2c --quiet
expect_grep "vp flush"            "^FLUSH$"           $C videopoker deal:2h,5h,9h,jh,kh --quiet
expect_grep "vp straight"         "^STRAIGHT$"        $C videopoker deal:5c,6d,7h,8s,9c --quiet
expect_grep "vp ace-low straight" "^STRAIGHT$"        $C videopoker deal:ac,2d,3h,4s,5c --quiet
expect_grep "vp ace-high straight" "^STRAIGHT$"       $C videopoker deal:10c,jd,qh,ks,ac --quiet
expect_grep "vp no wraparound"    "^HIGH_CARD$"       $C videopoker deal:qc,kd,ah,2s,3c --quiet
expect_grep "vp three of a kind"  "^THREE_OF_A_KIND$" $C videopoker deal:7c,7d,7h,2s,9c --quiet
expect_grep "vp two pair"         "^TWO_PAIR$"        $C videopoker deal:4c,4d,9h,9s,kc --quiet
expect_grep "vp pair of jacks"    "^JACKS_OR_BETTER$" $C videopoker deal:jc,jd,3h,7s,9c --quiet
expect_grep "vp pair of aces"     "^JACKS_OR_BETTER$" $C videopoker deal:ac,ad,3h,7s,9c --quiet
expect_grep "vp pair of tens is low" "^LOW_PAIR$"     $C videopoker deal:10c,10d,3h,7s,9c --quiet
expect_grep "vp high card"        "^HIGH_CARD$"       $C videopoker deal:2c,5d,9h,jc,kh --quiet
expect_grep "vp royal payout 250" '"payout":250' \
    $C videopoker deal:10h,jh,qh,kh,ah --json

# holds: none, all, partial with position semantics (seed 1 deals
# Kd 7c Jc 6h 5c; holding 1,3 must keep Kd and Jc in place)
expect_exit "vp hold none"  0 $C videopoker hold:none --seed 1
expect_grep "vp hold all keeps hand" \
    '"initial_hand":\["Kd","7c","Jc","6h","5c"\],"held":\[1,2,3,4,5\],"final_hand":\["Kd","7c","Jc","6h","5c"\]' \
    $C videopoker hold:all --seed 1 --json
expect_grep "vp partial hold positions" '"final_hand":\["Kd",.*,"Jc",' \
    $C videopoker hold:1,3 --seed 1 --json
expect_grep "vp held array" '"held":\[1,3\]' \
    $C videopoker hold:1,3 --seed 1 --json
expect_grep "vp hold none redraws all" '"held":\[\]' \
    $C videopoker hold:none --seed 1 --json

# validation
expect_exit "vp duplicate hold"    2 $C videopoker hold:1,1
expect_exit "vp hold out of range" 2 $C videopoker hold:6
expect_exit "vp hold zero"         2 $C videopoker hold:0
expect_exit "vp malformed hold"    2 $C videopoker hold:x
expect_exit "vp bare hold"         2 $C videopoker hold
expect_exit "vp multiple holds"    2 $C videopoker hold:1 hold:2
expect_exit "vp unknown argument"  2 $C videopoker red
expect_exit "vp malformed deal"    2 $C videopoker deal:zz
expect_exit "vp short deal"        2 $C videopoker deal:ah,kh
expect_exit "vp duplicate card"    2 $C videopoker deal:ah,ah,2c,3c,4c
expect_exit "vp deal plus hold"    2 $C videopoker deal:2c,5d,9h,jc,kh hold:1

# --optimal is the GUI strategy trainer: videopoker --gui only
expect_exit "vp optimal needs gui"   2 $C videopoker --optimal
expect_exit "vp optimal alone+seed"  2 $C videopoker --optimal --seed 1
expect_exit "vp optimal rejects bets" 2 $C videopoker --gui --optimal hold:1
expect_exit "vp optimal rejects json" 2 $C videopoker --gui --optimal --json
expect_exit "bj optimal rejected"    2 $C blackjack --gui --optimal
expect_exit "bac optimal rejected"   2 $C baccarat --gui --optimal
expect_exit "roulette optimal rejected" 2 $C roulette --optimal
expect_grep "vp optimal in help"     "optimal.*GUI strategy" $C videopoker --help
expect_grep "vp optimal in list-bets" "gui --optimal" $C videopoker --list-bets

# the terminal trainer is unaffected by the GUI trainer
expect_grep "vp trainer runs"    "VIDEO POKER TRAINER" \
    sh -c "echo 1,3 | $C videopoker --trainer --seed 1"
expect_grep "vp trainer grades"  "^OPTIMAL$\|^SUBOPTIMAL$" \
    sh -c "echo 1,3 | $C videopoker --trainer --seed 1"
expect_grep "vp trainer summary" "Optimal decisions: 1" \
    sh -c "echo 1,3 | $C videopoker --trainer --seed 1"

# output modes and determinism
a=$($C videopoker hold:1,3 --seed 123 --json)
b=$($C videopoker hold:1,3 --seed 123 --json)
[ "$a" = "$b" ] && ok || bad "vp seeded runs identical"

expect_grep "vp json game key" '"game":"videopoker"' \
    $C videopoker hold:none --seed 1 --json
expect_grep "vp json variant"  '"variant":"jacks_or_better"' \
    $C videopoker hold:none --seed 1 --json
expect_grep "vp transcript"    "Final hand:" $C videopoker hold:1,3 --seed 1
expect_grep "vp help works"    "usage:"      $C videopoker --help
expect_grep "vp list-bets"     "ROYAL_FLUSH" $C videopoker --list-bets

# interactive: piped holds are used, EOF keeps the dealt hand
expect_exit "vp interactive piped" 0 sh -c "echo 1,3 | $C videopoker --seed 1"
expect_grep "vp interactive EOF quiet clean" "^HIGH_CARD$" \
    sh -c "$C videopoker --seed 1 --quiet </dev/null 2>/dev/null"

n=$($C videopoker hold:none --seed 7 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "vp iterations produce 5 lines (got $n)"

expect_grep "vp stats table" "CATEGORY" \
    $C videopoker hold:none --seed 7 --iterations 200 --stats
expect_grep "vp stats json"  '"iterations":200' \
    $C videopoker hold:none --seed 7 --iterations 200 --stats --json

# sanity: any-pair rate over 100k discard-all hands near 42.3%
pr=$($C videopoker hold:none --seed 5 --iterations 100000 --stats --json |
     sed 's/.*"low_pair":\([0-9]*\).*/\1/')
[ "$pr" -gt 27500 ] && [ "$pr" -lt 31000 ] && ok || \
    bad "vp low pair rate plausible (got $pr / 100000)"

ln -sf casino videopoker
expect_grep "vp symlink invocation" '"game":"videopoker"' \
    sh -c "./videopoker hold:none --seed 1 --json"
rm -f videopoker

# --- ASCII card art (CASINO_CARDS=art forces on; piped default is plain) --
expect_grep "art card border"  "┌─────────┐ ┌─────────┐" \
    env CASINO_CARDS=art $C baccarat player --seed 2
expect_grep "art rank 10 left"  "│10       │" \
    env CASINO_CARDS=art $C videopoker deal:10h,jh,qh,kh,ah
expect_grep "art rank 10 right" "│       10│" \
    env CASINO_CARDS=art $C videopoker deal:10h,jh,qh,kh,ah
expect_grep "art ace"    "│A        │" env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
expect_grep "art spades" "♠" env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
expect_grep "art diamonds" "♦" env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
expect_grep "art hearts" "♥" env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
expect_grep "art clubs"  "♣" env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
expect_grep "art hidden hole card" "│░░░░░░░░░│" \
    env CASINO_CARDS=art $C blackjack --seed 1 s
expect_grep "art vp positions" "1           2           3           4           5" \
    env CASINO_CARDS=art $C videopoker deal:as,7d,kh,2c,5s
# machine output is art-free even when art is forced
expect_grep "art quiet unchanged" "^\(WIN\|LOSS\|PUSH\|BLACKJACK\).* net=" \
    sh -c "CASINO_CARDS=art $C blackjack --seed 5 s --quiet 2>/dev/null"
expect_grep "art json unchanged" '^{"game":"blackjack"' \
    sh -c "CASINO_CARDS=art $C blackjack --seed 5 s --json 2>/dev/null"
# piped (non-TTY) default stays plain
if $C blackjack --seed 1 s | grep -q "┌"; then
    bad "piped output stays plain"
else
    ok
fi

# --- --runs (shared simulation mode: --iterations N + implied --stats) ---
expect_exit "runs=1 valid"        0 $C roulette red --runs 1 --seed 1
expect_exit "runs=10 valid"       0 $C slots --runs 10 --seed 1
expect_exit "runs zero rejected"  2 $C roulette red --runs 0
expect_exit "runs negative"       2 $C roulette red --runs -5
expect_exit "runs malformed"      2 $C roulette red --runs x
expect_exit "runs fractional"     2 $C roulette red --runs 1.5

# games that cannot run non-interactively must refuse simulation
expect_exit "runs bj needs script"  2 $C blackjack --runs 10
expect_exit "runs vp needs hold"    2 $C videopoker --runs 10
expect_exit "runs bj script ok"     0 $C blackjack s --runs 10 --seed 1
expect_exit "runs vp hold ok"       0 $C videopoker hold:none --runs 10 --seed 1

# deterministic seeded aggregates
a=$($C craps pass --runs 10000 --seed 42 --json)
b=$($C craps pass --runs 10000 --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "runs seeded aggregates identical"

# counters sum to runs (baccarat quiet line: wins+losses+pushes == runs)
line=$($C baccarat banker --runs 1000 --seed 9 --quiet)
sum=$(echo "$line" | sed 's/.*wins=\([0-9]*\) losses=\([0-9]*\) pushes=\([0-9]*\).*/\1+\2+\3/')
[ "$(( $sum ))" -eq 1000 ] && ok || bad "runs counters sum to N (got $line)"
# and the outcome distribution sums too
dsum=$(echo "$line" | sed 's/.*player=\([0-9]*\) banker=\([0-9]*\) tie=\([0-9]*\).*/\1+\2+\3/')
[ "$(( $dsum ))" -eq 1000 ] && ok || bad "runs distribution sums to N"

# per-game stats shapes
expect_grep "runs roulette expected" "EXP%" $C roulette red --runs 100 --seed 1
expect_grep "runs roulette json expected" '"expected_hit_rate":0.486486' \
    $C roulette red --runs 100 --seed 1 --json
expect_grep "runs bj busts"     "dealer busts"    $C blackjack s --runs 100 --seed 1
expect_grep "runs bj json busts" '"player_busts":' \
    $C blackjack s --runs 100 --seed 1 --json
expect_grep "runs bac outcomes" "OUTCOME"         $C baccarat banker --runs 100 --seed 1
expect_grep "runs bac json distribution" '"distribution":{"player":' \
    $C baccarat banker --runs 100 --seed 1 --json
expect_grep "runs craps avg rolls" "Avg rolls/round:" $C craps pass --runs 100 --seed 1
expect_grep "runs craps json avg"  '"avg_rolls":'  $C craps pass --runs 100 --seed 1 --json
expect_grep "runs slots quiet"  "^runs=100 jackpot=" $C slots --runs 100 --seed 1 --quiet
expect_grep "runs vp quiet"     " return="        $C videopoker hold:none --runs 100 --seed 1 --quiet
expect_grep "runs dice json"    '"game":"dice"'   $C dice 2d6 total:7 --runs 100 --seed 1 --json
expect_grep "runs coin quiet"   "^bet=heads runs=100 wins=" \
    $C coin heads --runs 100 --seed 1 --quiet
expect_grep "runs quiet one-liner" \
    "^bet=red runs=1000 wins=[0-9]* losses=[0-9]* hit_rate=0\." \
    $C roulette red --runs 1000 --seed 123 --quiet

# no per-round output leaks in runs mode
n=$($C roulette red --runs 1000 --seed 1 --quiet | wc -l)
[ "$n" -eq 1 ] && ok || bad "runs quiet is a single line (got $n)"
n=$($C blackjack s --runs 1000 --seed 1 --json | wc -l)
[ "$n" -eq 1 ] && ok || bad "runs json is a single line (got $n)"

# large run: seeded 100k spins, wins deterministic and plausible
w=$($C roulette red --runs 100000 --seed 9 --json |
    sed 's/.*"wins":\([0-9]*\).*/\1/')
[ "$w" -gt 47500 ] && [ "$w" -lt 49800 ] && ok || \
    bad "runs 100k plausible (got $w)"

# --- videopoker EV solver / trainer --------------------------------------
# exactly 32 hold masks, with the correct combination counts
n=$($C videopoker solve:2c,7d,9h,4s,10c | grep -c "^hold=")
[ "$n" -eq 32 ] && ok || bad "solver evaluates 32 masks (got $n)"
expect_grep "solver C(47,5) draws" "^hold=none draws=1533939 " \
    $C videopoker solve:2c,7d,9h,4s,10c
expect_grep "solver C(47,4) draws" "^hold=1 draws=178365 " \
    $C videopoker solve:2c,7d,9h,4s,10c
expect_grep "solver C(47,3) draws" "^hold=1,2 draws=16215 " \
    $C videopoker solve:2c,7d,9h,4s,10c
expect_grep "solver C(47,2) draws" "^hold=1,2,3 draws=1081 " \
    $C videopoker solve:2c,7d,9h,4s,10c
expect_grep "solver C(47,1) draws" "^hold=1,2,3,4 draws=47 " \
    $C videopoker solve:2c,7d,9h,4s,10c
expect_grep "solver pat-hand draws" "^hold=1,2,3,4,5 draws=1 " \
    $C videopoker solve:2c,7d,9h,4s,10c

# known optimal plays
expect_grep "solver pat royal"     "optimal: hold=1,2,3,4,5 ev=250.0000" \
    $C videopoker solve:10h,jh,qh,kh,ah
expect_grep "solver 4 to royal beats pair" "optimal: hold=1,2,3,4 ev=7.9574" \
    $C videopoker solve:10s,js,qs,ks,kd
expect_grep "solver four of a kind" "ev=25.0000" \
    $C videopoker solve:2c,2d,2h,2s,kc
expect_grep "solver pat full house" "optimal: hold=1,2,3,4,5 ev=9.0000" \
    $C videopoker solve:kc,kd,kh,2s,2c
expect_grep "solver high pair"     "optimal: hold=1,2 ev=1.5365" \
    $C videopoker solve:jc,jd,3h,7s,9c
expect_grep "solver low pair"      "optimal: hold=1,2 ev=0.8237" \
    $C videopoker solve:10c,10d,3h,7s,9c
expect_grep "solver 4 to flush"    "optimal: hold=1,2,3,4 ev=1.2128" \
    $C videopoker solve:2h,5h,9h,jh,3c
expect_grep "solver worthless hand" "optimal: hold=none" \
    $C videopoker solve:2c,7d,9h,4s,10c

# exact ties are recognised: quads pay 25 whether the kicker is held or not
n=$($C videopoker solve:2c,2d,2h,2s,kc | grep -c " \*$")
[ "$n" -eq 2 ] && ok || bad "solver marks both equal-EV quad holds (got $n)"

# solver validation
expect_exit "solver malformed hand" 2 $C videopoker solve:zz
expect_exit "solver extra args"     2 $C videopoker solve:2c,7d,9h,4s,10c hold:1

# trainer: piped session, deterministic seed, session stats
expect_grep "trainer suboptimal verdict" "SUBOPTIMAL" \
    sh -c "printf '4,5\n\n' | $C videopoker --trainer --seed 5"
expect_grep "trainer optimal hold shown" "Optimal hold: Jc Qd" \
    sh -c "printf '4,5\n\n' | $C videopoker --trainer --seed 5"
expect_grep "trainer ev lost"  "EV lost:      0.1815" \
    sh -c "printf '4,5\n\n' | $C videopoker --trainer --seed 5"
expect_grep "trainer optimal verdict" "^OPTIMAL$" \
    sh -c "printf '1,4\n\n' | $C videopoker --trainer --seed 5"
expect_grep "trainer session stats" "Optimal decisions: 1" \
    sh -c "printf '1,4\n\n' | $C videopoker --trainer --seed 5"
expect_grep "trainer accuracy" "Accuracy:          100.0%" \
    sh -c "printf '1,4\n\n' | $C videopoker --trainer --seed 5"

# trainer gating
expect_exit "trainer other game"   2 $C roulette red --trainer
expect_exit "trainer quiet mode"   2 sh -c "$C videopoker --trainer --quiet </dev/null"
expect_exit "trainer with bets"    2 sh -c "$C videopoker --trainer hold:1 </dev/null"
expect_exit "trainer EOF exits 0"  0 sh -c "$C videopoker --trainer --seed 1 </dev/null"

# --- GUI gating (the GUIs themselves need a display; not run here) --------
expect_exit "gui other game"      2 $C roulette red --gui
expect_exit "gui with quiet"      2 $C videopoker --gui --quiet
expect_exit "gui with bets"       2 $C videopoker --gui hold:1
expect_exit "bac gui with bet"    2 $C baccarat --gui player
expect_exit "bac gui with quiet"  2 $C baccarat --gui --quiet
expect_exit "bac gui with json"   2 $C baccarat --gui --json
expect_exit "bac gui with runs"   2 $C baccarat --gui --runs 10
expect_exit "bj gui with actions" 2 $C blackjack --gui s
expect_exit "bj gui with quiet"   2 $C blackjack --gui --quiet
expect_exit "bj gui with runs"    2 $C blackjack --gui --runs 10
expect_exit "rtb gui with actions" 2 $C ridethebus --gui r,h,o,s
expect_exit "rtb gui with bet"     2 $C ridethebus --gui bet:250
expect_exit "rtb gui with quiet"   2 $C ridethebus --gui --quiet
expect_exit "rtb gui with json"    2 $C ridethebus --gui --json
expect_exit "rtb gui with runs"    2 $C ridethebus --gui --runs 10
expect_exit "rtb gui with optimal" 2 $C ridethebus --gui --optimal
# --counting is the blackjack GUI Hi-Lo trainer, and nothing else
expect_exit "bj counting needs gui"    2 $C blackjack --counting
expect_exit "bj counting alone+seed"   2 $C blackjack --counting --seed 1
expect_exit "bj counting with script"  2 $C blackjack --gui --counting s
expect_exit "bj counting with json"    2 $C blackjack --gui --counting --json
expect_exit "bj counting with runs"    2 $C blackjack --gui --counting --runs 10
expect_exit "vp counting rejected"     2 $C videopoker --gui --counting
expect_exit "bac counting rejected"    2 $C baccarat --gui --counting
expect_exit "rtb counting rejected"    2 $C ridethebus --gui --counting
expect_exit "roulette counting rejected" 2 $C roulette --counting
expect_grep "counting in global help"  "counting.*Hi-Lo" $C blackjack --help
# missing assets (or a CLI-only build) must fail cleanly, never crash
root=$PWD
for g in videopoker baccarat blackjack ridethebus threecard letitride; do
    out=$(cd /tmp && "$root/casino" $g --gui 2>&1)
    case "$out" in
    *"missing asset"*|*"no GUI support"*) ok ;;
    *) bad "$g gui fails cleanly outside repo root (got: $out)" ;;
    esac
done

# --- ride the bus --------------------------------------------------------
# pure rule predicates (self-test, no RNG involved)
expect_exit "rtb rule self-test passes" 0 $C ridethebus check
expect_grep "rtb check no failures" "check: 28 passed, 0 failed" \
    $C ridethebus check
expect_grep "rtb red heart"    "^red heart  *red "      $C ridethebus check
expect_grep "rtb red diamond"  "^red diamond  *red "    $C ridethebus check
expect_grep "rtb black club"   "^red club  *black "     $C ridethebus check
expect_grep "rtb black spade"  "^red spade  *black "    $C ridethebus check
# ace stays above king
expect_grep "rtb cmp KvA"      "^cmp KvA  *higher "     $C ridethebus check
expect_grep "rtb cmp AvK"      "^cmp AvK  *lower "      $C ridethebus check
expect_grep "rtb cmp AvQ"      "^cmp AvQ  *lower "      $C ridethebus check
expect_grep "rtb cmp 7v7"      "^cmp 7v7  *equal "      $C ridethebus check
# round 2: an equal rank counts as HIGHER
expect_grep "rtb 7 higher 7 wins"  "^hilo 7 higher 7  *win "  $C ridethebus check
expect_grep "rtb 7 lower 7 loses"  "^hilo 7 lower 7  *loss "  $C ridethebus check
expect_grep "rtb 7 higher J wins"  "^hilo 7 higher J  *win "  $C ridethebus check
expect_grep "rtb 7 lower 4 wins"   "^hilo 7 lower 4  *win "   $C ridethebus check
expect_grep "rtb K higher A wins"  "^hilo K higher A  *win "  $C ridethebus check
# round 3: either boundary rank counts as INSIDE
expect_grep "rtb 7J inside 7 wins"  "^inout 7J inside 7  *win "   $C ridethebus check
expect_grep "rtb 7J inside J wins"  "^inout 7J inside J  *win "   $C ridethebus check
expect_grep "rtb 7J outside 7 loses" "^inout 7J outside 7  *loss " $C ridethebus check
expect_grep "rtb 7J outside J loses" "^inout 7J outside J  *loss " $C ridethebus check
expect_grep "rtb 7J inside 9 wins"  "^inout 7J inside 9  *win "   $C ridethebus check
expect_grep "rtb 7J outside K wins" "^inout 7J outside K  *win "  $C ridethebus check
expect_grep "rtb 7J outside A wins" "^inout 7J outside A  *win "  $C ridethebus check
expect_grep "rtb suit match"   "^suit s+s  *match "     $C ridethebus check
expect_grep "rtb suit nomatch" "^suit s+h  *nomatch "   $C ridethebus check

# scripted play: one guess per round, no separate ride action
expect_exit "rtb scripted game"    0 $C ridethebus r,h,o,s --seed 17
expect_exit "rtb space separated"  0 $C ridethebus r h o s --seed 17
expect_exit "rtb full words"       0 \
    $C ridethebus red higher outside spades --seed 17
expect_grep "rtb rides the bus" "^BUS rounds=4 bet=100 payout=2000 net=+1900$" \
    $C ridethebus r,h,o,s --seed 17 --quiet
expect_grep "rtb full words same result" "^BUS rounds=4 " \
    $C ridethebus red higher outside spades --seed 17 --quiet
expect_grep "rtb case insensitive" "^BUS rounds=4 " \
    $C ridethebus R,Higher,O,SPADES --seed 17 --quiet
expect_grep "rtb loses round 1" "^LOSS rounds=0 bet=100 payout=0 net=-100$" \
    $C ridethebus r,h,o,s --seed 7 --quiet

# cashing out shares the next round's prompt: x / cash / cashout
expect_grep "rtb cash out with x" "^CASHOUT rounds=2 bet=100 payout=300" \
    $C ridethebus r,h,x --seed 17 --quiet
expect_grep "rtb cash out with cash" "^CASHOUT rounds=2 bet=100 payout=300" \
    $C ridethebus r h cash --seed 17 --quiet
expect_grep "rtb cash out with cashout" "^CASHOUT rounds=2 bet=100 payout=300" \
    $C ridethebus r,h,cashout --seed 17 --quiet
# a script that simply stops also cashes out
expect_grep "rtb short script cashes" "^CASHOUT rounds=1 bet=100 payout=200" \
    $C ridethebus r --seed 17 --quiet
# there is nothing to cash out before round 1
expect_exit "rtb cannot cash at round 1" 2 $C ridethebus x --seed 17
# ride actions are gone: 'r' is not a round 2 answer
expect_exit "rtb no ride action"   2 $C ridethebus r,r --seed 17

# payouts scale from the ORIGINAL wager (2x/3x/4x/20x)
expect_grep "rtb bet size honoured" "^BUS rounds=4 bet=250 payout=5000 net=+4750$" \
    $C ridethebus bet:250 r,h,o,s --seed 17 --quiet
expect_grep "rtb round1 cashout 2x" "^CASHOUT rounds=1 bet=100 payout=200" \
    $C ridethebus r,x --seed 17 --quiet
expect_grep "rtb round2 cashout 3x" "^CASHOUT rounds=2 bet=100 payout=300" \
    $C ridethebus r,h,x --seed 17 --quiet
expect_grep "rtb round3 cashout 4x" "^CASHOUT rounds=3 bet=100 payout=400" \
    $C ridethebus r,h,o,x --seed 17 --quiet

# ties resolve in place, never re-drawn: seed 12 deals 9s then 9h
expect_grep "rtb tie cards dealt once" '"cards":\["9s","9h"\]' \
    $C ridethebus b,h,x --seed 12 --json
expect_grep "rtb tie counts as higher" "^CASHOUT rounds=2 " \
    $C ridethebus b,h --seed 12 --quiet
expect_grep "rtb tie loses for lower"  "^LOSS rounds=1 " \
    $C ridethebus b,l --seed 12 --quiet
# boundary resolves in place: seed 4 deals Js, 5c then 5h (equals the 5)
expect_grep "rtb boundary cards dealt once" '"cards":\["Js","5c","5h"\]' \
    $C ridethebus b,l,i,x --seed 4 --json
expect_grep "rtb boundary counts as inside" "^CASHOUT rounds=3 " \
    $C ridethebus b,l,i --seed 4 --quiet
expect_grep "rtb boundary loses for outside" "^LOSS rounds=2 " \
    $C ridethebus b,l,o --seed 4 --quiet

# output modes
expect_grep "rtb json game key" '"game":"ridethebus"' \
    $C ridethebus r,h,o,s --seed 17 --json
expect_grep "rtb json result"   '"result":"bus","payout":2000,"net":1900' \
    $C ridethebus r,h,o,s --seed 17 --json
expect_grep "rtb json guesses"  '"guesses":\["red","higher","outside","spades"\]' \
    $C ridethebus r,h,o,s --seed 17 --json
expect_grep "rtb transcript"    "YOU RODE THE BUS" \
    $C ridethebus r,h,o,s --seed 17
expect_grep "rtb prompt offers cash out" "\[X\] Cash out" \
    $C ridethebus r,h,o,s --seed 17
expect_grep "rtb round4 keeps clubs key" "\[C\] Clubs" \
    $C ridethebus r,h,o,s --seed 17
expect_grep "rtb art cards"     "┌─────────┐ ┌─────────┐" \
    env CASINO_CARDS=art $C ridethebus r,h,o,s --seed 17
expect_grep "rtb help works"    "usage:"   $C ridethebus --help
expect_grep "rtb list-bets"     "ride the bus" $C ridethebus --list-bets
expect_grep "rtb help notes tie rule" "equal rank counts as HIGHER" \
    $C ridethebus --list-bets
expect_grep "rtb help notes boundary rule" "boundary rank counts as INSIDE" \
    $C ridethebus --list-bets
expect_grep "rtb in game list"  "ridethebus" $C --help

a=$($C ridethebus r,h,o,s --seed 42 --json)
b=$($C ridethebus r,h,o,s --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "rtb seeded runs identical"

# interactive: piped choices are consumed, EOF ends cleanly
expect_grep "rtb interactive cashout" "CASHED OUT after round 2" \
    sh -c "printf 'red\nhigher\nx\n' | $C ridethebus --seed 17"
expect_grep "rtb reprompts on bad input" "invalid choice" \
    sh -c "printf 'zzz\nred\nx\n' | $C ridethebus --seed 17"
expect_exit "rtb interactive EOF"  0 sh -c "$C ridethebus --seed 17 </dev/null"

# validation
expect_exit "rtb bet zero"        2 $C ridethebus bet:0
expect_exit "rtb bet negative"    2 $C ridethebus bet:-5
expect_exit "rtb bet malformed"   2 $C ridethebus bet:x
expect_exit "rtb cashout range"   2 $C ridethebus cashout:9
expect_exit "rtb unknown action"  2 $C ridethebus banana
expect_exit "rtb bad action word" 2 $C ridethebus z,h,o
expect_exit "rtb check is alone"  2 $C ridethebus check bet:100

# simulation: no prompting, random valid guesses, aggregate only
n=$($C ridethebus --runs 200 --seed 3 --quiet </dev/null | wc -l)
[ "$n" -eq 1 ] && ok || bad "rtb runs quiet is one line (got $n)"
expect_grep "rtb runs table" "inside/outside" $C ridethebus --runs 500 --seed 3
expect_grep "rtb runs json"  '"iterations":500' \
    $C ridethebus --runs 500 --seed 3 --json
# counters must chain: round N+1 is reached exactly as often as N was won
$C ridethebus --runs 20000 --seed 3 --quiet | awk '
{
  for (i = 1; i <= NF; i++) {
    if ($i ~ /^r[1-4]=/) { split($i, a, /[=\/]/); won[a[1]] = a[2]; rch[a[1]] = a[3] }
    if ($i ~ /^completed=/) { split($i, c, "="); comp = c[2] }
  }
}
END {
  ok = (rch["r2"] == won["r1"]) && (rch["r3"] == won["r2"]) &&
       (rch["r4"] == won["r3"]) && (comp == won["r4"]) && (rch["r1"] == 20000)
  print (ok ? "CHAIN_OK" : "CHAIN_BAD")
}' | grep -q CHAIN_OK && ok || bad "rtb round counters chain correctly"

# sanity: random guesses complete all four rounds ~3.125% of the time
comp=$($C ridethebus --runs 200000 --seed 3 --json |
       sed 's/.*"completed":\([0-9]*\).*/\1/')
[ "$comp" -gt 5700 ] && [ "$comp" -lt 6900 ] && ok || \
    bad "rtb completion rate plausible (got $comp / 200000)"
# cashing out after round 1 pays 2x on a 50% shot: exactly break even
ret=$($C ridethebus cashout:1 --runs 200000 --seed 3 --json |
      sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/')
case "$ret" in
0.9[89]*|1.0*) ok ;;
*) bad "rtb cashout:1 is break-even (got $ret)" ;;
esac

ln -sf casino ridethebus
expect_grep "rtb symlink invocation" '"game":"ridethebus"' \
    sh -c "./ridethebus r,h,o,s --seed 17 --json"
rm -f ridethebus

# frontend session API the GUI plays through (stage transitions, payout
# ladder, cash out, forfeit on a loss, one card per round).  See
# tests/rtb_front.c.
if ${CC:-cc} -std=c11 -Isrc -o build/rtb_front_test tests/rtb_front.c \
        src/games/ridethebus.c src/cardart.c src/cards.c src/cli.c \
        src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "rtb frontend session checks" 0 ./build/rtb_front_test
else
    bad "rtb frontend test did not build"
fi

# --- casino war (6-deck shoe, ace high, war or surrender on a tie) --------
# registered rather than planned
expect_grep "war in game list" "^  war  *casino war (6-deck" $C --help
expect_grep "war list-bets"    "casino war: one card each" $C war --list-bets
expect_grep "war help works"   "usage:"                    $C war --help

# plain rounds: higher rank wins 1:1, lower rank loses the wager
expect_exit "war plain round"  0 $C war war --seed 1
expect_grep "war win pays 1:1" \
    "^WIN player=Qh dealer=5d wagered=100 returned=200 net=+100$" \
    $C war war --seed 1 --quiet
expect_grep "war loss keeps nothing" \
    "^LOSS player=2c dealer=As wagered=100 returned=0 net=-100$" \
    $C war war --seed 5 --quiet
# ace is high, suits are irrelevant
expect_grep "war ace beats king" "^WIN player=As dealer=Kh " \
    $C war war --seed 38 --quiet
expect_grep "war ace beats jack" "^LOSS player=Jh dealer=Ah " \
    $C war war --seed 71 --quiet

# scripted war: match the wager, burn three, one more card each
expect_grep "war scripted war loses both" \
    "^LOSS player=Ks dealer=Kh tie=war war_player=9s war_dealer=Js second_tie=no wagered=200 returned=0 net=-200$" \
    $C war war --seed 17 --quiet
expect_grep "war scripted war wins raise" \
    "^WIN player=2s dealer=2d tie=war war_player=5c war_dealer=4d second_tie=no wagered=200 returned=300 net=+100$" \
    $C war war --seed 56 --quiet
# a second tie pushes the war wager and returns both wagers
expect_grep "war second tie pushes" \
    "^PUSH player=5h dealer=5c tie=war war_player=9d war_dealer=9d second_tie=yes wagered=200 returned=200 net=+0$" \
    $C war war --seed 230 --quiet
expect_grep "war burns three cards" '"burn":\["5c","7h","10d"\]' \
    $C war war --seed 230 --json

# scripted surrender: half the original wager, no extra money at risk
expect_grep "war surrender loses half" \
    "^SURRENDER player=7s dealer=7s tie=surrender wagered=100 returned=50 net=-50$" \
    $C war surrender --seed 7 --quiet
# half-unit accounting stays exact on an odd wager
expect_grep "war surrender half is exact" \
    "^SURRENDER player=7s dealer=7s tie=surrender wagered=25 returned=12.5 net=-12.5$" \
    $C war surrender bet:25 --seed 7 --quiet
expect_grep "war surrender half in json" '"returned":12.5,"net":-12.5' \
    $C war surrender bet:25 --seed 7 --json
# the same tie resolves either way, from the same cards
expect_grep "war strategies share the tie" "player=7s dealer=7s tie=war" \
    $C war war --seed 7 --quiet

# bet size scales the whole round
expect_grep "war bet size honoured" \
    "^WIN player=Qh dealer=5d wagered=250 returned=500 net=+250$" \
    $C war bet:250 war --seed 1 --quiet

# output modes
expect_grep "war json game key" '"game":"war"'      $C war war --seed 1 --json
expect_grep "war json no tie"   '"tie":false,"decision":null' \
    $C war war --seed 1 --json
expect_grep "war json tie keys" '"tie":true,"decision":"war"' \
    $C war war --seed 17 --json
expect_grep "war normal shows cards" "^Player: Ks$"  $C war war --seed 17
expect_grep "war normal shows dealer" "^Dealer: Kh$" $C war war --seed 17
expect_grep "war normal shows tie"   "^TIE!$"        $C war war --seed 17
expect_grep "war normal echoes choice" "^> war$"     $C war war --seed 17
expect_grep "war normal shows war"   "^WAR!$"        $C war war --seed 17
expect_grep "war normal shows result" "^Result:   LOSS$" $C war war --seed 17
expect_grep "war normal shows net"   "^Net:          -200$" $C war war --seed 17
expect_grep "war art cards" "┌─────────┐" \
    env CASINO_CARDS=art $C war war --seed 56

a=$($C war war --seed 42 --json)
b=$($C war war --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "war seeded runs identical"
n=$($C war war --seed 3 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "war iterations produce 5 lines (got $n)"

# interactive: the tie prompt is offered, choices are consumed, EOF concedes
expect_grep "war prompts on a tie" "\[w\]ar / \[s\]urrender" \
    sh -c "printf 'w\n' | $C war --seed 17"
expect_grep "war interactive war" "^Wagered:       200$" \
    sh -c "printf 'war\n' | $C war --seed 17"
expect_grep "war interactive surrender" "^Result:   SURRENDER$" \
    sh -c "printf 's\n' | $C war --seed 17"
expect_grep "war reprompts on bad input" "invalid choice" \
    sh -c "printf 'zzz\ns\n' | $C war --seed 17"
expect_grep "war EOF concedes the tie" "^Result:   SURRENDER$" \
    sh -c "$C war --seed 17 </dev/null"
expect_exit "war interactive EOF"  0 sh -c "$C war --seed 17 </dev/null"
# no tie, no prompt
expect_exit "war interactive no tie" 0 sh -c "$C war --seed 1 </dev/null"

# validation
expect_exit "war unknown argument"   2 $C war banana
expect_exit "war two strategies"     2 $C war war surrender
expect_exit "war strategy with value" 2 $C war war:1
expect_exit "war bet zero"           2 $C war war bet:0
expect_exit "war bet negative"       2 $C war war bet:-5
expect_exit "war bet malformed"      2 $C war war bet:x
expect_exit "war unknown option"     2 $C war war --nope
expect_exit "war gui refused"        2 $C war war --gui

# simulation needs a scripted tie strategy
expect_exit "war runs needs strategy" 2 $C war --runs 10
expect_exit "war stats needs strategy" 2 $C war --stats --iterations 10
expect_exit "war runs war ok"        0 $C war war --runs 10 --seed 1
expect_exit "war runs surrender ok"  0 $C war surrender --runs 10 --seed 1
expect_grep "war runs table"    "second ties" $C war war --runs 100 --seed 1
expect_grep "war runs json"     '"strategy":"war"' \
    $C war war --runs 100 --seed 1 --json
expect_grep "war runs quiet"    "^runs=100 bet=100 strategy=surrender " \
    $C war surrender --runs 100 --seed 1 --quiet
n=$($C war war --runs 1000 --seed 1 --quiet | wc -l)
[ "$n" -eq 1 ] && ok || bad "war runs quiet is a single line (got $n)"
n=$($C war war --runs 1000 --seed 1 --json | wc -l)
[ "$n" -eq 1 ] && ok || bad "war runs json is a single line (got $n)"

# accounting: counters partition the rounds and the money adds up exactly,
# including the extra war wager
$C war war --runs 20000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1]); sub(/\.0$/, "", kv[2])
    v[kv[1]] = kv[2]
  }
  bet = 100
  paid = (v["player_wins"] * 2 + v["war_wins"] * 3 + v["second_ties"] * 2) * bet
  ok = v["rounds"] == 20000 &&
       v["player_wins"] + v["dealer_wins"] + v["ties"] == v["rounds"] &&
       v["wars"] + v["surrenders"] == v["ties"] &&
       v["war_wins"] + v["war_losses"] + v["second_ties"] == v["wars"] &&
       v["surrenders"] == 0 &&
       v["wagered"] == (v["rounds"] + v["wars"]) * bet &&
       v["returned"] == paid &&
       v["net"] == v["returned"] - v["wagered"]
  print (ok ? "WAR_OK" : "WAR_BAD")
}' | grep -q WAR_OK && ok || bad "war accounting adds up (war strategy)"

$C war surrender --runs 20000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1]); sub(/\.0$/, "", kv[2])
    v[kv[1]] = kv[2]
  }
  bet = 100
  ok = v["wars"] == 0 && v["second_ties"] == 0 &&
       v["surrenders"] == v["ties"] &&
       v["wagered"] == v["rounds"] * bet &&
       v["returned"] == v["player_wins"] * 2 * bet + v["ties"] * bet / 2 &&
       v["net"] == v["returned"] - v["wagered"]
  print (ok ? "SUR_OK" : "SUR_BAD")
}' | grep -q SUR_OK && ok || bad "war accounting adds up (surrender strategy)"

# both strategies see the same deal, so the tie counts must match
tw=$($C war war --runs 20000 --seed 3 --json |
     sed 's/.*"ties":\([0-9]*\).*/\1/')
ts=$($C war surrender --runs 20000 --seed 3 --json |
     sed 's/.*"ties":\([0-9]*\).*/\1/')
[ "$tw" = "$ts" ] && ok || bad "war tie count is strategy independent"
# sanity: a 6-deck tie happens 23/311 ~ 7.4% of the time
[ "$tw" -gt 1300 ] && [ "$tw" -lt 1660 ] && ok || \
    bad "war tie rate plausible (got $tw / 20000)"

ln -sf casino war
expect_grep "war symlink invocation" '"game":"war"' \
    sh -c "./war war --seed 17 --json"
rm -f war

# --- three card poker (ante/play, ante bonus, pair plus) ------------------
# registered rather than planned
expect_grep "tc in game list" "^  threecard  *three-card poker (ante/play" \
    $C --help
expect_grep "tc list-bets"    "three card poker: three cards each" \
    $C threecard --list-bets
expect_grep "tc help works"   "usage:"  $C threecard --help
# the pay tables in the help text are read from the engine
expect_grep "tc lists ante bonus"  "^  Straight          1:1$" \
    $C threecard --list-bets
expect_grep "tc lists trips bonus" "^  Three of a Kind   4:1$" \
    $C threecard --list-bets
expect_grep "tc lists sf bonus"    "^  Straight Flush    5:1$" \
    $C threecard --list-bets
expect_grep "tc lists pairplus 30" "^  Three of a Kind  30:1$" \
    $C threecard --list-bets
expect_grep "tc lists pairplus 40" "^  Straight Flush   40:1$" \
    $C threecard --list-bets

# pure rule predicates (self-test, no RNG involved)
expect_exit "tc rule self-test passes" 0 $C threecard check
expect_grep "tc check no failures" "check: 48 passed, 0 failed" \
    $C threecard check
# dealer qualification: queen-high or better
expect_grep "tc Q-high qualifies"  "^qualify Q-high  *qualifies " \
    $C threecard check
expect_grep "tc J-high does not"   "^qualify J-high  *no " \
    $C threecard check
expect_grep "tc low pair qualifies" "^qualify low pair  *qualifies " \
    $C threecard check
# ace is high, except in the lowest straight
expect_grep "tc A-2-3 is a straight" "^cat A-2-3 straight  *straight " \
    $C threecard check
expect_grep "tc Q-K-A is a straight" "^cat Q-K-A straight  *straight " \
    $C threecard check
expect_grep "tc K-A-2 is not"        "^cat K-A-2 not straight  *high_card " \
    $C threecard check
expect_grep "tc A-2-3 ranks lowest"  "^A-2-3 is the low straight  *lower " \
    $C threecard check
expect_grep "tc Q-K-A ranks highest" "^Q-K-A is the high straight  *higher " \
    $C threecard check
# the three-card order, which is not the five-card order
expect_grep "tc straight beats flush" "^straight beats flush  *higher " \
    $C threecard check
expect_grep "tc trips beats straight" "^trips beats straight  *higher " \
    $C threecard check
expect_grep "tc sf beats trips"       "^sf beats trips  *higher " \
    $C threecard check
expect_grep "tc flush beats pair"     "^flush beats pair  *higher " \
    $C threecard check
# equal categories compare on rank
expect_grep "tc pair kicker"      "^pair kicker higher  *higher " \
    $C threecard check
expect_grep "tc pair rank first"  "^pair rank beats kicker  *higher " \
    $C threecard check
expect_grep "tc high card third"  "^high card third  *higher " \
    $C threecard check
expect_grep "tc flush ranks"      "^flush ranks  *higher " \
    $C threecard check
expect_grep "tc equal hands push" "^equal hands push  *equal " \
    $C threecard check

# fixed deals: settlement edge cases, no RNG involved
# folding loses the ante and nothing else
expect_grep "tc fold loses ante" \
    "^FOLD player=2h,7d,9c hand=high_card action=fold .* ante=-25 play=0 bonus=0 wagered=25 returned=0 net=-25$" \
    $C threecard deal:2h,7d,9c,as,kd,qh fold --quiet
# pair plus resolves even after a fold
expect_grep "tc pairplus pays after fold" "pairplus=+5 wagered=30 returned=10 net=-20$" \
    $C threecard deal:9h,9d,2c,as,kd,qh pairplus:5 fold --quiet
expect_grep "tc pairplus lost after fold" "pairplus=-5 wagered=30 returned=0 net=-30$" \
    $C threecard deal:2h,7d,9c,as,kd,qh pairplus:5 fold --quiet
# the ante bonus needs a played hand
expect_grep "tc no bonus after fold" "bonus=0 wagered=25 returned=0 net=-25$" \
    $C threecard deal:2h,3d,4c,as,kd,qh fold --quiet
# ... and is paid on a played hand even when the dealer wins
expect_grep "tc bonus paid on a loss" \
    "^LOSS .* ante=-25 play=-25 bonus=+25 wagered=50 returned=25 net=-25$" \
    $C threecard deal:2h,3d,4c,5s,6d,7h play --quiet
expect_grep "tc bonus trips 4:1" "bonus=+100 " \
    $C threecard deal:2h,2d,2c,as,kd,9h play --quiet
expect_grep "tc bonus sf 5:1"    "bonus=+125 " \
    $C threecard deal:2h,3h,4h,as,kd,9c play --quiet
# a non-qualifying dealer pays the ante and pushes the play
expect_grep "tc no qualify pays ante" \
    "^NOQUALIFY .* qualifies=no ante=+25 play=0 bonus=0 wagered=50 returned=75 net=+25$" \
    $C threecard deal:2h,7d,9c,js,8d,4h play --quiet
# ... even when the dealer's cards would have won
expect_grep "tc no qualify beats better" "^NOQUALIFY .* net=+25$" \
    $C threecard deal:2h,3d,5c,js,9d,4h play --quiet
# a qualifying dealer is compared
expect_grep "tc play beats dealer" \
    "^WIN .* ante=+25 play=+25 bonus=0 wagered=50 returned=100 net=+50$" \
    $C threecard deal:ah,ad,2c,qs,9d,4h play --quiet
expect_grep "tc play loses to dealer" \
    "^LOSS .* ante=-25 play=-25 bonus=0 wagered=50 returned=0 net=-50$" \
    $C threecard deal:2h,7d,9c,as,kd,qh play --quiet
expect_grep "tc equal hands push both" \
    "^PUSH .* ante=0 play=0 bonus=0 wagered=50 returned=50 net=0$" \
    $C threecard deal:ah,qd,9c,as,qc,9d play --quiet
# the whole pair plus pay table
expect_grep "tc pairplus pair 1:1"     "pairplus=+5 "   \
    $C threecard deal:9h,9d,2c,as,kd,qh pairplus:5 play --quiet
expect_grep "tc pairplus flush 4:1"    "pairplus=+20 "  \
    $C threecard deal:2h,7h,jh,as,kd,qc pairplus:5 play --quiet
expect_grep "tc pairplus straight 6:1" "pairplus=+30 "  \
    $C threecard deal:2h,3d,4c,as,kd,qh pairplus:5 play --quiet
expect_grep "tc pairplus trips 30:1"   "pairplus=+150 " \
    $C threecard deal:2h,2d,2c,as,kd,qh pairplus:5 play --quiet
expect_grep "tc pairplus sf 40:1"      "pairplus=+200 " \
    $C threecard deal:2h,3h,4h,as,kd,qc pairplus:5 play --quiet
# a bigger ante scales every payout on the hand
expect_grep "tc ante scales the round" \
    "ante=+50 play=+50 bonus=+250 wagered=100 returned=450 net=+350$" \
    $C threecard ante:50 deal:2h,3h,4h,as,kd,9c play --quiet

# seeded deals
expect_grep "tc seeded no qualify" \
    "^NOQUALIFY player=Kd,7c,Jc hand=high_card action=play dealer=6h,5c,Js dhand=high_card qualifies=no " \
    $C threecard play --seed 1 --quiet
expect_grep "tc seeded Q-high qualifies" "dealer=Jc,Qd,4s dhand=high_card qualifies=yes" \
    $C threecard play --seed 11 --quiet
expect_grep "tc seeded fold" "^FOLD .* action=fold .* net=-25$" \
    $C threecard fold --seed 1 --quiet
expect_grep "tc seeded ante bonus" \
    "^WIN player=10h,9c,Jh hand=straight .* bonus=+25 .* net=+75$" \
    $C threecard play --seed 16 --quiet
expect_grep "tc seeded trips bonus and pairplus" \
    "^WIN player=Kc,Kd,Kh hand=three_of_a_kind .* bonus=+100 pairplus=+150 wagered=55 returned=355 net=+300$" \
    $C threecard pairplus:5 play --seed 373 --quiet
# pair plus can win on a hand that loses the ante and play
expect_grep "tc pairplus wins on a loss" \
    "^LOSS .* ante=-25 play=-25 bonus=0 pairplus=+5 wagered=55 returned=10 net=-45$" \
    $C threecard pairplus:5 play --seed 410 --quiet

a=$($C threecard play --seed 42 --json)
b=$($C threecard play --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "tc seeded runs identical"
n=$($C threecard play --seed 3 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "tc iterations produce 5 lines (got $n)"

# output modes
expect_grep "tc json game key"  '"game":"threecard"' \
    $C threecard play --seed 1 --json
expect_grep "tc json player"    '"player":{"cards":\["Kd","7c","Jc"\],"category":"high_card"}' \
    $C threecard play --seed 1 --json
expect_grep "tc json dealer"    '"dealer":{"cards":\["6h","5c","Js"\],"category":"high_card"}' \
    $C threecard play --seed 1 --json
expect_grep "tc json qualifies" '"dealer_qualifies":false,"outcome":"no_qualify"' \
    $C threecard play --seed 1 --json
expect_grep "tc json settlement" '"ante_net":25,"play_net":0,"ante_bonus":0' \
    $C threecard play --seed 1 --json
expect_grep "tc json pairplus"  '"pairplus_net":150' \
    $C threecard pairplus:5 play --seed 373 --json
# the JSON round object has no duplicate keys
n=$($C threecard pairplus:5 play --seed 1 --json |
    tr ',' '\n' | grep -c '"ante":')
[ "$n" -eq 1 ] && ok || bad "tc json ante key appears once (got $n)"
# normal output shows the whole hand, both categories and the breakdown
expect_grep "tc normal shows player"  "^Player:  Kd 7c Jc$" \
    $C threecard play --seed 1
expect_grep "tc normal shows category" "^Hand:    High Card$" \
    $C threecard play --seed 1
expect_grep "tc normal echoes choice" "^> play$"  $C threecard play --seed 1
expect_grep "tc normal shows dealer"  "^Dealer:  6h 5c Js$" \
    $C threecard play --seed 1
expect_grep "tc normal shows qualify" "^Qualify: no (below queen-high)$" \
    $C threecard play --seed 1
expect_grep "tc normal shows result"  "^Result:      NO QUALIFY$" \
    $C threecard play --seed 1
expect_grep "tc normal shows bonus"   "^Ante bonus:         0$" \
    $C threecard play --seed 1
expect_grep "tc normal shows net"     "^Net:              +25$" \
    $C threecard play --seed 1
expect_grep "tc normal shows pairplus" "^Pair Plus:       +150$" \
    $C threecard pairplus:5 play --seed 373
expect_grep "tc art cards" "┌─────────┐" \
    env CASINO_CARDS=art $C threecard play --seed 1

# interactive: the dealer stays down until the decision is made
expect_grep "tc prompts for a decision" "\[p\]lay / \[f\]old" \
    sh -c "printf 'p\n' | $C threecard --seed 1"
expect_grep "tc interactive play" "^Result:      NO QUALIFY$" \
    sh -c "printf 'play\n' | $C threecard --seed 1"
expect_grep "tc interactive fold" "^Result:      FOLD$" \
    sh -c "printf 'f\n' | $C threecard --seed 1"
expect_grep "tc reprompts on bad input" "invalid choice" \
    sh -c "printf 'zzz\nf\n' | $C threecard --seed 1"
expect_grep "tc EOF folds" "^Result:      FOLD$" \
    sh -c "$C threecard --seed 1 </dev/null"
expect_exit "tc interactive EOF" 0 sh -c "$C threecard --seed 1 </dev/null"
# the dealer's cards are never printed before the prompt
$C threecard play --seed 1 | awk '
/^\[p\]play|^\[p\]lay/ { seen_prompt = 1 }
/^Dealer:/ { if (!seen_prompt) early = 1 }
END { print (early ? "EARLY" : "HIDDEN") }' | grep -q HIDDEN && ok || \
    bad "tc dealer stays hidden until the decision"

# validation
expect_exit "tc unknown argument"  2 $C threecard banana
expect_exit "tc two actions"       2 $C threecard play fold
expect_exit "tc action with value" 2 $C threecard play:1
expect_exit "tc ante zero"         2 $C threecard play ante:0
expect_exit "tc ante negative"     2 $C threecard play ante:-5
expect_exit "tc ante too big"      2 $C threecard play ante:100000
expect_exit "tc ante malformed"    2 $C threecard play ante:x
expect_exit "tc pairplus negative" 2 $C threecard play pairplus:-1
expect_exit "tc pairplus malformed" 2 $C threecard play pairplus:x
expect_exit "tc pairplus zero ok"  0 $C threecard play pairplus:0 --seed 1
expect_exit "tc deal too few"      2 $C threecard play deal:2h,3h,4h
expect_exit "tc deal duplicate"    2 $C threecard play deal:2h,3h,4h,2h,kd,qc
expect_exit "tc deal bad card"     2 $C threecard play deal:2x,3h,4h,as,kd,qc
expect_exit "tc deal cannot run"   2 $C threecard play deal:2h,3h,4h,as,kd,qc --runs 10
expect_exit "tc check is alone"    2 $C threecard check ante:25
expect_exit "tc unknown option"    2 $C threecard play --nope

# simulation needs a scripted action
expect_exit "tc runs needs action"  2 $C threecard --runs 10
expect_exit "tc stats needs action" 2 $C threecard --stats --iterations 10
expect_exit "tc runs play ok"       0 $C threecard play --runs 10 --seed 1
expect_exit "tc runs fold ok"       0 $C threecard fold --runs 10 --seed 1
expect_grep "tc runs table" "dealer qualifies" $C threecard play --runs 100 --seed 1
expect_grep "tc runs hand table" "^PLAYER HAND" $C threecard play --runs 100 --seed 1
expect_grep "tc runs json" '"strategy":"play"' \
    $C threecard play --runs 100 --seed 1 --json
expect_grep "tc runs json hands" '"player_hands":{"high_card":' \
    $C threecard play --runs 100 --seed 1 --json
expect_grep "tc runs quiet" "^runs=100 ante=25 pairplus=5 strategy=play " \
    $C threecard pairplus:5 play --runs 100 --seed 1 --quiet
n=$($C threecard play --runs 1000 --seed 1 --quiet | wc -l)
[ "$n" -eq 1 ] && ok || bad "tc runs quiet is a single line (got $n)"
n=$($C threecard play --runs 1000 --seed 1 --json | wc -l)
[ "$n" -eq 1 ] && ok || bad "tc runs json is a single line (got $n)"

# accounting: the counters partition the rounds and the money adds up.
# playing stakes a second wager equal to the ante; the ante bonus is a
# payout, never an extra wager.
$C threecard pairplus:5 play --runs 20000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1])
    v[kv[1]] = kv[2]
  }
  cats = v["high_card"] + v["pair"] + v["flush"]
  cats = cats + v["straight"] + v["three_of_a_kind"] + v["straight_flush"]
  ok = v["rounds"] == 20000 &&
       v["plays"] + v["folds"] == v["rounds"] &&
       v["folds"] == 0 &&
       v["player_wins"] + v["dealer_wins"] + v["pushes"] + v["no_qualify"] == v["plays"] &&
       cats == v["rounds"] &&
       v["wagered"] == v["rounds"] * (25 + 25 + 5) &&
       v["net"] == v["returned"] - v["wagered"]
  print (ok ? "TC_OK" : "TC_BAD")
}' | grep -q TC_OK && ok || bad "tc accounting adds up (play strategy)"

# folding stakes the ante only, and never the play wager
$C threecard fold --runs 20000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1])
    v[kv[1]] = kv[2]
  }
  ok = v["plays"] == 0 && v["folds"] == v["rounds"] &&
       v["ante_bonus_hits"] == 0 &&
       v["wagered"] == v["rounds"] * 25 && v["returned"] == 0 &&
       v["net"] == -v["wagered"]
  print (ok ? "FOLD_OK" : "FOLD_BAD")
}' | grep -q FOLD_OK && ok || bad "tc accounting adds up (fold strategy)"

# the dealt hands do not depend on the action, so the dealer qualifies
# just as often either way
qp=$($C threecard play --runs 20000 --seed 3 --json |
     sed 's/.*"dealer_qualifies":\([0-9]*\).*/\1/')
qf=$($C threecard fold --runs 20000 --seed 3 --json |
     sed 's/.*"dealer_qualifies":\([0-9]*\).*/\1/')
[ "$qp" = "$qf" ] && ok || bad "tc qualification is action independent"
# sanity: the dealer qualifies on 69.59% of all C(52,3) hands
[ "$qp" -gt 13500 ] && [ "$qp" -lt 14300 ] && ok || \
    bad "tc dealer qualification rate plausible (got $qp / 20000)"
# sanity: a pair is 3744/22100 = 16.94% of hands
pr=$($C threecard play --runs 20000 --seed 3 --json |
     sed 's/.*"pair":\([0-9]*\).*/\1/')
[ "$pr" -gt 3100 ] && [ "$pr" -lt 3700 ] && ok || \
    bad "tc pair rate plausible (got $pr / 20000)"

# GUI gating (the GUI itself needs a display; not run here)
expect_exit "tc gui with action" 2 $C threecard --gui play
expect_exit "tc gui with ante"   2 $C threecard --gui ante:25
expect_exit "tc gui with quiet"  2 $C threecard --gui --quiet
expect_exit "tc gui with json"   2 $C threecard --gui --json
expect_exit "tc gui with runs"   2 $C threecard --gui --runs 10
expect_exit "tc gui with counting" 2 $C threecard --gui --counting
expect_grep "tc gui in global help" "threecard" $C --help

ln -sf casino threecard
expect_grep "tc symlink invocation" '"game":"threecard"' \
    sh -c "./threecard play --seed 1 --json"
rm -f threecard

# session API the GUI plays through (wager plumbing, bankroll accounting,
# PLAY funding, locked wagers mid-hand, pay tables).  See tests/tc_front.c.
if ${CC:-cc} -std=c11 -Isrc -o build/tc_front_test tests/tc_front.c \
        src/games/threecard.c src/cardart.c src/cards.c src/cli.c \
        src/output.c src/rng.c >/dev/null 2>&1; then
    expect_exit "tc session checks" 0 ./build/tc_front_test
else
    bad "tc frontend test did not build"
fi

# --- let it ride (three wagers, tens or better) ---------------------------
expect_grep "lir in game list" "^  letitride  *let it ride (three wagers" \
    $C --help
expect_grep "lir list-bets" "let it ride: three cards plus two community" \
    $C letitride --list-bets
expect_grep "lir help works" "usage:" $C letitride --help
# the pay table in the help text is read from the engine
expect_grep "lir lists royal"    "^  ROYAL FLUSH            1000:1$" \
    $C letitride --list-bets
expect_grep "lir lists sf"       "^  STRAIGHT FLUSH          200:1$" \
    $C letitride --list-bets
expect_grep "lir lists quads"    "^  FOUR OF A KIND           50:1$" \
    $C letitride --list-bets
expect_grep "lir lists boat"     "^  FULL HOUSE               11:1$" \
    $C letitride --list-bets
expect_grep "lir lists tens"     "^  TENS OR BETTER            1:1$" \
    $C letitride --list-bets

# pure rule predicates (self-test, no RNG involved)
expect_exit "lir rule self-test passes" 0 $C letitride check
expect_grep "lir check no failures" "check: 31 passed, 0 failed" \
    $C letitride check
# the qualifying pair is TENS or better, not video poker's jacks or better
expect_grep "lir pair of tens qualifies" \
    "^pair of tens  *pair_tens_or_better 1:1 " $C letitride check
expect_grep "lir pair of nines does not" "^pair of nines  *nothing 0:1 " \
    $C letitride check
expect_grep "lir pair of jacks"  "^pair of jacks  *pair_tens_or_better 1:1 " \
    $C letitride check
expect_grep "lir pair of queens" "^pair of queens  *pair_tens_or_better 1:1 " \
    $C letitride check
expect_grep "lir pair of kings"  "^pair of kings  *pair_tens_or_better 1:1 " \
    $C letitride check
expect_grep "lir pair of aces"   "^pair of aces  *pair_tens_or_better 1:1 " \
    $C letitride check
# the rest of the ladder
expect_grep "lir two pair 2:1"   "^two pair  *two_pair 2:1 "  $C letitride check
expect_grep "lir trips 3:1"      "^three of a kind  *three_of_a_kind 3:1 " \
    $C letitride check
expect_grep "lir straight 5:1"   "^straight  *straight 5:1 "  $C letitride check
expect_grep "lir flush 8:1"      "^flush  *flush 8:1 "        $C letitride check
expect_grep "lir boat 11:1"      "^full house  *full_house 11:1 " \
    $C letitride check
expect_grep "lir quads 50:1"     "^four of a kind  *four_of_a_kind 50:1 " \
    $C letitride check
expect_grep "lir sf 200:1"       "^straight flush  *straight_flush 200:1 " \
    $C letitride check
# a royal takes the royal price, not the generic straight-flush one
expect_grep "lir royal 1000:1"   "^royal flush  *royal_flush 1000:1 " \
    $C letitride check
expect_grep "lir king-high sf stays 200" \
    "^king high sf  *straight_flush 200:1 " $C letitride check
# community cards stay down until their own stage
expect_grep "lir nothing shown at first" "^before decision 1  *0 community " \
    $C letitride check
expect_grep "lir one after decision 1"   "^after decision 1  *1 community " \
    $C letitride check
expect_grep "lir two after decision 2"   "^after decision 2  *2 community " \
    $C letitride check

# fixed deals: the four decision combinations on one losing hand.  a pulled
# wager is handed back, so it never counts as a gambling loss.
expect_grep "lir ride,ride risks three" \
    "^LOSS hand=nothing cards=9h,9d,2c,5s,7d bets=ride,ride,ride riding=3 pulled_back=0 wagered=75 returned=0 net=-75$" \
    $C letitride deal:9h,9d,2c,5s,7d ride,ride --quiet
expect_grep "lir pull,ride risks two" \
    "^LOSS .* bets=pulled,ride,ride riding=2 pulled_back=25 wagered=50 returned=0 net=-50$" \
    $C letitride deal:9h,9d,2c,5s,7d pull,ride --quiet
expect_grep "lir ride,pull risks two" \
    "^LOSS .* bets=ride,pulled,ride riding=2 pulled_back=25 wagered=50 returned=0 net=-50$" \
    $C letitride deal:9h,9d,2c,5s,7d ride,pull --quiet
expect_grep "lir pull,pull risks one" \
    "^LOSS .* bets=pulled,pulled,ride riding=1 pulled_back=50 wagered=25 returned=0 net=-25$" \
    $C letitride deal:9h,9d,2c,5s,7d pull,pull --quiet
# every riding wager is paid separately: stake plus profit, each
expect_grep "lir boat pays every rider" \
    "^WIN hand=full_house .* riding=3 pulled_back=0 wagered=75 returned=900 net=+825$" \
    $C letitride deal:6h,6d,6c,9s,9d ride,ride --quiet
expect_grep "lir boat pays two riders" \
    "^WIN hand=full_house .* riding=2 pulled_back=25 wagered=50 returned=600 net=+550$" \
    $C letitride deal:6h,6d,6c,9s,9d pull,ride --quiet
expect_grep "lir boat pays one rider" \
    "^WIN hand=full_house .* riding=1 pulled_back=50 wagered=25 returned=300 net=+275$" \
    $C letitride deal:6h,6d,6c,9s,9d pull,pull --quiet
# representative categories at their listed price (bet 25, three riding)
expect_grep "lir tens pays 1:1"  "returned=150 net=+75$" \
    $C letitride deal:10h,10d,2c,5s,7d ride,ride --quiet
expect_grep "lir nines pay nothing" "returned=0 net=-75$" \
    $C letitride deal:9h,9d,2c,5s,7d ride,ride --quiet
expect_grep "lir two pair pays 2:1" "returned=225 net=+150$" \
    $C letitride deal:3h,3d,5c,5s,9d ride,ride --quiet
expect_grep "lir straight pays 5:1" "returned=450 net=+375$" \
    $C letitride deal:5h,6d,7c,8s,9d ride,ride --quiet
expect_grep "lir flush pays 8:1"    "returned=675 net=+600$" \
    $C letitride deal:2h,5h,9h,jh,kh ride,ride --quiet
expect_grep "lir quads pay 50:1"    "returned=3825 net=+3750$" \
    $C letitride deal:7h,7d,7c,7s,2d ride,ride --quiet
expect_grep "lir sf pays 200:1"     "returned=15075 net=+15000$" \
    $C letitride deal:5h,6h,7h,8h,9h ride,ride --quiet
expect_grep "lir royal pays 1000:1" "returned=75075 net=+75000$" \
    $C letitride deal:10h,jh,qh,kh,ah ride,ride --quiet
# the bet size scales the whole round, and bet:N is EACH wager
expect_grep "lir bet:10 commits 30" '"bet":10,"committed":30' \
    $C letitride bet:10 deal:6h,6d,6c,9s,9d ride,ride --json
expect_grep "lir bet:10 boat"       "wagered=30 returned=360 net=+330$" \
    $C letitride bet:10 deal:6h,6d,6c,9s,9d ride,ride --quiet

# seeded deals
expect_grep "lir seeded loss" \
    "^LOSS hand=nothing cards=3s,10c,9c,7d,Kc bets=ride,ride,ride riding=3 pulled_back=0 wagered=75 returned=0 net=-75$" \
    $C letitride ride,ride --seed 3 --quiet
expect_grep "lir seeded qualifying pair" \
    "^WIN hand=pair_tens_or_better cards=7c,3h,Ac,Ah,10d .* net=+75$" \
    $C letitride ride,ride --seed 8 --quiet
expect_grep "lir seeded two pair"  "^WIN hand=two_pair cards=2s,Jc,7h,2d,Js .* net=+150$" \
    $C letitride ride,ride --seed 2 --quiet
expect_grep "lir seeded trips"     "^WIN hand=three_of_a_kind .* net=+225$" \
    $C letitride ride,ride --seed 120 --quiet
expect_grep "lir seeded straight"  "^WIN hand=straight .* net=+375$" \
    $C letitride ride,ride --seed 113 --quiet
expect_grep "lir seeded flush"     "^WIN hand=flush .* net=+600$" \
    $C letitride ride,ride --seed 132 --quiet
# the same seed deals the same cards whatever the decisions
expect_grep "lir pulls keep the cards" "cards=3s,10c,9c,7d,Kc" \
    $C letitride pull,pull --seed 3 --quiet

a=$($C letitride ride,ride --seed 42 --json)
b=$($C letitride ride,ride --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "lir seeded runs identical"
n=$($C letitride ride,ride --seed 3 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "lir iterations produce 5 lines (got $n)"

# output modes
expect_grep "lir json game key" '"game":"letitride"' \
    $C letitride ride,ride --seed 3 --json
expect_grep "lir json player"   '"player":\["3s","10c","9c"\]' \
    $C letitride ride,ride --seed 3 --json
expect_grep "lir json community" '"community":\["7d","Kc"\]' \
    $C letitride ride,ride --seed 3 --json
expect_grep "lir json bets"     '"bets":\["pulled","riding","riding"\],"riding":2' \
    $C letitride pull,ride --seed 3 --json
expect_grep "lir json payout"   '"hand":"full_house","payout_per_unit":11' \
    $C letitride deal:6h,6d,6c,9s,9d ride,ride --json
# normal output shows the board growing one card at a time
expect_grep "lir shows three cards" "^Your cards: 3s 10c 9c$" \
    $C letitride ride,ride --seed 3
expect_grep "lir shows four cards"  "^Board:      3s 10c 9c 7d$" \
    $C letitride ride,ride --seed 3
expect_grep "lir shows five cards"  "^Final hand: 3s 10c 9c 7d Kc$" \
    $C letitride ride,ride --seed 3
expect_grep "lir echoes decisions"  "^> ride$"     $C letitride ride,ride --seed 3
expect_grep "lir shows pulled bet"  "^Bet 1:      PULLED$" \
    $C letitride pull,ride --seed 3
expect_grep "lir shows riding count" "^Riding:     2 of 3$" \
    $C letitride pull,ride --seed 3
expect_grep "lir shows pulled back" "^Pulled back:      25$" \
    $C letitride pull,ride --seed 3
expect_grep "lir shows the price"   "(pays 11:1)" \
    $C letitride deal:6h,6d,6c,9s,9d ride,ride
expect_grep "lir art cards" "┌─────────┐" \
    env CASINO_CARDS=art $C letitride ride,ride --seed 3
# no community card may appear before its decision
$C letitride ride,ride --seed 3 | awk '
/^Your cards:/ { if (NF != 5) bad = 1 }   # two label words + three cards
/^Board:/      { if (NF != 5) bad = 1 }   # one label word + four cards
/^Final hand:/ { if (NF != 7) bad = 1 }   # two label words + five cards
END { print (bad ? "EARLY" : "STAGED") }' | grep -q STAGED && ok || \
    bad "lir reveals one community card per stage"

# interactive
expect_grep "lir prompts twice" "\[r\]ide / \[p\]ull" \
    sh -c "printf 'r\nr\n' | $C letitride --seed 3"
expect_grep "lir interactive pull" "^Bet 1:      PULLED$" \
    sh -c "printf 'pull\nride\n' | $C letitride --seed 3"
expect_grep "lir interactive ride" "^Riding:     3 of 3$" \
    sh -c "printf 'ride\nride\n' | $C letitride --seed 3"
expect_grep "lir reprompts on bad input" "invalid choice" \
    sh -c "printf 'zzz\nr\nr\n' | $C letitride --seed 3"
expect_grep "lir EOF lets it ride" "^Riding:     3 of 3$" \
    sh -c "$C letitride --seed 3 </dev/null"
expect_exit "lir interactive EOF" 0 sh -c "$C letitride --seed 3 </dev/null"

# validation
expect_exit "lir unknown argument"  2 $C letitride banana
expect_exit "lir one decision only" 2 $C letitride ride
expect_exit "lir three decisions"   2 $C letitride ride,ride,ride
expect_exit "lir bad action word"   2 $C letitride ride,zzz
expect_exit "lir action with value" 2 $C letitride ride:1
expect_exit "lir bet zero"          2 $C letitride ride,ride bet:0
expect_exit "lir bet negative"      2 $C letitride ride,ride bet:-5
expect_exit "lir bet too big"       2 $C letitride ride,ride bet:100000
expect_exit "lir bet malformed"     2 $C letitride ride,ride bet:x
expect_exit "lir deal too few"      2 $C letitride ride,ride deal:ah,kh,qh
expect_exit "lir deal duplicate"    2 $C letitride ride,ride deal:ah,ah,qh,jh,10h
expect_exit "lir deal bad card"     2 $C letitride ride,ride deal:ax,kh,qh,jh,10h
expect_exit "lir deal cannot run"   2 \
    $C letitride ride,ride deal:ah,kh,qh,jh,10h --runs 10
expect_exit "lir check is alone"    2 $C letitride check bet:25
expect_exit "lir unknown option"    2 $C letitride ride,ride --nope
expect_exit "lir space separated"   0 $C letitride ride ride --seed 3

# simulation needs scripted decisions
expect_exit "lir runs needs script"  2 $C letitride --runs 10
expect_exit "lir stats needs script" 2 $C letitride --stats --iterations 10
expect_exit "lir runs ride,ride ok"  0 $C letitride ride,ride --runs 10 --seed 1
expect_exit "lir runs pull,pull ok"  0 $C letitride pull,pull --runs 10 --seed 1
expect_grep "lir runs table" "^FINAL HAND" $C letitride ride,ride --runs 100 --seed 1
expect_grep "lir runs risk note" "only what stayed at risk" \
    $C letitride ride,ride --runs 100 --seed 1
expect_grep "lir runs json" '"strategy":"pull,ride"' \
    $C letitride pull,ride --runs 100 --seed 1 --json
expect_grep "lir runs quiet" "^runs=100 bet=25 strategy=ride,ride " \
    $C letitride ride,ride --runs 100 --seed 1 --quiet
n=$($C letitride ride,ride --runs 1000 --seed 1 --quiet | wc -l)
[ "$n" -eq 1 ] && ok || bad "lir runs quiet is a single line (got $n)"
n=$($C letitride ride,ride --runs 1000 --seed 1 --json | wc -l)
[ "$n" -eq 1 ] && ok || bad "lir runs json is a single line (got $n)"

# accounting: pulled stakes come back and never count as gambled
$C letitride pull,ride --runs 20000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1])
    v[kv[1]] = kv[2]
  }
  hands = v["nothing"] + v["pair_tens_or_better"] + v["two_pair"]
  hands = hands + v["three_of_a_kind"] + v["straight"] + v["flush"]
  hands = hands + v["full_house"] + v["four_of_a_kind"]
  hands = hands + v["straight_flush"] + v["royal_flush"]
  ok = v["rounds"] == 20000 &&
       v["bet1_pulls"] == 20000 && v["bet2_pulls"] == 0 &&
       v["2"] == v["rounds"] &&
       hands == v["rounds"] &&
       v["wins"] + v["losses"] == v["rounds"] &&
       v["committed"] == v["rounds"] * 75 &&
       v["pulled_back"] == v["rounds"] * 25 &&
       v["wagered"] == v["committed"] - v["pulled_back"] &&
       v["net"] == v["returned"] - v["wagered"]
  print (ok ? "LIR_OK" : "LIR_BAD")
}' | grep -q LIR_OK && ok || bad "lir accounting adds up (pull,ride)"

# riding counts follow the script exactly
$C letitride pull,pull --runs 5000 --seed 3 --json | awk '
{
  n = split($0, f, /[{},]/)
  for (i = 1; i <= n; i++) {
    split(f[i], kv, ":")
    gsub(/"/, "", kv[1])
    v[kv[1]] = kv[2]
  }
  ok = v["bet1_pulls"] == 5000 && v["bet2_pulls"] == 5000 &&
       v["wagered"] == 5000 * 25 &&
       v["pulled_back"] == 5000 * 50 &&
       v["committed"] == 5000 * 75
  print (ok ? "PP_OK" : "PP_BAD")
}' | grep -q PP_OK && ok || bad "lir accounting adds up (pull,pull)"

# the deal does not depend on the decisions, so the hand distribution and
# the per-unit return are the same however the wagers are pulled
r3=$($C letitride ride,ride --runs 20000 --seed 3 --json |
     sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/')
r1=$($C letitride pull,pull --runs 20000 --seed 3 --json |
     sed 's/.*"return_per_unit":\([0-9.]*\).*/\1/')
[ "$r3" = "$r1" ] && ok || bad "lir return per unit is pull independent"
# sanity: a paying hand (tens or better) turns up about 23.9% of the time
w=$($C letitride ride,ride --runs 20000 --seed 3 --json |
    sed 's/.*"wins":\([0-9]*\).*/\1/')
[ "$w" -gt 4400 ] && [ "$w" -lt 5200 ] && ok || \
    bad "lir winning-hand rate plausible (got $w / 20000)"

# GUI gating (the GUI itself needs a display; not run here)
expect_exit "lir gui with actions" 2 $C letitride --gui ride,ride
expect_exit "lir gui with bet"     2 $C letitride --gui bet:25
expect_exit "lir gui with quiet"   2 $C letitride --gui --quiet
expect_exit "lir gui with json"    2 $C letitride --gui --json
expect_exit "lir gui with runs"    2 $C letitride --gui --runs 10
expect_exit "lir gui with counting" 2 $C letitride --gui --counting

ln -sf casino letitride
expect_grep "lir symlink invocation" '"game":"letitride"' \
    sh -c "./letitride ride,ride --seed 3 --json"
rm -f letitride

# session API the GUI plays through (wager plumbing, bankroll accounting,
# pulled stakes, the community-card reveal gate).  See tests/lir_front.c.
if ${CC:-cc} -std=c11 -Isrc -o build/lir_front_test tests/lir_front.c \
        src/games/letitride.c src/cardart.c src/cards.c src/cli.c \
        src/output.c src/poker.c src/rng.c >/dev/null 2>&1; then
    expect_exit "lir session checks" 0 ./build/lir_front_test
else
    bad "lir frontend test did not build"
fi

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
