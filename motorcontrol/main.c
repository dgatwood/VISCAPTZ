#include <stdio.h>      //printf()
#include <stdlib.h>     //exit()
#include <signal.h>
#include "DEV_Config.h"
#include "MotorDriver.h"

void  Handler(int signo)
{
    //System Exit
    printf("\r\nHandler:Motor Stop\r\n");
    Motor_Stop(MOTORA);
    Motor_Stop(MOTORB);
    DEV_ModuleExit();

    exit(0);
}

int main(void)
{
    //1.System Initialization
    if(DEV_ModuleInit())
        exit(0);
    
    //2.Motor Initialization
    Motor_Init();

    printf("Motor_Run\r\n");
    Motor_Run(MOTORA, FORWARD, 100);
    Motor_Run(MOTORB, BACKWARD, 100);

    // Exception handling:ctrl + c
    signal(SIGINT, Handler);
    while(1) {

    }

    //3.System Exit
    DEV_ModuleExit();
    return 0;
}

