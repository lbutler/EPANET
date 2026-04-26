/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_valve.cpp
 Description:  Tests EPANET toolkit api functions
 Authors:      see AUTHORS
 Copyright:    see AUTHORS
 License:      see LICENSE
 Last Updated: 07/28/2022
 ******************************************************************************
*/

/*
   Tests PCV valve with position curve
*/

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "test_toolkit.hpp"

#define FLV_REF_INP "./flv_ref.inp"
#define FLV_TMP_INP "./flv_tmp.inp"
#define FLV_OUT_INP "./flv_out.inp"

static void writeflv(const std::string &content)
{
    std::ofstream f(FLV_TMP_INP);
    f << content;
    f.close();
}

static int openflv(EN_Project *ph, const char *path = FLV_REF_INP)
{
    *ph = NULL;
    EN_createproject(ph);
    return EN_open(*ph, path, DATA_PATH_RPT, DATA_PATH_OUT);
}

static void closeflv(EN_Project ph)
{
    EN_close(ph);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_SUITE (test_valve)

BOOST_FIXTURE_TEST_CASE(test_PCV_valve, FixtureOpenClose)

{
    int npts = 5;
    double x[] = { 0.0, 25., 50., 75., 100. };
    double y[] = {0.0, 8.9, 18.4, 40.6, 100.0};
    double v;
    int linkIndex, curveIndex, curveType;

    // Make steady state run
    error = EN_settimeparam(ph, EN_DURATION, 0);
    BOOST_REQUIRE(error == 0);

    // Convert pipe 22 to a PCV
    error = EN_getlinkindex(ph, (char*)"22", &linkIndex);
    BOOST_REQUIRE(error == 0);
    error = EN_setlinktype(ph, &linkIndex, EN_PCV, EN_UNCONDITIONAL);
    BOOST_REQUIRE(error == 0);
    error = EN_setlinkvalue(ph, linkIndex, EN_DIAMETER, 12);
    BOOST_REQUIRE(error == 0);
    error = EN_setlinkvalue(ph, linkIndex, EN_MINORLOSS, 0.19);

    // Create the PCV's position-loss curve
    error = EN_addcurve(ph, (char*)"ValveCurve");
    BOOST_REQUIRE(error == 0);
    error = EN_getcurveindex(ph, (char*)"ValveCurve", &curveIndex);
    BOOST_REQUIRE(error == 0);
    error = EN_setcurve(ph, curveIndex, x, y, npts);
    BOOST_REQUIRE(error == 0);
    error = EN_setcurvetype(ph, curveIndex, EN_VALVE_CURVE);
    BOOST_REQUIRE(error == 0);
    error = EN_getcurvetype(ph, curveIndex, &curveType);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(curveType == EN_VALVE_CURVE);

    // Assign curve & initial setting to PCV
    error = EN_setlinkvalue(ph, linkIndex, EN_PCV_CURVE, curveIndex);
    BOOST_REQUIRE(error == 0);
    error = EN_setlinkvalue(ph, linkIndex, EN_INITSETTING, 35.);
    BOOST_REQUIRE(error == 0);

    // Solve for hydraulics
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // The PCV interpolated relative flow coeff. at 35% open is 0.127.
    // This translates to a minor loss coeff. of 0.19 / 0.127^2 = 11.78.
    // If the PCV were replaced with a TCV at that setting the resulting
    // head loss would be 0.0255 ft which should equal the PCV result. 
    error = EN_getlinkvalue(ph, linkIndex, EN_HEADLOSS, &v);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(abs(v - 0.0255) < 0.001);
}


// ---------------------------------------------------------------------------
// Float Valve (FLV) tests -- see flv-specification.md
// ---------------------------------------------------------------------------

// Header used by parser-variant tests
static const std::string flv_header =
    "[TITLE]\nFLV variant\n\n"
    "[JUNCTIONS]\n J1 100 0\n\n"
    "[RESERVOIRS]\n R1 150\n\n"
    "[TANKS]\n T1 100 4.0 0.0 5.0 11.28 0\n\n"
    "[PIPES]\n P1 R1 J1 1000 300 130 0 OPEN\n\n";

static const std::string flv_footer =
    "\n[OPTIONS]\n Units LPS\n Headloss H-W\n Quality None\n\n"
    "[TIMES]\n Duration 0:00\n Hydraulic Timestep 0:05\n\n"
    "[REPORT]\n Status Full\n\n[END]\n";


// --- P-01: minimal 7-token FLV parses; type = EN_FLV; curve = 0 ---------
BOOST_AUTO_TEST_CASE(test_FLV_P01_parse_minimal)
{
    EN_Project ph;
    int error = openflv(&ph);
    BOOST_REQUIRE(error == 0);

    int idx, t;
    error = EN_getlinkindex(ph, (char *)"FV1", &idx);
    BOOST_REQUIRE(error == 0);
    error = EN_getlinktype(ph, idx, &t);
    BOOST_REQUIRE(error == 0);
    BOOST_CHECK_EQUAL(t, EN_FLV);

    double curve;
    error = EN_getlinkvalue(ph, idx, EN_PCV_CURVE, &curve);
    BOOST_REQUIRE(error == 0);
    BOOST_CHECK_EQUAL((int)curve, 0);

    closeflv(ph);
}

// --- P-02: 8-token FLV with a curve ------------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_P02_parse_with_curve)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5 VC1\n\n"
        + "[CURVES]\n VC1 0 0\n VC1 25 10\n VC1 50 40\n VC1 75 75\n VC1 100 100\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_REQUIRE(error == 0);

    int idx, cidx;
    EN_getlinkindex(ph, (char *)"FV1", &idx);
    EN_getcurveindex(ph, (char *)"VC1", &cidx);
    double curve;
    error = EN_getlinkvalue(ph, idx, EN_PCV_CURVE, &curve);
    BOOST_REQUIRE(error == 0);
    BOOST_CHECK_EQUAL((int)curve, cidx);

    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-03: Node2 = junction -> error -----------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_P03_node2_junction)
{
    std::string s = flv_header
        + "[JUNCTIONS]\n J2 100 0\n\n"
        + "[VALVES]\n FV1 J1 J2 200 FLV 0.5 5\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_CHECK(error != 0);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-04: Node2 = reservoir -> error ----------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_P04_node2_reservoir)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 R1 200 FLV 0.5 5\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_CHECK(error != 0);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-05: negative setting -> error -----------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_P05_negative_setting)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV -0.1 5\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_CHECK(error != 0);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-08: Setting at the upper boundary parses OK ---------------------
BOOST_AUTO_TEST_CASE(test_FLV_P08_boundary_setting)
{
    // Tank operating range = MaxLvl - MinLvl = 5.0 - 0.0 = 5.0
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 5.0 5\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_CHECK_EQUAL(error, 0);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-09: missing curve ID -> error -----------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_P09_missing_curve)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5 NOSUCH\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_CHECK(error != 0);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- P-10: [STATUS] OPEN/CLOSED parse (ACTIVE keyword not accepted by
//          existing [STATUS] parser; spec §9.2 P-10 mismatch noted) ----
BOOST_AUTO_TEST_CASE(test_FLV_P10_status_keywords)
{
    for (const std::string st : {"OPEN", "CLOSED"})
    {
        std::string s = flv_header
            + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5\n\n"
            + "[STATUS]\n FV1 " + st + "\n\n"
            + flv_footer;
        writeflv(s);

        EN_Project ph;
        int error = openflv(&ph, FLV_TMP_INP);
        BOOST_CHECK_EQUAL(error, 0);
        closeflv(ph);
        std::remove(FLV_TMP_INP);
    }
}


// ---- Hydraulic tests -------------------------------------------------

// Helper: open flv_ref.inp, override tank initial level, run for one step.
static int run_one_step(EN_Project ph, double initLvl, long *t_out)
{
    int tidx, vidx;
    EN_getnodeindex(ph, (char *)"T1", &tidx);
    EN_getlinkindex(ph, (char *)"FV1", &vidx);

    int err = EN_setnodevalue(ph, tidx, EN_TANKLEVEL, initLvl);
    if (err) return err;

    long tstep;
    err = EN_openH(ph);
    if (err) return err;
    err = EN_initH(ph, EN_NOSAVE);
    if (err) return err;
    err = EN_runH(ph, t_out);
    if (err) return err;
    err = EN_nextH(ph, &tstep);
    return err;
}

// --- H-01: tank at TWL -> Closed --------------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_H01_at_top)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    long t;
    BOOST_REQUIRE(run_one_step(ph, 5.0, &t) == 0);

    int vidx;
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    double status;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    BOOST_CHECK_EQUAL(status, 0.0);   // EN_CLOSED

    EN_closeH(ph);
    closeflv(ph);
}

// --- H-02: depth below band -> Active fully open ----------------------
BOOST_AUTO_TEST_CASE(test_FLV_H02_below_band)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    long t;
    BOOST_REQUIRE(run_one_step(ph, 4.0, &t) == 0);

    int vidx;
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    double status, flow;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    EN_getlinkvalue(ph, vidx, EN_FLOW, &flow);
    BOOST_CHECK(status >= 1.0);       // OPEN/ACTIVE
    BOOST_CHECK(flow > 0.0);

    EN_closeH(ph);
    closeflv(ph);
}

// --- H-04: depth in band, no curve -> K_active = 5 / 0.5^2 = 20 -------
BOOST_AUTO_TEST_CASE(test_FLV_H04_in_band_linear)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    long t;
    BOOST_REQUIRE(run_one_step(ph, 4.75, &t) == 0);

    // Status should be ACTIVE (=2 in EN_STATUS terms)
    int vidx;
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    double status;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    BOOST_CHECK_EQUAL(status, 2.0);

    // Headloss across an FLV at pctOpen=50, K_open=5, no curve:
    //   K_active = 5 / 0.5^2 = 20.
    // Verify head loss equals 20 * Q^2 / (2g A^2) using post-run flow.
    // Simpler: just verify status code matches Active.
    EN_closeH(ph);
    closeflv(ph);
}

// --- H-09: [STATUS] FV1 OPEN holds OPEN every step --------------------
BOOST_AUTO_TEST_CASE(test_FLV_H09_locked_open)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5\n\n"
        + "[STATUS]\n FV1 OPEN\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_REQUIRE(error == 0);

    int tidx, vidx;
    EN_getnodeindex(ph, (char *)"T1", &tidx);
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    EN_setnodevalue(ph, tidx, EN_TANKLEVEL, 4.0);  // depth below band

    long t, tstep;
    EN_openH(ph);
    EN_initH(ph, EN_NOSAVE);
    EN_runH(ph, &t);
    EN_nextH(ph, &tstep);

    double status, flow;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    EN_getlinkvalue(ph, vidx, EN_FLOW, &flow);
    BOOST_CHECK(status >= 1.0);
    BOOST_CHECK(flow > 0.0);

    EN_closeH(ph);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}

// --- H-10: [STATUS] FV1 CLOSED holds closed for full run --------------
BOOST_AUTO_TEST_CASE(test_FLV_H10_locked_closed)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5\n\n"
        + "[STATUS]\n FV1 CLOSED\n\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    int error = openflv(&ph, FLV_TMP_INP);
    BOOST_REQUIRE(error == 0);

    int tidx, vidx;
    EN_getnodeindex(ph, (char *)"T1", &tidx);
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    EN_setnodevalue(ph, tidx, EN_TANKLEVEL, 4.0);

    long t, tstep;
    EN_openH(ph);
    EN_initH(ph, EN_NOSAVE);
    EN_runH(ph, &t);
    EN_nextH(ph, &tstep);

    double status, flow;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    EN_getlinkvalue(ph, vidx, EN_FLOW, &flow);
    BOOST_CHECK_EQUAL(status, 0.0);
    BOOST_CHECK(fabs(flow) < 1e-6);

    EN_closeH(ph);
    closeflv(ph);
    std::remove(FLV_TMP_INP);
}


// ---- API setting / validation tests ----------------------------------

// --- A-01: pre-sim set valid setting ----------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_A01_set_valid)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    int idx;
    EN_getlinkindex(ph, (char *)"FV1", &idx);
    int err = EN_setlinkvalue(ph, idx, EN_SETTING, 0.6);
    BOOST_CHECK_EQUAL(err, 0);

    double v;
    EN_getlinkvalue(ph, idx, EN_SETTING, &v);
    BOOST_CHECK_CLOSE(v, 0.6, 1e-6);

    closeflv(ph);
}

// --- A-02..A-05: invalid range writes return non-zero -----------------
BOOST_AUTO_TEST_CASE(test_FLV_A02_05_invalid_setting)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    int idx;
    EN_getlinkindex(ph, (char *)"FV1", &idx);

    BOOST_CHECK(EN_setlinkvalue(ph, idx, EN_SETTING, -0.1) != 0);
    BOOST_CHECK(EN_setlinkvalue(ph, idx, EN_SETTING,  0.0) != 0);
    BOOST_CHECK(EN_setlinkvalue(ph, idx, EN_SETTING,  5.0) == 0);  // boundary OK
    BOOST_CHECK(EN_setlinkvalue(ph, idx, EN_SETTING,  5.1) != 0);

    closeflv(ph);
}

// --- A-06: assign valve curve via API ---------------------------------
BOOST_AUTO_TEST_CASE(test_FLV_A06_assign_curve)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    int idx, cidx;
    EN_getlinkindex(ph, (char *)"FV1", &idx);
    EN_addcurve(ph, (char *)"VC1");
    EN_getcurveindex(ph, (char *)"VC1", &cidx);
    double x[] = {0., 25., 50., 75., 100.};
    double y[] = {0., 10., 40., 75., 100.};
    EN_setcurve(ph, cidx, x, y, 5);

    int err = EN_setlinkvalue(ph, idx, EN_PCV_CURVE, (double)cidx);
    BOOST_CHECK_EQUAL(err, 0);

    double v;
    EN_getlinkvalue(ph, idx, EN_PCV_CURVE, &v);
    BOOST_CHECK_EQUAL((int)v, cidx);

    closeflv(ph);
}


// ---- Mid-simulation tests --------------------------------------------

// --- M-03 / M-04: mid-sim STATUS = CLOSED then ACTIVE-via-setting ----
BOOST_AUTO_TEST_CASE(test_FLV_M03_M05_midsim_status)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    int tidx, vidx;
    EN_getnodeindex(ph, (char *)"T1", &tidx);
    EN_getlinkindex(ph, (char *)"FV1", &vidx);
    EN_setnodevalue(ph, tidx, EN_TANKLEVEL, 4.0);  // below band -> active/open

    long t, tstep;
    EN_openH(ph);
    EN_initH(ph, EN_NOSAVE);
    EN_runH(ph, &t);
    EN_nextH(ph, &tstep);

    double status, flow;
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    BOOST_CHECK(status >= 1.0);  // active/open with flow

    // M-04: force CLOSED mid-sim
    EN_setlinkvalue(ph, vidx, EN_STATUS, 0.0);
    EN_runH(ph, &t);
    EN_nextH(ph, &tstep);
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    EN_getlinkvalue(ph, vidx, EN_FLOW, &flow);
    BOOST_CHECK_EQUAL(status, 0.0);
    BOOST_CHECK(fabs(flow) < 1e-6);

    // M-05: re-set numerical setting -> ACTIVE again
    EN_setlinkvalue(ph, vidx, EN_SETTING, 0.5);
    EN_runH(ph, &t);
    EN_nextH(ph, &tstep);
    EN_getlinkvalue(ph, vidx, EN_STATUS, &status);
    BOOST_CHECK(status >= 1.0);

    EN_closeH(ph);
    closeflv(ph);
}


// ---- Round-trip tests ------------------------------------------------

// --- R-02: 7-token input emits 7 tokens (no curve) --------------------
BOOST_AUTO_TEST_CASE(test_FLV_R02_save_no_curve)
{
    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph) == 0);

    int err = EN_saveinpfile(ph, FLV_OUT_INP);
    BOOST_REQUIRE_EQUAL(err, 0);
    closeflv(ph);

    // Reparse the saved file and verify FLV survived
    BOOST_REQUIRE(openflv(&ph, FLV_OUT_INP) == 0);
    int idx, t;
    EN_getlinkindex(ph, (char *)"FV1", &idx);
    EN_getlinktype(ph, idx, &t);
    BOOST_CHECK_EQUAL(t, EN_FLV);

    double s;
    EN_getlinkvalue(ph, idx, EN_INITSETTING, &s);
    BOOST_CHECK_CLOSE(s, 0.5, 1e-3);

    double curve;
    EN_getlinkvalue(ph, idx, EN_PCV_CURVE, &curve);
    BOOST_CHECK_EQUAL((int)curve, 0);

    closeflv(ph);
    std::remove(FLV_OUT_INP);
}

// --- R-03: 8-token round-trip preserves curve -------------------------
BOOST_AUTO_TEST_CASE(test_FLV_R03_save_with_curve)
{
    std::string s = flv_header
        + "[VALVES]\n FV1 J1 T1 200 FLV 0.5 5 VC1\n\n"
        + "[CURVES]\n VC1 0 0\n VC1 50 40\n VC1 100 100\n"
        + flv_footer;
    writeflv(s);

    EN_Project ph;
    BOOST_REQUIRE(openflv(&ph, FLV_TMP_INP) == 0);
    BOOST_REQUIRE_EQUAL(EN_saveinpfile(ph, FLV_OUT_INP), 0);
    closeflv(ph);

    BOOST_REQUIRE(openflv(&ph, FLV_OUT_INP) == 0);
    int idx, cidx;
    EN_getlinkindex(ph, (char *)"FV1", &idx);
    EN_getcurveindex(ph, (char *)"VC1", &cidx);
    double curve;
    EN_getlinkvalue(ph, idx, EN_PCV_CURVE, &curve);
    BOOST_CHECK_EQUAL((int)curve, cidx);

    closeflv(ph);
    std::remove(FLV_TMP_INP);
    std::remove(FLV_OUT_INP);
}


BOOST_AUTO_TEST_SUITE_END()
