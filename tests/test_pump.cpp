/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_pump.cpp
 Description:  Tests Variable Speed Pump (VSP) with PRESSURE keyword
 Authors:      see AUTHORS
 Copyright:    see AUTHORS
 License:      see LICENSE
 Last Updated: 03/23/2026
 ******************************************************************************
*/

#include <math.h>

#include <boost/test/unit_test.hpp>

#include "epanet2_2.h"

#define DATA_PATH_VSP          "./pump-test-vsp.inp"
#define DATA_PATH_NOVSP        "./pump-test-novsp.inp"
#define DATA_PATH_3PT_VSP      "./pump-test-3pt-vsp.inp"
#define DATA_PATH_3PT_NOVSP    "./pump-test-3pt-novsp.inp"
#define DATA_PATH_VSP_MINSPD   "./pump-test-vsp-minspeed.inp"
#define DATA_PATH_RPT_A        "./test_vsp_a.rpt"
#define DATA_PATH_OUT_A        "./test_vsp_a.out"
#define DATA_PATH_RPT_B        "./test_vsp_b.rpt"
#define DATA_PATH_OUT_B        "./test_vsp_b.out"


// Helper: run a VSP network through EPS, check pressure=target at every step
static void run_vsp_eps_test(const char* inp_path, double target_pressure)
{
    int error;
    EN_Project ph = NULL;
    int nodeIndex, linkIndex;
    double pressure, setting;
    long t, tstep;

    EN_createproject(&ph);
    error = EN_open(ph, inp_path, DATA_PATH_RPT_A, DATA_PATH_OUT_A);
    BOOST_REQUIRE(error == 0);

    EN_getnodeindex(ph, (char*)"3", &nodeIndex);
    EN_getlinkindex(ph, (char*)"PU1", &linkIndex);

    error = EN_openH(ph);
    BOOST_REQUIRE(error == 0);
    error = EN_initH(ph, EN_NOSAVE);
    BOOST_REQUIRE(error == 0);

    int step_count = 0;
    do {
        error = EN_runH(ph, &t);
        BOOST_REQUIRE(error <= 6);

        EN_getnodevalue(ph, nodeIndex, EN_PRESSURE, &pressure);
        BOOST_CHECK_CLOSE(pressure, target_pressure, 1.0);

        EN_getlinkvalue(ph, linkIndex, EN_SETTING, &setting);
        BOOST_CHECK(setting > 0.1);
        BOOST_CHECK(setting < 3.0);

        step_count++;
        error = EN_nextH(ph, &tstep);
        BOOST_REQUIRE(error == 0);
    } while (tstep > 0);

    BOOST_CHECK(step_count > 1);

    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
}

// Helper: run VSP then cross-validate with fixed speed on no-VSP network
static void run_vsp_crossvalidation_test(const char* vsp_path,
                                         const char* novsp_path,
                                         double target_pressure)
{
    int error;
    EN_Project ph_vsp = NULL, ph_novsp = NULL;
    int nIdx, lIdx;

    // --- Run VSP network for first timestep ---
    EN_createproject(&ph_vsp);
    error = EN_open(ph_vsp, vsp_path, DATA_PATH_RPT_A, DATA_PATH_OUT_A);
    BOOST_REQUIRE(error == 0);

    EN_getnodeindex(ph_vsp, (char*)"3", &nIdx);
    EN_getlinkindex(ph_vsp, (char*)"PU1", &lIdx);
    EN_settimeparam(ph_vsp, EN_DURATION, 0);

    error = EN_solveH(ph_vsp);
    BOOST_REQUIRE(error <= 6);

    double speed_vsp, pressure_vsp, flow_vsp;
    EN_getlinkvalue(ph_vsp, lIdx, EN_SETTING, &speed_vsp);
    EN_getnodevalue(ph_vsp, nIdx, EN_PRESSURE, &pressure_vsp);
    EN_getlinkvalue(ph_vsp, lIdx, EN_FLOW, &flow_vsp);

    BOOST_CHECK_CLOSE(pressure_vsp, target_pressure, 1.0);

    EN_close(ph_vsp);
    EN_deleteproject(ph_vsp);

    // --- Run no-VSP network with VSP speed ---
    EN_createproject(&ph_novsp);
    error = EN_open(ph_novsp, novsp_path, DATA_PATH_RPT_B, DATA_PATH_OUT_B);
    BOOST_REQUIRE(error == 0);

    EN_getnodeindex(ph_novsp, (char*)"3", &nIdx);
    EN_getlinkindex(ph_novsp, (char*)"PU1", &lIdx);
    EN_settimeparam(ph_novsp, EN_DURATION, 0);
    EN_setlinkvalue(ph_novsp, lIdx, EN_INITSETTING, speed_vsp);

    error = EN_solveH(ph_novsp);
    BOOST_REQUIRE(error == 0);

    double pressure_novsp, flow_novsp;
    EN_getnodevalue(ph_novsp, nIdx, EN_PRESSURE, &pressure_novsp);
    EN_getlinkvalue(ph_novsp, lIdx, EN_FLOW, &flow_novsp);

    BOOST_CHECK_CLOSE(pressure_novsp, target_pressure, 1.0);
    BOOST_CHECK_CLOSE(pressure_novsp, pressure_vsp, 1.0);
    BOOST_CHECK_CLOSE(flow_novsp, flow_vsp, 1.0);

    EN_close(ph_novsp);
    EN_deleteproject(ph_novsp);
}


BOOST_AUTO_TEST_SUITE(test_pump)

// === 1-point curve tests ===

BOOST_AUTO_TEST_CASE(test_vsp_1pt_eps)
{
    run_vsp_eps_test(DATA_PATH_VSP, 15.0);
}

BOOST_AUTO_TEST_CASE(test_vsp_1pt_crossvalidation)
{
    run_vsp_crossvalidation_test(DATA_PATH_VSP, DATA_PATH_NOVSP, 15.0);
}

// === 3-point curve tests ===

BOOST_AUTO_TEST_CASE(test_vsp_3pt_eps)
{
    run_vsp_eps_test(DATA_PATH_3PT_VSP, 15.0);
}

BOOST_AUTO_TEST_CASE(test_vsp_3pt_crossvalidation)
{
    run_vsp_crossvalidation_test(DATA_PATH_3PT_VSP, DATA_PATH_3PT_NOVSP, 15.0);
}

// === MINSPEED test ===

BOOST_AUTO_TEST_CASE(test_vsp_minspeed_closes_pump)
/*
    Tests that a VSP pump closes when the computed speed falls
    below MINSPEED. Normal operating speed for this network is ~0.54,
    so MINSPEED 0.8 should cause the pump to close.
*/
{
    int error;
    EN_Project ph = NULL;
    int linkIndex;
    double setting;

    EN_createproject(&ph);
    error = EN_open(ph, DATA_PATH_VSP_MINSPD, DATA_PATH_RPT_A, DATA_PATH_OUT_A);
    BOOST_REQUIRE(error == 0);

    EN_getlinkindex(ph, (char*)"PU1", &linkIndex);

    error = EN_solveH(ph);
    // May get warnings due to pump closure
    BOOST_REQUIRE(error <= 6);

    // Pump should be closed (setting = 0) since computed speed ~0.54 < MINSPEED 0.8
    EN_getlinkvalue(ph, linkIndex, EN_SETTING, &setting);
    BOOST_CHECK_EQUAL(setting, 0.0);

    EN_close(ph);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_SUITE_END()
