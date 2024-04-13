// In this file, you should modify the main loop inside launchSimulation and
// use the workload structure (either directly or through the getters and
// setters).

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "simulation.h"
#include "process.h"
#include "utils.h"
#include "computer.h"
#include "schedulingLogic.h"

#define MAX_CHAR_PER_LINE 500


/* --------------------------- struct definitions -------------------------- */

/**
 * The ProcessEvent strcut represent processes events as they are in the input
 * file (CPU or IO). They are represented as a linked list where each event
 * points to the next one.
 */
typedef struct ProcessEvent_t ProcessEvent;
/**
 * The ProcessSimulationInfo struct contains all the input file information
 * and the advancement time of a particular process. The Workload struct
 * contains an array of ProcessSimulationInfo.
 */
typedef struct ProcessSimulationInfo_t ProcessSimulationInfo;

typedef enum
{
    CPU_BURST,
    IO_BURST, // For simplicity, we'll consider that IO bursts are blocking (among themselves)
} ProcessEventType;

struct ProcessEvent_t
{
    ProcessEventType type;
    int time; // Time at which the event occurs. /!\ time relative to the process
    ProcessEvent *nextEvent; // Pointer to the next event in the queue
};

struct ProcessSimulationInfo_t
{
    PCB *pcb;
    int startTime;
    int processDuration; // CPU + IO !
    int advancementTime; // CPU + IO !
    ProcessEvent *nextEvent; // Pointer to the next event after the current one
};

struct Workload_t
{
    ProcessSimulationInfo **processesInfo;
    int nbProcesses;
};

//Debug:
const char* ProcessStateToString(ProcessState state);
void printProcessStates(const Workload *workload);

/* ---------------------------- static functions --------------------------- */

/**
 * Return the index of the process with the given pid in the array of processes
 * inside the workload.
 *
 * @param workload: the workload
 * @param pid: the pid of the process
 *
 * @return the index of the process in the workload
 */
static int getProcessIndex(Workload *workload, int pid);

/**
 * Set the advancement time of the process with the given pid in the workload.
 *
 * @param workload: the workload
 * @param pid: the pid of the process
 * @param advancementTime: the new advancement time
 */
static void setProcessAdvancementTime(Workload *workload, int pid, int advancementTime);

/*
 * Returns true if all processes in the workload have finished
 * (advancementTime == processDuration).
 *
 * @param workload: the workload
 * @return true if all processes have finished, false otherwise
 */
static bool workloadOver(const Workload *workload);

static void addAllProcessesToStats(AllStats *stats, Workload *workload);

/**
 * Compare function used in qsort before the main simulation loop. If you don't
 * use qsort, you can remove this function.
 *
 * @param a, b: pointers to ProcessSimulationInfo to compare
 *
 * @return < 0 if process a is first, > 0 if b is first, = 0 if they start at
 *         the same time
 */
static int compareProcessStartTime(const void *a, const void *b);

static void setProcessState(Workload *workload, int pid, ProcessState state);

static ProcessState getProcessState(Workload *workload, int index);

static ProcessEventType getProcessNextEventType(Workload *workload, int pid);

static PCB *getPCBFromWorkload(Workload *workload, int index);

void handleSimulationEvents(Computer *computer, Workload *workload, int time, ProcessGraph *graph, AllStats *stats);

//static
void advanceNextEvent(Workload *workload, int pid);

static int getNextProcessEventTime(Workload *Workload, Scheduler *scheduler);

static void processArrived(Scheduler *scheduler, Workload *workload, int time, AllStats *stats);

static void advanceProcessTime(int time, int next_time, Workload *workload, Computer *computer, ProcessGraph *graph, AllStats *stats);

int minTime(int time, int next_time);

/* -------------------------- getters and setters -------------------------- */

int getProcessCount(const Workload *workload)
{
    return workload->nbProcesses;
}

int getPIDFromWorkload(Workload *workload, int index)
{
    return workload->processesInfo[index]->pcb->pid;
}

int getProcessStartTime(Workload *workload, int pid)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        if (getPIDFromWorkload(workload, i) == pid)
        {
            return workload->processesInfo[i]->startTime;
        }
    }
    return -1;
}

int getProcessDuration(Workload *workload, int pid)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        if (getPIDFromWorkload(workload, i) == pid)
        {
            return workload->processesInfo[i]->processDuration;
        }
    }
    return -1;
}

int getProcessAdvancementTime(Workload *workload, int pid)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        if (getPIDFromWorkload(workload, i) == pid)
        {
            return workload->processesInfo[i]->advancementTime;
        }
    }
    return -1;
}

int getProcessNextEventTime(Workload *workload, int pid)
{
    int processNextEventTime = getProcessDuration(workload, pid); // relative to the process
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        if (getPIDFromWorkload(workload, i) != pid)
        {
            continue;
        }
        if (workload->processesInfo[i]->nextEvent)
        {
            processNextEventTime = workload->processesInfo[i]->nextEvent->time;
        }
        break;
    }
    return processNextEventTime;
}

int getProcessCurEventTimeLeft(Workload *workload, int pid)
{
    return getProcessNextEventTime(workload, pid)
           - getProcessAdvancementTime(workload, pid);
}

static int getProcessIndex(Workload *workload, int pid)
{
    int processIndex = 0;
    for (; processIndex < workload->nbProcesses; processIndex++)
    {
        if (getPIDFromWorkload(workload, processIndex) != pid)
        {
            continue;
        }
        break;
    }

    return processIndex;
}

static void setProcessAdvancementTime(Workload *workload, int pid, int advancementTime)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        if (getPIDFromWorkload(workload, i) == pid)
        {
            workload->processesInfo[i]->advancementTime = advancementTime;
            return;
        }
    }
}

static void setProcessState(Workload *workload, int pid, ProcessState state)
{
    int index = getProcessIndex(workload, pid);
    workload->processesInfo[index]->pcb->state = state;
}

static ProcessState getProcessState(Workload *workload, int index)
{
    return workload->processesInfo[index]->pcb->state;
}

static ProcessEventType getProcessNextEventType(Workload *workload, int pid)
{
    int index = getProcessIndex(workload, pid);
    if (workload->processesInfo[index]->nextEvent)
    {
        return workload->processesInfo[index]->nextEvent->type;
    }
    return -1; //defualt value indicating there is no nextEvent
}

static PCB *getPCBFromWorkload(Workload *workload, int index)
{
    return workload->processesInfo[index]->pcb;
}

/* -------------------------- init/free functions -------------------------- */

Workload *parseInputFile(const char *fileName)
{
    printVerbose("Parsing input file...\n");
    FILE *file = fopen(fileName, "r");
    if (!file)
    {
        fprintf(stderr, "Error: could not open file %s\n", fileName);
        return NULL;
    }

    Workload *workload = (Workload *) malloc(sizeof(Workload));
    if (!workload)
    {
        fprintf(stderr, "Error: could not allocate memory for workload\n");
        fclose(file);
        return NULL;
    }

    char line[MAX_CHAR_PER_LINE];
    int nbProcesses = 0;
    // 1 line == 1 process
    while (fgets(line, MAX_CHAR_PER_LINE, file))
    {
        if (line[strlen(line) - 1] != '\n')
        {
            fprintf(stderr, "Error: line too long in the input file.\n");
            freeWorkload(workload);
            fclose(file);
            return NULL;
        }
        if (line[0] == '#' || line[0] == '\n')
        {
            continue;
        }
        nbProcesses++;
    }
    
    workload->processesInfo = (ProcessSimulationInfo **) malloc(
            sizeof(ProcessSimulationInfo *) * nbProcesses);
    if (!workload->processesInfo)
    {
        fprintf(stderr, "Error: could not allocate memory for processes info\n");
        freeWorkload(workload);
        fclose(file);
        return NULL;
    }

    workload->nbProcesses = 0;

    rewind(file);
    while (fgets(line, MAX_CHAR_PER_LINE, file)) // Read file line by line
    {
        if (line[0] == '#' || line[0] == '\n')
        {
            continue;
        }

        ProcessSimulationInfo *processInfo = (ProcessSimulationInfo *) malloc(
                sizeof(ProcessSimulationInfo));
        if (!processInfo)
        {
            fprintf(stderr, "Error: could not allocate memory for process info\n");
            freeWorkload(workload);
            fclose(file);
            return NULL;
        }

        processInfo->pcb = (PCB *) malloc(sizeof(PCB));
        if (!processInfo->pcb)
        {
            fprintf(stderr, "Error: could not allocate memory for PCB\n");
            free(processInfo);
            freeWorkload(workload);
            fclose(file);
            return NULL;
        }

        processInfo->pcb->state = READY;

        char *token = strtok(line, ",");
        processInfo->pcb->pid = atoi(token);

        token = strtok(NULL, ",");
        processInfo->startTime = atoi(token);

        token = strtok(NULL, ",");
        processInfo->processDuration = atoi(token);

        token = strtok(NULL, ",");
        processInfo->pcb->priority = atoi(token);

        processInfo->advancementTime = 0;

        token = strtok(NULL, "(");

        ProcessEvent *event = NULL;
        while (strstr(token, ",") || strstr(token, "[")) // Read events
        {
            if (strstr(token, "[")) // first event
            {
                event = (ProcessEvent *) malloc(sizeof(ProcessEvent));
                processInfo->nextEvent = event;
            }
            else
            {
                event->nextEvent = (ProcessEvent *) malloc(sizeof(ProcessEvent));
                event = event->nextEvent;
            }
            if (!event)
            {
                fprintf(stderr, "Error: could not allocate memory for event\n");
                free(processInfo->pcb);
                free(processInfo);
                freeWorkload(workload);
                fclose(file);
                return NULL;
            }

            token = strtok(NULL, ",");
            event->time = atoi(token);

            token = strtok(NULL, ")");

            if (token != NULL)
            {
                if (strstr(token, "CPU"))
                {
                    event->type = CPU_BURST;
                }
                else if (strstr(token, "IO"))
                {
                    event->type = IO_BURST;
                }
                else
                {
                    fprintf(stderr, "Error: Unknown operation type\n");
                }
            }

            event->nextEvent = NULL;
            token = strtok(NULL, "(");
        } // End of events
        workload->processesInfo[workload->nbProcesses] = processInfo;
        workload->nbProcesses++;
    } // End of file

    fclose(file);

    printVerbose("Input file parsed successfully\n");

    return workload;
}

void freeWorkload(Workload *workload)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        ProcessEvent *event = workload->processesInfo[i]->nextEvent;
        while (event)
        {
            ProcessEvent *nextEvent = event->nextEvent;
            free(event);
            event = nextEvent;
        }
        free(workload->processesInfo[i]->pcb);
        free(workload->processesInfo[i]);
    }
    free(workload->processesInfo);
    free(workload);
}


/* ---------------------------- other functions ---------------------------- */

void launchSimulation(Workload *workload, SchedulingAlgorithm **algorithms, int algorithmCount, int cpuCoreCount, ProcessGraph *graph, AllStats *stats)
{
    for (int i = 0; i < getProcessCount(workload); i++)
    {
        addProcessToGraph(graph, getPIDFromWorkload(workload, i));
    }
    setNbProcessesInStats(stats, getProcessCount(workload));

    Scheduler *scheduler = initScheduler(algorithms, algorithmCount, workload->nbProcesses);
    if (!scheduler)
    {
        fprintf(stderr, "Error: could not initialize scheduler\n");
        return;
    }

    CPU *cpu = initCPU(cpuCoreCount);
    if (!cpu)
    {
        fprintf(stderr, "Error: could not initialize CPU\n");
        freeScheduler(scheduler);
        return;
    }

    Disk *disk = initDisk();
    if (!disk)
    {
        fprintf(stderr, "Error: could not initialize disk\n");
        freeCPU(cpu);
        freeScheduler(scheduler);
        return;
    }

    Computer *computer = initComputer(scheduler, cpu, disk);
    if (!computer)
    {
        fprintf(stderr, "Error: could not initialize computer\n");
        freeDisk(disk);
        freeCPU(cpu);
        freeScheduler(scheduler);
        return;
    }

    addAllProcessesToStats(stats, workload);

    int time = 0;
    /* Main loop of the simulation.*/
    while (!workloadOver(workload)) // You probably want to change this condition
    {
        printf("----------in while time: %d---------------\n", time);
        // TODO
        //1. Handle event(s): simulator and the scheduler check if an event is triggered at the current time unit and handle it
        //Ex: if a process arrives in the system, the simulator will call the scheduler to put the process in the ready queue.
        //Ex: event = scheduling events, such as a process needing to move to an upper queue because of aging
        //Ex: event = hardware events, such as the triggering of an interrupt.
        //handleSchedulerEvents(computer, workload, time, stats);
        handleSimulationEvents(computer, workload, time, graph, stats);

        //2. Assign processes to resources: This is the step where the main scheduling decisions are made:
        //choosing what processes to execute next.
        assignProcessesToResources(computer, workload);

        /*//3. Get next event time: Here, the simulator and scheduler will simply check what is the time unit
        //of the next event in order to jump directly to it in the next step.
        int nextProcessEventTime = getNextProcessEventTime(workload, scheduler);
        //int nextSchedulingEventTime = getNextSchedulingEventTime(workload, scheduler);
        int next_time = nextProcessEventTime;//(nextProcessEventTime, nextSchedulingEventTime);
        printf("time=%d    next event time= %d\n", time, next_time);*/

        //4. Advance time to the next event: This is where the progression of time is simulated. The simulator
        //will update the advancement of the processes in the workload and the scheduler will update its
        //timers.
        int next_time=time+1; //tmp
        advanceProcessTime(time, next_time, workload, computer, graph, stats);
        advanceSchedulingTime(time, next_time, computer);

        printStats(stats);
        printGraph(graph);
        printCPUStates(computer->cpu);
        printDiskStates(computer->disk);
        printReadyQueues(computer->scheduler);
        
        //Then the current time can be updated too in order to deal with the next event at the next iteration of the loop.
        if (next_time == - 1 || time == next_time)
            break;
        time = next_time;
        
        if (time > 40)
            break; //to suppress once everything done
    }
    freeComputer(computer);
}

/* ---------------------------- static functions --------------------------- */

static bool workloadOver(const Workload *workload)
{
    //disk idle, cpu idle, readyqueue empty & no more next event
    printProcessStates(workload);
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        //if (workload->processesInfo[i]->advancementTime < workload->processesInfo[i]->processDuration)
        if (getProcessState(workload, i) != TERMINATED)
        {
            return 0;
        }
    }

    return 1;
}

static void addAllProcessesToStats(AllStats *stats, Workload *workload)
{
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        ProcessStats *processStats = (ProcessStats *) malloc(sizeof(ProcessStats));
        if (!processStats)
        {
            fprintf(stderr, "Error: could not allocate memory for process stats\n");
            return;
        }
        processStats->processId = getPIDFromWorkload(workload, i);
        processStats->priority = workload->processesInfo[i]->pcb->priority;
        processStats->arrivalTime = 0;
        processStats->finishTime = 0;
        processStats->turnaroundTime = 0;
        processStats->cpuTime = 0;
        processStats->waitingTime = 0;
        processStats->meanResponseTime = 0;
        // You could want to put this field to -1
        processStats->nbContextSwitches = 0;

        addProcessStats(stats, processStats);
    }
}

static int compareProcessStartTime(const void *a, const void *b)
{
    const ProcessSimulationInfo *infoA = *(const ProcessSimulationInfo **)a;
    const ProcessSimulationInfo *infoB = *(const ProcessSimulationInfo **)b;

    if (infoA->startTime < infoB->startTime)
    {
        return -1;
    }
    else if (infoA->startTime > infoB->startTime)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

//static 
void advanceNextEvent(Workload *workload, int pid)
{
    printf("advance next event----------------------------\n");
    int index = getProcessIndex(workload, pid);
    ProcessEvent *event = workload->processesInfo[index]->nextEvent;
    ProcessEvent *nextEvent = event->nextEvent;
    free(event);
    workload->processesInfo[index]->nextEvent = nextEvent;
}

//get the time of the next process that has an event to be able to advance to it in the simulation
static int getNextProcessEventTime(Workload *workload, Scheduler *scheduler)
{
    //next event: soit il y a plus rien, process arrive, disk fini, IO ..., CPU
    int time = -1;
    for (int i = 0; i < getProcessCount(workload); i++)
    {
        int pid = getPIDFromWorkload(workload, i);
        if (pid != -1)
        {
            int next_time = getProcessNextEventTime(workload, pid) + getProcessStartTime(workload, pid); //need "real" time not of th eprocess
            printf("nextPevent: time=%d    next_time=%d\n", time, next_time);
            time = minTime(time, next_time);
        }
    }
    return time;
}

//Here or in simulation.c
void handleSimulationEvents(Computer *computer, Workload *workload, int time, ProcessGraph *graph, AllStats *stats)
{
    //1. Handle event(s): simulator and the scheduler check if an event is triggered at the current time unit and handle it
    //Ex: if a process arrives in the system, the simulator will call the scheduler to put the process in the ready queue.
    printf("\nbeginning of handleEvents\n");
    processArrived(computer->scheduler, workload, time, stats);

    //Ex: event = scheduling events, such as a process needing to move to an upper queue because of aging

    /*for (int j = 0; j < getProcessCount(workload); j++)
    {
        int pid = getPIDFromWorkload(workload, j);
        if (getProcessStartTime(workload, pid) < time)
        {
            int remainingEventTime = getProcessCurEventTimeLeft(workload, pid);
            if (remainingEventTime <= 0) //or = only
            {
                printf("need to deal with finish op for pid:%d\n", pid);
                
            }
        }
    }*/
    //cpu: switch-in/out
    printf("-------------------------ok1\n");
    for (int i = 0; i < computer->cpu->coreCount; i++)
    {
        if (computer->cpu->cores[i]->pcb) //if there is a process on the core
        {
            //printf("getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid) = %d, getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid) == 0 = %d\n", getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid), getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid) == 0);
            //printf("getProcessNextEventTime(workload, computer->cpu->cores[i]->pcb->pid) = %d, getProcessAdvancementTime(workload, computer->cpu->cores[i]->pcb->pid) = %d\n", getProcessNextEventTime(workload, computer->cpu->cores[i]->pcb->pid), getProcessAdvancementTime(workload, computer->cpu->cores[i]->pcb->pid));
            int pid = computer->cpu->cores[i]->pcb->pid;
            //printf("computer->cpu->cores[i]->switchInTimer = %d  computer->cpu->cores[i]->switchOutTimer=%d  pid=%d\n", computer->cpu->cores[i]->switchInTimer, computer->cpu->cores[i]->switchOutTimer, pid);
            if (computer->cpu->cores[i]->state == SWITCH_IN && computer->cpu->cores[i]->switchInTimer == 0)
            {
                printf("pid %d switch-in finished\n", pid);
                computer->cpu->cores[i]->state = OCCUPIED;
                setProcessState(workload, pid, RUNNING);
            }
            else if (computer->cpu->cores[i]->state == SWITCH_OUT && computer->cpu->cores[i]->switchOutTimer == 0)
            {
                printf("pid %d switch-out finished\n", pid);
                computer->cpu->cores[i]->state = IDLE;
                computer->cpu->cores[i]->pcb = NULL; //release the core
                //might need to free in case we apss a node from the queue
            }
            //else if (computer->cpu->cores[i]->state == OCCUPIED && getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid) == 0) //terminated
            else if (computer->cpu->cores[i]->pcb && getProcessCurEventTimeLeft(workload, computer->cpu->cores[i]->pcb->pid) == 0) //terminated
            {
                //start switch out/terminated
                printf("process %d terminated or begins switch out\n", pid);
                bool terminated = (getProcessAdvancementTime(workload, pid) == getProcessDuration(workload, pid));
                //printf("getProcessAdvancementTime(workload, pid)=%d   getProcessDuration(workload, pid)=%d    terminated=%d", getProcessAdvancementTime(workload, pid), getProcessDuration(workload, pid), getProcessAdvancementTime(workload, pid) == getProcessDuration(workload, pid));
                if (terminated)
                {
                    //terminated: process can "disappear"
                    printf("process %d terminated at time=%d\n", pid, time);
                    setProcessState(workload, pid, TERMINATED);
                    computer->cpu->cores[i]->state = IDLE; //release the core
                    computer->cpu->cores[i]->pcb = NULL; //release the core
                    //might need to free in case we pass a node from the queue

                    //compute the graph and stats for terminated here to avoid repeating it every time step
                    addProcessEventToGraph(graph, pid, time, TERMINATED, NO_CORE);
                    getProcessStats(stats, pid)->finishTime = time;
                    getProcessStats(stats, pid)->turnaroundTime = time - getProcessStats(stats, pid)->arrivalTime; //finish-arrival
                    getProcessStats(stats, pid)->meanResponseTime = (double)getProcessStats(stats, pid)->waitingTime/(getProcessStats(stats, pid)->nbContextSwitches+1);
                }
                else {
                    //start switch out/terminated
                    PCB *pcb = computer->cpu->cores[i]->pcb;
                    if (SWITCH_OUT_DURATION > 0)
                    {
                        printf("process %d starts switch-out\n", pid);
                        computer->cpu->cores[i]->state = SWITCH_OUT;
                        computer->cpu->cores[i]->switchOutTimer = SWITCH_OUT_DURATION; // start timer
                    } else {
                        printf("process %d no switch-out\n", pid);
                        computer->cpu->cores[i]->state = IDLE;
                        computer->cpu->cores[i]->pcb = NULL; //release the core
                        //might need to free in case we pass a node from the queue
                    }
                    //"An I/O operation can start without having to wait for the process that initiated it to be switched out. Therefore, you must put the process on the wait queue directly (at the same time as the context switch starts)"
                    //The list of events will alternate between CPU and I/O events => next event is a IO_BURST
                    addProcessEventToGraph(graph, pid, time, WAITING, NO_CORE);
                    printf("Next event in an IO: need to add to waiting queue\n");
                    handleProcessForDisk(computer->scheduler, pcb);
                    /*
                    //need to deal with next event: * io -> waiting queue
                    //                              * cpu -> readyqueue
                    ProcessEventType type = getProcessNextEventType(workload, pid);
                    if (type == CPU_BURST)
                        addProcessEventToGraph(graph, pid, time, READY, NO_CORE);
                    else if (type == IO_BURST) {
                        addProcessEventToGraph(graph, pid, time, WAITING, NO_CORE);
                        printf("Next event in an IO: need to add to waiting queue");
                        handleProcessForDisk(computer->scheduler, pcb);
                    }*/

                    getProcessStats(stats, pid)->nbContextSwitches += 1;
                }
            }
        }
        if (computer->cpu->cores[i]->state == INTERRUPTED && computer->cpu->cores[i]->interruptTimer == 0)
        {
            printf("interrupt finished on core %d: needs to come back to previous state - interrupt for pid %d\n", i, computer->disk->pcb->pid);
            //the process that was waiting for the IO operation to complete will be put back on the ready queue.
            computer->disk->pcb->state = READY;
            handleProcessForCPU(computer->scheduler, computer->disk->pcb);

            //-------debug
            computer->disk->pcb = NULL;
            // need to come back to previous state in the CPU core
            if (computer->cpu->cores[i]->pcb) { //there was a process on the core
                int pid = computer->cpu->cores[i]->pcb->pid;
                if (computer->cpu->cores[i]->switchInTimer != 0)
                {
                    printf("pid %d switch-in continuing\n", pid);
                    computer->cpu->cores[i]->state = SWITCH_IN;
                }
                else if (computer->cpu->cores[i]->switchOutTimer != 0)
                {
                    printf("pid %d switch-out continuing\n", pid);
                    computer->cpu->cores[i]->state = SWITCH_OUT;
                }
                else
                {
                    printf("pid %d execution continuing\n", pid);
                    computer->cpu->cores[i]->state = OCCUPIED;
                    computer->cpu->cores[i]->pcb->state = RUNNING; //"restart" the process execution
                }
            }
            else {
                computer->cpu->cores[i]->state = IDLE;
            }
        }
    }
    printf("-------------------------ok2\n");
    printCPUStates(computer->cpu);
    printDiskStates(computer->disk);

    //Ex: event = hardware events, such as the triggering of an interrupt.
    if (computer->disk->pcb && getProcessCurEventTimeLeft(workload, computer->disk->pcb->pid) == 0) //terminated -> interrupt
    {
        //start interrupt
        printf("process %d begins interrupt\n", computer->disk->pcb->pid);
        handleInterrupt(computer);
    }
}

void processArrived(Scheduler *scheduler, Workload *workload, int time, AllStats *stats)
{
    for (int i = 0; i < getProcessCount(workload); i++)
    {
        int pid = getPIDFromWorkload(workload, i);
        //"The first event will always be a CPU event (at time 0)"
        if (getProcessStartTime(workload, pid) == time) {
            printf("process %d arrived at time %d\n", pid, time);
            handleProcessForCPU(scheduler, getPCBFromWorkload(workload, i));
            getProcessStats(stats, pid)->arrivalTime = time;
            setProcessState(workload, pid, READY);
        }
    }
}

void advanceProcessTime(int time, int next_time, Workload *workload, Computer *computer, ProcessGraph *graph, AllStats *stats) {
    int delta_time = next_time - time;
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        ProcessState state = getProcessState(workload, i);
        int pid = getPIDFromWorkload(workload, i);
        switch (state)
        {
            case READY:
                //Process ready by default => need to differenciate
                // * READY: process not started: no graph
                // * READY: process in readyQueue: update graph/stats
                // * READY: process in switch-in/switch-out
                // * READY: process interrupted
                if (getProcessStartTime(workload, pid) <= time)
                {
                    if (processInReadyQueues(computer->scheduler, pid))
                        getProcessStats(stats, pid)->waitingTime += delta_time;
                    addProcessEventToGraph(graph, pid, time, state, NO_CORE);
                }
                break;
            case WAITING:
                addProcessEventToGraph(graph, pid, time, state, NO_CORE);
                if (computer->disk->state == DISK_RUNNING_) {
                    printf("add disk running in graph\n");
                    setProcessAdvancementTime(workload, pid, getProcessAdvancementTime(workload, pid) + delta_time);
                    addDiskEventToGraph(graph, pid, time, DISK_RUNNING);
                }
                else if (computer->disk->state == DISK_IDLE_) { //debug
                    printf("add disk idle while interrupt\n");
                    addDiskEventToGraph(graph, pid, time, DISK_IDLE);
                }
               break;
            case RUNNING:
                getProcessStats(stats, pid)->cpuTime += delta_time;
                int core;
                for (core = 0; core < computer->cpu->coreCount; core++)
                    if (computer->cpu->cores[core]->pcb->pid == pid)
                        break;
                addProcessEventToGraph(graph, pid, time, state, core);
                setProcessAdvancementTime(workload, pid, getProcessAdvancementTime(workload, pid) + delta_time);
                break;
            default:
                break;
            //pour terminated je pense que quand on detectera que c'est fini directement on fera le graph et stats sinon on va le faire en boucle a chaque temps avancés
        }
    }

    if (computer->disk->state == DISK_IDLE_) { //debug
        //Issue on submission platform if not present
        addDiskEventToGraph(graph, -1, time, DISK_IDLE);
    }
}

//to deal with time = -1
int minTime(int time, int next_time) {
    if (time < 0)
        return next_time;
    else if (next_time < 0)
        return time;
    else
        return min(time, next_time);
}

//debug:
const char* ProcessStateToString(ProcessState state) {
    switch (state) {
        case READY:
            return "READY";
        case RUNNING:
            return "RUNNING";
        case WAITING:
            return "WAITING";
        case TERMINATED:
            return "TERMINATED";
        default:
            return "NOT A STATE: HUGE ERROR";
    }
}

void printProcessStates(const Workload *workload) {
    for (int i = 0; i < workload->nbProcesses; i++)
    {
        printf("process at index %d, with pid %d, at state %s, ", i, workload->processesInfo[i]->pcb->pid, ProcessStateToString(workload->processesInfo[i]->pcb->state));
        printf("processDuration=%d and advancementTime=%d\n", workload->processesInfo[i]->processDuration, workload->processesInfo[i]->advancementTime);
    }
}