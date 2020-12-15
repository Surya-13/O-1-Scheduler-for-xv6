#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
	set_flag_1();
	int pid;
	int i;
	for(i=0; i < atoi(argv[1]); i++){
		pid=fork();  // Create new processes.

		if(pid==0){
			// Some garbage calculations to keep the CPU busy.
			int x=0;
			x = i*3.14*1231*100 - x; 
		}	
	}
	set_flag_0();
	exit(0);

}