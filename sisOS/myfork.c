#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char* argv[])
{
	int pid = fork();
   	if (pid < 0) 
      	printf(0, "fork error\n");
   	else if (pid == 0)//Child
   	{
   		printf(1, "Child %d is created!\n", getpid());
   		for (int a = 0; a < 100000000000; a++)
   		{
        	for (int b = 0; b < 100000000; b++)
        	{
                   a*=b;
      		}
   		}
      	exit();
  	}
    wait();
    return 0;
}