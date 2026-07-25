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

# --- slots --------------------------------------------------------------
expect_exit "slots spin exits 0"     0 $C slots --seed 1
expect_exit "slots takes no bets"    2 $C slots red
expect_exit "slots bet with value"   2 $C slots line:1

# every payout category, pinned to deterministic seeds
expect_grep "slots jackpot 4xSEVEN" "^JACKPOT$"   $C slots --seed 143 --quiet
expect_grep "slots big win 4xBAR"   "^BIG_WIN$"   $C slots --seed 84  --quiet
expect_grep "slots 4xBELL win"      "^WIN$"       $C slots --seed 92  --quiet
expect_grep "slots 4xCHERRY win"    "^WIN$"       $C slots --seed 5   --quiet
expect_grep "slots three-cherry"    "^SMALL_WIN$" $C slots --seed 26  --quiet
expect_grep "slots loss"            "^LOSS$"      $C slots --seed 1   --quiet
# four of a non-paying symbol is still a loss
expect_grep "slots 4xLEMON loses"   "^LOSS$"      $C slots --seed 25  --quiet
expect_grep "slots 4xLEMON reels" \
    '"reels":\["BELL","LEMON","ORANGE","LEMON","LEMON","LEMON"\]' \
    $C slots --seed 25 --json

a=$($C slots --seed 123 --json)
b=$($C slots --seed 123 --json)
[ "$a" = "$b" ] && ok || bad "slots seeded runs identical"

expect_grep "slots normal reels" \
    "\[ .* \] \[ .* \] \[ .* \] \[ .* \] \[ .* \] \[ .* \]" $C slots --seed 1
expect_grep "slots payout line"   "Payout: 100:1" $C slots --seed 143
expect_grep "slots json game key" '"game":"slots"'  $C slots --seed 143 --json
expect_grep "slots json reels" \
    '"reels":\["SEVEN","SEVEN","LEMON","SEVEN","SEVEN","BAR"\]' \
    $C slots --seed 143 --json
expect_grep "slots json result"   '"result":"jackpot"' $C slots --seed 143 --json
expect_grep "slots json payout"   '"payout":"100:1"'   $C slots --seed 143 --json
expect_grep "slots help works"    "usage:"    $C slots --help
expect_grep "slots list-bets"     "JACKPOT"   $C slots --list-bets

n=$($C slots --seed 7 --iterations 5 --quiet | wc -l)
[ "$n" -eq 5 ] && ok || bad "slots iterations produce 5 lines (got $n)"

expect_grep "slots stats table" "RESULT" $C slots --seed 3 --iterations 200 --stats
expect_grep "slots stats json"  '"iterations":200' \
    $C slots --seed 3 --iterations 200 --stats --json

# sanity: jackpot (4+ SEVEN of 6 reels) over 100k seeded spins near 0.12%
jp=$($C slots --seed 3 --iterations 100000 --stats --json |
     sed 's/.*"jackpot":\([0-9]*\).*/\1/')
[ "$jp" -gt 60 ] && [ "$jp" -lt 220 ] && ok || \
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

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
