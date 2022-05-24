#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char* argv[])
{
    int tempSche;
    if (argc < 2)
    {
        printf(0, "Invalid command.\n");
    }
    else
    {
        tempSche = atoi(argv[1]);
        if (tempSche < 0 || tempSche > 2)
            printf(0, "sche_method num error.\n");
        else
            changeSche(tempSche);
    }
    return 0;
}
