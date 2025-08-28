/*
 * test_link_info.c - Test to understand link indexing
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
    Network *net = &pr->network;
    
    printf("Link Information:\n");
    printf("Total links: %d\n\n", net->Nlinks);
    
    // Print first 10 links
    for (int i = 1; i <= net->Nlinks && i <= 10; i++) {
        printf("Index %d: ID='%s', Type=%d, Diam=%.2f\", N1=%d, N2=%d\n",
               i, 
               net->Link[i].ID,
               net->Link[i].Type,
               net->Link[i].Diam,
               net->Link[i].N1,
               net->Link[i].N2);
    }
    
    // Find link with ID "11"
    int idx;
    errcode = EN_getlinkindex(ph, "11", &idx);
    if (errcode == 0) {
        printf("\nLink '11' is at index %d\n", idx);
        printf("  Diameter: %.2f inches\n", net->Link[idx].Diam);
    }
    
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}