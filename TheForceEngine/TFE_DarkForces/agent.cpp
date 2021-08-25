#include "agent.h"
#include "util.h"
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <assert.h>

namespace TFE_DarkForces
{
	enum AgentConstants
	{
		MAX_LEVEL_COUNT = 14,
		MAX_AGENT_COUNT = 14,
	};

#pragma pack(push)
#pragma pack(1)
	struct AgentData
	{
		char name[32];
		s32 u20;
		s32 u24;
		u8 difficulty;
		u8 completed[MAX_LEVEL_COUNT];
	};
	struct LevelSaveData
	{
		AgentData agentData;

		// Inventory
		// 32 items * 14 levels = 448 values
		u8 inv[448];

		// Ammo (includes health, shields, energy)
		// 10 items * 14 levels = 140 values (each 4 bytes).
		s32 ammo[140];

		s32 pad;
	};
#pragma pack(pop)

	static AgentData s_agentData[AGENT_COUNT];
	static s32 s_maxLevelIndex;
	static char** s_levelDisplayNames;
	static char** s_levelGamePaths;
	static char** s_levelSrcPaths;
		
	s32 agent_loadData()
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return 0;
		}

		assert(sizeof(AgentData) == 55);
		assert(sizeof(LevelSaveData) == 1067);

		s32 agentReadCount = 0;
		for (s32 i = 0; i < MAX_AGENT_COUNT; i++)
		{
			LevelSaveData saveData;
			if (agent_readConfigData(&file, i, &saveData))
			{
				memcpy(&s_agentData[i], &saveData.agentData, sizeof(AgentData));
				agentReadCount++;
			}
		}

		file.close();
		return agentReadCount;
	}
		
	JBool agent_loadLevelList(const char* fileName)
	{
		FilePath filePath;
		if (!TFE_Paths::getFilePath(fileName, &filePath))
		{
			return JFALSE;
		}
		char* buffer = nullptr;
		u32 len = FileStream::readContents(&filePath, (void**)&buffer);
		TFE_Parser parser;
		parser.init(buffer, len);
		parser.addCommentString("#");

		size_t bufferPos = 0;
		const char* line = parser.readLine(bufferPos);
		s32 count;
		if (sscanf(line, "LEVELS %d", &count) < 1)
		{
			free(buffer);
			return JFALSE;
		}
		else
		{
			s_maxLevelIndex = count;
			if (count)
			{
				s_levelDisplayNames = (char**)malloc(count * sizeof(char*));
				s_levelGamePaths    = (char**)malloc(count * sizeof(char*));
				s_levelSrcPaths     = (char**)malloc(count * sizeof(char*));
			}
		}

		for (s32 i = 0; i < count; i++)
		{
			line = parser.readLine(bufferPos);
			char* displayName = strtok((char*)line, ",");
			char* gamePath    = strtok(nullptr, ", \t\n\r");
			char* srcPath     = strtok(nullptr, ", \t\n\r");

			if (displayName[0] && gamePath[0])
			{
				s_levelDisplayNames[i] = copyAndAllocateString(displayName);
				s_levelGamePaths[i]    = copyAndAllocateString(gamePath);
				s_levelSrcPaths[i]     = nullptr;
			}
		}

		free(buffer);
		return JTRUE;
	}

	JBool agent_readConfigData(FileStream* file, s32 agentId, LevelSaveData* saveData)
	{
		const s32 dataSize = (s32)sizeof(LevelSaveData);

		// Note the extra 5 is from the 5 byte header.
		s32 offset = 5 + agentId * dataSize;
		if (!file->seek(offset))
		{
			return JFALSE;
		}
		if (file->readBuffer(saveData, dataSize) != dataSize)
		{
			return JFALSE;
		}
		return JTRUE;
	}

	// This function differs slightly from Dark Forces in the following ways:
	// 1. First it tries to open the CFG file from PATH_PROGRAM_DATA/DarkPilot.cfg
	// 2. If (1) fails, it will then attempt to copy the file from PATH_SOURCE/DarkPilot.cfg to PATH_PROGRAM_DATA/DarkPilot.cfg
	// 3. Instead of returning a handle, the caller passes in a FileStream pointer to be filled in.
	// This is done so that the original data cannot be corrupted by a potentially buggy Alpha build.
	// Also, later, the TFE based save format will likely change - so importing will be necessary anyway.
	JBool openDarkPilotConfig(FileStream* file)
	{
		assert(file);

		// TFE uses its own local copy of the save game data to avoid corrupting existing data.
		// If this copy does not exist, then copy it.
		char programDataPath[TFE_MAX_PATH];
		char sourcePath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_PROGRAM_DATA, "DARKPILO.CFG", programDataPath);
		if (!FileUtil::exists(programDataPath))
		{
			TFE_Paths::appendPath(PATH_SOURCE_DATA, "DARKPILO.CFG", sourcePath);
			if (FileUtil::exists(sourcePath))
			{
				FileUtil::copyFile(sourcePath, programDataPath);
			}
		}
		// Then try opening the file.
		if (!file->open(programDataPath, FileStream::MODE_READ))
		{
			return JFALSE;
		}
		// Then verify the file.
		u8 buffer[5];
		file->readBuffer(buffer, 5);
		if (buffer[3] == 0x12 && buffer[4] == 0x0e && strncasecmp((char*)buffer, "PCF", 3) == 0)
		{
			return JTRUE;
		}
		// If it is not correct, then close the file and return false.
		file->close();
		return JFALSE;
	}
}  // namespace TFE_DarkForces