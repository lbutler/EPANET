/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_rules_variable_actions.cpp
 Description:  Tests EPANET named variables as action targets in rules (Feature B)
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
static std::string create_temp_inp_with_rules(const std::string &rules_section)
{
    // Use a simple, predictable filename
    std::string temp_inp = "test_rules_actions.inp";

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

BOOST_AUTO_TEST_SUITE(test_rules_variable_actions)

// Test 1: Variable as STATUS action target - should work after Feature B implementation
BOOST_AUTO_TEST_CASE(VarAction_Status_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
VARIABLE ControlValveStatus = LINK 9 STATUS

RULE 1
IF TankLevel < 110
THEN ControlValveStatus = OPEN

RULE 2
IF TankLevel > 140
THEN ControlValveStatus = CLOSED
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work after Feature B implementation
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_rules_actions.rpt", "");
    BOOST_CHECK(error == 0); // Should parse successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 2: Variable as SETTING action target - should work after Feature B implementation
BOOST_AUTO_TEST_CASE(VarAction_Setting_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
VARIABLE PRVSetting = LINK 12 SETTING

RULE 1
IF TankLevel < 110
THEN PRVSetting = 45

RULE 2
IF TankLevel > 140
THEN PRVSetting = 60
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work after Feature B implementation
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_rules_actions.rpt", "");
    BOOST_CHECK(error == 0); // Should parse successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 3: Invalid target (NODE PRESSURE) - should always fail
BOOST_AUTO_TEST_CASE(VarAction_InvalidTarget_errors)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
VARIABLE J2Pressure = NODE 2 PRESSURE

RULE 1
IF TankLevel < 110
THEN J2Pressure = 45
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should always fail - NODE PRESSURE is not assignable
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_rules_actions.rpt", "");
    BOOST_CHECK(error != 0); // Should fail to parse

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 4: Wrong value type for target - should always fail
BOOST_AUTO_TEST_CASE(VarAction_Status_word_validation)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
VARIABLE PRVSetting = LINK 12 SETTING

RULE 1
IF TankLevel < 110
THEN PRVSetting = OPEN
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should always fail - OPEN is not valid for SETTING
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_rules_actions.rpt", "");
    BOOST_CHECK(error != 0); // Should fail to parse

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

BOOST_AUTO_TEST_SUITE_END()