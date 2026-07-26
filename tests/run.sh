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
expect_grep "art quiet unchanged" "^\(WIN\|LOSS\|PUSH\|BLACKJACK\)$" \
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

# --- videopoker GUI gating (the GUI itself needs a display; not run here) --
expect_exit "gui other game"      2 $C roulette red --gui
expect_exit "gui with quiet"      2 $C videopoker --gui --quiet
expect_exit "gui with bets"       2 $C videopoker --gui hold:1
# missing assets (or a CLI-only build) must fail cleanly, never crash
root=$PWD
out=$(cd /tmp && "$root/casino" videopoker --gui 2>&1)
case "$out" in
*"missing asset"*|*"no GUI support"*) ok ;;
*) bad "gui fails cleanly outside repo root (got: $out)" ;;
esac

# --- ride the bus --------------------------------------------------------
# pure rule predicates (self-test, no RNG involved)
expect_exit "rtb rule self-test passes" 0 $C ridethebus check
expect_grep "rtb check no failures" "check: 18 passed, 0 failed" \
    $C ridethebus check
expect_grep "rtb red heart"    "^red heart  *red "      $C ridethebus check
expect_grep "rtb red diamond"  "^red diamond  *red "    $C ridethebus check
expect_grep "rtb black club"   "^red club  *black "     $C ridethebus check
expect_grep "rtb black spade"  "^red spade  *black "    $C ridethebus check
expect_grep "rtb cmp 7vJ"      "^cmp 7vJ  *higher "     $C ridethebus check
expect_grep "rtb cmp Jv7"      "^cmp Jv7  *lower "      $C ridethebus check
expect_grep "rtb cmp 7v7"      "^cmp 7v7  *equal "      $C ridethebus check
expect_grep "rtb cmp KvA"      "^cmp KvA  *higher "     $C ridethebus check
expect_grep "rtb inside 7,J,9" "^inside 7,J,9  *inside " $C ridethebus check
expect_grep "rtb 7,J,K not inside" "^inside 7,J,K  *outside " \
    $C ridethebus check
expect_grep "rtb 7,J,7 boundary" "^inside 7,J,7  *boundary " \
    $C ridethebus check
expect_grep "rtb outside 7,J,4" "^outside 7,J,4  *outside " \
    $C ridethebus check
expect_grep "rtb 7,J,J boundary" "^outside 7,J,J  *boundary " \
    $C ridethebus check
expect_grep "rtb suit match"   "^suit s+s  *match "     $C ridethebus check
expect_grep "rtb suit nomatch" "^suit s+h  *nomatch "   $C ridethebus check

# scripted play, deterministic via --seed
expect_exit "rtb scripted game"    0 $C ridethebus r,r,h,r,o,r,s --seed 17
expect_exit "rtb space separated"  0 $C ridethebus r r h r o r s --seed 17
# full words: space-separated, since one comma-joined token is capped at
# 31 chars by the shared bet-name field
expect_exit "rtb full words"       0 \
    $C ridethebus red ride higher ride outside ride spades --seed 17
expect_exit "rtb mixed words/letters" 0 \
    $C ridethebus red,ride higher,r outside r spades --seed 17
expect_grep "rtb rides the bus" "^BUS rounds=4 bet=100 payout=2000 net=+1900$" \
    $C ridethebus r,r,h,r,o,r,s --seed 17 --quiet
expect_grep "rtb full words same result" "^BUS rounds=4 " \
    $C ridethebus red ride higher ride outside ride spades --seed 17 --quiet
expect_grep "rtb case insensitive" "^BUS rounds=4 " \
    $C ridethebus R,RIDE,H,Ride,OUTSIDE,r,S --seed 17 --quiet
expect_grep "rtb loses round 1" "^LOSS rounds=0 bet=100 payout=0 net=-100$" \
    $C ridethebus r,r,h,r,o,r,s --seed 7 --quiet
# script ending at a ride prompt cashes out; ending at a guess is an error
expect_grep "rtb short script cashes" "^CASHOUT rounds=1 bet=100 payout=200" \
    $C ridethebus r --seed 17 --quiet
expect_exit "rtb script ends at guess" 2 $C ridethebus r,r --seed 17

# payouts scale from the ORIGINAL wager (2x/3x/4x/20x)
expect_grep "rtb bet size honoured" "^BUS rounds=4 bet=250 payout=5000 net=+4750$" \
    $C ridethebus bet:250 r,r,h,r,o,r,s --seed 17 --quiet
expect_grep "rtb round1 cashout 2x" "^CASHOUT rounds=1 bet=100 payout=200" \
    $C ridethebus r,c --seed 17 --quiet
expect_grep "rtb round2 cashout 3x" "^CASHOUT rounds=2 bet=100 payout=300" \
    $C ridethebus r,r,h,c --seed 17 --quiet
expect_grep "rtb round3 cashout 4x" "^CASHOUT rounds=3 bet=100 payout=400" \
    $C ridethebus r,r,h,r,o,c --seed 17 --quiet

# tie / boundary pushes: re-drawn from the same deck, never a loss
# seed 25 deals 10d then 10h (equal rank -> push), then 7h
expect_grep "rtb round2 tie pushes" \
    '"cards":\["10d","10h","7h"\],"pushes":\[2\]' \
    $C ridethebus r,r,h,r,o,r,s --seed 25 --json
# seed 102 deals 2c 9s then 2d (equals the low boundary -> push), then 6s
expect_grep "rtb round3 boundary pushes" \
    '"cards":\["2c","9s","2d","6s","5c"\],"pushes":\[3\]' \
    $C ridethebus b,r,h,r,i,r,h --seed 102 --json
expect_grep "rtb push still wins the round" '"rounds_won":3' \
    $C ridethebus b,r,h,r,i,r,h --seed 102 --json

# output modes
expect_grep "rtb json game key" '"game":"ridethebus"' \
    $C ridethebus r,r,h,r,o,r,s --seed 17 --json
expect_grep "rtb json result"   '"result":"bus","payout":2000,"net":1900' \
    $C ridethebus r,r,h,r,o,r,s --seed 17 --json
expect_grep "rtb transcript"    "YOU RODE THE BUS" \
    $C ridethebus r,r,h,r,o,r,s --seed 17
expect_grep "rtb art cards"     "┌─────────┐ ┌─────────┐" \
    env CASINO_CARDS=art $C ridethebus r,r,h,r,o,r,s --seed 17
expect_grep "rtb help works"    "usage:"   $C ridethebus --help
expect_grep "rtb list-bets"     "RIDE THE BUS\|ride the bus" \
    $C ridethebus --list-bets
expect_grep "rtb in game list"  "ridethebus" $C --help

a=$($C ridethebus r,r,h,r,o,r,s --seed 42 --json)
b=$($C ridethebus r,r,h,r,o,r,s --seed 42 --json)
[ "$a" = "$b" ] && ok || bad "rtb seeded runs identical"

# interactive: piped choices are consumed, EOF ends cleanly
expect_grep "rtb interactive cashout" "CASHED OUT after round 2" \
    sh -c "printf 'red\nride\nhigher\ncash\n' | $C ridethebus --seed 17"
expect_grep "rtb reprompts on bad input" "invalid choice" \
    sh -c "printf 'zzz\nred\ncash\n' | $C ridethebus --seed 17"
expect_exit "rtb interactive EOF"  0 sh -c "$C ridethebus --seed 17 </dev/null"

# validation
expect_exit "rtb bet zero"        2 $C ridethebus bet:0
expect_exit "rtb bet negative"    2 $C ridethebus bet:-5
expect_exit "rtb bet malformed"   2 $C ridethebus bet:x
expect_exit "rtb cashout range"   2 $C ridethebus cashout:9
expect_exit "rtb unknown action"  2 $C ridethebus banana
expect_exit "rtb bad action word" 2 $C ridethebus z,r,h
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
    sh -c "./ridethebus r,r,h,r,o,r,s --seed 17 --json"
rm -f ridethebus

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
