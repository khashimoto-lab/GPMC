#!/bin/bash
# Regression test for GPMC
# Usage: ./run_tests.sh [path/to/gpmc [path/to/gpmc-pp]]

GPMC=${1:-"$(dirname "$0")/../build/gpmc"}
GPMC_PP=${2:-"$(dirname "$GPMC")/gpmc-pp"}
CNF_DIR="$(dirname "$0")/cnf"
PASS=0
FAIL=0
SKIP=0

# gpmc-pp is an optional build target (-DGPMC_BUILD_PP=ON); when it is absent
# the tests that exercise it are skipped rather than failed.
have_pp=0
[ -x "$GPMC_PP" ] && have_pp=1

run_test() {
    local name="$1"
    local expected="$2"
    local actual
    shift 2
    actual=$("$GPMC" "$@" 2>&1 | grep "c s exact" | grep -v "arb frac" | awk '{print $NF}')
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name  (expected=$expected, got=$actual)"
        FAIL=$((FAIL + 1))
    fi
}

# Checks which of "c s log10-estimate" / "c s neglog10-estimate" is printed
# (MCC output format: neglog10-estimate for a negative result, log10-estimate
# otherwise), independent of the exact() value checked by run_test above.
run_log10_tag_test() {
    local name="$1"
    local expected_tag="$2"
    local actual_tag
    shift 2
    actual_tag=$("$GPMC" "$@" 2>&1 | grep -o "c s neglog10-estimate\|c s log10-estimate" | awk '{print $3}')
    if [ "$actual_tag" = "$expected_tag" ]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name  (expected=$expected_tag, got=$actual_tag)"
        FAIL=$((FAIL + 1))
    fi
}

# Run the standalone gpmc-pp preprocessor and check the global count factor it
# records: "c MUST MULTIPLY BY <factor> 0" (weighted) or "c MUST SHIFT BY <n> 0"
# (unweighted free vars). Both put the value in field 5. This is the only
# coverage of the gpmc-pp binary's own fold handling, distinct from the
# in-process preprocessing the gpmc body does.
run_pp_factor_test() {
    local name="$1"
    if [ "$have_pp" -ne 1 ]; then
        echo "SKIP: $name  (gpmc-pp not built)"
        SKIP=$((SKIP + 1))
        return
    fi
    local expected="$2"
    local cnf="$3"
    shift 3
    local out="${cnf%.cnf}-pp.txt"
    rm -f "$out"
    "$GPMC_PP" "$@" "$cnf" >/dev/null 2>&1
    local actual
    actual=$(grep -m1 "MUST .* BY" "$out" 2>/dev/null | awk '{print $5}')
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name  (expected=$expected, got=$actual)"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$out"
}

# Preprocess with gpmc-pp, then read the written CNF back into the gpmc body and
# check the count matches. Exercises the file round-trip (incl. "MUST MULTIPLY
# BY" with no weight lines left), which previously crashed the reader.
run_pp_roundtrip_test() {
    local name="$1"
    if [ "$have_pp" -ne 1 ]; then
        echo "SKIP: $name  (gpmc-pp not built)"
        SKIP=$((SKIP + 1))
        return
    fi
    local expected="$2"
    local cnf="$3"
    shift 3
    local out="${cnf%.cnf}-pp.txt"
    rm -f "$out"
    "$GPMC_PP" "$@" "$cnf" >/dev/null 2>&1
    local actual
    actual=$("$GPMC" "$@" --no-preprocess "$out" 2>&1 | grep "c s exact" | grep -v "arb frac" | awk '{print $NF}')
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name  (expected=$expected, got=$actual)"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$out"
}

echo "=== GPMC Regression Tests ==="
echo "Binary: $GPMC"
echo ""

# MC tests
run_test "t01_mc_tiny  [MC, 3vars, 2cls]"        "4"    --no-preprocess "$CNF_DIR/t01_mc_tiny.cnf"
run_test "t02_mc_unsat [MC, UNSAT]"               "0"    --no-preprocess "$CNF_DIR/t02_mc_unsat.cnf"
run_test "t03_mc_free  [MC, 3vars, no cls]"       "8"    --no-preprocess "$CNF_DIR/t03_mc_free.cnf"
run_test "t04_mc_chain [MC, 4vars, 4cls]"         "5"    --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"

# Heuristic consistency: same answers with every selector (paper comparison set)
run_test "t01_vs1      [VSADS_Lex,    t01]"       "4"    --vs-heuristic 1 --no-preprocess "$CNF_DIR/t01_mc_tiny.cnf"
run_test "t04_vs1      [VSADS_Lex,    t04]"       "5"    --vs-heuristic 1 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "t04_vs0      [VSADS,        t04]"       "5"    --vs-heuristic 0 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "t04_vs2      [VSADS_TD,     t04]"       "5"    --vs-heuristic 2 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "t04_vs3      [VSADS_TD_Lex, t04]"       "5"    --vs-heuristic 3 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "t04_vs4      [VSIDS,        t04]"       "5"    --vs-heuristic 4 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "t04_vs5      [DLCS,         t04]"       "5"    --vs-heuristic 5 --no-preprocess "$CNF_DIR/t04_mc_chain.cnf"

# MC tests with long (3+) clauses — exercises the long-clause component path
run_test "t08_mc_long  [MC, 3vars, 1 long cls]"   "7"    --no-preprocess "$CNF_DIR/t08_mc_long_clause.cnf"
run_test "t09_mc_long2 [MC, 3vars, 2 long cls]"   "6"    --no-preprocess "$CNF_DIR/t09_mc_long_clauses.cnf"

# PMC tests
run_test "t05_pmc      [PMC, 4vars, proj={1,2}]"  "3"    --mode pmc --no-preprocess "$CNF_DIR/t05_pmc_simple.cnf"
run_test "t07_pmc_nonproj_unsat [PMC, SAT then UNSAT nonproj]" "1" --mode pmc --no-preprocess "$CNF_DIR/t07_pmc_nonproj_sat_then_unsat.cnf"

# Exists-check: same answers with the Counter-DPLL SAT check (paper comparison)
run_test "t05_exists   [PMC+exists-check, t05]"   "3"    --mode pmc --exists-check --no-preprocess "$CNF_DIR/t05_pmc_simple.cnf"
run_test "t07_exists   [PMC+exists-check, t07]"   "1"    --mode pmc --exists-check --no-preprocess "$CNF_DIR/t07_pmc_nonproj_sat_then_unsat.cnf"
run_test "t16_exists   [PWMC+exists-check, t16]"  "1"    --mode pwmc --exists-check --no-preprocess "$CNF_DIR/t16_pwmc_simple.cnf"

# WMC tests
# ground truth verified with bin/gpmc-v1 on 2026-06-07
run_test "t06_wmc          [WMC, 2vars, full weights]"         "0.58"  --mode wmc --no-preprocess "$CNF_DIR/t06_wmc_simple.cnf"
run_test "t06_wmc_vs4      [WMC, VSIDS]"                       "0.58"  --mode wmc --vs-heuristic 4 --no-preprocess "$CNF_DIR/t06_wmc_simple.cnf"
run_test "t06_wmc_vs5      [WMC, DLCS]"                        "0.58"  --mode wmc --vs-heuristic 5 --no-preprocess "$CNF_DIR/t06_wmc_simple.cnf"
run_test "t11_wmc_unsat    [WMC, UNSAT]"                       "0"     --mode wmc --no-preprocess "$CNF_DIR/t11_wmc_unsat.cnf"
run_test "t12_wmc_free     [WMC, 1var no clause]"              "1"     --mode wmc --no-preprocess "$CNF_DIR/t12_wmc_no_clause.cnf"
run_test "t13_wmc_one_side [WMC, complement rule]"             "1"     --mode wmc --no-preprocess "$CNF_DIR/t13_wmc_one_side.cnf"
run_test "t14_wmc_unit     [WMC, unit prop fixes x1=T]"        "0.3"   --mode wmc --no-preprocess "$CNF_DIR/t14_wmc_unit_prop.cnf"
run_test "t15_wmc_defwt    [WMC, default weight=1 for x2]"     "1.3"   --mode wmc --no-preprocess "$CNF_DIR/t15_wmc_default_weight.cnf"

# PWMC tests
# ground truth verified with bin/gpmc-v1 on 2026-06-07
run_test "t16_pwmc_simple  [PWMC, proj=x1, nonproj=x2]"        "1"     --mode pwmc --no-preprocess "$CNF_DIR/t16_pwmc_simple.cnf"
run_test "t17_pwmc_unit    [PWMC, unit prop fixes x1=T]"        "0.3"   --mode pwmc --no-preprocess "$CNF_DIR/t17_pwmc_nonproj_free.cnf"
run_test "t18_pwmc_unsat   [PWMC, UNSAT]"                       "0"     --mode pwmc --no-preprocess "$CNF_DIR/t18_pwmc_unsat.cnf"
run_test "t19_pwmc_constr  [PWMC, x1 and x2, proj=x1]"         "0.3"   --mode pwmc --no-preprocess "$CNF_DIR/t19_pwmc_constraint.cnf"
run_test "t20_pwmc_twoproj [PWMC, proj={x1,x2}]"               "0.58"  --mode pwmc --no-preprocess "$CNF_DIR/t20_pwmc_twoproj.cnf"

# Individual-flag mode path: --projected / --weight-type instead of --mode.
# --mode is a shortcut for these flags (Main.cc), so the answers must match
# the corresponding --mode tests above. --mode and these flags are mutually
# exclusive, so the shortcut is dropped here.
run_test "flag_pmc   [--projected = pmc, t05]"                "3"     --projected --no-preprocess "$CNF_DIR/t05_pmc_simple.cnf"
run_test "flag_wmc   [--weight-type rational = wmc, t06]"     "0.58"  --weight-type rational --no-preprocess "$CNF_DIR/t06_wmc_simple.cnf"
run_test "flag_pwmc  [--projected --weight-type rational, t16]" "1"   --projected --weight-type rational --no-preprocess "$CNF_DIR/t16_pwmc_simple.cnf"
# Integer weight type: unreachable via --mode (which only sets rational), so it
# needs an integer-weighted CNF. Hand-computed sum = 39 (see t34 header).
run_test "flag_int   [--weight-type integer, t34]"           "39"    --weight-type integer --no-preprocess "$CNF_DIR/t34_imc_integer.cnf"
run_test "flag_int_pp [--weight-type integer +pp, t34]"      "39"    --weight-type integer --preprocess "$CNF_DIR/t34_imc_integer.cnf"

# --preprocess consistency: same answers with CaDiCaL preprocessing
run_test "pp_t01  [PP+MC,  t01]"                      "4"    --preprocess "$CNF_DIR/t01_mc_tiny.cnf"
run_test "pp_t04  [PP+MC,  t04]"                      "5"    --preprocess "$CNF_DIR/t04_mc_chain.cnf"
run_test "pp_t05  [PP+PMC, t05]"                      "3"    --mode pmc --preprocess "$CNF_DIR/t05_pmc_simple.cnf"
run_test "pp_t06  [PP+WMC, t06]"                      "0.58" --mode wmc --preprocess "$CNF_DIR/t06_wmc_simple.cnf"
run_test "pp_t14  [PP+WMC, unit prop]"                "0.3"  --mode wmc --preprocess "$CNF_DIR/t14_wmc_unit_prop.cnf"
run_test "pp_t16  [PP+PWMC, simple]"                  "1"    --mode pwmc --preprocess "$CNF_DIR/t16_pwmc_simple.cnf"
run_test "pp_t17  [PP+PWMC, unit]"                    "0.3"  --mode pwmc --preprocess "$CNF_DIR/t17_pwmc_nonproj_free.cnf"
run_test "pp_t02  [PP+MC,  UNSAT]"                    "0"    --preprocess "$CNF_DIR/t02_mc_unsat.cnf"

# Equivalence elimination (phase-1 unfrozen EE): weight folding, sign
# handling, chains, isolated classes, and projection promotion.
# Expected values verified by hand and against --no-preprocess.
run_test "ee_t21  [PP+WMC, x1<->x2 fold]"             "0.275" --mode wmc  --preprocess "$CNF_DIR/t21_wmc_equiv.cnf"
run_test "ee_t22  [PP+WMC, chain x1<->x2<->x3]"       "0.145" --mode wmc  --preprocess "$CNF_DIR/t22_wmc_equiv_chain.cnf"
run_test "ee_t23  [PP+WMC, isolated equiv class]"     "0.29"  --mode wmc  --preprocess "$CNF_DIR/t23_wmc_equiv_isolated.cnf"
run_test "ee_t24  [PP+WMC, x1<->NOT x2 sign]"         "0.275" --mode wmc  --preprocess "$CNF_DIR/t24_wmc_equiv_neg.cnf"
run_test "ee_t25  [PP+PWMC, promote nonproj surv]"    "0.8"   --mode pwmc --preprocess "$CNF_DIR/t25_pwmc_equiv_promote.cnf"
run_test "ee_t33  [PP+PWMC, nonproj stray weight]"    "0.8"   --mode pwmc --preprocess "$CNF_DIR/t33_pwmc_nonproj_weight.cnf"
run_test "ee_t26  [PP+PMC, promote nonproj surv]"     "2"     --mode pmc  --preprocess "$CNF_DIR/t26_pmc_equiv_promote.cnf"
run_test "ee_t27  [PP+MC,  x1<->x2 no double count]"  "3"     --preprocess "$CNF_DIR/t27_mc_equiv.cnf"
run_test "ee_t28  [PP+MC,  isolated equiv class]"     "6"     --preprocess "$CNF_DIR/t28_mc_equiv_isolated.cnf"
# t32: x1<->x2 merge whose clauses then vanish, so the class goes free and the
# survivor must be folded by applyIsolatedVars (the "merged-then-free" path,
# never hit by 113 where survivor_fallback was 0).
run_test "ee_t32  [PP+WMC, merge-then-free isolated]" "0.54"  --mode wmc --preprocess "$CNF_DIR/t32_wmc_merge_then_free.cnf"
run_test "ee_t32n [WMC,  t32 no-pp]"                  "0.54"  --mode wmc --no-preprocess "$CNF_DIR/t32_wmc_merge_then_free.cnf"

# No-preprocess ground truth for the EE cases (counter-only sanity check)
# DVE (phase 3): definable variable elimination.
# t27: x2 definable by x1 (high-degree x1 stays definer) -> eliminated.
# t29: x3 <-> x1 AND x2, all projected; with no definer kept (forced DVE,
#      min-pvars 0) the definers-empty guard must keep x1,x2,x3 -> answer 4.
run_test "dve_t27  [DVE-only, x2 def by x1]"          "3"     --preprocess --dve-min-pvars 0 "$CNF_DIR/t27_mc_equiv.cnf"
run_test "dve_t29  [DVE, AND gate, default skip]"     "4"     --preprocess "$CNF_DIR/t29_mc_def_and.cnf"
run_test "dve_t29f [DVE, AND gate, forced no-defin]"  "4"     --preprocess --dve-min-pvars 0 "$CNF_DIR/t29_mc_def_and.cnf"
# t30: DVE weight folding + exact equals() for asymmetric weights.
# Without the fix, forced DVE eliminates x2 and drops its weight -> 4.17e-01
# (off by 2x). Truth and forced-DVE must both give 2.08e-01.
run_test "dve_t30  [WMC, no-pp truth]"                "2.08333333333333e-01" --mode wmc --no-preprocess "$CNF_DIR/t30_wmc_dve_asym.cnf"
run_test "dve_t30f [WMC, forced DVE]"                 "2.08333333333333e-01" --mode wmc --preprocess --dve-min-pvars 0 "$CNF_DIR/t30_wmc_dve_asym.cnf"
# t31: same DVE weight-folding/equals() guard reached through a gate definition
# (not a bare equivalence), so the definable-var path is exercised differently.
run_test "dve_t31  [WMC, gate, no-pp truth]"          "1.04166666666667e-01" --mode wmc --no-preprocess "$CNF_DIR/t31_wmc_dve_gate_asym.cnf"
run_test "dve_t31f [WMC, gate, forced DVE]"           "1.04166666666667e-01" --mode wmc --preprocess --dve-min-pvars 0 "$CNF_DIR/t31_wmc_dve_gate_asym.cnf"
# t35: routing instance exercising the low-freq DVE leg (default --dve-low-freq-max 4).
# It has a mutually-definable (rank-deficient) projection cluster: Padoa marks every
# member definable, but BVE can only remove as many as the dependencies allow, leaving
# one as a free (orphan) variable carrying a real degree of freedom (x2). Before the
# eliminated()-check fix, applyDVE recorded all of them as definable, so that survivor
# was dropped as eliminated (x1) instead of counted as isolated (x2): 9 instead of 18.
# Truth is 18 (no-pp, --dve-low-freq-max 0, and --no-pp-dve all agree).
run_test "dve_t35  [PMC, low-freq DVE orphan x2]"    "18"    --mode pmc --preprocess "$CNF_DIR/t35_pmc_dve_lowfreq.cnf"

run_test "ee_t21n [WMC,  t21 no-pp]"                  "0.275" --mode wmc  --no-preprocess "$CNF_DIR/t21_wmc_equiv.cnf"
run_test "ee_t23n [WMC,  t23 no-pp]"                  "0.29"  --mode wmc  --no-preprocess "$CNF_DIR/t23_wmc_equiv_isolated.cnf"
run_test "ee_t25n [PWMC, t25 no-pp]"                  "0.8"   --mode pwmc --no-preprocess "$CNF_DIR/t25_pwmc_equiv_promote.cnf"
run_test "ee_t33n [PWMC, t33 no-pp]"                  "0.8"   --mode pwmc --no-preprocess "$CNF_DIR/t33_pwmc_nonproj_weight.cnf"

# Standalone gpmc-pp binary: the recorded multiplier must match the body's
# weight_factor (4/5). t33 regresses the nonproj stray-weight fold; t25 the
# promoted-survivor case. Both gave 33/50 before the completeMccWeights fix.
run_pp_factor_test "pp_t33  [gpmc-pp, t33 factor]"    "4/5"   "$CNF_DIR/t33_pwmc_nonproj_weight.cnf" --mode pwmc
run_pp_factor_test "pp_t25  [gpmc-pp, t25 factor]"    "4/5"   "$CNF_DIR/t25_pwmc_equiv_promote.cnf"  --mode pwmc

# Round-trip the gpmc-pp output back through the body. t33/t25 fold to an empty
# CNF carrying only "MUST MULTIPLY BY 4/5"; reading that back used to crash.
run_pp_roundtrip_test "pp_t33rt [gpmc-pp->gpmc, t33]" "0.8" "$CNF_DIR/t33_pwmc_nonproj_weight.cnf" --mode pwmc
run_pp_roundtrip_test "pp_t25rt [gpmc-pp->gpmc, t25]" "0.8" "$CNF_DIR/t25_pwmc_equiv_promote.cnf"  --mode pwmc

# Unweighted counterpart: free vars fold to "c MUST SHIFT BY <n>" (not a
# multiplier). t36 folds to an empty CNF carrying SHIFT BY 2; the count must
# round-trip back to 2^2 = 4.
run_pp_factor_test    "pp_t36  [gpmc-pp, t36 shift]" "2" "$CNF_DIR/t36_mc_isolated_shift.cnf"
run_pp_roundtrip_test "pp_t36rt [gpmc-pp->gpmc, t36]" "4" "$CNF_DIR/t36_mc_isolated_shift.cnf"

# t37: MCC format allows negative per-literal weights. x1 is forced true by
# the unit clause, so the count is exactly w(x1) = -2 -> the result line and
# the log10 tag must both reflect a negative count (neglog10-estimate).
run_test         "t37_pwmc_neg  [PWMC, negative weight]"        "-2" --mode pwmc --no-preprocess "$CNF_DIR/t37_pwmc_negative_weight.cnf"
run_log10_tag_test "t37_pwmc_neg_tag [PWMC, neglog10-estimate tag]" "neglog10-estimate" --mode pwmc --no-preprocess "$CNF_DIR/t37_pwmc_negative_weight.cnf"

echo ""
if [ "$SKIP" -gt 0 ]; then
    echo "=== Results: $PASS passed, $FAIL failed, $SKIP skipped (gpmc-pp not built) ==="
else
    echo "=== Results: $PASS passed, $FAIL failed ==="
fi
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
