static inline s32
GetCPUCoreCount(void)
{
	s32 CPUCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
	Assert(CPUCoreCount != -1);
	return CPUCoreCount;
}

static void
AddTaskToQueue(task_queue *Queue, task_to_do Procedure, void *Data)
{
	s32 TaskIndex = atomic_fetch_add(&Queue->NextTaskToAddIndex, 1);
	Assert(TaskIndex < Queue->MaxTaskCount);
	Assert(!Queue->Tasks[TaskIndex].Procedure);
	Queue->Tasks[TaskIndex].Procedure = Procedure;
	Queue->Tasks[TaskIndex].Data = Data;
	Queue->TotalTaskCount += 1;
	sem_post(&Queue->Semaphore);
}

static task
GetTaskFromQueue(task_queue *Queue, s32 TaskIndex)
{
	task TaskToDo = Queue->Tasks[TaskIndex];
	Assert(TaskToDo.Procedure);
	ZeroStruct(&Queue->Tasks[TaskIndex]);
	return TaskToDo;
}

static b32
DoTask(task_queue *Queue, thread_id ThreadID)
{
	b32 ThreadShouldSleep = true;

	s32 TaskToDoIndex = Queue->NextTaskToDoIndex;
	if (TaskToDoIndex != Queue->NextTaskToAddIndex)
	{
		s32 NewNextTaskToDoIndex = (TaskToDoIndex + 1) % Queue->MaxTaskCount;
		b32 Exchanged = atomic_compare_exchange_strong(
			&Queue->NextTaskToDoIndex, &TaskToDoIndex, NewNextTaskToDoIndex);
		if (Exchanged)
		{
			task Task = GetTaskFromQueue(Queue, TaskToDoIndex);
			Task.Procedure(ThreadID, Task.Data);
			Queue->CompletedTaskCount += 1;
		}
		ThreadShouldSleep = false;
	}

	return ThreadShouldSleep;
}

static inline b32
SomeTaskToDoExists(task_queue *Queue)
{
	b32 Result = Queue->CompletedTaskCount != Queue->TotalTaskCount;
	return Result;
}

static void *
ThreadProcess(void *Argument)
{
	thread_info *Info = Argument;

	for (;;)
	{
		// NOTE(ariel) A thread should sleep when there doesn't exist work for it to
		// do -- unless it's the main thread.
		b32 ThreadShouldSleep = DoTask(Info->TaskQueue, Info->ID);
		if (ThreadShouldSleep)
		{
			sem_wait(&Info->TaskQueue->Semaphore);
		}
	}

	Assert(!"UNREACHABLE");
}

static void
InitializeThreads(arena *Arena, task_queue *Queue)
{
	s32 Status = 0;

	s32 InitialValue = 0;
	s32 YesShareSemaphoreOnlyBetweenThreads = 0;
	Status = sem_init(&Queue->Semaphore, YesShareSemaphoreOnlyBetweenThreads, InitialValue);
	Assert(Status == 0);

	Assert(Queue->MaxTaskCount > 0);
	Queue->Tasks = PushArrayToArena(Arena, task, Queue->MaxTaskCount);

	// NOTE(ariel) Thread management is sort of like memory management in that
	// it's best -- faster for the machine and easier for the human -- to set
	// it up eagerly ahead of time rather than lazily on demand.
	s32 CPUCoreCount = GetCPUCoreCount();
	Queue->AdditionalThreadCount = CPUCoreCount - 1;
	Queue->ThreadInfo = PushArrayToArena(Arena, thread_info, Queue->AdditionalThreadCount);
	for (s32 Index = 0; Index < Queue->AdditionalThreadCount; Index += 1)
	{
		thread_info *Info = &Queue->ThreadInfo[Index];
		Info->TaskQueue = Queue;
		Info->ID = Index;
		Status = pthread_create(&Info->PthreadLabel, 0, ThreadProcess, Info);
		Status = pthread_detach(Info->PthreadLabel);
		Assert(Status == 0);
	}
}
