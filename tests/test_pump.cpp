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

#define DATA_PATH_VSP       "./pump-test-vsp.inp"
#define DATA_PATH_NOVSP     "./pump-test-novsp.inp"
#define DATA_PATH_RPT_A     "./test_vsp_a.rpt"
#define DATA_PATH_OUT_A     "./test_vsp_a.out"
#define DATA_PATH_RPT_B     "./test_vsp_b.rpt"
#define DATA_PATH_OUT_B     "./test_vsp_b.out"


BOOST_AUTO_TEST_SUITE(test_pump)

BOOST_AUTO_TEST_CASE(test_vsp_pump_eps)
/*
    Tests that a VSP pump achieves target pressure at every timestep
    of an extended period simulation with varying demand.
*/
{
    int error;
    EN_Project ph = NULL;
    int nodeIndex, linkIndex;
    double pressure, setting;
    long t, tstep;

    EN_createproject(&ph);
    error = EN_open(ph, DATA_PATH_VSP, DATA_PATH_RPT_A, DATA_PATH_OUT_A);
    BOOST_REQUIRE(error == 0);

    error = EN_getnodeindex(ph, (char*)"3", &nodeIndex);
    BOOST_REQUIRE(error == 0);
    error = EN_getlinkindex(ph, (char*)"PU1", &linkIndex);
    BOOST_REQUIRE(error == 0);

    error = EN_openH(ph);
    BOOST_REQUIRE(error == 0);
    error = EN_initH(ph, EN_NOSAVE);
    BOOST_REQUIRE(error == 0);

    int step_count = 0;
    do {
        error = EN_runH(ph, &t);
        BOOST_REQUIRE(error <= 6);

        // Check pressure at node 3 = target (15 m)
        error = EN_getnodevalue(ph, nodeIndex, EN_PRESSURE, &pressure);
        BOOST_REQUIRE(error == 0);
        BOOST_CHECK_CLOSE(pressure, 15.0, 1.0);

        // Check pump speed is reasonable
        error = EN_getlinkvalue(ph, linkIndex, EN_SETTING, &setting);
        BOOST_REQUIRE(error == 0);
        BOOST_CHECK(setting > 0.1);
        BOOST_CHECK(setting < 3.0);

        step_count++;
        error = EN_nextH(ph, &tstep);
        BOOST_REQUIRE(error == 0);
    } while (tstep > 0);

    BOOST_CHECK(step_count > 1);

    error = EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
}


BOOST_AUTO_TEST_CASE(test_vsp_speed_validates_against_fixed_speed)
/*
    Validates the VSP-computed speed by running the same network
    without PRESSURE keyword at the first timestep with the
    VSP-computed speed. This uses a fresh solve (no EPS lag) so
    the pressure should match exactly.
*/
{
    int error;
    EN_Project ph_vsp = NULL, ph_novsp = NULL;
    int nIdx, lIdx;
    long t, tstep;

    // --- Run VSP network for first timestep ---
    EN_createproject(&ph_vsp);
    error = EN_open(ph_vsp, DATA_PATH_VSP, DATA_PATH_RPT_A, DATA_PATH_OUT_A);
    BOOST_REQUIRE(error == 0);

    EN_getnodeindex(ph_vsp, (char*)"3", &nIdx);
    EN_getlinkindex(ph_vsp, (char*)"PU1", &lIdx);

    // Set single timestep
    EN_settimeparam(ph_vsp, EN_DURATION, 0);

    error = EN_solveH(ph_vsp);
    BOOST_REQUIRE(error <= 6);

    double speed_vsp, pressure_vsp, flow_vsp;
    EN_getlinkvalue(ph_vsp, lIdx, EN_SETTING, &speed_vsp);
    EN_getnodevalue(ph_vsp, nIdx, EN_PRESSURE, &pressure_vsp);
    EN_getlinkvalue(ph_vsp, lIdx, EN_FLOW, &flow_vsp);

    // VSP should achieve target
    BOOST_CHECK_CLOSE(pressure_vsp, 15.0, 1.0);

    EN_close(ph_vsp);
    EN_deleteproject(ph_vsp);

    // --- Run no-VSP network with VSP speed ---
    EN_createproject(&ph_novsp);
    error = EN_open(ph_novsp, DATA_PATH_NOVSP, DATA_PATH_RPT_B, DATA_PATH_OUT_B);
    BOOST_REQUIRE(error == 0);

    EN_getnodeindex(ph_novsp, (char*)"3", &nIdx);
    EN_getlinkindex(ph_novsp, (char*)"PU1", &lIdx);

    // Set single timestep and pump speed
    EN_settimeparam(ph_novsp, EN_DURATION, 0);
    error = EN_setlinkvalue(ph_novsp, lIdx, EN_INITSETTING, speed_vsp);
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph_novsp);
    BOOST_REQUIRE(error == 0);

    double pressure_novsp, flow_novsp;
    EN_getnodevalue(ph_novsp, nIdx, EN_PRESSURE, &pressure_novsp);
    EN_getlinkvalue(ph_novsp, lIdx, EN_FLOW, &flow_novsp);

    // Pressure at node 3 with VSP-computed speed should match target
    BOOST_CHECK_CLOSE(pressure_novsp, 15.0, 1.0);

    // Pressure should match what VSP reported
    BOOST_CHECK_CLOSE(pressure_novsp, pressure_vsp, 1.0);

    // Flow should match
    BOOST_CHECK_CLOSE(flow_novsp, flow_vsp, 1.0);

    EN_close(ph_novsp);
    EN_deleteproject(ph_novsp);
}

BOOST_AUTO_TEST_SUITE_END()
