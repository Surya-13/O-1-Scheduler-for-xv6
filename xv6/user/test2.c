#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
	int pid;
	for(int i=-20; i<20; i++){
		pid = fork();
		if(pid==0){
			// Change the priority of different processes differently
			nice(i);

			// Consume CPU time
			for(int k=0;k<100;k++){
				for(int j=0;j<100000000;j++){}
			}
		}
	}
	exit(0);
}