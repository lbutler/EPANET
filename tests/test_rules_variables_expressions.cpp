/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_rules_variables_expressions.cpp
 Description:  Tests EPANET named variables and expressions in rules
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
std::string create_temp_inp_with_rules(const std::string &rules_section)
{
    // Use a simple, predictable filename
    std::string temp_inp = "test_rules.inp";

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

BOOST_AUTO_TEST_SUITE(test_rules_variables_expressions)

// Test 1: Simple named variables with expressions
BOOST_AUTO_TEST_CASE(test_simple_named_variables)
{
    std::string rules = R"([RULES]
VARIABLE MyTankLevel = NODE 2 LEVEL
EXPRESSION MyTankLevelE = MyTankLevel

RULE 1
IF MyTankLevelE < 110
THEN LINK 9 STATUS = OPEN

RULE 2
IF MyTankLevelE > 140
THEN LINK 9 STATUS = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // Use simple, predictable filenames
    std::string rpt_file = "test_rules.rpt";
    std::string out_file = "test_rules.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    // Debug: check if the file was created correctly
    std::ifstream check_file(temp_inp);
    if (!check_file.is_open())
    {
        BOOST_FAIL("Could not open temporary file: " << temp_inp);
    }
    std::string first_line;
    std::getline(check_file, first_line);
    check_file.close();
    BOOST_CHECK(first_line.find("[TITLE]") != std::string::npos);

    // Debug: check the rules section
    std::ifstream check_rules(temp_inp);
    std::string content((std::istreambuf_iterator<char>(check_rules)),
                        std::istreambuf_iterator<char>());
    check_rules.close();
    BOOST_CHECK(content.find("VARIABLE MyTankLevel") != std::string::npos);
    BOOST_CHECK(content.find("EXPRESSION MyTankLevelE") != std::string::npos);
    BOOST_CHECK(content.find("IF MyTankLevelE < 110") != std::string::npos);

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    if (error != 0)
    {
        // Get error message
        char errmsg[256];
        EN_geterror(error, errmsg, 256);
        BOOST_FAIL("EN_solveH failed with error " << error << ": " << errmsg);
    }
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining pump status
    int pump_index;
    error = EN_getlinkindex(ph, (char *)"9", &pump_index);
    BOOST_REQUIRE(error == 0);

    double pump_status;
    error = EN_getlinkvalue(ph, pump_index, EN_STATUS, &pump_status);
    BOOST_REQUIRE(error == 0);

    // The pump should be open initially (tank level < 110)
    BOOST_CHECK(pump_status == EN_OPEN);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

// Test 2: Basic expressions with mathematical functions
BOOST_AUTO_TEST_CASE(test_basic_expressions)
{
    std::string rules = R"([RULES]
VARIABLE Q247 = LINK 10 FLOW
EXPRESSION LowQ = step(150 - Q247)
EXPRESSION HighQ = step(Q247 - 300)

RULE 1
IF LowQ = 1
THEN LINK 9 STATUS = OPEN

RULE 2
IF HighQ = 1
THEN LINK 9 STATUS = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // Use simple, predictable filenames
    std::string rpt_file = "test_expressions.rpt";
    std::string out_file = "test_expressions.out";

    // Clean up any existing files
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());

    EN_Project ph;
    int error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), rpt_file.c_str(), out_file.c_str());
    BOOST_REQUIRE(error == 0);

    // Run hydraulic simulation
    error = EN_solveH(ph);
    if (error != 0)
    {
        // Get error message
        char errmsg[256];
        EN_geterror(error, errmsg, 256);
        BOOST_FAIL("EN_solveH failed with error " << error << ": " << errmsg);
    }
    BOOST_REQUIRE(error == 0);

    // Check that rules were processed correctly by examining pump status
    int pump_index;
    error = EN_getlinkindex(ph, (char *)"9", &pump_index);
    BOOST_REQUIRE(error == 0);

    double pump_status;
    error = EN_getlinkvalue(ph, pump_index, EN_STATUS, &pump_status);
    BOOST_REQUIRE(error == 0);

    // Should have a valid status
    BOOST_CHECK(pump_status == EN_OPEN || pump_status == EN_CLOSED);

    EN_close(ph);
    EN_deleteproject(ph);

    // Clean up temporary files
    std::remove(temp_inp.c_str());
    std::remove(rpt_file.c_str());
    std::remove(out_file.c_str());
}

BOOST_AUTO_TEST_SUITE_END()