#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char * argv[])
{
    int tempPriority, tempPid;
    if(argc < 3)
    {
		printf(0, "Invalid command.\n");
    }
    else
    {
        tempPid = atoi(argv[1]);
		tempPriority = atoi(argv[2]);
		if(tempPriority < 0 || tempPriority > 31) 
	        printf(0, "Priority num error.\n");
		else 
			changePriority(tempPid, tempPriority);
	}
   	return 0;
}
