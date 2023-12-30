#include <dirent.h>

#include "base.h"
#include "memory.h"
#include "arena.h"
#include "rss.h"
#include "str.h"

#if defined(__linux__)
#include "memory_linux.c"
#elif defined(_WIN64)
#include "memory_windows.c"
#else
#error `memory.h` is not implemented on this platform.
#endif
#include "arena.c"
#include "rss.c"
#include "str.c"

static string
LoadFile(arena *Arena, FILE *File)
{
	string Contents = {0};

	fseek(File, 0l, SEEK_END);
	Contents.len = (s32)ftell(File);
	rewind(File);

	Contents.str = PushBytesToArena(Arena, Contents.len);
	ssize Length = fread(Contents.str, Contents.len, sizeof(char), File);
	if(!Length)
	{
		Contents.str = 0;
		Contents.len = 0;
	}

	return Contents;
}

int
main(void)
{
	arena Arena = {0};
	InitializeArena(&Arena);

	DIR *Inputs = opendir("./tests/inputs");
	AssertAlways(Inputs);

	struct dirent *Input = 0;
	while((Input = readdir(Inputs)))
	{
		if(Input->d_type == DT_REG)
		{
			arena_checkpoint Checkpoint = SetArenaCheckpoint(&Arena);

			static char InputFilePath[1024];
			static char OutputFilePath[1024];
			snprintf(InputFilePath, sizeof(InputFilePath), "%s/%s", "./tests/inputs", Input->d_name);
			snprintf(OutputFilePath, sizeof(OutputFilePath), "%s/%s", "./tests/actual_outputs", Input->d_name);

			FILE *InputFile = fopen(InputFilePath, "rb"); AssertAlways(InputFile);
			FILE *OutputFile = fopen(OutputFilePath, "w"); AssertAlways(OutputFile);

			string Source = LoadFile(&Arena, InputFile);
			RSS_Tree *Tree = parse_rss(&Arena, Source);
			RSS_PrintTree(Tree, OutputFile);

			fclose(OutputFile);
			fclose(InputFile);

			RestoreArenaFromCheckpoint(Checkpoint);
		}
	}

	closedir(Inputs);
	return 0;
}
