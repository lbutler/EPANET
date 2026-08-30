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
    int error = 0;
    double cause, node, link, count;
    char id[EN_MAXID + 1];

    // Junctions J3 & J4 are cut off from the reservoir by closed pipes
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_island.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 110);

    // Failure is diagnosed as a disconnection at one of the
    // isolated junctions caused by one of the closed pipes
    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_DISCONNECTED);

    error = EN_getstatistic(ph, EN_HYDERRNODE, &node);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodeid(ph, (int)node, id);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(strcmp(id, "J3") == 0 || strcmp(id, "J4") == 0);

    error = EN_getstatistic(ph, EN_HYDERRLINK, &link);
    BOOST_REQUIRE(error == 0);
    error = EN_getlinkid(ph, (int)link, id);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(strcmp(id, "PC1") == 0 || strcmp(id, "PC2") == 0);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 2);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_CASE(test_hyderr_valve)
{
    int error = 0;
    double cause, node, link;
    char id[EN_MAXID + 1];

    // PRV V2's setting conflicts with the network: badvalve() forces
    // it open but the retry still fails at its downstream node D2,
    // which remains connected to the reservoir through open pipes
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_valve.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 110);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_VALVE);

    error = EN_getstatistic(ph, EN_HYDERRNODE, &node);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodeid(ph, (int)node, id);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(strcmp(id, "D2") == 0);

    error = EN_getstatistic(ph, EN_HYDERRLINK, &link);
    BOOST_REQUIRE(error == 0);
    error = EN_getlinkid(ph, (int)link, id);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(strcmp(id, "V2") == 0);

    error = EN_close(ph);
    BOOST_REQUIRE(error == 0);
    EN_deleteproject(ph);
}

BOOST_AUTO_TEST_CASE(test_hyderr_other)
{
    int error = 0;
    double cause, node, link, count;
    char id[EN_MAXID + 1];

    // An all-open pure-pipe network with an extreme conductance
    // contrast: the failing node is reachable from both reservoirs
    // and touches no control valve, so no cause can be assigned
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);
    error = EN_open(ph, "./test_hyderr_other.inp", DATA_PATH_RPT, "");
    BOOST_REQUIRE(error == 0);

    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 110);

    error = EN_getstatistic(ph, EN_HYDERRCAUSE, &cause);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(cause == EN_HYDERR_OTHER);

    error = EN_getstatistic(ph, EN_HYDERRNODE, &node);
    BOOST_REQUIRE(error == 0);
    error = EN_getnodeid(ph, (int)node, id);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(strcmp(id, "J1") == 0);

    error = EN_getstatistic(ph, EN_HYDERRLINK, &link);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(link == 0);

    error = EN_getstatistic(ph, EN_DISCONNECTEDNODES, &count);
    BOOST_REQUIRE(error == 0);
    BOOST_REQUIRE(count == 0);

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
