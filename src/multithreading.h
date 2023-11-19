#ifndef MULTITHREADING_H
#define MULTITHREADING_H

typedef s32 thread_id;
typedef void (*task_to_do)(thread_id, void *Data);

typedef struct task task;
struct task
{
	task_to_do Procedure;
	void *Data;
};

// NOTE(ariel) Forward declare `thread_info` to access it from `task_queue`.
typedef struct thread_info thread_info;

// NOTE(ariel) Model single producer, multiple consumer.
typedef struct task_queue task_queue;
struct task_queue
{
	sem_t Semaphore;

	_Atomic s32 CompletedTaskCount;
	_Atomic s32 TotalTaskCount;
	_Atomic s32 NextTaskToDoIndex;
	_Atomic s32 NextTaskToAddIndex;

	task *Tasks;
	s32 MaxTaskCount;

	thread_info *ThreadInfo;
	s32 AdditionalThreadCount;
};

typedef struct thread_info thread_info;
struct thread_info
{
	// NOTE(ariel) Each thread must maintain access to these two fields (only the
	// second strictly) in any program.
	s32 ID;
	pthread_t PthreadLabel;
	task_queue *TaskQueue;

	// NOTE(ariel) Each thread may also maintain access to custom fields for each
	// individual program.
	CURL *CurlHandle;
	Arena ScratchArena;
	Arena PersistentArena;
};

// NOTE(ariel) To maintain thread safety, only the main thread should call
// AddTaskToQueue() and CompleteTasks().
static void AddTaskToQueue(task_queue *Queue, task_to_do Procedure, void *Data);

#endif
