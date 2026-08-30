/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_hyderror.cpp
 Description:  Tests EPANET toolkit api functions
 Authors:      see AUTHORS
 Copyright:    see AUTHORS
 License:      see LICENSE
 Last Updated: 08/30/2026
 ******************************************************************************
*/

/*
   Tests the diagnosis of hydraulic solver failures (error 110)
*/

#include <boost/test/unit_test.hpp>

#include "test_toolkit.hpp"

BOOST_AUTO_TEST_SUITE (test_hyderror)

BOOST_AUTO_TEST_CASE(test_hyderr_disconnected)
{
    int error = 0, index;
    double cause, count, head;

    // Junctions J3 & J4 are cut off from the reservoir by closed pipes.
    // Instead of failing with an ill-conditioned matrix (error 110) the
    // solver fixes the island's heads at its lowest elevation and ends
    // the run with warning 3 (system disconnected).
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_island.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 3);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_NONE);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 2);

    // Both island junctions sit at their (common) elevation of 100
    error = EN_getnodeindex(ph, (char *)"J3", &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, EN_HEAD, &head);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(abs(head - 100.0) < 0.001);
    error = EN_getnodeindex(ph, (char *)"J4", &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, EN_HEAD, &head);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(abs(head - 100.0) < 0.001);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_CASE(test_hyderr_ky3_island)
{
    int error = 0, index, i;
    double count, demand, pressure;
    const char *island[] = {"J-134", "J-256", "J-275"};

    // A real distribution system (KY3 from the University of Kentucky
    // dataset) with one pipe closed for a shutdown, cutting three
    // junctions off from every reservoir. The unhandled result was a
    // converged run reporting pressures below -700000 psi at the cut-off
    // junctions with their full demands counted as delivered.
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_ky3.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 3);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 3);

    // The cut-off junctions deliver no demand and hold no positive pressure
    for (i = 0; i < 3; i++)
    {
        error = EN_getnodeindex(ph, (char *)island[i], &index);
        BOOST_REQUIRE(error == 0);
        error = EN_getnodevalue(ph, index, EN_DEMAND, &demand);
        BOOST_REQUIRE(error == 0);
        BOOST_REQUIRE(abs(demand) < 0.001);
        error = EN_getnodevalue(ph, index, EN_PRESSURE, &pressure);
        BOOST_REQUIRE(error == 0);
        BOOST_REQUIRE(pressure < 0.001 && pressure > -1000.0);
    }

    // A junction that stays connected keeps its full demand
    error = EN_getnodeindex(ph, (char *)"J-1", &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, EN_DEMAND, &demand);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, EN_FULLDEMAND, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(abs(demand - count) < 0.001);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_CASE(test_hyderr_valve)
{
    int error = 0, index;
    double cause, count, demand;

    // PRV V2's setting conflicts with the network: badvalve() forces
    // it open on the first trial, after which the matrix would fail
    // at its downstream node D2 whose only real connection has a
    // conductance below the closed-link placeholder. The solver
    // detects that D2 & U2 are effectively cut off, fixes their
    // heads and ends with warning 3 instead of error 110.
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_valve.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 3);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_NONE);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 2);

    // The unservable demand at D2 reads as undelivered, not served
    error = EN_getnodeindex(ph, (char *)"D2", &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, EN_DEMAND, &demand);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(abs(demand) < 0.001);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_CASE(test_hyderr_other)
{
    int error = 0;
    double cause, count;

    // An all-open pure-pipe network whose central pipe's conductance
    // exceeds its feed pipes' by 44 orders of magnitude. The feeds
    // fall below the closed-link placeholder conductance, so the
    // solver treats the two junctions as cut off, fixes their heads
    // and ends with warning 3 instead of failing with error 110 on
    // an exactly zero Cholesky pivot.
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_other.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 3);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_NONE);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 2);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_FIXTURE_TEST_CASE(test_hyderr_none, FixtureOpenClose)
{
    double value;

    // A successful run leaves no failure diagnosis behind
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == EN_HYDERR_NONE);

    error = EN_getstatistic(ph, EN_HYDERRNODE, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 0);

    error = EN_getstatistic(ph, EN_HYDERRLINK, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 0);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 0);
}

BOOST_AUTO_TEST_SUITE_END()
