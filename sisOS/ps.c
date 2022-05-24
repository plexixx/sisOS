#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int args,char *argv[])
{
    if(args == 1)
        showProcess(0);
    else if(args == 2) {
        if(argv[0] == 'r')
            showProcess(1);
        if(argv[0] == 's')
            showProcess(2);
    }
    return 0;
}