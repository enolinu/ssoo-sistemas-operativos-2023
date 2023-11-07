#include "Simulator.h"
#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateExecutingProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();

// The process table
// PCB processTable[PROCESSTABLEMAXSIZE];
PCB * processTable;

// Address base for OS code in this version
int OS_address_base; // = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation (Not assigned)
int initialPID=-1;

// Begin indes for daemons in programList
// int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
heapItem *readyToRunQueue[NUMBEROFQUEUES];
// int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// char DAEMONS_PROGRAMS_FILE[MAXFILENAMELENGTH]="teachersDaemons";

int MAINMEMORYSECTIONSIZE = 60;

extern int MAINMEMORYSIZE;

int PROCESSTABLEMAXSIZE = 4;

// Estados posibles de los procesos.
char * statesNames [5] = {"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// Vector con el número de procesos listos para ejecutar en cada cola.
int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0,0};

// Hay dos colas.
char * queueNames [NUMBEROFQUEUES] = {"USER","DAEMONS"}; 

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int programsFromFileIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	
// In this version, with original configuration of memory size (300) and number of processes (4)
// every process occupies a 60 positions main memory chunk 
// and OS code and the system stack occupies 60 positions 

	MAINMEMORYSECTIONSIZE =(MAINMEMORYSIZE / (PROCESSTABLEMAXSIZE+1));
	OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

	if (initialPID<0) // if not assigned in options...
		initialPID=PROCESSTABLEMAXSIZE-1; 
	
	// Space for the processTable
	processTable = (PCB *) malloc(PROCESSTABLEMAXSIZE*sizeof(PCB));
	
	// Space for the ready to run queues (one queue initially...)
	for(int i=0; i<NUMBEROFQUEUES; i++) {
		readyToRunQueue[i] = (heapItem *) malloc(PROCESSTABLEMAXSIZE*sizeof(heapItem));
	}

	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL){
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list all user or system daemon processes
	OperatingSystem_PrepareDaemons(programsFromFileIndex);
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")
		&& processTable[sipID].state==READY) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}

	//Ej 14.
	/*
	if() {

	}
	*/

	// At least, one process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;

	
	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
		
		PID=OperatingSystem_CreateProcess(i);

		// If CreateProccess returns an error, we cant create the proccess.
		if(PID == NOFREEENTRY) {
			ComputerSystem_DebugMessage(103, ERROR, programList[i] -> executableName);
		}

		else if(PID == PROGRAMDOESNOTEXIST) {
			ComputerSystem_DebugMessage(104, ERROR, programList[i] -> executableName, "it does not exist");
		}

		else if(PID == PROGRAMNOTVALID) {
			ComputerSystem_DebugMessage(104, ERROR, programList[i] -> executableName, "invalid priority or size");
		}

		else if(PID == TOOBIGPROCESS) {
			ComputerSystem_DebugMessage(105, ERROR, programList[i] -> executableName);
		}

		else {
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}

		
	}

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();

	// If there is no enough space in the PCB, we delegate the error to the LTS.
	if(PID == NOFREEENTRY) {
		return NOFREEENTRY;
	}

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");

	// If the program was not found, we delegate the error to the LTS.
	if (programFile == NULL) {
		return PROGRAMDOESNOTEXIST;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);

	// If the program dont have a valid size, we delegate the error to the LTS.
	if (processSize == 0) {
		return PROGRAMNOTVALID;
	}	

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);

	if (PID == PROGRAMNOTVALID) {
		return PROGRAMNOTVALID;
	}
	
	// Obtain enough memory space
 	loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);

	// Delegate to LTS if the process is too big.
	if(loadingPhysicalAddress == TOOBIGPROCESS) {
		return TOOBIGPROCESS;
	}

	// Load program in the allocated memory only if OperatingSystem_LoadProgram (size == number of inst).
	if(OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize) == TOOBIGPROCESS) {
		return TOOBIGPROCESS;
	}
	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	// Show message "Process [PID] created from program [executableName]\n"
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].copyOfSPRegister=initialPhysicalAddress+processSize;
	processTable[PID].state=NEW;
	processTable[PID].copyOfAccumulator=0;

	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
	}

	// Guardamos en el campo queueId de la posición PID de la tabla de procesos el tipo de programa.
	// Accedemos al programa en la lista de programas y extraemos el tipo ("->" porque es un array de punteros).

	processTable[PID].queueID = programList[processPLIndex] -> type;

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	
	if (Heap_add(PID, readyToRunQueue[0],QUEUE_PRIORITY ,&(numberOfReadyToRunProcesses[0]) ,PROCESSTABLEMAXSIZE)>=0) {

		OperatingSystem_StateChangeAndPrint(PID, READY, "NEW", "READY");

	} 

	OperatingSystem_PrintReadyToRunQueue();
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess = NOPROCESS;

	// Buscamos un proceso en la cola "usuario"
	selectedProcess=OperatingSystem_ExtractFromReadyToRun(USERPROCESSQUEUE);

	// Si no encuentra ninguno en esa cola, lo busca en la cola "demonio".
	if(selectedProcess == NOPROCESS) {
		selectedProcess=OperatingSystem_ExtractFromReadyToRun(DAEMONSQUEUE);
	}
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queue) {
  
	int selectedProcess=NOPROCESS;

	selectedProcess=Heap_poll(readyToRunQueue[queue],QUEUE_PRIORITY ,&(numberOfReadyToRunProcesses[queue]));
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	OperatingSystem_StateChangeAndPrint(PID, EXECUTING, "READY", "EXECUTING");
	processTable[PID].state=EXECUTING;
	
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	Processor_SetRegisterSP(processTable[PID].copyOfSPRegister);

	// Rescatamos el valor del acumulador guardado en el PCB del proceso.
	Processor_SetAccumulator(processTable[PID].copyOfSPRegister);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	
	processTable[PID].copyOfSPRegister=Processor_GetRegisterSP();

	// Copiamos el acumulador, para que no afecte su modificación por parte de otros procesos.
	processTable[PID].copyOfAccumulator = Processor_GetAccumulator();

}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	
	OperatingSystem_TerminateExecutingProcess();
}

// All tasks regarding the removal of the executing process
void OperatingSystem_TerminateExecutingProcess() {

	int PID = executingProcessID;
	OperatingSystem_StateChangeAndPrint(PID, EXIT, "EXECUTING", "EXIT");
	//processTable[executingProcessID].state=EXIT;
	
	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0) {
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			Processor_CopyInSystemStack(MAINMEMORYSIZE-1,OS_address_base+1);
			executingProcessID=NOPROCESS;
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
	}

	// Select the next process to execute (sipID if no more user processes)
	int selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,Processor_GetRegisterA(),Processor_GetRegisterB());
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateExecutingProcess();
			break;
		case SYSCALL_YIELD:
			// TO DO
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;

	}

}

// Funciones propias.

// E9 => Imprime el PID y la prioridad de las colas de procesos listos.
void OperatingSystem_PrintReadyToRunQueue() {

	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE, "Ready-to-run process queue (USER):");

	for(int i=0; i<numberOfReadyToRunProcesses[0]; i++) {

		// Buscamos el pid en la cola de procesos listos (cola para procesos de tipo "USER").
		int pid = readyToRunQueue[0][i].info;

		// Lo imprimimos junto a su prioridad
		ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, pid, processTable[pid].priority);
	}

	ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE);

	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE, "Ready-to-run process queue (DAEMON):");

	for(int i=0; i<numberOfReadyToRunProcesses[1]; i++) {

		// Buscamos el pid en la cola de procesos listos (cola para procesos de tipo "DAEMON").
		int pid = readyToRunQueue[1][i].info;

		// Lo imprimimos junto a su prioridad
		ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, pid, processTable[pid].priority);
	}

	// Salto de linea
	ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE);

}

// E10
void OperatingSystem_StateChangeAndPrint(int PID, int newState, char *oldStName, char *newStName) {

	// PCBInitialization (NEW)
	// MoveToTheReadyState
	// Dispatch (ready => executing)
	// TerminateProcess (executing => exit)

	// Guardamos los nombres de los estados a imprimir
	int oldState = statesNames[processTable[PID].state];

	// Imprimimos el mensaje.
	ComputerSystem_DebugMessage(110, SYSPROC, PID, programList[processTable[PID].programListIndex] -> executableName, 
								oldStName, newStName);

	// Cambiamos el estado.
	processTable[PID].state = newState;

}
