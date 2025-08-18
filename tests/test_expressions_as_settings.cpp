/*
 ******************************************************************************
 Project:      OWA EPANET
 Version:      2.3
 Module:       test_expressions_as_settings.cpp
 Description:  Tests EPANET expressions as settings in rules
 Authors:      see AUTHORS
 Copyright:    see AUTHORS
 License:      see LICENSE
 Last Updated: 08/17/2025
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
    std::string temp_inp = "test_expressions_as_settings.inp";

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

BOOST_AUTO_TEST_SUITE(test_expressions_as_settings)

// Test 0: Basic expression parsing - should work
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_BasicParsing_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
EXPRESSION SimpleSetting = 45

RULE 1
IF TankLevel < 130
THEN LINK 9 STATUS = OPEN
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work with basic expression parsing
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK_MESSAGE(error == 0, "EN_open failed with error code: " << error); // Should parse successfully

    // Run the simulation
    error = EN_solveH(ph);
    BOOST_CHECK_MESSAGE(error == 0, "EN_solveH failed with error code: " << error); // Should run successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 1: Simple expression as setting - should work
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_Simple_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
EXPRESSION SimpleSetting = 45

RULE 1
IF TankLevel < 130
THEN LINK 9 SETTING = SimpleSetting
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work with the new expression as setting feature
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK_MESSAGE(error == 0, "EN_open failed with error code: " << error); // Should parse successfully

    // Run the simulation
    error = EN_solveH(ph);
    BOOST_CHECK_MESSAGE(error == 0, "EN_solveH failed with error code: " << error); // Should run successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 2: Complex expression as setting - should work
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_Complex_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
VARIABLE PumpFlow = LINK 9 FLOW
EXPRESSION DynamicSetting = TankLevel * 0.5 + 30
EXPRESSION FlowBasedSetting = PumpFlow * 0.1 + 20

RULE 1
IF TankLevel < 110
THEN LINK 9 SETTING = DynamicSetting

RULE 2
IF PumpFlow > 200
THEN LINK 9 SETTING = FlowBasedSetting
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work with complex expressions as settings
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK(error == 0); // Should parse successfully

    // Run the simulation
    error = EN_solveH(ph);
    BOOST_CHECK(error == 0); // Should run successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 3: Expression as setting with mathematical functions - should work
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_MathFunctions_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
EXPRESSION MathSetting = abs(TankLevel - 120) + 30

RULE 1
IF TankLevel < 130
THEN LINK 9 SETTING = MathSetting
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work with mathematical functions in expressions
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK(error == 0); // Should parse successfully

    // Run the simulation
    error = EN_solveH(ph);
    BOOST_CHECK(error == 0); // Should run successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 4: Expression as setting in legacy LINK format - should work
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_LegacyFormat_works)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
EXPRESSION LegacySetting = 50

RULE 1
IF TankLevel < 130
THEN LINK 9 SETTING = LegacySetting
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should work with the legacy LINK format
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK(error == 0); // Should parse successfully

    // Run the simulation
    error = EN_solveH(ph);
    BOOST_CHECK(error == 0); // Should run successfully

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 5: Invalid expression name - should fail
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_InvalidExpression_fails)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL

RULE 1
IF TankLevel < 130
THEN LINK 9 SETTING = NonExistentExpression
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should fail because NonExistentExpression is not defined
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK(error != 0); // Should fail to parse

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

// Test 6: Expression used for STATUS instead of SETTING - should fail
BOOST_AUTO_TEST_CASE(ExpressionAsSetting_ExpressionForStatus_fails)
{
    std::string rules = R"([RULES]
VARIABLE TankLevel = NODE 2 LEVEL
EXPRESSION StatusExpression = 1

RULE 1
IF TankLevel < 130
THEN LINK 9 STATUS = StatusExpression
)";

    std::string temp_inp = create_temp_inp_with_rules(rules);

    // This should fail because expressions can only be used for SETTING, not STATUS
    int error;
    EN_Project ph = NULL;
    error = EN_createproject(&ph);
    BOOST_REQUIRE(error == 0);

    error = EN_open(ph, temp_inp.c_str(), "test_expressions_as_settings.rpt", "");
    BOOST_CHECK(error != 0); // Should fail to parse

    EN_deleteproject(ph);

    // Clean up
    std::remove(temp_inp.c_str());
}

BOOST_AUTO_TEST_SUITE_END()