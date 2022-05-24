#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char* argv[])
{
    int tempTime, tempPid;
    if (argc < 3)
    {
        printf(0, "Invalid command.\n");
    }
    else
    {
        tempPid = atoi(argv[1]);
        tempTime = atoi(argv[2]);
        if (tempTime < 0)
            printf(0, "tempTime num error.\n");
        else
            changeTime(tempPid, tempTime);
    }
    return 0;
}