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
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_HandleClockInterrupt(); // V2_EJERCICIO_2
void OperatingSystem_MoveToTheBLOCKEDState(); // V2_EJERCICIO_5
int OperatingSystem_ExtractFromBlockedToReady(); // V2_EJERCICIO_6
void OperatingSystem_ReleaseMainMemory(); // V4_EJERCICIO_8

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
// int initialPID=0;
// V1_EJERCICIO_8
// Por defecto es, ahora, la última posición de la tabla de procesos
int initialPID = PROCESSTABLEMAXSIZE - 1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// V1_EJERCICIO_11 
// Array that contains the identifiers of the READY processes
// heapItem readyToRunQueue[PROCESSTABLEMAXSIZE];
// int numberOfReadyToRunProcesses=0;
heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE]; 
int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = { 0, 0}; 
char * queueNames [NUMBEROFQUEUES] = { "USER", "DAEMONS"};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

char DAEMONS_PROGRAMS_FILE[MAXIMUMLENGTH]="teachersDaemons";

// V1_EJERCICIO_10
char * statesNames[5] = {"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// V2_EJERCICIO_4
int numberOfClockInterrupts = 0;

// V2_EJERCICIO_5
// Heap with blocked processes sort by when to wakeup 
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE]; 
int numberOfSleepingProcesses = 0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	OperatingSystem_InitializePartitionTable(); // V4_EJERCICIO_5

	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL) {
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		OperatingSystem_ShowTime(SHUTDOWN); // V2_EJERCICIO_1
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE; i++) {
		processTable[i].busy=0;
	}

	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	// V3_EJERCICIO_1
	ComputerSystem_FillInArrivalTimeQueue();
	OperatingSystem_PrintStatus();

	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();

	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN); // V2_EJERCICIO_1
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);
	}

	// V1_EJERCICIO_15
	// if (numberOfNotTerminatedUserProcesses == 0)
	// V3_EJERCICIO_5
	// Quizá no se crea ningún proceso en el instante 0, pero sí que hay con instante de llegada posterior
	if (numberOfNotTerminatedUserProcesses == 0 && numberOfProgramsInArrivalTimeQueue == 0)
		OperatingSystem_ReadyToShutdown();

	// At least, one user process has been created
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
	int PID, i, numberOfSuccessfullyCreatedProcesses = 0;
	// V3_EJERCICIO_3
	// for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
	int isThereANewProgram = OperatingSystem_IsThereANewProgram();
	while (isThereANewProgram == YES) {
		i = Heap_poll(arrivalTimeQueue, QUEUE_ARRIVAL, &numberOfProgramsInArrivalTimeQueue);
		PID = OperatingSystem_CreateProcess(i);
		if (PID == NOFREEENTRY) {	// V1_EJERCICIO_4	
			OperatingSystem_ShowTime(ERROR); // V2_EJERCICIO_1
			ComputerSystem_DebugMessage(103, ERROR, "ERROR: There are not free entries in the process table for the program", programList[i]->executableName);
		} else if (PID == PROGRAMDOESNOTEXIST) {	// V1_EJERCICIO_5
			OperatingSystem_ShowTime(ERROR); // V2_EJERCICIO_1
			ComputerSystem_DebugMessage(104, ERROR, "ERROR: Program", programList[i]->executableName, "is not valid", "it does not exist");
		} else if (PID == PROGRAMNOTVALID) {	// V1_EJERCICIO_5
			OperatingSystem_ShowTime(ERROR); // V2_EJERCICIO_1
			ComputerSystem_DebugMessage(104, ERROR, "ERROR: Program", programList[i]->executableName, "is not valid", "invalid priority or size");
		} else if (PID == TOOBIGPROCESS) {	// V1_EJERCICIO_6
			OperatingSystem_ShowTime(ERROR); // V2_EJERCICIO_1
			ComputerSystem_DebugMessage(105, ERROR, "ERROR: Program", programList[i]->executableName, "is too big");
		} else if (PID == MEMORYFULL) {	// V4_EJERCICIO_6
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(144, ERROR, "ERROR: A process could not be created from program", programList[i]->executableName, "because an appropriate partition is not available");
		} else {
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
		isThereANewProgram = OperatingSystem_IsThereANewProgram();
	}

	// V2_EJERCICIO_7
	if (numberOfSuccessfullyCreatedProcesses > 0)
		OperatingSystem_PrintStatus();

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
	// V1_EJERCICIO_4
	if (PID == NOFREEENTRY)
		return NOFREEENTRY;

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");
	// V1_EJERCICIO_5
	if (programFile == NULL)
		return PROGRAMDOESNOTEXIST;

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);	

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	
	// V1_EJERCICIO_5
	if (processSize == PROGRAMNOTVALID || priority == PROGRAMNOTVALID)
		return PROGRAMNOTVALID;
	
	// V4_EJERCICIO_6
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142, SYSMEM, "Process", PID, executableProgram->executableName, "requests", processSize, "memory positions");
	// Obtain enough memory space
 	loadingPhysicalAddress = OperatingSystem_ObtainMainMemory(processSize, PID);
	// V1_EJERCICIO_6
	if (loadingPhysicalAddress == TOOBIGPROCESS)
		return TOOBIGPROCESS;
	else if (loadingPhysicalAddress == MEMORYFULL)
		return MEMORYFULL;
	OperatingSystem_ShowPartitionTable("before allocating memory"); // V4_EJERCICIO_7
	partitionsTable[loadingPhysicalAddress].PID = PID;
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(143, SYSMEM, "Partition", loadingPhysicalAddress, partitionsTable[loadingPhysicalAddress].initAddress, partitionsTable[loadingPhysicalAddress].size, "has been assigned to process", PID, executableProgram->executableName);
		
	// Load program in the allocated memory
	int programLoad = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	// V1_EJERCICIO_7
	if (programLoad == TOOBIGPROCESS)
		return TOOBIGPROCESS;

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	OperatingSystem_ShowPartitionTable("after allocating memory"); // V4_EJERCICIO_7

	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT); // V2_EJERCICIO_1
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);
	
	return PID;
}

// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {
	if (processSize > MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	// return PID*MAINMEMORYSECTIONSIZE;
	// V4_EJERCICIO_6
	// Política del mejor ajuste
	int i, puntero = MEMORYFULL;
	for (i = 0; i < PARTITIONTABLEMAXSIZE && partitionsTable[i].initAddress >= 0; i++) {
		if (partitionsTable[i].PID == NOPROCESS && partitionsTable[i].size >= processSize) {
			if (puntero == MEMORYFULL || partitionsTable[i].size < partitionsTable[puntero].size)
				puntero = i;
		}
	}
	return puntero;
}

// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {
	processTable[PID].busy = 1;
	processTable[PID].initialPhysicalAddress = initialPhysicalAddress;
	processTable[PID].processSize = processSize;
	OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
	// V1_EJERCICIO_10
	ComputerSystem_DebugMessage(111, SYSPROC, "New process", PID, programList[processPLIndex]->executableName,
		"moving to the", statesNames[0], "state");
	processTable[PID].state = NEW;
	processTable[PID].priority = priority;
	processTable[PID].programListIndex = processPLIndex;
	// V1_EJERCICIO_13
	processTable[PID].copyOfAccumulatorRegister = 0;
	// V2_EJERCICIO_5
	processTable[PID].whenToWakeUp = 0;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister = initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister = ((unsigned int) 1) << EXECUTION_MODE_BIT;
		processTable[PID].queueID = 1; // V1_EJERCICIO_11
	} 
	else {
		processTable[PID].copyOfPCRegister = 0;
		processTable[PID].copyOfPSWRegister = 0;
		processTable[PID].queueID = 0; // V1_EJERCICIO_11
	}
}

// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	// V1_EJERCICIO_11
	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[processTable[PID].queueID], PROCESSTABLEMAXSIZE) >= 0) {
		OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
		// V1_EJERCICIO_10
		ComputerSystem_DebugMessage(110, SYSPROC, "Process", PID, programList[processTable[PID].programListIndex]->executableName,
			"moving from the", statesNames[processTable[PID].state], "state to the", statesNames[1], "state");
		processTable[PID].state = READY;
	}
	// V1_EJERCICIO_9 ; V2_EJERCICIO_8
	// OperatingSystem_PrintReadyToRunQueue();
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {	
	int selectedProcess;
	selectedProcess = OperatingSystem_ExtractFromReadyToRun();	
	return selectedProcess;
}

// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun() {  
	int selectedProcess = NOPROCESS;

	// V1_EJERCICIO_11, tienen mayor prioridad los procesos de usuario
	if (numberOfReadyToRunProcesses[0] == 0)
		selectedProcess = Heap_poll(readyToRunQueue[1], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[1]);
	else
		selectedProcess = Heap_poll(readyToRunQueue[0], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[0]);
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {
	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
	// V1_EJERCICIO_10
	ComputerSystem_DebugMessage(110, SYSPROC, "Process", PID, programList[processTable[PID].programListIndex]->executableName,
		"moving from the", statesNames[processTable[PID].state], "state to the", statesNames[2], "state");
	processTable[PID].state = EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}

// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) { 
	// V1_EJERCICIO_13
	Processor_SetAccumulator(processTable[PID].copyOfAccumulatorRegister);

	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1, processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2, processTable[PID].copyOfPSWRegister);
	
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
	executingProcessID = NOPROCESS;
}

// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	// V1_EJERCICIO_13
	processTable[PID].copyOfAccumulatorRegister = Processor_GetAccumulator();

	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
}

// Exception management routine
void OperatingSystem_HandleException() {	
	// OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1 
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	// ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);	
	// V4_EJERCICIO_2
	OperatingSystem_ShowTime(INTERRUPT);
	switch (Processor_GetRegisterB()) {	// obtiene el tipo de excepcion
		case DIVISIONBYZERO:
		    ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, "division by zero");
			break;
		case INVALIDPROCESSORMODE:
		    ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, "invalid processor mode");
			break;
		case INVALIDADDRESS:
		    ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, "invalid address");
			break;		
		case INVALIDINSTRUCTION:	// V4_EJERCICIO_3
		    ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, "invalid instruction");
			break;
	}

	OperatingSystem_TerminateProcess();

	// V2_EJERCICIO_7
	OperatingSystem_PrintStatus();
}

// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {  
	int selectedProcess;
  	
	OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
	// V1_EJERCICIO_10
	ComputerSystem_DebugMessage(110, SYSPROC, "Process", executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName,
		"moving from the", statesNames[processTable[executingProcessID].state], "state to the", statesNames[4], "state");
	processTable[executingProcessID].state = EXIT;

	OperatingSystem_ReleaseMainMemory(); // V4_EJERCICIO_8

	// One more user process that has terminated
	if (programList[processTable[executingProcessID].programListIndex]->type == USERPROGRAM)
		numberOfNotTerminatedUserProcesses--;
	
	// if (numberOfNotTerminatedUserProcesses == 0) {
	// V3_EJERCICIO_5
	if (numberOfNotTerminatedUserProcesses == 0  && numberOfProgramsInArrivalTimeQueue == 0) {
		if (executingProcessID == sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN); // V2_EJERCICIO_1
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
	int systemCallID;

	// V1_EJERCICIO_12
	int previousPID, cola, selectedProcess;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;
		
		// V1_EJERCICIO_12
		case SYSCALL_YIELD:
			previousPID = executingProcessID;
			cola = processTable[previousPID].queueID;
			// Compruebo si el proceso mas prioritario de su cola de listos tiene su misma prioridad
			if (processTable[previousPID].priority == processTable[readyToRunQueue[cola][0].info].priority) {
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE); // V2_EJERCICIO_1
				ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, "Process", previousPID, 
					programList[processTable[previousPID].programListIndex]->executableName,
					"will transfer the control of the processor to process", readyToRunQueue[cola][0].info,
					programList[processTable[readyToRunQueue[cola][0].info].programListIndex]->executableName);
				OperatingSystem_PreemptRunningProcess();
				selectedProcess = OperatingSystem_ShortTermScheduler();
				OperatingSystem_Dispatch(selectedProcess);	
				// V2_EJERCICIO_7
				OperatingSystem_PrintStatus();		
			}
			break;

		case SYSCALL_END:
			OperatingSystem_ShowTime(SYSPROC); // V2_EJERCICIO_1
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			// V2_EJERCICIO_7
			OperatingSystem_PrintStatus();
			break;
		
		// V2_EJERCICIO_5
		case SYSCALL_SLEEP:
			OperatingSystem_MoveToTheBLOCKEDState();
			selectedProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_Dispatch(selectedProcess);
			OperatingSystem_PrintStatus();
			break;
		
		// V4_EJERCICIO_4
		default:
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, systemCallID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint) {
	switch (entryPoint) {
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT=9, V2_EJERCICIO_2
			OperatingSystem_HandleClockInterrupt();
			break;
	}
}

// V1_EJERCICIO_9 (creado) y V1_EJERCICIO_11 (modificado)
// Muestra en pantalla el contenido de la cola de procesos listos
void OperatingSystem_PrintReadyToRunQueue() {
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE); // V2_EJERCICIO_1
	ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE, "Ready-to-run processes queues:");
	int i, j;
	for (i=0; i<NUMBEROFQUEUES; i++) {
		ComputerSystem_DebugMessage(113, SHORTTERMSCHEDULE, queueNames[i]);
		for (j=0; j<numberOfReadyToRunProcesses[i]; j++) {
			// Muestra el PID y la prioridad de cada proceso
			ComputerSystem_DebugMessage(114, SHORTTERMSCHEDULE, readyToRunQueue[i][j].info, processTable[readyToRunQueue[i][j].info].priority);
			if (j+1 != numberOfReadyToRunProcesses[i])
				ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, ",");				
		}
		ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, "\n");
	}
}

// V2_EJERCICIO_2
void OperatingSystem_HandleClockInterrupt() {
	// V2_EJERCICIO_4
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120, INTERRUPT, "Clock interrupt number", numberOfClockInterrupts, "has occurred");
	
	// V2_EJERCICIO_6
	int selectedProcess = NOPROCESS;
	int numberOfWokenUpProcesses = 0;
	while (numberOfSleepingProcesses > 0 && processTable[sleepingProcessesQueue[0].info].whenToWakeUp == numberOfClockInterrupts) {
		selectedProcess = OperatingSystem_ExtractFromBlockedToReady();
		OperatingSystem_MoveToTheREADYState(selectedProcess);
		numberOfWokenUpProcesses++;
	}

	// V3_EJERCICIO_4
	int numberOfSuccessfullyCreatedProcesses = OperatingSystem_LongTermScheduler();

	if (numberOfWokenUpProcesses > 0 || numberOfSuccessfullyCreatedProcesses > 0) {
		// if (numberOfWokenUpProcesses > 0) // El LTS ya hizo esta llamada
		OperatingSystem_PrintStatus();
		int firstProcess = Heap_getFirst(readyToRunQueue[0], numberOfReadyToRunProcesses[0]);
		// ¿El proceso en ejecución sigue siendo el más prioritario?
		// El más prioritario posee un valor menor en la prioridad
		// Procesos de usuario siguen siendo más prioritarios que los daemons
		if (processTable[executingProcessID].priority > processTable[firstProcess].priority ||
			processTable[executingProcessID].queueID == 1) {
			// Sustituyo al proceso en ejecución por el más prioritario
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(121, SHORTTERMSCHEDULE, "Process", executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName,
				"will be thrown out of the processor by process", firstProcess, programList[processTable[firstProcess].programListIndex]->executableName);
			OperatingSystem_PreemptRunningProcess();
			selectedProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_Dispatch(selectedProcess);
			OperatingSystem_PrintStatus();
		}
	}

	// V3_EJERCICIO_5
	if (numberOfNotTerminatedUserProcesses == 0 && numberOfProgramsInArrivalTimeQueue == 0)
		OperatingSystem_ReadyToShutdown();

	return;
}

// V2_EJERCICIO_5
void OperatingSystem_MoveToTheBLOCKEDState() {	
	processTable[executingProcessID].whenToWakeUp = abs(Processor_GetAccumulator()) + numberOfClockInterrupts + 1;
	if (Heap_add(executingProcessID, sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses, PROCESSTABLEMAXSIZE) >= 0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110, SYSPROC, "Process", executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName,
			"moving from the", statesNames[processTable[executingProcessID].state], "state to the", statesNames[3], "state");
		processTable[executingProcessID].state = BLOCKED;
	}
	OperatingSystem_SaveContext(executingProcessID);
	executingProcessID = NOPROCESS;
}

// V2_EJERCICIO_6
int OperatingSystem_ExtractFromBlockedToReady() { 
	int process = NOPROCESS;
	if (numberOfSleepingProcesses > 0)
		process = Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
	return process;
}

// V3_EJERCICIO_2
int OperatingSystem_GetExecutingProcessID() {
	return executingProcessID;
}

// V4_EJERCICIO_8
void OperatingSystem_ReleaseMainMemory() {
	OperatingSystem_ShowPartitionTable("before releasing memory");
	int i;
	for (i = 0; i < PARTITIONTABLEMAXSIZE && partitionsTable[i].initAddress >= 0; i++) {
		if (executingProcessID == partitionsTable[i].PID) {
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(145, SYSMEM, "Partition", i, partitionsTable[i].initAddress, partitionsTable[i].size, "used by process", executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, "has been released");
			partitionsTable[i].PID = NOPROCESS;
		}
	}
	OperatingSystem_ShowPartitionTable("after releasing memory");
}
