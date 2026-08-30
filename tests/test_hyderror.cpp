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
   Tests the handling of junctions with no path to a supply source
*/

#include <boost/test/unit_test.hpp>

#include "test_toolkit.hpp"

namespace {

// Opens a project on an input file; returns the EN_solveH() result
int solve(EN_Project &ph, const char *inpfile)
{
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, inpfile, DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);
    return EN_solveH(ph);
}

void closeproject(EN_Project &ph)
{
    int error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

double nodevalue(EN_Project ph, const char *id, int prop)
{
    int index, error;
    double v;
    error = EN_getnodeindex(ph, (char *)id, &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodevalue(ph, index, prop, &v);
    BOOST_REQUIRE(error == 0);
    return v;
}

double linkvalue(EN_Project ph, const char *id, int prop)
{
    int index, error;
    double v;
    error = EN_getlinkindex(ph, (char *)id, &index);
    BOOST_REQUIRE(error == 0);
    error = EN_getlinkvalue(ph, index, prop, &v);
    BOOST_REQUIRE(error == 0);
    return v;
}

double statistic(EN_Project ph, int type)
{
    double v;
    int error = EN_getstatistic(ph, type, &v);
    BOOST_REQUIRE(error == 0);
    return v;
}

} // namespace

BOOST_AUTO_TEST_SUITE (test_hyderror)

BOOST_AUTO_TEST_CASE(test_island_zero_demand)
{
    // Junctions J3 (elev 100) & J4 (elev 90), no demand, cut off from
    // the reservoir by closed pipes. The island solves with warning 3:
    // both heads pinned at the island's lowest junction elevation
    // (see islandhead() in hydcoeffs.c), zero flow in its internal
    // links.
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_island.inp");
    BOOST_REQUIRE(error == 3);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 2);

    BOOST_REQUIRE(abs(nodevalue(ph, "J3", EN_HEAD) - 90.0) < 1e-6);
    BOOST_REQUIRE(abs(nodevalue(ph, "J4", EN_HEAD) - 90.0) < 1e-6);
    BOOST_REQUIRE(abs(linkvalue(ph, "PI1", EN_FLOW)) < 1e-6);
    BOOST_REQUIRE(abs(linkvalue(ph, "PI16", EN_FLOW)) < 1e-6);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_island_with_demand)
{
    // The same island carrying demand at both junctions: no error 110,
    // no absurd pressures - warning 3, pinned heads, zero delivered
    // demand, zero internal flows.
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_island2.inp");
    BOOST_REQUIRE(error == 3);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 2);
    BOOST_REQUIRE(abs(nodevalue(ph, "J3", EN_HEAD) - 90.0) < 1e-6);
    BOOST_REQUIRE(abs(nodevalue(ph, "J4", EN_HEAD) - 90.0) < 1e-6);
    BOOST_REQUIRE(abs(nodevalue(ph, "J3", EN_DEMAND)) < 1e-6);
    BOOST_REQUIRE(abs(nodevalue(ph, "J4", EN_DEMAND)) < 1e-6);
    BOOST_REQUIRE(abs(linkvalue(ph, "PI1", EN_FLOW)) < 1e-6);

    // The mainland is served in full
    BOOST_REQUIRE(abs(nodevalue(ph, "J1", EN_DEMAND) - 50.0) < 1e-6);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_island_ky3)
{
    // A real distribution system (KY3 from the University of Kentucky
    // dataset) with one pipe closed for a shutdown, cutting junctions
    // J-134, J-256 & J-275 off from every reservoir. Previously this
    // converged with pressures below -700,000 psi and the demands
    // counted as delivered.
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_ky3.inp");
    BOOST_REQUIRE(error == 3);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 3);

    const char *island[] = {"J-134", "J-256", "J-275"};
    double p;
    int i;
    for (i = 0; i < 3; i++)
    {
        BOOST_REQUIRE(abs(nodevalue(ph, island[i], EN_DEMAND)) < 0.001);
        p = nodevalue(ph, island[i], EN_PRESSURE);
        // hydrostatic above the island's lowest junction: no
        // positive pressure and nothing below a sane bound
        BOOST_REQUIRE(p < 0.001 && p > -1000.0);
    }

    // A junction that stays connected keeps its full demand
    BOOST_REQUIRE(abs(nodevalue(ph, "J-1", EN_DEMAND) -
                      nodevalue(ph, "J-1", EN_FULLDEMAND)) < 0.001);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_island_reconnect)
{
    // Net1 with controls that cut junction 13 off at hour 6 and
    // reconnect it at hour 12. Steps before the closure must be
    // identical (results & trial counts) to unmodified Net1; the
    // island window reports warning 3 with node 13 pinned at its
    // elevation; after reconnection service resumes with no warning.
    EN_Project ph = NULL, ref = NULL;
    int error, e2;
    long t, tstep, t2, tstep2;

    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_reconnect.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);
    error = EN_createproject(&ref);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ref, DATA_PATH_NET1, "./ref.rpt", "");
    BOOST_REQUIRE(error == 0);

    error = EN_openH(ph);
    BOOST_REQUIRE(error == 0);
    error = EN_initH(ph, EN_NOSAVE);
    BOOST_REQUIRE(error == 0);
    error = EN_openH(ref);
    BOOST_REQUIRE(error == 0);
    error = EN_initH(ref, EN_NOSAVE);
    BOOST_REQUIRE(error == 0);

    do {
        error = EN_runH(ph, &t);
        e2 = EN_runH(ref, &t2);
        BOOST_REQUIRE(error <= 3);   // never a hard error

        if (t < 6 * 3600)
        {
            // pre-closure: identical to unmodified Net1, trial for trial
            BOOST_REQUIRE(error == 0 && e2 == 0);
            BOOST_REQUIRE(abs(nodevalue(ph, "13", EN_HEAD) -
                              nodevalue(ref, "13", EN_HEAD)) < 1e-9);
            BOOST_REQUIRE(abs(nodevalue(ph, "22", EN_HEAD) -
                              nodevalue(ref, "22", EN_HEAD)) < 1e-9);
            BOOST_REQUIRE(statistic(ph, EN_ITERATIONS) ==
                          statistic(ref, EN_ITERATIONS));
        }
        else if (t >= 6 * 3600 && t < 12 * 3600)
        {
            // island window: warning 3, junction 13 pinned at its
            // elevation (a one-junction island), no demand delivered
            BOOST_REQUIRE(error == 3);
            BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 1);
            BOOST_REQUIRE(abs(nodevalue(ph, "13", EN_HEAD) - 695.0) < 1e-6);
            BOOST_REQUIRE(abs(nodevalue(ph, "13", EN_DEMAND)) < 1e-6);
        }
        else
        {
            // reconnected: no warning, full demand delivered again
            BOOST_REQUIRE(error == 0);
            BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 0);
            BOOST_REQUIRE(abs(nodevalue(ph, "13", EN_DEMAND) -
                              nodevalue(ph, "13", EN_FULLDEMAND)) < 1e-6);
        }
        error = EN_nextH(ph, &tstep);
        BOOST_REQUIRE(error == 0);
        e2 = EN_nextH(ref, &tstep2);
        BOOST_REQUIRE(e2 == 0);
    } while (tstep > 0);

    EN_closeH(ph);
    EN_closeH(ref);
    closeproject(ph);
    closeproject(ref);
}

BOOST_AUTO_TEST_CASE(test_island_tank_empties)
{
    // Net1 fed only by its tank (pump shut). When the tank empties
    // mid-run its outlet links close and the whole junction set
    // becomes an island: warning 3 with sane hydrostatic heads, no
    // error 110, no absurd pressures.
    EN_Project ph = NULL;
    int error, sawwarn3 = 0;
    long t, tstep;
    double p, minp = 1.e30;

    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_tankdry.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);
    error = EN_openH(ph);
    BOOST_REQUIRE(error == 0);
    error = EN_initH(ph, EN_NOSAVE);
    BOOST_REQUIRE(error == 0);

    do {
        error = EN_runH(ph, &t);
        BOOST_REQUIRE(error <= 6);   // warnings only, never error 110
        if (error == 3)
        {
            sawwarn3 = 1;
            BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) > 0);
        }
        p = nodevalue(ph, "13", EN_PRESSURE);
        if (p < minp) minp = p;
        error = EN_nextH(ph, &tstep);
        BOOST_REQUIRE(error == 0);
    } while (tstep > 0);

    BOOST_REQUIRE(sawwarn3 == 1);
    BOOST_REQUIRE(minp > -1000.0);

    EN_closeH(ph);
    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_fcv_fed_section)
{
    // A junction fed only through an ACTIVE FCV physically receives
    // the valve's flow setting and must not be treated as an island.
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_fcv.inp");
    BOOST_REQUIRE(error != 3);
    BOOST_REQUIRE(error < 100);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 0);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_parallel_links)
{
    // Two junctions joined by two parallel pipes, one closed; the
    // section beyond them is fed only through the open twin and must
    // not be flagged as disconnected.
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_parallel.inp");
    BOOST_REQUIRE(error == 0);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 0);
    BOOST_REQUIRE(abs(nodevalue(ph, "J3", EN_DEMAND) - 10.0) < 1e-6);

    closeproject(ph);
}

BOOST_FIXTURE_TEST_CASE(test_no_disconnection, FixtureOpenClose)
{
    double value;

    // A successful run on a fully connected network leaves no
    // disconnection count behind
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 0);

    error = EN_getstatistic(ph, EN_ILLCONDITIONEDNODE, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 0);
}

BOOST_AUTO_TEST_CASE(test_isolation_off_by_default)
{
    // Isolation is off unless asked for: the same island model then
    // behaves exactly as it did before the option existed - the matrix
    // is ill-conditioned and the run fails - while the failure is still
    // diagnosed as a disconnection
    EN_Project ph = NULL;
    double value;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_island2.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_getoption(ph, EN_ISOLATION, &value);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(value == 1);          // this fixture asks for it

    error = EN_setoption(ph, EN_ISOLATION, 0);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(EN_solveH(ph) == 110);
    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 2);

    // The failing equation's node is retrievable rather than only
    // printed, so a caller can point at it without parsing the report.
    // Which member of the offending group the solver stops on depends on
    // the equation ordering, so only require that it is one of them.
    int j3, j4, errnode = (int)statistic(ph, EN_ILLCONDITIONEDNODE);
    BOOST_REQUIRE(EN_getnodeindex(ph, (char *)"J3", &j3) == 0);
    BOOST_REQUIRE(EN_getnodeindex(ph, (char *)"J4", &j4) == 0);
    BOOST_REQUIRE(errnode == j3 || errnode == j4);
    BOOST_REQUIRE(nodevalue(ph, "J3", EN_ISOLATED) == 0);

    // Turning it on solves the same network with the island out of service
    error = EN_setoption(ph, EN_ISOLATION, 1);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(EN_solveH(ph) == 3);
    BOOST_REQUIRE(nodevalue(ph, "J3", EN_ISOLATED) == 1);
    BOOST_REQUIRE(statistic(ph, EN_ILLCONDITIONEDNODE) == 0);

    // Only the two flag values are accepted
    BOOST_REQUIRE(EN_setoption(ph, EN_ISOLATION, 2) == 213);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_isolation_inflow_source)
{
    // A junction with a negative demand injects water, so it supplies
    // the area around it: the group it belongs to keeps its demand and
    // is never taken out of service, even though no tank can reach it
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_inflow.inp");
    BOOST_REQUIRE(error == 0);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 0);
    BOOST_REQUIRE(nodevalue(ph, "J3", EN_ISOLATED) == 0);
    BOOST_REQUIRE(nodevalue(ph, "J4", EN_ISOLATED) == 0);
    BOOST_REQUIRE(abs(nodevalue(ph, "J4", EN_DEMAND) - 5.0) < 1e-6);
    BOOST_REQUIRE(nodevalue(ph, "J4", EN_PRESSURE) > 0.0);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_isolation_follows_asset_direction)
{
    // A check valve only conveys flow from its upstream to its
    // downstream node: J2 beyond CV1's outlet is supplied, while J3
    // behind CV2's inlet is not - the status logic closes CV2 when
    // flow tries to reverse through it and the refreshed detection
    // then takes J3 out of service
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_direction.inp");
    BOOST_REQUIRE(error == 3);

    BOOST_REQUIRE(statistic(ph, EN_DISCONNECTEDNODES) == 1);
    BOOST_REQUIRE(nodevalue(ph, "J2", EN_ISOLATED) == 0);
    BOOST_REQUIRE(nodevalue(ph, "J3", EN_ISOLATED) == 1);
    BOOST_REQUIRE(abs(nodevalue(ph, "J2", EN_DEMAND) - 10.0) < 1e-6);
    BOOST_REQUIRE(nodevalue(ph, "J3", EN_DEMAND) == 0.0);

    closeproject(ph);
}

BOOST_AUTO_TEST_CASE(test_isolation_reports_lost_demand)
{
    // Demand lost to a disconnection is reported through the same
    // statistics as demand lost to low pressure under PDA, and each
    // node's share is retrievable individually
    EN_Project ph = NULL;
    int error = solve(ph, "./test_hyderr_ky3.inp");
    BOOST_REQUIRE(error == 3);

    BOOST_REQUIRE(statistic(ph, EN_DEFICIENTNODES) == 3);
    BOOST_REQUIRE(abs(statistic(ph, EN_DEMANDREDUCTION) - 100.0) < 1e-6);

    // Each isolated node reports the whole of its requested demand as
    // undelivered, and they sum to the shortfall in delivered demand
    const char *isolated[] = {"J-134", "J-256", "J-275"};
    double lost = 0.0;
    for (int i = 0; i < 3; i++)
    {
        BOOST_REQUIRE(nodevalue(ph, isolated[i], EN_ISOLATED) == 1);
        double full = nodevalue(ph, isolated[i], EN_FULLDEMAND);
        BOOST_REQUIRE(full > 0.0);
        BOOST_REQUIRE(nodevalue(ph, isolated[i], EN_DEMANDFLOW) == 0.0);
        BOOST_REQUIRE(abs(nodevalue(ph, isolated[i], EN_DEMANDDEFICIT) - full) < 1e-6);
        lost += full;
    }
    BOOST_REQUIRE(abs(lost - 7.88) < 0.01);

    closeproject(ph);
}

BOOST_AUTO_TEST_SUITE_END()
