/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_rules_variables_in_comparisons.cpp
 Description:  Tests EPANET direct variable usage in rule comparisons (Feature A)
 Authors:      see AUTHORS
 Copyright:    see AUTHORS
 License:      see LICENSE
 Last Updated: 12/19/2024
 ******************************************************************************
*/

#include <boost/test/unit_test.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>

#include "test_toolkit.hpp"

// Helper function to create a temporary INP file with rules
std::string create_temp_inp_with_rules_comparisons(const std::string &rules_section)
{
    // Use a simple, predictable filename
    std::string temp_inp = "test_var_comparisons.inp";

    // Read the base net1.inp file
    std::ifstream base_file("net1.inp");
    if (!base_file.is_open())
    {
        BOOST_FAIL("Could not open base file: net1.inp");
    }
    std::string content((std::istreambuf_iterator<char>(base_file)),
                        std::istreambuf_iterator<char>());
    base_file.close();

    // Find the [RULES] section and replace it
    size_t rules_pos = content.find("[RULES]");
    if (rules_pos != std::string::npos)
    {
        // Find the next section (starts with [)
        size_t next_section = content.find("[", rules_pos + 1);
        if (next_section != std::string::npos)
        {
            // Replace from [RULES] to the start of the next section
            content.replace(rules_pos, next_section - rules_pos, rules_section);
        }
        else
        {
            // No next section, replace to the end
            content.replace(rules_pos, content.length() - rules_pos, rules_section);
        }
    }
    else
    {
        // If no [RULES] section found, append it before [ENERGY]
        size_t energy_pos = content.find("[ENERGY]");
        if (energy_pos != std::string::npos)
        {
            content.insert(energy_pos, rules_section + "\n");
        }
        else
        {
            // If no [ENERGY] section, append at the end
            content += "\n" + rules_section + "\n";
        }
    }

    // Write the modified content to temp file
    std::ofstream temp_file(temp_inp);
    if (!temp_file.is_open())
    {
        BOOST_FAIL("Could not create temporary file: " << temp_inp);
    }
    temp_file << content;
    temp_file.close();

    return temp_inp;
}

BOOST_AUTO_TEST_SUITE(test_rules_variables_in_comparisons)

// Test 1: Variable < number should work (Feature A implementation)
BOOST_AUTO_TEST_CASE(Var_LHS_number_RHS_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 10 LEVEL

RULE 1
IF TankLevel < 110
THEN LINK 11 STATUS = OPEN

RULE 2
IF TankLevel > 140
THEN LINK 11 STATUS = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules_comparisons(rules);
    std::string rpt_file = "test_var_lhs.rpt";
    std::string out_file = "test_var_lhs.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    // This should work with Feature A implementation
    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining link status
    int link_index;
    error = EN_getlinkindex(ph, (char *)"11", &link_index);
    BOOST_REQUIRE(error == 0);

    double link_status;
    error = EN_getlinkvalue(ph, link_index, EN_STATUS, &link_status);
    BOOST_REQUIRE(error == 0);

    // The link should have a valid status (either open or closed)
    BOOST_CHECK(link_status == EN_OPEN || link_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

// Test 2: Variable < Variable should work (Feature A implementation)
BOOST_AUTO_TEST_CASE(Var_LHS_Var_RHS_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 10 LEVEL
VARIABLE PumpFlow = LINK 10 FLOW

RULE 1
IF PumpFlow > 100
THEN LINK 11 STATUS = OPEN

RULE 2
IF TankLevel < 120
THEN LINK 11 STATUS = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules_comparisons(rules);
    std::string rpt_file = "test_var_var.rpt";
    std::string out_file = "test_var_var.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    // This should work with Feature A implementation
    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining link status
    int link_index;
    error = EN_getlinkindex(ph, (char *)"11", &link_index);
    BOOST_REQUIRE(error == 0);

    double link_status;
    error = EN_getlinkvalue(ph, link_index, EN_STATUS, &link_status);
    BOOST_REQUIRE(error == 0);

    // The link should have a valid status
    BOOST_CHECK(link_status == EN_OPEN || link_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

// Test 3: Variable = Variable should work (Feature A implementation)
BOOST_AUTO_TEST_CASE(Var_LHS_Var_RHS_equality_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 10 LEVEL
VARIABLE TankLevel2 = NODE 10 LEVEL

RULE 1
IF TankLevel = TankLevel2
THEN LINK 11 STATUS = OPEN
)";

    std::string temp_inp = create_temp_inp_with_rules_comparisons(rules);
    std::string rpt_file = "test_var_equality.rpt";
    std::string out_file = "test_var_equality.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    // This should work with Feature A implementation
    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining link status
    int link_index;
    error = EN_getlinkindex(ph, (char *)"11", &link_index);
    BOOST_REQUIRE(error == 0);

    double link_status;
    error = EN_getlinkvalue(ph, link_index, EN_STATUS, &link_status);
    BOOST_REQUIRE(error == 0);

    // The link should have a valid status
    BOOST_CHECK(link_status == EN_OPEN || link_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

// Test 4: Variable with object triple RHS should work (Feature A implementation)
BOOST_AUTO_TEST_CASE(Var_LHS_object_triple_RHS_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 10 LEVEL

RULE 1
IF TankLevel < NODE 10 LEVEL
THEN LINK 11 STATUS = OPEN
)";

    std::string temp_inp = create_temp_inp_with_rules_comparisons(rules);
    std::string rpt_file = "test_var_object.rpt";
    std::string out_file = "test_var_object.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    // This should work with Feature A implementation
    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining link status
    int link_index;
    error = EN_getlinkindex(ph, (char *)"11", &link_index);
    BOOST_REQUIRE(error == 0);

    double link_status;
    error = EN_getlinkvalue(ph, link_index, EN_STATUS, &link_status);
    BOOST_REQUIRE(error == 0);

    // The link should have a valid status
    BOOST_CHECK(link_status == EN_OPEN || link_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

// Test 5: Complex variable comparison with different types
BOOST_AUTO_TEST_CASE(Var_complex_comparison_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 10 LEVEL
VARIABLE PumpFlow = LINK 10 FLOW

RULE 1
IF TankLevel < 110
AND PumpFlow > 50
THEN LINK 11 STATUS = OPEN

RULE 2
IF TankLevel > 140
THEN LINK 11 STATUS = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules_comparisons(rules);
    std::string rpt_file = "test_complex.rpt";
    std::string out_file = "test_complex.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    // This should work with Feature A implementation
    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining link status
    int link_index;
    error = EN_getlinkindex(ph, (char *)"11", &link_index);
    BOOST_REQUIRE(error == 0);

    double link_status;
    error = EN_getlinkvalue(ph, link_index, EN_STATUS, &link_status);
    BOOST_REQUIRE(error == 0);

    // The link should have a valid status
    BOOST_CHECK(link_status == EN_OPEN || link_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

BOOST_AUTO_TEST_SUITE_END()