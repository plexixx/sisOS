#include "types.h"
#include "stat.h"
#include "user.h"

char buf[512];

void
fmd(int fd, int d_mode)
{
  struct mode md;

  if(fmode(fd, &md) < 0){ 
    printf(2, "fmd: cannot grasp the access permission\n");
    close(fd);
    return;
  }
  if (!d_mode) { // only display permission msg
  	if(md.readable == 1)
  		printf(1,"r");
  	else
  		printf(1,"-");
  	if(md.writable == 1)
	    printf(1,"w\n");
	else
	   	printf(1,"-\n");	
  }
  else { // modify
  	fmodif(fd, d_mode);
  }
}

int
main(int argc, char *argv[])
{
  int fd;

  if(argc < 1){
    printf(1, "fmd: lack of filename to check state\n");
    exit();
  }
  
  if((fd = open(argv[1], 0)) < 0){
    printf(1, "fmd: cannot open %s\n", argv[1]);
    exit();
  }
  int pms = atoi(argv[2]);

  if (!pms) // display permission msg
  {
  	fmd(fd,0);
  } 
  else //modify permission msg
  {
  	printf(1,"set mode:%d\n",atoi(argv[2]));
	fmd(fd,atoi(argv[2]));
  }
  
  close(fd);
  exit();
  
  return 0;
}
