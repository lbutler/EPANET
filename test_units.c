/*
 * test_units.c - Test to understand unit conversions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/epanet2.h"
#include "include/epanet2_2.h"
#include "src/types.h"

int main(int argc, char *argv[])
{
    EN_Project ph;
    int errcode;
    double value;
    
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    
    errcode = EN_createproject(&ph);
    if (errcode) return errcode;
    
    errcode = EN_open(ph, argv[1], "", "");
    if (errcode) {
        EN_deleteproject(ph);
        return errcode;
    }
    
    Project *pr = (Project *)ph;
    
    // Check unit conversion factors
    printf("Unit Conversion Factors:\n");
    printf("Ucf[0-10]: ");
    for (int i = 0; i < 10; i++) {
        printf("%.3f ", pr->Ucf[i]);
    }
    printf("\n");
    
    // Get link 11's diameter through API
    int idx;
    errcode = EN_getlinkindex(ph, "11", &idx);
    if (errcode == 0) {
        EN_getlinkvalue(ph, idx, EN_DIAMETER, &value);
        printf("\nLink '11' diameter via API: %.2f\n", value);
        
        // Direct access
        printf("Link '11' diameter direct: %.2f\n", pr->network.Link[idx].Diam);
        
        // What it should be
        printf("Link '11' diameter expected: 14.00 inches\n");
    }
    
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}