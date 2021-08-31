#include "mission.h"
#include "actor.h"
#include "agent.h"
#include "animLogic.h"
#include "automap.h"
#include "hud.h"
#include "updateLogic.h"
#include "pickup.h"
#include "player.h"
#include "projectile.h"
#include "weapon.h"
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/Renderer/rlimits.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	#define CONV_6bitTo8bit(x) (((x)<<2) | ((x)>>4))
	// Show the loading screen for at least 1 second.
	#define MIN_LOAD_TIME 145

	struct DrawRect
	{
		s32 x0;
		s32 y0;
		s32 x1;
		s32 y1;
	};
	static DrawRect s_videoDrawRect =
	{
		0, 0,
		319, 199
	};

	enum GameMissionMode
	{
		MISSION_MODE_LOADING    = 0,	// causes the loading screen to be displayed.
		MISSION_MODE_MAIN       = 1,	// the main in-game experience.
		MISSION_MODE_UNKNOWN    = 2,	// unknown - not set (as far as I can tell).
		MISSION_MODE_LOAD_START = 3,	// set right as loading starts.
	};

	/////////////////////////////////////////////
	// Shared State
	/////////////////////////////////////////////
	JBool s_gamePaused = JTRUE;

	TextureData* s_loadScreen = nullptr;
	u8 s_loadingScreenPal[768];
	u8 s_levelPalette[768];

	// Move these to color?
	JBool s_palModified = JTRUE;
	JBool s_canChangePal = JTRUE;
	JBool s_screenFxEnabled = JTRUE;
	JBool s_screenBrightnessEnabled = JTRUE;
	JBool s_luminanceMask[3] = { JFALSE, JFALSE, JFALSE };
	JBool s_updateHudColors = JFALSE;

	JBool s_screenBrightnessChanged = JFALSE;
	JBool s_screenFxChanged = JFALSE;
	JBool s_lumMaskChanged = JFALSE;

	s32 s_flashFxLevel = 0;
	s32 s_healthFxLevel = 0;
	s32 s_shieldFxLevel = 0;
	s32 s_screenBrightness = ONE_16;

	u8* s_colormap;
	u8* s_lightSourceRamp;

	// Loading
	u8* s_levelColorMap = nullptr;
	u8* s_levelColorMapBasePtr = nullptr;
	u8 s_levelLightRamp[LIGHT_SOURCE_LEVELS];
	
	/////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////
	static u8 s_framebuffer[320 * 200];
	static JBool s_exitLevel = JFALSE;
	static GameMissionMode s_missionMode = MISSION_MODE_MAIN;
	static Task* s_levelEndTask = nullptr;
	static Task* s_mainTask = nullptr;
	static Task* s_missionLoadTask = nullptr;
	static char s_cheatString[32] = { 0 };
	static s32  s_cheatCharIndex = 0;
	static s32  s_cheatInputCount = 0;

	/////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////
	void mission_mainTaskFunc(s32 id);
	void setPalette(u8* pal);
	void textureBlitColumnOpaque(u8* image, u8* outBuffer, s32 yPixelCount);
	void textureBlitColumnTrans(u8* image, u8* outBuffer, s32 yPixelCount);
	void blitTextureToScreen(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, u8* output);
	void blitLoadingScreen();
	void displayLoadingScreen();
	void mission_setupTasks();
	u8*  color_loadMap(FilePath* path, u8* lightRamp, u8** basePtr);

	void setScreenBrightness(fixed16_16 brightness);
	void setScreenFxLevels(s32 healthFx, s32 shieldFx, s32 flashFx);
	void setLuminanceMask(JBool r, JBool g, JBool b);
	void setCurrentColorMap(u8* colorMap, u8* lightRamp);
	void mainTask_handleCall(s32 id);
			
	/////////////////////////////////////////////
	// API Implementation
	/////////////////////////////////////////////
	static Tick s_loadingScreenStart;
	static Tick s_loadingScreenDelta;

	void mission_startTaskFunc(s32 id)
	{
		task_begin;
		{
			s_invalidLevelIndex = JFALSE;
			s_levelComplete = JFALSE;
			s_exitLevel = JFALSE;
			
			s_missionMode = MISSION_MODE_LOAD_START;
			mission_setupTasks();
			displayLoadingScreen();

			// Add a yield here, so the loading screen is shown immediately.
			task_yield(TASK_NO_DELAY);
			s_loadingScreenStart = s_curTick;
			{
				const char* levelName = agent_getLevelName();
				if (level_load(levelName, s_agentData[s_agentId].difficulty + 1))
				{
					setScreenBrightness(ONE_16);
					setScreenFxLevels(0, 0, 0);
					setLuminanceMask(0, 0, 0);

					char palName[TFE_MAX_PATH];
					strcpy(palName, levelName);
					strcat(palName, ".PAL");
					FilePath filePath;
					if (TFE_Paths::getFilePath(palName, &filePath))
					{
						FileStream::readContents(&filePath, s_levelPalette, 768);
					}

					char colorMapName[TFE_MAX_PATH];
					strcpy(colorMapName, levelName);
					strcat(colorMapName, ".CMP");
					s_levelColorMap = nullptr;

					if (TFE_Paths::getFilePath(colorMapName, &filePath))
					{
						s_levelColorMap = color_loadMap(&filePath, s_levelLightRamp, &s_levelColorMapBasePtr);
					}
					else if (TFE_Paths::getFilePath("DEFAULT.CMP", &filePath))
					{
						TFE_System::logWrite(LOG_WARNING, "mission_startTaskFunc", "USING DEFAULT.CMP");
						s_levelColorMap = color_loadMap(&filePath, s_levelLightRamp, &s_levelColorMapBasePtr);
					}

					setCurrentColorMap(s_levelColorMap, s_levelLightRamp);
					automap_updateMapData(MAP_CENTER_PLAYER);
					setSkyParallax(s_parallax0, s_parallax1);
					// initSoundEffects();  <- TODO: Handle later
					s_missionMode = MISSION_MODE_MAIN;
					s_gamePaused = JFALSE;
				}
			}
			// Add a yield here, to get the delta time.
			task_yield(TASK_NO_DELAY);
			s_loadingScreenDelta = s_curTick - s_loadingScreenStart;
			// Make sure the loading screen is displayed for at least 3 seconds.
			if (s_loadingScreenDelta < MIN_LOAD_TIME)
			{
				task_yield(MIN_LOAD_TIME - s_loadingScreenDelta);
			}
			s_mainTask = pushTask(mission_mainTaskFunc);
		}
		// Sleep until we are done with the main task.
		task_yield(TASK_SLEEP);

		// Cleanup - shut down all tasks.
		task_freeAll();

		// End the task.
		task_end;
	}

	void mission_setLoadMissionTask(Task* task)
	{
		s_missionLoadTask = task;
	}

	void mission_mainTaskFunc(s32 id)
	{
		task_begin;
		while (id != -1)
		{
			// This means it is time to abort, we are done with this level.
			if (s_curTick >= 0 && (s_exitLevel || id < 0))
			{
				break;
			}
			// Handle delta time.
			s_deltaTime = div16(intToFixed16(s_curTick - s_prevTick), FIXED(145));
			s_deltaTime = min(s_deltaTime, FIXED(64));
			s_prevTick = s_curTick;
			s_playerTick = s_curTick;

			// setupCamera();

			switch (s_missionMode)
			{
				case MISSION_MODE_LOADING:
				{
					blitLoadingScreen();
				} break;
				case MISSION_MODE_MAIN:
				{
					// updateScreensize();
					// drawWorld();
				} break;
				// These modes never seem to get called.
				case MISSION_MODE_UNKNOWN:
				{
				} break;
				case MISSION_MODE_LOAD_START:
				{
					// vgaClearPalette();
				} break;
			}
			memset(s_framebuffer, 0, 320 * 200);

			// handleGeneralInput();
			// handlePaletteFx();
			if (s_drawAutomap)
			{
				automap_draw(s_framebuffer);
			}
			// hud_drawAndUpdate();
			// hud_drawHudText();

			// vgaSwapBuffers() in the DOS code.
			setPalette(s_levelPalette);
			TFE_RenderBackend::updateVirtualDisplay(s_framebuffer, 320 * 200);

			// Pump tasks and look for any with a different ID.
			do
			{
				task_yield(TASK_NO_DELAY);
				if (id != -1 && id != 0)
				{
					mainTask_handleCall(id);
				}
			} while (id != -1 && id != 0);
		}

		s_mainTask = nullptr;
		task_makeActive(s_missionLoadTask);
		task_end;
	}

	/////////////////////////////////////////////
	// Internal Implementation
	/////////////////////////////////////////////
	void mainTask_handleCall(s32 id)
	{
		if (id == 0x22)	// This message is sent when the power generator is enabled in Talay.
		{
			sector_changeGlobalLightLevel();
		}
	}
	
	// Convert the palette to 32 bit color and then send to the render backend.
	// This is functionally similar to loading the palette into VGA registers.
	void setPalette(u8* pal)
	{
		// Update the palette.
		u32 palette[256];
		u32* outColor = palette;
		u8* srcColor = pal;
		for (s32 i = 0; i < 256; i++, outColor++, srcColor += 3)
		{
			*outColor = CONV_6bitTo8bit(srcColor[0]) | (CONV_6bitTo8bit(srcColor[1]) << 8u) | (CONV_6bitTo8bit(srcColor[2]) << 16u) | (0xffu << 24u);
		}
		TFE_RenderBackend::setPalette(palette);
	}

	void textureBlitColumnOpaque(u8* image, u8* outBuffer, s32 yPixelCount)
	{
		s32 end = yPixelCount - 1;
		s32 offset = 0;
		for (s32 i = end; i >= 0; i--, offset += 320)
		{
			outBuffer[offset] = image[i];
		}
	}

	void textureBlitColumnTrans(u8* image, u8* outBuffer, s32 yPixelCount)
	{
		s32 end = yPixelCount - 1;
		s32 offset = 0;
		for (s32 i = end; i >= 0; i--, offset += 320)
		{
			if (image[i]) { outBuffer[offset] = image[i]; }
		}
	}

	void blitTextureToScreen(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, u8* output)
	{
		s32 x1 = x0 + texture->width - 1;
		s32 y1 = y0 + texture->height - 1;

		// Cull if outside of the draw rect.
		if (x1 < rect->x0 || y1 < rect->y0 || x0 > rect->x1 || y0 > rect->y1) { return; }

		s32 srcX = 0, srcY = 0;
		if (y0 < rect->y0)
		{
			y0 = rect->y0;
		}
		if (y1 > rect->y1)
		{
			srcY = y1 - rect->y1;
			y1 = rect->y1;
		}

		if (x0 < rect->x0)
		{
			x0 = rect->x0;
		}
		if (x1 > rect->x1)
		{
			srcX = x1 - rect->x1;
			x1 = rect->x1;
		}

		s32 yPixelCount = y1 - y0 + 1;
		if (yPixelCount <= 0) { return; }

		u8* buffer = texture->image + texture->height*srcX + srcY;
		if (texture->flags & OPACITY_TRANS)
		{
			for (s32 col = x0; col <= x1; col++, buffer += texture->height)
			{
				textureBlitColumnTrans(buffer, output + y0*320 + col, yPixelCount);
			}
		}
		else
		{
			for (s32 col = x0; col <= x1; col++, buffer += texture->height)
			{
				textureBlitColumnOpaque(buffer, output + y0*320 + col, yPixelCount);
			}
		}
	}

	void blitLoadingScreen()
	{
		if (!s_loadScreen) { return; }
		blitTextureToScreen(s_loadScreen, &s_videoDrawRect, 0/*x0*/, 0/*y0*/, s_framebuffer);
	}

	void displayLoadingScreen()
	{
		blitLoadingScreen();

		// Update twice to make sure the loading screen is visible.
		// The virtual display is buffered, meaning there is a frame of latency.
		// This hackery basically removes that latency so the image is properly displayed immediately.
		setPalette(s_loadingScreenPal);
		setPalette(s_loadingScreenPal);
		TFE_RenderBackend::updateVirtualDisplay(s_framebuffer, 320 * 200);
		TFE_RenderBackend::updateVirtualDisplay(s_framebuffer, 320 * 200);
	}

	void mission_setupTasks()
	{
		setSpriteAnimation(nullptr, nullptr);
		bitmap_setupAnimationTask();
		// resetFrameData();		// TODO(Core Game Loop Release)
		hud_startup();
		hud_clearMessage();
		automap_computeScreenBounds();
		weapon_clearFireRate();
		weapon_createPlayerWeaponTask();
		projectile_createTask();
		player_createController();
		inf_createElevatorTask();
		player_clearEyeObject();
		pickup_createTask();
		inf_createTeleportTask();
		inf_createTriggerTask();
		actor_createTask();
		hitEffect_createTask();
		// createIMuseTask();  <- this will wait until a later release.
		level_clearData();
		updateLogic_clearTask();
		//s_drawAutomap = JFALSE;
		s_levelEndTask = nullptr;
		s_cheatString[0] = 0;
		s_cheatCharIndex = 0;
		s_cheatInputCount = 0;

		for (s32 i = 9; i >= 0; i--)
		{
			s_goals[i] = JFALSE;
		}
	}
		
	void setScreenBrightness(fixed16_16 brightness)
	{
		if (brightness != s_screenBrightness)
		{
			s_screenBrightness = brightness;
			s_screenBrightnessChanged = JTRUE;
		}
	}

	void setScreenFxLevels(s32 healthFx, s32 shieldFx, s32 flashFx)
	{
		if (healthFx != s_healthFxLevel || shieldFx != s_shieldFxLevel || flashFx != s_flashFxLevel)
		{
			s_healthFxLevel = healthFx;
			s_shieldFxLevel = shieldFx;
			s_flashFxLevel = flashFx;
			s_screenFxChanged = JTRUE;
		}
	}

	void setLuminanceMask(JBool r, JBool g, JBool b)
	{
		if (r != s_luminanceMask[0] || g != s_luminanceMask[1] || b != s_luminanceMask[2])
		{
			s_luminanceMask[0] = r;
			s_luminanceMask[1] = g;
			s_luminanceMask[2] = b;
			s_lumMaskChanged = JTRUE;
		}
	}

	void setCurrentColorMap(u8* colorMap, u8* lightRamp)
	{
		s_colormap = colorMap;
		s_lightSourceRamp = lightRamp;
	}

	// Move to the appropriate place.
	u8* color_loadMap(FilePath* path, u8* lightRamp, u8** basePtr)
	{
		FileStream file;
		if (!file.open(path, FileStream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "color_loadMap", "Error loading color map.");
			return nullptr;
		}

		// Allocate 256 colors * 32 light levels + 256, where the last 256 is so that the address can be rounded to the next 256 byte boundary.
		u8* colorMapBase = (u8*)malloc(8448);
		u8* colorMap = colorMapBase;
		*basePtr = colorMapBase;
		if (size_t(colorMap) & 0xffu)
		{
			// colorMap2 is rounded to the next 256 bytes.
			colorMap = colorMapBase + 256 - (size_t(colorMap) & 0xff);
		}
		// 256 colors * 32 light levels = 8192
		file.readBuffer(colorMap, 8192);
		file.readBuffer(lightRamp, LIGHT_SOURCE_LEVELS);
		file.close();

		return colorMap;
	}

}  // TFE_DarkForces