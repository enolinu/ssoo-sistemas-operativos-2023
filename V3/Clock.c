#include "Clock.h"
#include "Processor.h"
#include "ComputerSystemBase.h"

int tics=0;

void Clock_Update() {
	tics++;
    // ComputerSystem_DebugMessage(97,CLOCK,tics); // Coment in V2-StudentsCode
	if(tics % intervalBetweenInterrupts == 0) {
		Processor_RaiseInterrupt(CLOCK_BIT);
	}
}


int Clock_GetTime() {

	return tics;
}
