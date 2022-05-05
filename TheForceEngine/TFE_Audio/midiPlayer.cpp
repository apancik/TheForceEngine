#include "midiPlayer.h"
#include "midiDevice.h"
#include "audioDevice.h"
#include <TFE_Asset/gmidAsset.h>
#include <TFE_System/system.h>
#include <TFE_System/Threads/thread.h>
#include <TFE_Settings/settings.h>
#include <TFE_FrontEndUI/console.h>
#include <algorithm>
#include <assert.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#undef min
#undef max
#endif

namespace TFE_MidiPlayer
{
	enum MidiPlayerCmd
	{
		MIDI_PAUSE,
		MIDI_RESUME,
		MIDI_CHANGE_VOL,
		MIDI_STOP_NOTES,
		MIDI_COUNT
	};

	struct MidiCmd
	{
		MidiPlayerCmd cmd;
		f32 newVolume;
	};

	enum { MAX_MIDI_CMD = 256 };
	static MidiCmd s_midiCmdBuffer[MAX_MIDI_CMD];
	static u32 s_midiCmdCount = 0;

	struct MidiCallback
	{
		void(*callback)(void) = nullptr;	// callback function to call.
		f64 timeStep = 0.0;					// delay between calls, this acts like an interrupt handler.
		f64 accumulator = 0.0;				// current accumulator.
	};

	static const f32 c_musicVolumeScale = 0.75f;
	static f32 s_masterVolume = 1.0f;
	static f32 s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
	static Thread* s_thread = nullptr;

	static atomic_bool s_runMusicThread;
	static u8 s_channelSrcVolume[16] = { 0 };
	static Mutex s_mutex;

	static MidiCallback s_midiCallback = {};

	TFE_THREADRET midiUpdateFunc(void* userData);
	void stopAllNotes();
	void changeVolume();

	// Console Functions
	void setMusicVolumeConsole(const ConsoleArgList& args);
	void getMusicVolumeConsole(const ConsoleArgList& args);

	bool init()
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_MidiPlayer::init");

		bool res = TFE_MidiDevice::init();
		TFE_MidiDevice::selectDevice(0);
		s_runMusicThread.store(true);

		MUTEX_INITIALIZE(&s_mutex);

		s_thread = Thread::create("MidiThread", midiUpdateFunc, nullptr);
		if (s_thread)
		{
			s_thread->run();
		}

		CCMD("setMusicVolume", setMusicVolumeConsole, 1, "Sets the music volume, range is 0.0 to 1.0");
		CCMD("getMusicVolume", getMusicVolumeConsole, 0, "Get the current music volume where 0 = silent, 1 = maximum.");

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		setVolume(soundSettings->musicVolume);

		return res && s_thread;
	}

	void destroy()
	{
		TFE_System::logWrite(LOG_MSG, "MidiPlayer", "Shutdown");
		// Destroy the thread before shutting down the Midi Device.
		s_runMusicThread.store(false);
		if (s_thread->isPaused())
		{
			s_thread->resume();
		}
		s_thread->waitOnExit();

		delete s_thread;
		TFE_MidiDevice::destroy();

		MUTEX_DESTROY(&s_mutex);
	}

	//////////////////////////////////////////////////
	// Command Buffer
	//////////////////////////////////////////////////
	MidiCmd* midiAllocCmd()
	{
		if (s_midiCmdCount >= MAX_MIDI_CMD) { return nullptr; }
		MidiCmd* cmd = &s_midiCmdBuffer[s_midiCmdCount];
		s_midiCmdCount++;
		return cmd;
	}

	void midiClearCmdBuffer()
	{
		s_midiCmdCount = 0;
	}

	//////////////////////////////////////////////////
	// Command Interface
	//////////////////////////////////////////////////
	void setVolume(f32 volume)
	{
		MUTEX_LOCK(&s_mutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_CHANGE_VOL;
			midiCmd->newVolume = volume;
		}
		MUTEX_UNLOCK(&s_mutex);
	}

	void pause()
	{
		MUTEX_LOCK(&s_mutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_PAUSE;
		}
		MUTEX_UNLOCK(&s_mutex);
	}

	void resume()
	{
		MUTEX_LOCK(&s_mutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_RESUME;
		}
		MUTEX_UNLOCK(&s_mutex);
	}

	void stopMidiSound()
	{
		MUTEX_LOCK(&s_mutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_STOP_NOTES;
		}
		MUTEX_UNLOCK(&s_mutex);
	}

	f32 getVolume()
	{
		return s_masterVolume;
	}

	void midiSetCallback(void(*callback)(void), f64 timeStep)
	{
		MUTEX_LOCK(&s_mutex);
		s_midiCallback.callback = callback;
		s_midiCallback.timeStep = timeStep;
		s_midiCallback.accumulator = 0.0;

		for (u32 i = 0; i < 16; i++)
		{
			s_channelSrcVolume[i] = CHANNEL_MAX_VOLUME;
		}
		changeVolume();
		MUTEX_UNLOCK(&s_mutex);
	}

	void midiClearCallback()
	{
		MUTEX_LOCK(&s_mutex);
		s_midiCallback.callback = nullptr;
		s_midiCallback.timeStep = 0.0;
		s_midiCallback.accumulator = 0.0;
		MUTEX_UNLOCK(&s_mutex);
	}

	//////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////
	void changeVolume()
	{
		for (u32 i = 0; i < 16; i++)
		{
			TFE_MidiDevice::sendMessage(MID_CONTROL_CHANGE + i, MID_VOLUME_MSB, u8(s_channelSrcVolume[i] * s_masterVolumeScaled));
		}
	}

	void stopAllNotes()
	{
		for (u32 i = 0; i < 16; i++)
		{
			TFE_MidiDevice::sendMessage(MID_CONTROL_CHANGE + i, MID_ALL_NOTES_OFF);
		}
	}
	
	void resetLocalTime(s32& loopStart, u64& localTime, f64& dt)
	{
		stopAllNotes();
		localTime = 0u;
		loopStart = -1;
		dt = 0.0;
	}

	void sendMessageDirect(u8 type, u8 arg1, u8 arg2)
	{
		u8 msg[] = { type, arg1, arg2 };
		if ((type & 0xf0) == MID_CONTROL_CHANGE && arg1 == MID_VOLUME_MSB)
		{
			const s32 channelIndex = type & 0x0f;
			s_channelSrcVolume[channelIndex] = arg2;
			msg[2] = u8(s_channelSrcVolume[channelIndex] * s_masterVolumeScaled);
		}
		TFE_MidiDevice::sendMessage(msg, 3);
	}

	// Thread Function
	TFE_THREADRET midiUpdateFunc(void* userData)
	{
		bool runThread  = true;
		bool wasPlaying = false;
		bool isPlaying  = false;
		bool isPaused = false;
		s32 loopStart = -1;
		u64 localTime = 0;
		u64 localTimeCallback = 0;
		f64 dt = 0.0;
		while (runThread)
		{
			MUTEX_LOCK(&s_mutex);
						
			// Read from the command buffer.
			MidiCmd* midiCmd = s_midiCmdBuffer;
			for (u32 i = 0; i < s_midiCmdCount; i++, midiCmd++)
			{
				switch (midiCmd->cmd)
				{
					case MIDI_PAUSE:
					{
						localTimeCallback = 0;
						isPaused = true;
						stopAllNotes();
					} break;
					case MIDI_RESUME:
					{
						isPaused = false;
					} break;
					case MIDI_CHANGE_VOL:
					{
						s_masterVolume = midiCmd->newVolume;
						s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
						changeVolume();
					} break;
					case MIDI_STOP_NOTES:
					{
						stopAllNotes();
						// Reset callback time.
						localTimeCallback = 0;
						s_midiCallback.accumulator = 0.0;
					} break;
				}
			}
			s_midiCmdCount = 0;

			// Process the midi callback, if it exists.
			if (s_midiCallback.callback && !isPaused)
			{
				s_midiCallback.accumulator += TFE_System::updateThreadLocal(&localTimeCallback);
				while (s_midiCallback.callback && s_midiCallback.accumulator >= s_midiCallback.timeStep)
				{
					s_midiCallback.callback();
					s_midiCallback.accumulator -= s_midiCallback.timeStep;
				}
			}

			MUTEX_UNLOCK(&s_mutex);

			runThread = s_runMusicThread.load();
		};
		
		return (TFE_THREADRET)0;
	}

	// Console Functions
	void setMusicVolumeConsole(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }
		f32 volume = TFE_Console::getFloatArg(args[1]);
		setVolume(volume);

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		soundSettings->musicVolume = volume;
		TFE_Settings::writeToDisk();
	}

	void getMusicVolumeConsole(const ConsoleArgList& args)
	{
		char res[256];
		sprintf(res, "Sound Volume: %2.3f", s_masterVolume);
		TFE_Console::addToHistory(res);
	}
}
