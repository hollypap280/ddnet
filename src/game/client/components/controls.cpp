/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

#include <base/color.h>
#include <base/dbg.h>
#include <base/io.h>
#include <base/str.h>
#include <base/mem.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

CControls::CControls()
{
	mem_zero(&m_aLastData, sizeof(m_aLastData));
	std::fill(std::begin(m_aMouseOnAction), std::end(m_aMouseOnAction), false);
	std::fill(std::begin(m_aMousePos), std::end(m_aMousePos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMousePosOnAction), std::end(m_aMousePosOnAction), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aTargetPos), std::end(m_aTargetPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMouseInputType), std::end(m_aMouseInputType), EMouseInputType::ABSOLUTE);
}

void CControls::OnReset()
{
	StopMacroPractice();
	ResetInput(0);
	ResetInput(1);

	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);

	m_LastSendTime = 0;
	m_MacroRecording = false;
	m_MacroPlaying = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_vMacroFrames.clear();
	std::fill(std::begin(m_aManualHook), std::end(m_aManualHook), false);
	std::fill(std::begin(m_aAutoHookActive), std::end(m_aAutoHookActive), false);
	std::fill(std::begin(m_aAutoHookFireActive), std::end(m_aAutoHookFireActive), false);
	std::fill(std::begin(m_aAutoAvoidActive), std::end(m_aAutoAvoidActive), false);
	std::fill(std::begin(m_aAutoEdgeHookActive), std::end(m_aAutoEdgeHookActive), false);
}

void CControls::ResetInput(int Dummy)
{
	m_aLastData[Dummy].m_Direction = 0;
	// simulate releasing the fire button
	if((m_aLastData[Dummy].m_Fire & 1) != 0)
		m_aLastData[Dummy].m_Fire++;
	m_aLastData[Dummy].m_Fire &= INPUT_STATE_MASK;
	m_aLastData[Dummy].m_Jump = 0;
	m_aInputData[Dummy] = m_aLastData[Dummy];

	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
}

void CControls::OnPlayerDeath()
{
	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);
}

struct CInputState
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
	bool m_IsMouseAction;
	bool m_IsHookAction = false;
	bool m_IsJumpAction = false;
	bool m_IsFireAction = false;
	bool m_IsDirectionLeft = false;
	bool m_IsDirectionRight = false;
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	*pState->m_apVariables[g_Config.m_ClDummy] = Stroke;
	if(Stroke != 0 && pState->m_pControls->m_MacroPracticeActive)
	{
		if(pState->m_IsDirectionLeft)
			pState->m_pControls->m_MacroPracticePendingDirectionLeft = true;
		if(pState->m_IsDirectionRight)
			pState->m_pControls->m_MacroPracticePendingDirectionRight = true;
	}
	if(pState->m_IsHookAction)
	{
		pState->m_pControls->m_aManualHook[g_Config.m_ClDummy] = Stroke != 0;
		if(Stroke != 0 && pState->m_pControls->m_MacroPracticeActive)
			pState->m_pControls->m_MacroPracticePendingHook = true;
	}
	if(pState->m_IsJumpAction && Stroke != 0 && pState->m_pControls->m_MacroPracticeActive)
		pState->m_pControls->m_MacroPracticePendingJump = true;

	if(Stroke != 0 && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming && pState->m_pControls->GameClient()->m_Binds.m_HandlingNewBind)
		pState->m_pControls->m_aMouseOnAction[g_Config.m_ClDummy] = true;
}

void CControls::ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if((pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active) || pState->m_pControls->GameClient()->m_Spectator.IsActive())
		return;

	int *pVariable = pState->m_apVariables[g_Config.m_ClDummy];
	const int Stroke = pResult->GetInteger(0);
	if(((*pVariable) & 1) != Stroke)
	{
		(*pVariable)++;
		if(pState->m_IsFireAction && pState->m_pControls->m_MacroPracticeActive)
			pState->m_pControls->m_MacroPracticePendingFireTransitions = std::min(pState->m_pControls->m_MacroPracticePendingFireTransitions + 1, 64);
		if(Stroke != 0 && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming && pState->m_pControls->GameClient()->m_Binds.m_HandlingNewBind)
			pState->m_pControls->m_aMouseOnAction[g_Config.m_ClDummy] = true;
	}
	*pVariable &= INPUT_STATE_MASK;
}

struct CInputSet
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
	int m_Value;
};

void CControls::ConKeyInputSet(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	if(pResult->GetInteger(0))
	{
		*pSet->m_apVariables[g_Config.m_ClDummy] = pSet->m_Value;
		if(pSet->m_pControls->m_MacroPracticeActive &&
			pSet->m_apVariables[0] == &pSet->m_pControls->m_aInputData[0].m_WantedWeapon)
			pSet->m_pControls->m_MacroPracticePendingWantedWeapon = pSet->m_Value;
	}
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	CControls *pSelf = pSet->m_pControls;
	const int Dummy = g_Config.m_ClDummy;
	int *pVariable = pSet->m_apVariables[Dummy];
	const int Stroke = pResult->GetInteger(0);

	// Do not reinterpret CInputSet as CInputState here: the two structures
	// have different trailing fields. Count each press/release transition
	// directly and queue it for the slower private practice clock.
	if(((*pVariable) & 1) != Stroke)
	{
		++(*pVariable);
		if(pSelf->m_MacroPracticeActive)
		{
			if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_NextWeapon)
				pSelf->m_MacroPracticePendingNextWeaponTransitions =
					std::min(pSelf->m_MacroPracticePendingNextWeaponTransitions + 1, 64);
			else if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_PrevWeapon)
				pSelf->m_MacroPracticePendingPrevWeaponTransitions =
					std::min(pSelf->m_MacroPracticePendingPrevWeaponTransitions + 1, 64);
		}
	}
	*pVariable &= INPUT_STATE_MASK;
	pSelf->m_aInputData[Dummy].m_WantedWeapon = 0;
}

void CControls::OnConsoleInit()
{
	// game commands
	{
		static CInputState s_State = {this, {&m_aInputDirectionLeft[0], &m_aInputDirectionLeft[1]}, false};
		s_State.m_IsDirectionLeft = true;
		Console()->Register("+left", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move left");
	}
	{
		static CInputState s_State = {this, {&m_aInputDirectionRight[0], &m_aInputDirectionRight[1]}, false};
		s_State.m_IsDirectionRight = true;
		Console()->Register("+right", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move right");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Jump, &m_aInputData[1].m_Jump}, false};
		s_State.m_IsJumpAction = true;
		Console()->Register("+jump", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Jump");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Hook, &m_aInputData[1].m_Hook}, true};
		s_State.m_IsHookAction = true;
		Console()->Register("+hook", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Hook");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Fire, &m_aInputData[1].m_Fire}, true};
		s_State.m_IsFireAction = true;
		Console()->Register("+fire", "", CFGFLAG_CLIENT, ConKeyInputCounter, &s_State, "Fire");
	}
	{
		static CInputState s_State = {this, {&m_aShowHookColl[0], &m_aShowHookColl[1]}, false};
		Console()->Register("+showhookcoll", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Show Hook Collision");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 1};
		Console()->Register("+weapon1", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to hammer");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 2};
		Console()->Register("+weapon2", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to gun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 3};
		Console()->Register("+weapon3", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to shotgun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 4};
		Console()->Register("+weapon4", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to grenade");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 5};
		Console()->Register("+weapon5", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to laser");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_NextWeapon, &m_aInputData[1].m_NextWeapon}, 0};
		Console()->Register("+nextweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to next weapon");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_PrevWeapon, &m_aInputData[1].m_PrevWeapon}, 0};
		Console()->Register("+prevweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to previous weapon");
	}

	Console()->Register("tp_to_cursor", "", CFGFLAG_CLIENT, ConTpToCursor, this, "Practice: teleport yourself to your cursor");
	Console()->Register("tp_dummy_to_cursor", "", CFGFLAG_CLIENT, ConTpDummyToCursor, this, "Practice: teleport the other tee to your cursor");

	Console()->Register("macro_record", "", CFGFLAG_CLIENT, ConMacroRecord, this, "Start recording a player input macro");
	Console()->Register("macro_stop", "", CFGFLAG_CLIENT, ConMacroStop, this, "Stop recording or playback of a player input macro");
	Console()->Register("macro_save", "s[name]", CFGFLAG_CLIENT, ConMacroSave, this, "Save the recorded player input macro");
	Console()->Register("macro_play", "s[name]", CFGFLAG_CLIENT, ConMacroPlay, this, "Play a saved player input macro");
	Console()->Register("macro_delete", "s[name]", CFGFLAG_CLIENT, ConMacroDelete, this, "Delete a saved player input macro");
	Console()->Register("+macro_rewind", "", CFGFLAG_CLIENT, ConMacroRewind, this, "Hold to rewind a macro; repeat speed increases while held");
	Console()->Register("+macro_seek", "", CFGFLAG_CLIENT, ConMacroSeek, this, "Jump forward by the configured number of macro ticks");
	Console()->Register("+macro_slow", "", CFGFLAG_CLIENT, ConMacroSlow, this, "Hold to slow down macro playback");
}

void CControls::ConTpToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->m_MacroPracticeActive)
	{
		pSelf->TeleportMacroPracticeCharacter(pSelf->m_MacroPracticeLocalId, pSelf->CursorWorldPos());
		return;
	}
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN);
}

void CControls::ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->m_MacroPracticeActive)
	{
		pSelf->TeleportMacroPracticeCharacter(pSelf->GameClient()->m_aLocalIds[!g_Config.m_ClDummy], pSelf->CursorWorldPos());
		return;
	}
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!pSelf->Client()->DummyConnected())
	{
		pSelf->GameClient()->Echo(Localize("Dummy is not connected"));
		return;
	}

	// Send on the inactive connection; the server only teleports the sender.
	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_MAIN : IClient::CONN_DUMMY);
}

vec2 CControls::CursorWorldPos() const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const vec2 TargetPos = m_aTargetPos[g_Config.m_ClDummy];
	return Center + (TargetPos - Center) * GameClient()->m_Camera.m_Zoom;
}


void CControls::TeleportMacroPracticeCharacter(int ClientId, vec2 Pos)
{
	if(!m_MacroPracticeActive || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(ClientId);
	if(!pCharacter)
	{
		GameClient()->Echo("Тренировочный тии для телепорта не найден.");
		return;
	}

	CCharacterCore Core = pCharacter->GetCore();
	Core.m_Pos = Pos;
	Core.m_Vel = vec2(0.0f, 0.0f);
	pCharacter->SetCore(Core);
	pCharacter->ReleaseHook();
	pCharacter->m_Pos = Pos;
	pCharacter->m_PrevPos = Pos;
	pCharacter->m_PrevPrevPos = Pos;
	pCharacter->m_IsMacroPracticeStatic = false;
	UpdateMacroPracticeGhost(pCharacter);
}

void CControls::SendPracticeTeleport(vec2 Pos, int Conn)
{
	CNetMsg_Cl_PracticeTeleport Msg;
	Msg.m_X = round_to_int(Pos.x);
	Msg.m_Y = round_to_int(Pos.y);
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CControls::SendPracticeTeleportToCursor(int Conn)
{
	SendPracticeTeleport(CursorWorldPos(), Conn);
}

namespace
{
	constexpr uint32_t MACRO_FILE_MAGIC = 0x444D4143;
	// Version 6 adds the private freeze fields to each stored frame.
	// Version 5 files may still use the previous, smaller frame layout.
	constexpr uint32_t MACRO_FILE_VERSION = 6;
	constexpr uint32_t MACRO_MAX_FRAMES = 18000;
}

struct SMacroListContext
{
	CControls *m_pControls;
	std::vector<CControls::SMacroInfo> *m_pMacros;
};

bool CControls::MacroFilePath(const char *pName, char *pBuffer, unsigned BufferSize) const
{
	if(!pName || !pName[0])
		return false;
	char aSafeName[64];
	int Out = 0;
	for(int i = 0; pName[i] && Out < (int)sizeof(aSafeName) - 1; ++i)
	{
		const unsigned char Ch = (unsigned char)pName[i];
		if((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') ||
			Ch == '_' || Ch == '-' || Ch == ' ' || Ch >= 0x80)
			aSafeName[Out++] = (char)Ch;
		else
			return false;
	}
	aSafeName[Out] = '\0';
	if(Out == 0)
		return false;
	str_format(pBuffer, BufferSize, "macros/%s.dmacro", aSafeName);
	return true;
}

int CControls::MacroListCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(IsDir || !pName || !pUser)
		return 0;

	auto *pContext = static_cast<SMacroListContext *>(pUser);
	CControls *pControls = pContext->m_pControls;
	std::vector<SMacroInfo> *pvMacros = pContext->m_pMacros;
	if(!pControls || !pvMacros)
		return 0;

	const size_t NameLength = str_length(pName);
	constexpr const char *pExtension = ".dmacro";
	constexpr size_t ExtensionLength = 7;
	if(NameLength <= ExtensionLength || str_comp(pName + NameLength - ExtensionLength, pExtension) != 0)
		return 0;

	SMacroInfo Info;
	const size_t BaseNameLength = std::min(NameLength - ExtensionLength, sizeof(Info.m_aName) - 1);
	str_copy(Info.m_aName, pName, BaseNameLength + 1);
	Info.m_aMapName[0] = '\0';

	char aPath[128];
	if(!pControls->MacroFilePath(Info.m_aName, aPath, sizeof(aPath)))
		return 0;

	IOHANDLE File = pControls->Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return 0;

	uint32_t Magic = 0;
	uint32_t Version = 0;
	uint32_t Count = 0;
	const bool HeaderValid = io_read(File, &Magic, sizeof(Magic)) == sizeof(Magic) &&
		io_read(File, &Version, sizeof(Version)) == sizeof(Version) &&
		io_read(File, &Count, sizeof(Count)) == sizeof(Count) &&
		Magic == MACRO_FILE_MAGIC && Version >= 1 && Version <= MACRO_FILE_VERSION &&
		Count <= MACRO_MAX_FRAMES;
	if(HeaderValid)
	{
		Info.m_TickCount = (int)Count;
		if(Version >= 3)
		{
			char aMapName[64]{};
			if(io_read(File, aMapName, sizeof(aMapName)) == sizeof(aMapName))
				str_copy(Info.m_aMapName, aMapName);
		}
	}
	io_close(File);

	if(HeaderValid)
	{
		if(!Info.m_aMapName[0])
			str_copy(Info.m_aMapName, "Карта не записана");
		pvMacros->push_back(Info);
	}
	return 0;
}

void CControls::GetMacroList(std::vector<SMacroInfo> &vMacros)
{
	vMacros.clear();
	SMacroListContext Context{this, &vMacros};
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "macros", MacroListCallback, &Context);
	std::sort(vMacros.begin(), vMacros.end(), [](const SMacroInfo &Left, const SMacroInfo &Right) {
		return str_comp_nocase(Left.m_aName, Right.m_aName) < 0;
	});
}

bool CControls::StartMacroPractice()
{
	if(m_MacroPlaybackLive)
		StopMacroRecording();
	if(m_MacroPracticeActive)
		return true;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(Client()->State() != IClient::STATE_ONLINE || LocalId < 0 || LocalId >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_pLocalCharacter ||
		!GameClient()->m_PredictedWorld.GetCharacterById(LocalId))
	{
		GameClient()->Echo("Для тренировки нужно подключиться к серверу и иметь доступного локального тии");
		return false;
	}

	m_MacroPracticeLocalId = LocalId;
	m_MacroPracticeWorld.CopyWorld(&GameClient()->m_PredictedWorld);

	// Fast Practice must simulate the same local gameplay as the normal
	// prediction world, even when the server does not request client prediction.
	// These flags affect only the private copy; the real server tee still gets
	// neutral input while practice is active.
	m_MacroPracticeWorld.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictWeapons = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictFreeze = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictTiles = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictDDRace = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictEvents = true;
	m_MacroPracticeWorld.m_WorldConfig.m_UseTuneZones = true;
	m_MacroPracticeWorld.m_Teams = GameClient()->m_Teams;

	PopulateMacroPracticeWorld();
	if(!m_MacroPracticeWorld.GetCharacterById(LocalId))
	{
		m_MacroPracticeWorld.Clear();
		GameClient()->Echo("Не удалось создать отдельный тренировочный мир");
		return false;
	}

	str_copy(m_aMacroMapName, "Неизвестная карта");
	const char *pMapName = Client()->MapDownloadName();
	if(pMapName && pMapName[0])
	{
		const char *pBaseName = pMapName;
		for(const char *p = pMapName; *p; ++p)
			if(*p == '/' || *p == '\\')
				pBaseName = p + 1;
		str_copy(m_aMacroMapName, pBaseName);
		for(char *p = m_aMacroMapName; *p; ++p)
			if(*p == '.') { *p = '\0'; break; }
	}
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = time_get();
	m_MacroPracticeWaiting = false;
	m_MacroPracticeActive = true;
	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticeFireCounter = m_aInputData[g_Config.m_ClDummy].m_Fire & INPUT_STATE_MASK;
	m_MacroPracticeInput = m_aInputData[g_Config.m_ClDummy];
	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticeHasRenderChar = false;

	CCharacter *pPracticeChar = m_MacroPracticeWorld.GetCharacterById(LocalId);
	m_MacroPracticeStartTick = m_MacroPracticeWorld.GameTick();
	pPracticeChar->Core()->Write(&m_MacroPracticeStartCore);
	m_MacroPracticeStartWeapon = pPracticeChar->GetActiveWeapon();
	m_MacroPracticeStartFreezeTime = pPracticeChar->m_FreezeTime;
	m_MacroPracticeStartFrozenLastTick = pPracticeChar->m_FrozenLastTick;
	UpdateMacroPracticeGhost(pPracticeChar);
	GameClient()->Echo("Тренировка началась. Серверный тии неподвижен, тренировочный тии существует только локально.");
	return true;
}

void CControls::ResetMacroPracticeCharacter()
{
	if(!m_MacroPracticeActive)
		return;

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
		return;

	CCharacterCore Core = pCharacter->GetCore();
	Core.Read(&m_MacroPracticeStartCore);
	if(Core.m_FreezeEnd > m_MacroPracticeStartTick)
		Core.m_FreezeEnd = m_MacroPracticeWorld.GameTick() + (Core.m_FreezeEnd - m_MacroPracticeStartTick);
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_HookState = HOOK_IDLE;
	Core.SetHookedPlayer(-1);
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	pCharacter->SetCore(Core);
	pCharacter->m_Pos = Core.m_Pos;
	pCharacter->m_PrevPos = Core.m_Pos;
	pCharacter->m_PrevPrevPos = Core.m_Pos;
	pCharacter->SetActiveWeapon(m_MacroPracticeStartWeapon);
	pCharacter->m_FreezeTime = m_MacroPracticeStartFreezeTime;
	pCharacter->m_FrozenLastTick = m_MacroPracticeStartFrozenLastTick;
	pCharacter->m_IsMacroPracticeStatic = false;
	pCharacter->ReleaseHook();
	CNetObj_PlayerInput IdleInput{};
	IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	IdleInput.m_TargetX = 1;
	pCharacter->SetInput(&IdleInput);

	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticeFireCounter = m_aInputData[g_Config.m_ClDummy].m_Fire & INPUT_STATE_MASK;
	m_MacroPracticeInput = IdleInput;
	UpdateMacroPracticeGhost(pCharacter);
	GameClient()->Echo("Тренировочный тии возвращён в начальную точку. Серверный персонаж не затронут.");
}

void CControls::StopMacroPractice()
{
	const int Dummy = g_Config.m_ClDummy;
	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
	m_aInputData[Dummy].m_Direction = 0;
	m_aInputData[Dummy].m_Jump = 0;
	m_aInputData[Dummy].m_Hook = 0;
	if(m_aInputData[Dummy].m_Fire & 1)
		++m_aInputData[Dummy].m_Fire;
	m_aInputData[Dummy].m_WantedWeapon = 0;
	m_aInputData[Dummy].m_NextWeapon = 0;
	m_aInputData[Dummy].m_PrevWeapon = 0;
	m_aManualHook[Dummy] = false;
	m_aAutoHookActive[Dummy] = false;
	m_aAutoHookFireActive[Dummy] = false;
	m_aAutoAvoidActive[Dummy] = false;
	m_aAutoEdgeHookActive[Dummy] = false;
	m_MacroRecording = false;
	m_MacroPlaying = false;
	m_MacroPracticeActive = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = 0;
	m_MacroPracticeLocalId = -1;
	m_MacroPlaybackOwnsDummy = false;
	m_MacroPlaybackOwnsPractice = false;
	m_vMacroCorrectionFrames.clear();
	m_aLoadedMacroName[0] = '\0';
	m_aRecordingMacroName[0] = '\0';
	m_MacroPlaybackLive = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroRewindHeld = false;
	m_MacroRewindHeldSince = 0;
	m_MacroRewindNextRepeat = 0;
	m_MacroSlowHeld = false;
	m_MacroPlaybackSlowTickCounter = 0;
	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticeInput = {};
	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticeHasRenderChar = false;
	m_MacroPlaybackIndex = 0;
	m_MacroPracticeWorld.Clear();
	if(GameClient())
		GameClient()->m_Ghost.ClearMacroPracticeCharacter();
}

void CControls::UpdateMacroPracticeGhost(CCharacter *pCharacter)
{
	(void)pCharacter;
	GameClient()->m_Ghost.SetMacroPracticeWorld(&m_MacroPracticeWorld);
}

void CControls::PopulateMacroPracticeWorld()
{
	m_MacroPracticeWorld.m_LocalClientId = m_MacroPracticeLocalId;

	// Read extended freeze timers against the real snapshot tick, while keeping
	// the controlled practice tee on its own slower virtual clock.
	const int PracticeTick = m_MacroPracticeWorld.GameTick();
	m_MacroPracticeWorld.m_GameTick = Client()->GameTick(g_Config.m_ClDummy);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		CCharacter *pPracticeCharacter = m_MacroPracticeWorld.GetCharacterById(ClientId);
		const auto &Info = GameClient()->m_Snap.m_aCharacters[ClientId];

		// The local practice tee is never overwritten by live server snapshots.
		if(ClientId == m_MacroPracticeLocalId)
			continue;

		const bool IsOwnDummy = ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
		if((!g_Config.m_ClMacroPracticeShowOthers && !IsOwnDummy) || !Info.m_Active)
		{
			if(pPracticeCharacter)
				pPracticeCharacter->Destroy();
			continue;
		}

		// In team mode preserve the server's team assignment. In normal
		// free-for-all mode all local tees belong to the same FLOCK team and
		// must remain hookable/collidable in the practice copy.
		const int LocalGameTeam = m_MacroPracticeWorld.Teams()->Team(m_MacroPracticeLocalId);
		const int GameTeam = GameClient()->IsTeamPlay() ? GameClient()->m_aClients[ClientId].m_Team : LocalGameTeam;
		// CCharacter::Team()/SameTeam() read CTeamsCore, not m_GameTeam.
		m_MacroPracticeWorld.Teams()->Team(ClientId, GameTeam);

		// Copy each remote tee once. Other players stay AFK, but their cloned
		// physics continues, so hooks, hammers and collisions can move them.
		if(!pPracticeCharacter)
		{
			CNetObj_Character Character = Info.m_Cur;
			CNetObj_DDNetCharacter *pExtended = Info.m_HasExtendedData ? &GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData : nullptr;
			pPracticeCharacter = new CCharacter(&m_MacroPracticeWorld, ClientId, &Character, pExtended);
			pPracticeCharacter->m_IsLocal = false;
			m_MacroPracticeWorld.InsertEntity(pPracticeCharacter);
		}
		// Import each tee from the live snapshot only once. After that the
		// private clone owns its position even if dummy-copy/fly binds are
		// toggled on or off; never snap it back to the server's old position.
		if(!pPracticeCharacter->m_IsMacroPracticeStatic)
		{
			pPracticeCharacter->Read(&GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur,
				Info.m_HasExtendedData ? &GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData : nullptr, false);
			CCharacterCore StaticCore = pPracticeCharacter->GetCore();
			StaticCore.m_Vel = vec2(0.0f, 0.0f);
			pPracticeCharacter->SetCore(StaticCore);
			pPracticeCharacter->ReleaseHook();
			CNetObj_PlayerInput IdleInput{};
			IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			IdleInput.m_TargetX = 1;
			pPracticeCharacter->SetInput(&IdleInput);
			pPracticeCharacter->m_IsMacroPracticeStatic = true;
		}
		pPracticeCharacter->m_GameTeam = GameTeam;
	}

	m_MacroPracticeWorld.RemoveMarkedEntities();
	m_MacroPracticeWorld.m_GameTick = PracticeTick;

	// While a macro is replaying, the recorded tee id controls the private world.
	if(!m_MacroPlaybackOwnsDummy)
	{
		// The dummy connection can finish switching after OnDummySwap() ran.
		// Promote it as soon as its character has appeared in the active snapshot,
		// while keeping the previous tee alive in this private world.
		const int RequestedLocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(RequestedLocalId >= 0 && RequestedLocalId < MAX_CLIENTS && RequestedLocalId != m_MacroPracticeLocalId)
		{
			if(CCharacter *pNewCharacter = m_MacroPracticeWorld.GetCharacterById(RequestedLocalId))
			{
				CNetObj_PlayerInput IdleInput{};
				IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
				IdleInput.m_TargetX = 1;
				if(CCharacter *pOldCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId))
				{
					pOldCharacter->SetInput(&IdleInput);
					pOldCharacter->m_IsLocal = false;
					pOldCharacter->m_IsMacroPracticeStatic = true;
				}
				pNewCharacter->m_IsLocal = true;
				pNewCharacter->m_IsMacroPracticeStatic = false;
				m_MacroPracticeLocalId = RequestedLocalId;
				m_MacroPracticeWorld.m_LocalClientId = RequestedLocalId;
				m_MacroPracticePendingFireTransitions = 0;
				m_MacroPracticePendingNextWeaponTransitions = 0;
				m_MacroPracticePendingPrevWeaponTransitions = 0;
				m_MacroPracticePendingWantedWeapon = 0;
				m_MacroPracticePendingJump = false;
				m_MacroPracticePendingHook = false;
				m_MacroPracticeFireCounter = m_aInputData[g_Config.m_ClDummy].m_Fire & INPUT_STATE_MASK;
				m_MacroPracticeInput = m_aInputData[g_Config.m_ClDummy];
				m_MacroPracticeAccumulator = 0.0f;
				m_MacroPracticeLastTime = time_get();
			}
		}
	
	}

	UpdateMacroPracticeGhost(m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId));
}

bool CControls::SwitchMacroPracticeCharacter(int ClientId)
{
	if(!m_MacroPracticeActive || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(ClientId == m_MacroPracticeLocalId)
		return true;

	CCharacter *pNewCharacter = m_MacroPracticeWorld.GetCharacterById(ClientId);
	if(!pNewCharacter)
	{
		PopulateMacroPracticeWorld();
		pNewCharacter = m_MacroPracticeWorld.GetCharacterById(ClientId);
	}
	if(!pNewCharacter)
		return false;

	CNetObj_PlayerInput IdleInput{};
	IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	IdleInput.m_TargetX = 1;
	if(CCharacter *pOldCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId))
	{
		pOldCharacter->SetInput(&IdleInput);
		pOldCharacter->m_IsLocal = false;
		// Keep this tee at its last practice position while the other local
		// tee is controlled. Never refresh it from the server snapshot.
		pOldCharacter->m_IsMacroPracticeStatic = true;
	}

	pNewCharacter->m_IsLocal = true;
	pNewCharacter->m_IsMacroPracticeStatic = false;
	m_MacroPracticeLocalId = ClientId;
	m_MacroPracticeWorld.m_LocalClientId = ClientId;
	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticeFireCounter = m_aInputData[g_Config.m_ClDummy].m_Fire & INPUT_STATE_MASK;
	m_MacroPracticeInput = m_aInputData[g_Config.m_ClDummy];
	UpdateMacroPracticeGhost(pNewCharacter);
	return true;
}

bool CControls::RestoreMacroPracticeFrame(int Index)
{
	if(!m_MacroPracticeActive || Index < 0 || Index >= (int)m_vMacroFrames.size())
		return false;

	const SMacroFrame &Frame = m_vMacroFrames[Index];
	if(Frame.m_ClientId >= 0 && !SwitchMacroPracticeCharacter(Frame.m_ClientId))
		return false;

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
		return false;
	if(Frame.m_HasCoordinates)
	{
		CCharacterCore Core = pCharacter->GetCore();
		Core.Read(&Frame.m_Core);
		pCharacter->SetCore(Core);
		pCharacter->m_Pos = Core.m_Pos;
		pCharacter->SetActiveWeapon(Frame.m_Weapon);
		pCharacter->m_FreezeTime = Frame.m_FreezeTime;
		pCharacter->m_FrozenLastTick = Frame.m_FrozenLastTick != 0;
		m_MacroPracticeWorld.m_GameTick = Frame.m_Tick;
		m_MacroPracticeTick = Frame.m_Tick;

		UpdateMacroPracticeGhost(pCharacter);
	}
	m_MacroPlaybackIndex = (std::size_t)Index + 1;
	m_MacroPracticeWaiting = false;
	return true;
}

void CControls::SimulateMacroPracticeTick(const CNetObj_PlayerInput &LiveInput, const CNetObj_PlayerInput &DummyInput)
{
	if(!m_MacroPracticeActive)
		return;

	if(m_MacroPlaying && m_MacroPlaybackOwnsDummy && m_MacroPlaybackIndex < m_vMacroFrames.size())
	{
		const SMacroFrame &NextFrame = m_vMacroFrames[m_MacroPlaybackIndex];
		const int RecordedClientId = NextFrame.m_ClientId;
		if(RecordedClientId >= 0 && RecordedClientId != m_MacroPracticeLocalId)
		{
			if(!SwitchMacroPracticeCharacter(RecordedClientId))
			{
				GameClient()->Echo("Не удалось переключить тренировочного тии по записи макроса.");
				StopMacroRecording();
				return;
			}
			if(NextFrame.m_HasCoordinates)
			{
				if(CCharacter *pRecordedCharacter = m_MacroPracticeWorld.GetCharacterById(RecordedClientId))
				{
					CCharacterCore RecordedStart = pRecordedCharacter->GetCore();
					RecordedStart.Read(&NextFrame.m_PreCore);
					pRecordedCharacter->SetCore(RecordedStart);
					pRecordedCharacter->m_Pos = RecordedStart.m_Pos;
					pRecordedCharacter->m_PrevPos = RecordedStart.m_Pos;
					pRecordedCharacter->m_PrevPrevPos = RecordedStart.m_Pos;
					pRecordedCharacter->SetActiveWeapon(NextFrame.m_PreWeapon);
					pRecordedCharacter->m_FreezeTime = NextFrame.m_PreFreezeTime;
					pRecordedCharacter->m_FrozenLastTick = NextFrame.m_PreFrozenLastTick != 0;
					UpdateMacroPracticeGhost(pRecordedCharacter);
				}
			}
		}
	}

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
	{
		GameClient()->Echo("Тренировочный тии потерян. Тренировка остановлена.");
		StopMacroPractice();
		return;
	}

	CNetObj_PlayerInput TickInput = LiveInput;
	TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	bool ReplayRecordedFrame = false;

	if(m_MacroPlaying)
	{
		if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
		{
			if(g_Config.m_ClMacroLoop && !m_vMacroFrames.empty())
			{
				const SMacroFrame &FirstFrame = m_vMacroFrames[0];
				if(FirstFrame.m_ClientId >= 0 && FirstFrame.m_ClientId != m_MacroPracticeLocalId)
					SwitchMacroPracticeCharacter(FirstFrame.m_ClientId);
				pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
				if(pCharacter && FirstFrame.m_HasCoordinates)
				{
					CCharacterCore FirstCore = pCharacter->GetCore();
					FirstCore.Read(&FirstFrame.m_PreCore);
					pCharacter->SetCore(FirstCore);
					pCharacter->m_Pos = FirstCore.m_Pos;
					pCharacter->m_PrevPos = FirstCore.m_Pos;
					pCharacter->m_PrevPrevPos = FirstCore.m_Pos;
					pCharacter->SetActiveWeapon(FirstFrame.m_PreWeapon);
					pCharacter->m_FreezeTime = FirstFrame.m_PreFreezeTime;
					pCharacter->m_FrozenLastTick = FirstFrame.m_PreFrozenLastTick != 0;
					m_MacroPracticeWorld.m_GameTick = std::max(0, FirstFrame.m_Tick - 1);
					m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
				}
				m_MacroPlaybackIndex = 0;
				m_MacroPracticeWaiting = false;
				if(pCharacter)
				{
					CNetObj_PlayerInput IdleInput{};
					IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
					IdleInput.m_TargetX = 1;
					pCharacter->SetInput(&IdleInput);
					UpdateMacroPracticeGhost(pCharacter);
				}
				return;
			}

			const bool StopOwnedPractice = m_MacroPlaybackOwnsPractice;
			m_MacroPlaying = false;
			m_MacroPlaybackOwnsPractice = false;
			m_MacroPracticeWaiting = false;
			CNetObj_PlayerInput IdleInput{};
			IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			IdleInput.m_TargetX = 1;
			pCharacter->SetInput(&IdleInput);
			GameClient()->Echo("Воспроизведение макроса завершено");
			if(StopOwnedPractice)
				StopMacroPractice();
			return;
		}

		const SMacroFrame &Frame = m_vMacroFrames[m_MacroPlaybackIndex];
		if(Frame.m_HasCoordinates)
		{
			const vec2 CurrentPos = pCharacter->Core()->m_Pos;
			const vec2 RequiredPos((float)Frame.m_PreCore.m_X, (float)Frame.m_PreCore.m_Y);
			// A recorded action is applied only when the practice tee reaches
			// the world coordinate where that action was originally performed.
			if(distance(CurrentPos, RequiredPos) > 24.0f)
			{
				// Let the player manually guide the local practice tee back to
				// the recorded coordinate. Do not advance or skip this macro tick.
				m_MacroPracticeWaiting = true;
			}
			else
			{
				m_MacroPracticeWaiting = false;
				if(g_Config.m_ClMacroTraining && !m_vMacroCorrectionFrames.empty())
				{
					const std::size_t CorrectionCount = m_vMacroCorrectionFrames.size();
					m_vMacroFrames.insert(m_vMacroFrames.begin() + static_cast<std::vector<SMacroFrame>::difference_type>(m_MacroPlaybackIndex),
						m_vMacroCorrectionFrames.begin(), m_vMacroCorrectionFrames.end());
					m_MacroPlaybackIndex += CorrectionCount;
					m_vMacroCorrectionFrames.clear();
					if(m_aLoadedMacroName[0] && SaveMacro(m_aLoadedMacroName))
						GameClient()->Echo("Макрос обучен: ручная коррекция сохранена для следующих запусков.");
					else
						GameClient()->Echo("Не удалось сохранить обученную коррекцию макроса.");
				}
				const SMacroFrame &ReadyFrame = m_vMacroFrames[m_MacroPlaybackIndex];
				TickInput = ReadyFrame.m_Input;
				TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
				ReplayRecordedFrame = true;
			}
		}
		else
		{
			m_MacroPracticeWaiting = false;
			TickInput = Frame.m_Input;
			TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			ReplayRecordedFrame = true;
		}
	}

	if(m_MacroPlaying && ReplayRecordedFrame)
	{
		// Movement/weapon actions stay deterministic, but the cursor remains free
		// so the player can aim left, right, up, and down while the macro runs.
		TickInput.m_TargetX = LiveInput.m_TargetX;
		TickInput.m_TargetY = LiveInput.m_TargetY;
	}

	if(!ReplayRecordedFrame)
	{
		// Direction presses shorter than the slowed practice interval must not
		// disappear between private-world ticks.
		if(TickInput.m_Direction == 0)
		{
			if(m_MacroPracticePendingDirectionLeft && !m_MacroPracticePendingDirectionRight)
				TickInput.m_Direction = -1;
			else if(!m_MacroPracticePendingDirectionLeft && m_MacroPracticePendingDirectionRight)
				TickInput.m_Direction = 1;
		}
		m_MacroPracticePendingDirectionLeft = false;
		m_MacroPracticePendingDirectionRight = false;

		if(m_MacroPracticePendingJump)
		{
			TickInput.m_Jump = 1;
			m_MacroPracticePendingJump = false;
		}
		if(m_MacroPracticePendingHook)
		{
			TickInput.m_Hook = 1;
			m_MacroPracticePendingHook = false;
		}
		if(m_MacroPracticePendingFireTransitions > 0)
		{
			m_MacroPracticeFireCounter = (m_MacroPracticeFireCounter + 1) & INPUT_STATE_MASK;
			--m_MacroPracticePendingFireTransitions;
		}
		TickInput.m_Fire = m_MacroPracticeFireCounter;

		// Preserve tap-style weapon binds between slow practice ticks.
		if(m_MacroPracticePendingWantedWeapon > 0)
		{
			TickInput.m_WantedWeapon = m_MacroPracticePendingWantedWeapon;
			m_MacroPracticePendingWantedWeapon = 0;
		}
		if(m_MacroPracticePendingNextWeaponTransitions > 0)
		{
			m_MacroPracticeInput.m_NextWeapon = (m_MacroPracticeInput.m_NextWeapon + 1) & INPUT_STATE_MASK;
			--m_MacroPracticePendingNextWeaponTransitions;
		}
		if(m_MacroPracticePendingPrevWeaponTransitions > 0)
		{
			m_MacroPracticeInput.m_PrevWeapon = (m_MacroPracticeInput.m_PrevWeapon + 1) & INPUT_STATE_MASK;
			--m_MacroPracticePendingPrevWeaponTransitions;
		}
		TickInput.m_NextWeapon = m_MacroPracticeInput.m_NextWeapon;
		TickInput.m_PrevWeapon = m_MacroPracticeInput.m_PrevWeapon;
		m_MacroPracticeInput.m_WantedWeapon = TickInput.m_WantedWeapon;
	}

	const bool LearnMacroCorrection = m_MacroPlaying && m_MacroPracticeWaiting && g_Config.m_ClMacroTraining;
	SMacroFrame NewFrame;
	if(m_MacroRecording || LearnMacroCorrection)
	{
		NewFrame.m_Input = TickInput;
		pCharacter->Core()->Write(&NewFrame.m_PreCore);
		NewFrame.m_PreWeapon = pCharacter->GetActiveWeapon();
		NewFrame.m_PreFreezeTime = pCharacter->m_FreezeTime;
		NewFrame.m_PreFrozenLastTick = pCharacter->m_FrozenLastTick ? 1 : 0;
		NewFrame.m_HasCoordinates = 1;
		NewFrame.m_ClientId = m_MacroPracticeLocalId;
	}

	// Match DDNet's normal prediction order: direct input handles weapon
	// presses/switches, predicted input updates movement, jump, hook and aim.
	pCharacter->OnDirectInput(&TickInput);
	pCharacter->OnPredictedInput(&TickInput);
	const int OtherLocalId = GameClient()->m_aLocalIds[!g_Config.m_ClDummy];
	CCharacter *pPracticeDummy = m_MacroPracticeWorld.GetCharacterById(OtherLocalId);
	if(!m_MacroPlaying && pPracticeDummy && pPracticeDummy != pCharacter &&
		(g_Config.m_ClDummyHammer || g_Config.m_ClDummyControl || g_Config.m_ClDummyCopyMoves))
	{
		CNetObj_PlayerInput DummyTickInput = DummyInput;
		DummyTickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		pPracticeDummy->m_IsMacroPracticeStatic = false;
		pPracticeDummy->OnDirectInput(&DummyTickInput);
		pPracticeDummy->OnPredictedInput(&DummyTickInput);
	}
	++m_MacroPracticeWorld.m_GameTick;
	m_MacroPracticeWorld.Tick();
	++m_MacroPracticeTick;

	pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
	{
		GameClient()->Echo("Тренировочный тии исчез");
		StopMacroPractice();
		return;
	}

	if(m_MacroRecording || LearnMacroCorrection)
	{
		pCharacter->Core()->Write(&NewFrame.m_Core);
		NewFrame.m_Weapon = pCharacter->GetActiveWeapon();
		NewFrame.m_AttackTick = pCharacter->GetAttackTick();
		NewFrame.m_Tick = m_MacroPracticeTick;
		NewFrame.m_FreezeTime = pCharacter->m_FreezeTime;
		NewFrame.m_FrozenLastTick = pCharacter->m_FrozenLastTick ? 1 : 0;
		static_cast<CNetObj_CharacterCore &>(NewFrame.m_Character) = NewFrame.m_Core;
		NewFrame.m_Character.m_Weapon = NewFrame.m_Weapon;
		NewFrame.m_Character.m_AttackTick = NewFrame.m_AttackTick;
		NewFrame.m_Character.m_Tick = NewFrame.m_Tick;
		if(m_MacroRecording)
		{
			m_vMacroFrames.push_back(NewFrame);
			if(m_vMacroFrames.size() >= MACRO_MAX_FRAMES)
			{
				GameClient()->Echo("Запись достигла ограничения в 18000 тиков. Макрос сохраняется.");
				StopMacroRecording();
				return;
			}
		}
		else
		{
			m_vMacroCorrectionFrames.push_back(NewFrame);
		}
	}
	else if(m_MacroPlaying && ReplayRecordedFrame)
	{
		const SMacroFrame &PlayedFrame = m_vMacroFrames[m_MacroPlaybackIndex];
		++m_MacroPlaybackIndex;
		if(PlayedFrame.m_HasCoordinates)
		{
			CCharacterCore RecordedCore = pCharacter->GetCore();
			RecordedCore.Read(&PlayedFrame.m_Core);
			pCharacter->SetCore(RecordedCore);
			pCharacter->m_Pos = RecordedCore.m_Pos;
			pCharacter->SetActiveWeapon(PlayedFrame.m_Weapon);
			pCharacter->m_FreezeTime = PlayedFrame.m_FreezeTime;
			pCharacter->m_FrozenLastTick = PlayedFrame.m_FrozenLastTick != 0;
			m_MacroPracticeWorld.m_GameTick = PlayedFrame.m_Tick;
			m_MacroPracticeTick = PlayedFrame.m_Tick;
		}
	}

	UpdateMacroPracticeGhost(pCharacter);
}

bool CControls::StartMacroRecording(const char *pName)
{
	if(m_MacroRecording)
	{
		GameClient()->Echo("Запись макроса уже идёт.");
		return false;
	}

	if(m_MacroPlaying || m_MacroPlaybackLive)
		StopMacroRecording();

	char aRecordingName[64]{};
	if(pName && pName[0])
	{
		str_copy(aRecordingName, pName);
	}
	else
	{
		std::vector<SMacroInfo> vExistingMacros;
		GetMacroList(vExistingMacros);
		for(unsigned Number = 1; Number < 100000; ++Number)
		{
			str_format(aRecordingName, sizeof(aRecordingName), "Macro_%03u", Number);
			const bool Exists = std::any_of(vExistingMacros.begin(), vExistingMacros.end(), [&](const SMacroInfo &Info) {
				return str_comp(Info.m_aName, aRecordingName) == 0;
			});
			if(!Exists)
				break;
			aRecordingName[0] = '\0';
		}
	}

	char aPath[128];
	if(!aRecordingName[0] || !MacroFilePath(aRecordingName, aPath, sizeof(aPath)))
	{
		GameClient()->Echo("Недопустимое название макроса. Используй буквы, цифры, пробел, дефис или подчёркивание.");
		return false;
	}

	if(!StartMacroPractice())
		return false;

	str_copy(m_aRecordingMacroName, aRecordingName);
	str_copy(m_aMacroMapName, "Неизвестная карта");
	const char *pMapName = Client()->MapDownloadName();
	if(pMapName && pMapName[0])
	{
		const char *pBaseName = pMapName;
		for(const char *p = pMapName; *p; ++p)
			if(*p == '/' || *p == '\\')
				pBaseName = p + 1;
		str_copy(m_aMacroMapName, pBaseName);
		for(char *p = m_aMacroMapName; *p; ++p)
		{
			if(*p == '.')
			{
				*p = '\0';
				break;
			}
		}
	}

	m_MacroPlaying = false;
	m_MacroPlaybackOwnsDummy = false;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = time_get();
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	m_vMacroFrames.clear();
	m_vMacroCorrectionFrames.clear();
	m_MacroRecording = true;

	char aMessage[128];
	str_format(aMessage, sizeof(aMessage), "Макрос «%s»: FP-тренировка началась. Скорость: %d тиков/с.",
		m_aRecordingMacroName, MacroPracticeRate());
	GameClient()->Echo(aMessage);
	return true;
}

void CControls::StopMacroRecording()
{
	const bool WasRecording = m_MacroRecording;
	const bool WasPlaying = m_MacroPlaying;
	const bool StopOwnedPractice = WasPlaying && m_MacroPlaybackOwnsPractice;

	if(WasRecording)
	{
		m_MacroRecording = false;
		char aName[64];
		str_copy(aName, m_aRecordingMacroName);
		if(m_vMacroFrames.empty())
		{
			GameClient()->Echo("Макрос пустой: нечего сохранять.");
		}
		else if(aName[0] && SaveMacro(aName))
		{
			char aMessage[160];
			str_format(aMessage, sizeof(aMessage), "Макрос «%s» сохранён автоматически (%d тиков).", aName, (int)m_vMacroFrames.size());
			GameClient()->Echo(aMessage);
		}
		else
		{
			char aMessage[128];
			str_format(aMessage, sizeof(aMessage), "Не удалось автоматически сохранить макрос «%s».", aName);
			GameClient()->Echo(aMessage);
		}
		m_aRecordingMacroName[0] = '\0';
	}

	if(WasPlaying)
		GameClient()->Echo("Воспроизведение макроса остановлено");

	if(m_MacroPlaybackLive)
	{
		const int Dummy = g_Config.m_ClDummy;
		m_aInputData[Dummy].m_Direction = 0;
		m_aInputData[Dummy].m_Jump = 0;
		m_aInputData[Dummy].m_Hook = 0;
		if(m_aInputData[Dummy].m_Fire & 1)
			m_aInputData[Dummy].m_Fire++;
	}

	m_MacroPlaying = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackOwnsPractice = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroRewindHeld = false;
	m_MacroRewindHeldSince = 0;
	m_MacroRewindNextRepeat = 0;
	m_MacroSlowHeld = false;
	m_MacroPlaybackSlowTickCounter = 0;
	if(WasRecording)
		m_MacroPlaybackIndex = m_vMacroFrames.size();
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;

	// Finishing a recording or stopping playback also ends the private FP
	// world immediately; the live server tee is already receiving neutral input.
	if(m_MacroPracticeActive && (WasRecording || WasPlaying || StopOwnedPractice))
		StopMacroPractice();
}

bool CControls::SaveMacro(const char *pName)
{
	char aPath[128];
	if(m_MacroRecording || m_vMacroFrames.empty() || !MacroFilePath(pName, aPath, sizeof(aPath)))
		return false;
	Storage()->CreateFolder("macros", IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const uint32_t Magic = MACRO_FILE_MAGIC;
	const uint32_t Version = MACRO_FILE_VERSION;
	const uint32_t Count = (uint32_t)m_vMacroFrames.size();
	const bool Written = io_write(File, &Magic, sizeof(Magic)) == sizeof(Magic) &&
		io_write(File, &Version, sizeof(Version)) == sizeof(Version) &&
		io_write(File, &Count, sizeof(Count)) == sizeof(Count) &&
		io_write(File, m_aMacroMapName, sizeof(m_aMacroMapName)) == sizeof(m_aMacroMapName) &&
		io_write(File, m_vMacroFrames.data(), Count * sizeof(SMacroFrame)) == Count * sizeof(SMacroFrame);
	io_close(File);
	return Written;
}

bool CControls::PlayMacro(const char *pName)
{
	char aResolvedName[64]{};
	if(!pName || !pName[0])
	{
		std::vector<SMacroInfo> vMacros;
		GetMacroList(vMacros);
		if(vMacros.empty())
			return false;
		str_copy(aResolvedName, vMacros.front().m_aName);
		pName = aResolvedName;
	}
	char aPath[128];
	if(!MacroFilePath(pName, aPath, sizeof(aPath)))
	{
		GameClient()->Echo("Недопустимое имя макроса. Используй буквы, цифры, пробел, дефис или подчёркивание.");
		return false;
	}
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		char aMessage[160];
		str_format(aMessage, sizeof(aMessage), "Файл macros/%s.dmacro не найден в папке сохранений.", pName);
		GameClient()->Echo(aMessage);
		return false;
	}

	// io_length() seeks to end and resets the stream to the beginning.
	// Capture the full size before consuming the header so it cannot reset
	// io_tell() and make the already-read 76-byte header count as payload.
	const int64_t FileLength = io_length(File);
	uint32_t Magic = 0;
	uint32_t Version = 0;
	uint32_t Count = 0;
	bool Valid = FileLength >= 0 &&
		io_read(File, &Magic, sizeof(Magic)) == sizeof(Magic) &&
		io_read(File, &Version, sizeof(Version)) == sizeof(Version) &&
		io_read(File, &Count, sizeof(Count)) == sizeof(Count) &&
		Magic == MACRO_FILE_MAGIC && Version >= 1 && Version <= MACRO_FILE_VERSION &&
		Count > 0 && Count <= MACRO_MAX_FRAMES;

	char aRecordedMapName[64]{};
	if(Valid && Version >= 3)
		Valid = io_read(File, aRecordedMapName, sizeof(aRecordedMapName)) == sizeof(aRecordedMapName);

	const int64_t HeaderBytes = (int64_t)sizeof(Magic) + sizeof(Version) + sizeof(Count) +
		(Version >= 3 ? (int64_t)sizeof(aRecordedMapName) : 0);
	const int64_t RemainingBytes = Valid && FileLength >= HeaderBytes ? FileLength - HeaderBytes : 0;
	if(Valid && FileLength < HeaderBytes)
		Valid = false;
	const int64_t CurrentFrameBytes = (int64_t)Count * sizeof(SMacroFrame);
	const int64_t Version5FrameBytes = (int64_t)Count * sizeof(SMacroFrameV4);
	const int64_t Version4FrameBytes = (int64_t)Count * sizeof(SMacroFrameV4);
	const int64_t Version3FrameBytes = (int64_t)Count * sizeof(SMacroFrameV3);
	const int64_t LegacyFrameBytes = (int64_t)Count * sizeof(SMacroFrameV1);
	std::vector<SMacroFrame> Frames;
	auto CopyVersion5Frames = [&](const std::vector<SMacroFrameV4> &OldFrames) {
		Frames.resize(Count);
		for(uint32_t i = 0; i < Count; ++i)
		{
			Frames[i].m_Input = OldFrames[i].m_Input;
			Frames[i].m_PreCore = OldFrames[i].m_PreCore;
			Frames[i].m_Core = OldFrames[i].m_Core;
			Frames[i].m_Character = OldFrames[i].m_Character;
			Frames[i].m_PreWeapon = OldFrames[i].m_PreWeapon;
			Frames[i].m_Weapon = OldFrames[i].m_Weapon;
			Frames[i].m_AttackTick = OldFrames[i].m_AttackTick;
			Frames[i].m_Tick = OldFrames[i].m_Tick;
			Frames[i].m_HasCoordinates = OldFrames[i].m_HasCoordinates;
			Frames[i].m_ClientId = OldFrames[i].m_ClientId;
		}
	};
	if(Valid && Version >= 6)
	{
		Frames.resize(Count);
		Valid = RemainingBytes == CurrentFrameBytes &&
			io_read(File, Frames.data(), CurrentFrameBytes) == CurrentFrameBytes;
	}
	else if(Valid && Version == 5)
	{
		// Some early Version 5 builds wrote the old frame size, while later
		// builds appended freeze fields without changing the file version.
		// Detect both layouts by the exact payload length and migrate the old one.
		if(RemainingBytes == CurrentFrameBytes)
		{
			Frames.resize(Count);
			Valid = io_read(File, Frames.data(), CurrentFrameBytes) == CurrentFrameBytes;
		}
		else if(RemainingBytes == Version5FrameBytes)
		{
			std::vector<SMacroFrameV4> OldFrames(Count);
			Valid = io_read(File, OldFrames.data(), Version5FrameBytes) == Version5FrameBytes;
			if(Valid)
				CopyVersion5Frames(OldFrames);
		}
		else
			Valid = false;
	}
	else if(Valid && Version == 4)
	{
		std::vector<SMacroFrameV4> OldFrames(Count);
		Valid = RemainingBytes == Version4FrameBytes &&
			io_read(File, OldFrames.data(), Version4FrameBytes) == Version4FrameBytes;
		if(Valid)
			CopyVersion5Frames(OldFrames);
	}
	else if(Valid && (Version >= 2 || (Version == 1 && RemainingBytes == Version3FrameBytes)))
	{
		std::vector<SMacroFrameV3> OldFrames(Count);
		Valid = RemainingBytes == Version3FrameBytes &&
			io_read(File, OldFrames.data(), Version3FrameBytes) == Version3FrameBytes;
		if(Valid)
		{
			Frames.resize(Count);
			for(uint32_t i = 0; i < Count; ++i)
			{
				Frames[i].m_Input = OldFrames[i].m_Input;
				Frames[i].m_PreCore = OldFrames[i].m_PreCore;
				Frames[i].m_Core = OldFrames[i].m_Core;
				Frames[i].m_Character = OldFrames[i].m_Character;
				Frames[i].m_PreWeapon = OldFrames[i].m_PreWeapon;
				Frames[i].m_Weapon = OldFrames[i].m_Weapon;
				Frames[i].m_AttackTick = OldFrames[i].m_AttackTick;
				Frames[i].m_Tick = OldFrames[i].m_Tick;
				Frames[i].m_HasCoordinates = OldFrames[i].m_HasCoordinates;
				Frames[i].m_ClientId = -1;
			}
		}
	}
	else if(Valid && Version == 1 && RemainingBytes == LegacyFrameBytes)
	{
		std::vector<SMacroFrameV1> OldFrames(Count);
		Valid = io_read(File, OldFrames.data(), LegacyFrameBytes) == LegacyFrameBytes;
		if(Valid)
		{
			Frames.resize(Count);
			for(uint32_t i = 0; i < Count; ++i)
			{
				Frames[i].m_Input.m_Direction = OldFrames[i].m_Direction;
				Frames[i].m_Input.m_Jump = OldFrames[i].m_Jump;
				Frames[i].m_Input.m_Fire = OldFrames[i].m_Fire;
				Frames[i].m_Input.m_Hook = OldFrames[i].m_Hook;
				Frames[i].m_Input.m_WantedWeapon = OldFrames[i].m_WantedWeapon;
				Frames[i].m_Input.m_NextWeapon = OldFrames[i].m_NextWeapon;
				Frames[i].m_Input.m_PrevWeapon = OldFrames[i].m_PrevWeapon;
				Frames[i].m_Input.m_TargetX = OldFrames[i].m_TargetX;
				Frames[i].m_Input.m_TargetY = OldFrames[i].m_TargetY;
				Frames[i].m_Tick = (int)i + 1;
				Frames[i].m_HasCoordinates = 0;
				Frames[i].m_ClientId = -1;
			}
		}
	}
	else
		Valid = false;
	io_close(File);
	if(!Valid)
	{
		char aMessage[192];
		str_format(aMessage, sizeof(aMessage),
			"Файл макроса «%s» имеет несовместимый формат (версия %u, кадров %u, осталось %lld байт; размеры кадров: %lld или %lld байт).",
			pName, Version, Count, (long long)RemainingBytes,
			(long long)CurrentFrameBytes, (long long)Version5FrameBytes);
		GameClient()->Echo(aMessage);
		return false;
	}
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("Для запуска макроса сначала подключись к серверу.");
		return false;
	}
	if(GameClient()->m_Snap.m_LocalClientId < 0 || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		GameClient()->Echo("Сервер ещё не прислал локального тии. Подожди появления персонажа и запусти макрос снова.");
		return false;
	}
	if(m_MacroRecording)
	{
		GameClient()->Echo("Сначала останови текущую запись макроса.");
		return false;
	}
	if(m_MacroPlaying)
		StopMacroRecording();
	if(!m_MacroPracticeActive && !StartMacroPractice())
		return false;

	m_MacroPlaybackOwnsDummy = true;
	if(Frames[0].m_ClientId >= 0 && !SwitchMacroPracticeCharacter(Frames[0].m_ClientId))
	{
		char aMessage[128];
		str_format(aMessage, sizeof(aMessage), "Не найден тренировочный тии из первого кадра записи (ID %d). Проверь подключение дамми.", Frames[0].m_ClientId);
		GameClient()->Echo(aMessage);
		StopMacroPractice();
		return false;
	}
	CCharacter *pPracticeCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pPracticeCharacter)
	{
		GameClient()->Echo("Не удалось найти тренировочного тии для воспроизведения.");
		StopMacroPractice();
		return false;
	}

	m_vMacroFrames = std::move(Frames);
	m_vMacroCorrectionFrames.clear();
	str_copy(m_aLoadedMacroName, pName);
	// Playback always owns the practice tee, including older files that do not store client IDs.
	m_MacroPlaybackOwnsDummy = true;
	m_MacroRecording = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackOwnsPractice = true;
	m_MacroPlaying = true;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroPlaybackSlowTickCounter = 0;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = time_get();

	// Restore the first pre-input state in the local-only practice world.
	// Nothing is teleported or replayed on the server-controlled tee.
	if(m_vMacroFrames[0].m_HasCoordinates)
	{
		const SMacroFrame &FirstFrame = m_vMacroFrames[0];
		CCharacterCore Core = pPracticeCharacter->GetCore();
		Core.Read(&FirstFrame.m_PreCore);
		pPracticeCharacter->SetCore(Core);
		pPracticeCharacter->m_Pos = Core.m_Pos;
		pPracticeCharacter->m_PrevPos = Core.m_Pos;
		pPracticeCharacter->m_PrevPrevPos = Core.m_Pos;
		pPracticeCharacter->SetActiveWeapon(FirstFrame.m_PreWeapon);
		pPracticeCharacter->m_FreezeTime = FirstFrame.m_PreFreezeTime;
		pPracticeCharacter->m_FrozenLastTick = FirstFrame.m_PreFrozenLastTick != 0;
		pPracticeCharacter->m_IsLocal = true;
		m_MacroPracticeWorld.m_GameTick = std::max(0, FirstFrame.m_Tick - 1);
		m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	}

	// Macro playback is intentionally map-agnostic. The saved map label is
	// informational only; it must not block a local playback attempt.

	UpdateMacroPracticeGhost(pPracticeCharacter);
	GameClient()->Echo("Макрос запущен в отдельном тренировочном мире. Настоящий тии остаётся неподвижным.");
	return true;
}
bool CControls::DeleteMacro(const char *pName)
{
	char aPath[128];
	return MacroFilePath(pName, aPath, sizeof(aPath)) && Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
}

void CControls::ConMacroRecord(IConsole::IResult *pResult, void *pUserData)
{
	((CControls *)pUserData)->StartMacroRecording();
}

void CControls::ConMacroStop(IConsole::IResult *pResult, void *pUserData)
{
	((CControls *)pUserData)->StopMacroRecording();
}

void CControls::ConMacroSave(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->SaveMacro(pResult->GetString(0)))
		pSelf->GameClient()->Echo("Не удалось сохранить макрос. Сначала остановите запись и введите допустимое название.");
	else
		pSelf->GameClient()->Echo("Макрос сохранён");
}

void CControls::ConMacroPlay(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->PlayMacro(pResult->GetString(0)))
		pSelf->GameClient()->Echo("Не удалось запустить макрос. Проверьте название и файл.");
}

void CControls::ConMacroDelete(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->DeleteMacro(pResult->GetString(0)))
		pSelf->GameClient()->Echo("Не удалось удалить макрос. Проверьте название.");
	else
		pSelf->GameClient()->Echo("Макрос удалён");
}

void CControls::ConMacroRewind(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(Pressed)
	{
		if(!pSelf->m_MacroRewindHeld)
		{
			pSelf->m_MacroRewindHeld = true;
			pSelf->m_MacroRewindHeldSince = time_get();
			pSelf->m_MacroRewindNextRepeat = pSelf->m_MacroRewindHeldSince + time_freq() * 3 / 10;
			pSelf->SeekMacro(-pSelf->MacroSeekStep());
		}
	}
	else
	{
		pSelf->m_MacroRewindHeld = false;
		pSelf->m_MacroRewindHeldSince = 0;
		pSelf->m_MacroRewindNextRepeat = 0;
	}
}

void CControls::ConMacroSeek(IConsole::IResult *pResult, void *pUserData)
{
	if(pResult->GetInteger(0) == 0)
		return;

	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->GameClient()->m_Chat.IsActive() || pSelf->GameClient()->m_Menus.IsActive())
		return;

	pSelf->SeekMacro(pSelf->MacroSeekStep(), true, true);
}

void CControls::ConMacroSlow(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	pSelf->m_MacroSlowHeld = pResult->GetInteger(0) != 0;
	if(!pSelf->m_MacroSlowHeld)
		pSelf->m_MacroPlaybackSlowTickCounter = 0;
}

int CControls::MacroSeekStep() const
{
	return g_Config.m_ClMacroRewindStep;
}

void CControls::SetMacroSeekStep(int Step)
{
	g_Config.m_ClMacroRewindStep = std::clamp(Step, 1, 18000);
}

void CControls::OnMacroPracticeDummySwap()
{
	if(!m_MacroPracticeActive || m_MacroPlaying)
		return;
	m_MacroPlaybackOwnsDummy = false;
	const int RequestedLocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(RequestedLocalId >= 0 && RequestedLocalId < MAX_CLIENTS)
		SwitchMacroPracticeCharacter(RequestedLocalId);
}

vec2 CControls::MacroPracticePosition()
{
	if(m_MacroPracticeActive)
	{
		const CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
		if(pCharacter)
			return pCharacter->Core()->m_Pos;
	}
	return GameClient()->m_LocalCharacterPos;
}

int CControls::MacroPracticeRate() const
{
	return g_Config.m_ClMacroPracticeRate;
}

void CControls::SetMacroPracticeRate(int Rate)
{
	g_Config.m_ClMacroPracticeRate = std::clamp(Rate, 1, (int)SERVER_TICK_SPEED);
}

bool CControls::SeekMacro(int Offset, bool ShowMessage, bool TeleportLive)
{
	if((!m_MacroPracticeActive && !m_MacroPlaybackLive) || m_MacroRecording || m_vMacroFrames.empty())
		return false;

	const int Current = std::clamp(m_MacroHasCurrentFrame ? (int)m_MacroPlaybackIndex - 1 : (int)m_MacroPlaybackIndex,
		0, (int)m_vMacroFrames.size() - 1);
	const int Target = std::clamp(Current + Offset, 0, (int)m_vMacroFrames.size() - 1);
	if(m_MacroPlaybackLive)
	{
		if(TeleportLive)
		{
			const SMacroFrame &Frame = m_vMacroFrames[Target];
			if(Frame.m_HasCoordinates)
			{
				const int Conn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
				SendPracticeTeleport(vec2((float)Frame.m_PreCore.m_X, (float)Frame.m_PreCore.m_Y), Conn);
			}
			else if(ShowMessage)
				GameClient()->Echo("Эта старая запись не содержит координат для телепортации.");
		}
		m_MacroPlaybackIndex = (std::size_t)Target;
		m_MacroHasCurrentFrame = false;
		m_MacroPlaybackLastTick = -1;
		m_MacroPlaybackSlowTickCounter = 0;
		m_MacroPracticeWaiting = false;
	}
	else if(!RestoreMacroPracticeFrame(Target))
		return false;

	if(ShowMessage)
	{
		char aMessage[128];
		str_format(aMessage, sizeof(aMessage), "Позиция макроса: тик %d / %d (шаг %d тиков)",
			Target + 1, (int)m_vMacroFrames.size(), MacroSeekStep());
		GameClient()->Echo(aMessage);
	}
	return true;
}

bool CControls::OnInput(const IInput::CEvent &Event)
{
	// Arrow keys remain available to user binds during macro playback.
	if(m_MacroPlaying)
		return false;
	if((!m_MacroPracticeActive && !m_MacroPlaybackLive) || m_MacroRecording || m_vMacroFrames.empty() ||
		GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() ||
		!(Event.m_Flags & IInput::FLAG_PRESS))
		return false;

	if(Event.m_Key == KEY_LEFT)
		return SeekMacro(-MacroSeekStep());
	if(Event.m_Key == KEY_RIGHT)
		return SeekMacro(MacroSeekStep());
	return false;
}
void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons)
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = pMsg->m_Weapon + 1;
		// We don't really know ammo count, until we'll switch to that weapon, but any non-zero count will suffice here
		m_aAmmoCount[std::max(0, pMsg->m_Weapon % NUM_WEAPONS)] = 10;
	}
}

void CControls::ApplyHookAutomation(int Dummy)
{
	const CNetObj_Character *pLocal = GameClient()->m_Snap.m_pLocalCharacter;
	const CCharacter *pPracticeLocal = m_MacroPracticeActive ? m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId) : nullptr;
	const bool ManualHook = m_aManualHook[Dummy];
	if(!pLocal && !pPracticeLocal)
	{
		m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
		m_aAutoHookActive[Dummy] = false;
		m_aAutoAvoidActive[Dummy] = false;
		m_aAutoHookFireActive[Dummy] = false;
		return;
	}

	const CCharacterCore &LocalCore = pPracticeLocal ? *pPracticeLocal->Core() : GameClient()->m_PredictedChar;
	const vec2 LocalPos = pPracticeLocal ? LocalCore.m_Pos : vec2((float)pLocal->m_X, (float)pLocal->m_Y);
	const vec2 LocalVel = pPracticeLocal ? LocalCore.m_Vel : vec2(pLocal->m_VelX / 256.0f, pLocal->m_VelY / 256.0f);
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const int DummyId = GameClient()->m_aLocalIds[!g_Config.m_ClDummy];
	const bool PracticeActive = pPracticeLocal != nullptr;

	bool FoundTarget = false;
	vec2 BestTarget(0.0f, 0.0f);

	// Manual hook always has priority. During macro practice the same
	// automation works from the private tee's position and mirrored teammates.
	if((g_Config.m_ClAutoHook || g_Config.m_ClAutoTriplefly) && !ManualHook)
	{
		const float Range = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		const float RangeSquared = Range * Range;
		float aBestDistances[3] = {RangeSquared + 1.0f, RangeSquared + 1.0f, RangeSquared + 1.0f};
		vec2 aBestTargets[3] = {};
		const vec2 ManualAim = m_aMousePos[Dummy];
		const float AimLength = length(ManualAim);
		const float AimAngle = std::clamp((float)g_Config.m_ClAutoHookAngle, 10.0f, 360.0f);

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			// Triplefly is a three-tee setup: controlled tee, local dummy,
			// then the third tee. Do not accidentally hook the dummy instead.
			if(ClientId == LocalId || (g_Config.m_ClAutoTriplefly && ClientId == DummyId) ||
				!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				continue;
			if(GameClient()->IsOtherTeam(ClientId) || GameClient()->m_aClients[ClientId].m_Solo ||
				GameClient()->m_aClients[ClientId].m_HookHitDisabled)
				continue;

			const CCharacter *pPracticeTarget = PracticeActive ? m_MacroPracticeWorld.GetCharacterById(ClientId) : nullptr;
			if(PracticeActive && !pPracticeTarget)
				continue;

			const vec2 TargetPos = pPracticeTarget ? pPracticeTarget->Core()->m_Pos :
				vec2((float)GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X, (float)GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
			const vec2 Delta = TargetPos - LocalPos;
			const float DistanceSquared = dot(Delta, Delta);
			if(DistanceSquared < 1.0f || DistanceSquared > RangeSquared)
				continue;

			if(AimLength > 1.0f && AimAngle < 359.9f)
			{
				const float AimDot = dot(ManualAim, Delta) / (AimLength * std::sqrt(DistanceSquared));
				const float MinimumAimDot = std::cos(AimAngle * 0.5f * pi / 180.0f);
				if(AimDot < MinimumAimDot)
					continue;
			}

			vec2 CollisionPos;
			vec2 BeforeCollision;
			if(Collision()->IntersectLineTeleHook(LocalPos, TargetPos, &CollisionPos, &BeforeCollision) != 0)
				continue;

			for(int Rank = 0; Rank < 3; ++Rank)
			{
				if(DistanceSquared < aBestDistances[Rank])
				{
					for(int Shift = 2; Shift > Rank; --Shift)
					{
						aBestDistances[Shift] = aBestDistances[Shift - 1];
						aBestTargets[Shift] = aBestTargets[Shift - 1];
					}
					aBestDistances[Rank] = DistanceSquared;
					aBestTargets[Rank] = Delta;
					break;
				}
			}
		}

		if(aBestDistances[0] <= RangeSquared)
		{
			BestTarget = aBestTargets[0];
			FoundTarget = true;
			m_aInputData[Dummy].m_TargetX = round_to_int(BestTarget.x);
			m_aInputData[Dummy].m_TargetY = round_to_int(BestTarget.y);
			m_aInputData[Dummy].m_Hook = 1;
			m_aAutoHookActive[Dummy] = true;
		}
		else
		{
			m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
			m_aAutoHookActive[Dummy] = false;
		}
	}
	else
	{
		m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
		m_aAutoHookActive[Dummy] = false;
	}

	int AutoWeapon = -1;
	// Optional weapon fire is aimed at the same selected tee. Use the private
	// tee's weapon state while practicing, never the stationary server tee.
	if(AutoWeapon < 0 && FoundTarget && !ManualHook)
	{
		const int aWeapons[] = {WEAPON_LASER, WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_GUN};
		const bool aEnabled[] = {
			g_Config.m_ClAutoHookWeaponLaser != 0,
			g_Config.m_ClAutoHookWeaponShotgun != 0,
			g_Config.m_ClAutoHookWeaponGrenade != 0,
			g_Config.m_ClAutoHookWeaponGun != 0};
		for(int i = 0; i < 4; ++i)
		{
			const int Weapon = aWeapons[i];
			if(aEnabled[i] && LocalCore.m_aWeapons[Weapon].m_Got &&
				(GameClient()->m_GameInfo.m_UnlimitedAmmo || LocalCore.m_aWeapons[Weapon].m_Ammo != 0))
			{
				AutoWeapon = Weapon;
				break;
			}
		}
	}
	if(AutoWeapon >= 0)
	{
		m_aInputData[Dummy].m_WantedWeapon = AutoWeapon + 1;
		if(!(m_aInputData[Dummy].m_Fire & 1))
			m_aInputData[Dummy].m_Fire++;
		m_aAutoHookFireActive[Dummy] = true;
	}
	else if(m_aAutoHookFireActive[Dummy])
	{
		if(m_aInputData[Dummy].m_Fire & 1)
			m_aInputData[Dummy].m_Fire++;
		m_aInputData[Dummy].m_WantedWeapon = 0;
		m_aAutoHookFireActive[Dummy] = false;
	}

	if(m_aAutoAvoidActive[Dummy])
	{
		if(!m_aAutoHookActive[Dummy])
			m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
		m_aAutoAvoidActive[Dummy] = false;
	}
	if(!FoundTarget && !ManualHook && g_Config.m_ClAutoAvoid)
	{
		const float VelX = LocalVel.x;
		const float VelY = LocalVel.y;
		vec2 Candidates[2];
		int NumCandidates = 0;
		if(VelY < -2.0f)
			Candidates[NumCandidates++] = vec2(0.0f, 1.0f);
		else if(VelY > 2.0f)
		{
			const float Side = VelX >= 0.0f ? -1.0f : 1.0f;
			Candidates[NumCandidates++] = normalize(vec2(Side, -0.30f));
			Candidates[NumCandidates++] = normalize(vec2(-Side, -0.30f));
		}

		float BestLength = 1000000.0f;
		vec2 BestDirection(0.0f, 0.0f);
		for(int i = 0; i < NumCandidates; ++i)
		{
			vec2 CollisionPos;
			vec2 BeforeCollision;
			const int Hit = Collision()->IntersectLineTeleHook(LocalPos, LocalPos + Candidates[i] * 260.0f, &CollisionPos, &BeforeCollision);
			if(Hit == 0 || Hit == TILE_NOHOOK)
				continue;
			const vec2 Aim = BeforeCollision - LocalPos;
			const float AimLength = length(Aim);
			if(AimLength < 32.0f || AimLength >= BestLength)
				continue;
			BestLength = AimLength;
			BestDirection = Aim;
		}

		if(BestLength < 1000000.0f)
		{
			m_aInputData[Dummy].m_TargetX = round_to_int(BestDirection.x);
			m_aInputData[Dummy].m_TargetY = round_to_int(BestDirection.y);
			m_aInputData[Dummy].m_Hook = 1;
			m_aAutoAvoidActive[Dummy] = true;
		}
	}
}


void CControls::ApplyEdgeAssist(int Dummy)
{
	if(!g_Config.m_ClAutoEdge || !m_aManualHook[Dummy] || m_aAutoHookActive[Dummy] || m_aAutoAvoidActive[Dummy])
		return;

	const CNetObj_Character *pLocal = GameClient()->m_Snap.m_pLocalCharacter;
	const CCharacter *pPracticeLocal = m_MacroPracticeActive ? m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId) : nullptr;
	if(!pLocal && !pPracticeLocal)
		return;

	const vec2 LocalPos = pPracticeLocal ? pPracticeLocal->Core()->m_Pos :
		vec2((float)pLocal->m_X, (float)pLocal->m_Y);
	const vec2 ManualAim((float)m_aInputData[Dummy].m_TargetX, (float)m_aInputData[Dummy].m_TargetY);
	if(length(ManualAim) < 1.0f)
		return;

	const float BaseAngle = std::atan2(ManualAim.y, ManualAim.x);
	float BestScore = 1000000.0f;
	vec2 BestAim(0.0f, 0.0f);
	bool Found = false;

	// Scan a narrow fan around the requested angle. The point immediately
	// before the first hookable collision gives a stable edge/corner pixel.
	for(int Offset = -8; Offset <= 8; ++Offset)
	{
		const float Angle = BaseAngle + Offset * (pi / 180.0f);
		const vec2 Direction(std::cos(Angle), std::sin(Angle));
		vec2 CollisionPos;
		vec2 BeforeCollision;
		const int Hit = Collision()->IntersectLineTeleHook(LocalPos, LocalPos + Direction * 360.0f, &CollisionPos, &BeforeCollision);
		if(Hit == 0 || Hit == TILE_NOHOOK)
			continue;

		const vec2 Candidate = BeforeCollision - LocalPos;
		const float CandidateLength = length(Candidate);
		if(CandidateLength < 12.0f || CandidateLength > 360.0f)
			continue;
		const float Score = std::abs((float)Offset) * 1000.0f + CandidateLength;
		if(Score < BestScore)
		{
			BestScore = Score;
			BestAim = Candidate;
			Found = true;
		}
	}

	if(Found)
	{
		m_aInputData[Dummy].m_TargetX = round_to_int(BestAim.x);
		m_aInputData[Dummy].m_TargetY = round_to_int(BestAim.y);
	}
}

void CControls::ApplyEdgeHook(int Dummy)
{
	// Release last tick's automatic edge hook before deciding whether the
	// current movement still needs it. This prevents a stale hook from sticking.
	if(m_aAutoEdgeHookActive[Dummy])
	{
		if(!m_aManualHook[Dummy] && !m_aAutoHookActive[Dummy] && !m_aAutoAvoidActive[Dummy])
			m_aInputData[Dummy].m_Hook = 0;
		m_aAutoEdgeHookActive[Dummy] = false;
	}

	const CNetObj_Character *pLocal = GameClient()->m_Snap.m_pLocalCharacter;
	const CCharacter *pPracticeLocal = m_MacroPracticeActive ? m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId) : nullptr;
	if(!g_Config.m_ClAutoEdgeHook || (!pLocal && !pPracticeLocal) ||
		m_aManualHook[Dummy] || m_aAutoHookActive[Dummy] || m_aAutoAvoidActive[Dummy])
		return;

	const int Direction = m_aInputData[Dummy].m_Direction;
	if(Direction == 0)
	{
		m_aAutoEdgeHookActive[Dummy] = false;
		return;
	}

	const vec2 LocalPos = pPracticeLocal ? pPracticeLocal->Core()->m_Pos :
		vec2((float)pLocal->m_X, (float)pLocal->m_Y);
	const bool Grounded = Collision()->IsOnGround(LocalPos, 14.0f);
	const bool GroundAhead = Collision()->CheckPoint(LocalPos.x + Direction * 20.0f, LocalPos.y + 18.0f);
	if(!Grounded || GroundAhead)
	{
		m_aAutoEdgeHookActive[Dummy] = false;
		return;
	}

	// Search from straight ahead to the lower side of the ledge. This is a
	// hook-only rescue: it never presses jump and never changes movement input.
	float BestScore = 1000000.0f;
	vec2 BestAim(0.0f, 0.0f);
	bool Found = false;
	for(int Degrees = -15; Degrees <= 85; Degrees += 5)
	{
		const float Angle = Degrees * pi / 180.0f;
		const vec2 DirectionVector(Direction * std::cos(Angle), std::sin(Angle));
		vec2 CollisionPos;
		vec2 BeforeCollision;
		const int Hit = Collision()->IntersectLineTeleHook(
			LocalPos, LocalPos + DirectionVector * 300.0f, &CollisionPos, &BeforeCollision);
		if(Hit == 0 || Hit == TILE_NOHOOK)
			continue;

		const vec2 Candidate = BeforeCollision - LocalPos;
		const float CandidateLength = length(Candidate);
		if(CandidateLength < 20.0f || CandidateLength > 300.0f)
			continue;

		const float Score = CandidateLength + std::abs((float)Degrees - 35.0f) * 0.8f;
		if(Score < BestScore)
		{
			BestScore = Score;
			BestAim = Candidate;
			Found = true;
		}
	}

	if(Found)
	{
		m_aInputData[Dummy].m_TargetX = round_to_int(BestAim.x);
		m_aInputData[Dummy].m_TargetY = round_to_int(BestAim.y);
		m_aInputData[Dummy].m_Hook = 1;
		m_aAutoEdgeHookActive[Dummy] = true;
	}
	else
	{
		m_aAutoEdgeHookActive[Dummy] = false;
	}
}
int CControls::SnapInput(int *pData)
{
	// update player state
	if(GameClient()->m_Chat.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_CHATTING;
	else if(GameClient()->m_Menus.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_IN_MENU;
	else
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_PLAYING;

	if(GameClient()->m_Scoreboard.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SCOREBOARD;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Controls.m_aShowHookColl[g_Config.m_ClDummy])
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_AIM;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Camera.CamType() == CCamera::CAMTYPE_SPEC)
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SPEC_CAM;

	switch(m_aMouseInputType[g_Config.m_ClDummy])
	{
	case CControls::EMouseInputType::AUTOMATED:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE;
		break;
	case CControls::EMouseInputType::ABSOLUTE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE | PLAYERFLAG_INPUT_MANUAL;
		break;
	case CControls::EMouseInputType::RELATIVE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_MANUAL;
		break;
	}

	// Isolated macro practice: inputs are consumed by a cloned prediction world.
	// Only neutral input is ever sent to the real server character.
	if(m_MacroPracticeActive)
	{
		// Refresh remote tee copies from the active snapshot. Their physics
		// then continues independently inside the private practice world.
		PopulateMacroPracticeWorld();

		const int Dummy = g_Config.m_ClDummy;

		// Build the same live input that the normal client path would use.
		// Run automatic assistance before copying it into the private world,
		// so Auto Hook / Unfreeze / Edge Hook targets are not overwritten by
		// the raw mouse position afterwards.
		m_aInputData[Dummy].m_PlayerFlags = PLAYERFLAG_PLAYING;
		m_aInputData[Dummy].m_TargetX = (int)m_aMousePos[Dummy].x;
		m_aInputData[Dummy].m_TargetY = (int)m_aMousePos[Dummy].y;
		m_aInputData[Dummy].m_Direction = 0;
		if(m_aInputDirectionLeft[Dummy] && !m_aInputDirectionRight[Dummy])
			m_aInputData[Dummy].m_Direction = -1;
		if(!m_aInputDirectionLeft[Dummy] && m_aInputDirectionRight[Dummy])
			m_aInputData[Dummy].m_Direction = 1;

		if(!m_MacroPlaying)
		{
			ApplyHookAutomation(Dummy);
			ApplyEdgeAssist(Dummy);
			ApplyEdgeHook(Dummy);
		}

		CNetObj_PlayerInput PracticeInput = m_aInputData[Dummy];
		PracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		// Direct weapon binds are edge-triggered. Keep the captured value in
		// PracticeInput and its pending queue, then clear the live staging field
		// so a short tap cannot stick across multiple slowed practice ticks.
		if(!m_MacroPlaying)
			m_aInputData[Dummy].m_WantedWeapon = 0;
		CNetObj_PlayerInput PracticeDummyInput = GameClient()->m_DummyInput;
		PracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		if(g_Config.m_ClDummyHammer)
		{
			if(GameClient()->m_DummyFire % 25 == 0)
			{
				++GameClient()->m_DummyFire;
				GameClient()->m_HammerInput.m_Fire = (GameClient()->m_HammerInput.m_Fire + 1) | 1;
				GameClient()->m_HammerInput.m_WantedWeapon = WEAPON_HAMMER + 1;
				if(!g_Config.m_ClDummyRestoreWeapon)
					GameClient()->m_DummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;
				if(const CCharacter *pPracticeDummy = m_MacroPracticeWorld.GetCharacterById(GameClient()->m_aLocalIds[!Dummy]))
				{
					if(const CCharacter *pPracticeLocal = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId))
					{
						const vec2 Dir = pPracticeLocal->Core()->m_Pos - pPracticeDummy->Core()->m_Pos;
						GameClient()->m_HammerInput.m_TargetX = round_to_int(Dir.x);
						GameClient()->m_HammerInput.m_TargetY = round_to_int(Dir.y);
					}
				}
			}
			else
				++GameClient()->m_DummyFire;
			PracticeDummyInput = GameClient()->m_HammerInput;
			PracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		}
		else if(g_Config.m_ClDummyCopyMoves)
		{
			PracticeDummyInput.m_Direction = PracticeInput.m_Direction;
			PracticeDummyInput.m_Hook = PracticeInput.m_Hook;
			PracticeDummyInput.m_Jump = PracticeInput.m_Jump;
			PracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			PracticeDummyInput.m_TargetX = PracticeInput.m_TargetX;
			PracticeDummyInput.m_TargetY = PracticeInput.m_TargetY;
			PracticeDummyInput.m_WantedWeapon = PracticeInput.m_WantedWeapon;
			PracticeDummyInput.m_Fire = (PracticeDummyInput.m_Fire + PracticeInput.m_Fire -
				m_aLastData[Dummy].m_Fire) & INPUT_STATE_MASK;
		}
		else if(g_Config.m_ClDummyControl)
		{
			PracticeDummyInput.m_Jump = g_Config.m_ClDummyJump;
			if(g_Config.m_ClDummyFire)
				PracticeDummyInput.m_Fire = g_Config.m_ClDummyFire;
			else if(PracticeDummyInput.m_Fire & 1)
				++PracticeDummyInput.m_Fire;
			PracticeDummyInput.m_Hook = g_Config.m_ClDummyHook;
		}

		if(m_aInputData[Dummy].m_PlayerFlags & PLAYERFLAG_PLAYING)
		{
			const int64_t CurrentTime = time_get();
			if(m_MacroPracticeLastTime == 0)
				m_MacroPracticeLastTime = CurrentTime;
			const int64_t ElapsedTime = std::max<int64_t>(0, CurrentTime - m_MacroPracticeLastTime);
			m_MacroPracticeLastTime = CurrentTime;
			m_MacroPracticeAccumulator += (float)ElapsedTime / (float)time_freq() * MacroPracticeRate();
			int Steps = 0;
			while(m_MacroPracticeAccumulator >= 1.0f && Steps < SERVER_TICK_SPEED)
			{
				SimulateMacroPracticeTick(PracticeInput, PracticeDummyInput);
				m_MacroPracticeAccumulator -= 1.0f;
				++Steps;
				if(!m_MacroPracticeActive)
					break;
			}
		}

		CNetObj_PlayerInput ServerInput{};
		// The real server-side tee is an idle observer only. Never forward
		// the practice copy's fire or weapon inputs to the live connection.
		ServerInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		ServerInput.m_TargetX = 1;
		ServerInput.m_TargetY = 0;
		ServerInput.m_Fire = 0;
		ServerInput.m_WantedWeapon = 0;
		ServerInput.m_NextWeapon = 0;
		ServerInput.m_PrevWeapon = 0;

		const bool SendNeutral = ServerInput.m_PlayerFlags != m_aLastData[Dummy].m_PlayerFlags ||
			ServerInput.m_TargetX != m_aLastData[Dummy].m_TargetX ||
			ServerInput.m_TargetY != m_aLastData[Dummy].m_TargetY ||
			ServerInput.m_Fire != m_aLastData[Dummy].m_Fire ||
			time_get() > m_LastSendTime + time_freq() / 25;
		m_aLastData[Dummy] = ServerInput;
		if(!SendNeutral)
			return 0;
		m_LastSendTime = time_get();
		mem_copy(pData, &ServerInput, sizeof(ServerInput));
		return sizeof(ServerInput);
	}

	bool Send = m_aLastData[g_Config.m_ClDummy].m_PlayerFlags != m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	m_aLastData[g_Config.m_ClDummy].m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	// we freeze the input if chat or menu is activated
	if(!(m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING))
	{
		if(!GameClient()->m_GameInfo.m_BugDDRaceInput)
			ResetInput(g_Config.m_ClDummy);

		mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

		// set the target anyway though so that we can keep seeing our surroundings,
		// even if chat or menu are activated
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePos[g_Config.m_ClDummy].x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePos[g_Config.m_ClDummy].y;

		// send once a second just to be sure
		Send = Send || time_get() > m_LastSendTime + time_freq();
	}
	else
	{
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePos[g_Config.m_ClDummy].x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePos[g_Config.m_ClDummy].y;

		if(g_Config.m_ClSubTickAiming && m_aMousePosOnAction[g_Config.m_ClDummy] != vec2(0.0f, 0.0f))
		{
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePosOnAction[g_Config.m_ClDummy].x;
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePosOnAction[g_Config.m_ClDummy].y;
			m_aMousePosOnAction[g_Config.m_ClDummy] = vec2(0.0f, 0.0f);
		}

		if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
		{
			m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;
			m_aMousePos[g_Config.m_ClDummy].x = 1;
		}

		// set direction
		m_aInputData[g_Config.m_ClDummy].m_Direction = 0;
		if(m_aInputDirectionLeft[g_Config.m_ClDummy] && !m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = -1;
		if(!m_aInputDirectionLeft[g_Config.m_ClDummy] && m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = 1;

		// Saved macros run on the real server-controlled tee. The first frame
		// waits for the recorded starting position; after that, one frame is
		// selected per predicted game tick and sent through the regular input path.
		bool MacroLiveInput = false;
		if(m_MacroPlaybackLive && m_MacroPlaying &&
			(m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING))
		{
			const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
			if(!GameClient()->m_Snap.m_pLocalCharacter)
			{
				StopMacroRecording();
			}
			else if(!m_MacroHasCurrentFrame || PredTick != m_MacroPlaybackLastTick)
			{
				bool AdvanceFrame = !m_MacroHasCurrentFrame;
				if(m_MacroHasCurrentFrame)
				{
					if(m_MacroSlowHeld)
					{
						++m_MacroPlaybackSlowTickCounter;
					if(m_MacroPlaybackSlowTickCounter >= std::clamp(g_Config.m_ClMacroSlowFactor, 2, 10))
						AdvanceFrame = true;
					}
					else
						AdvanceFrame = true;
				}
				m_MacroPlaybackLastTick = PredTick;

				if(AdvanceFrame)
				{
					m_MacroPlaybackSlowTickCounter = 0;
					if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
					{
						StopMacroRecording();
					}
					else
					{
						const SMacroFrame &Frame = m_vMacroFrames[m_MacroPlaybackIndex];
						bool AtStartPosition = true;
						if(!m_MacroHasCurrentFrame && m_MacroPlaybackIndex == 0 && Frame.m_HasCoordinates)
						{
							const vec2 CurrentPos = GameClient()->m_PredictedChar.m_Pos;
							const vec2 RequiredPos((float)Frame.m_PreCore.m_X, (float)Frame.m_PreCore.m_Y);
						AtStartPosition = distance(CurrentPos, RequiredPos) <= 24.0f;
						}
						m_MacroPracticeWaiting = !AtStartPosition;
						if(AtStartPosition)
						{
							m_MacroCurrentFrame = Frame;
							m_MacroHasCurrentFrame = true;
							++m_MacroPlaybackIndex;
						}
					}
				}
			}

			if(m_MacroPlaybackLive && m_MacroPlaying && m_MacroHasCurrentFrame && !m_MacroPracticeWaiting)
			{
				const int PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;
				m_aInputData[g_Config.m_ClDummy] = m_MacroCurrentFrame.m_Input;
				m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PlayerFlags;
				m_aInputData[g_Config.m_ClDummy].m_Fire = (m_aInputData[g_Config.m_ClDummy].m_Fire + m_MacroLiveFireOffset) & INPUT_STATE_MASK;
				m_aInputData[g_Config.m_ClDummy].m_NextWeapon = (m_aInputData[g_Config.m_ClDummy].m_NextWeapon + m_MacroLiveNextWeaponOffset) & INPUT_STATE_MASK;
				m_aInputData[g_Config.m_ClDummy].m_PrevWeapon = (m_aInputData[g_Config.m_ClDummy].m_PrevWeapon + m_MacroLivePrevWeaponOffset) & INPUT_STATE_MASK;
				m_aMousePos[g_Config.m_ClDummy] = vec2(
					(float)m_aInputData[g_Config.m_ClDummy].m_TargetX,
					(float)m_aInputData[g_Config.m_ClDummy].m_TargetY);
				MacroLiveInput = true;
			}
		}

		if(!MacroLiveInput)
		{
			ApplyHookAutomation(g_Config.m_ClDummy);
			ApplyEdgeAssist(g_Config.m_ClDummy);
			ApplyEdgeHook(g_Config.m_ClDummy);

		}

		// dummy copy moves
		if(g_Config.m_ClDummyCopyMoves && !m_MacroPlaybackLive)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;

			// Don't copy any input to dummy when spectating others
			if(!GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
			{
				pDummyInput->m_Direction = m_aInputData[g_Config.m_ClDummy].m_Direction;
				pDummyInput->m_Hook = m_aInputData[g_Config.m_ClDummy].m_Hook;
				pDummyInput->m_Jump = m_aInputData[g_Config.m_ClDummy].m_Jump;
				pDummyInput->m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;
				pDummyInput->m_TargetX = m_aInputData[g_Config.m_ClDummy].m_TargetX;
				pDummyInput->m_TargetY = m_aInputData[g_Config.m_ClDummy].m_TargetY;
				pDummyInput->m_WantedWeapon = m_aInputData[g_Config.m_ClDummy].m_WantedWeapon;

				if(!g_Config.m_ClDummyControl)
					pDummyInput->m_Fire += m_aInputData[g_Config.m_ClDummy].m_Fire - m_aLastData[g_Config.m_ClDummy].m_Fire;

				pDummyInput->m_NextWeapon += m_aInputData[g_Config.m_ClDummy].m_NextWeapon - m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
				pDummyInput->m_PrevWeapon += m_aInputData[g_Config.m_ClDummy].m_PrevWeapon - m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
			}

			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		if(g_Config.m_ClDummyControl && !m_MacroPlaybackLive)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;
			pDummyInput->m_Jump = g_Config.m_ClDummyJump;

			if(g_Config.m_ClDummyFire)
				pDummyInput->m_Fire = g_Config.m_ClDummyFire;
			else if((pDummyInput->m_Fire & 1) != 0)
				pDummyInput->m_Fire++;

			pDummyInput->m_Hook = g_Config.m_ClDummyHook;
			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		// stress testing
		if(g_Config.m_DbgStress)
		{
			float t = Client()->LocalTime();
			mem_zero(&m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

			m_aInputData[g_Config.m_ClDummy].m_Direction = ((int)t / 2) & 1;
			m_aInputData[g_Config.m_ClDummy].m_Jump = ((int)t);
			m_aInputData[g_Config.m_ClDummy].m_Fire = ((int)(t * 10));
			m_aInputData[g_Config.m_ClDummy].m_Hook = ((int)(t * 2)) & 1;
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = ((int)t) % NUM_WEAPONS;
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)(std::sin(t * 3) * 100.0f);
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)(std::cos(t * 3) * 100.0f);
		}


		// check if we need to send input
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Direction != m_aLastData[g_Config.m_ClDummy].m_Direction;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Jump != m_aLastData[g_Config.m_ClDummy].m_Jump;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Fire != m_aLastData[g_Config.m_ClDummy].m_Fire;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Hook != m_aLastData[g_Config.m_ClDummy].m_Hook;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_WantedWeapon != m_aLastData[g_Config.m_ClDummy].m_WantedWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_NextWeapon != m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_PrevWeapon != m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
		Send = Send || time_get() > m_LastSendTime + time_freq() / 25; // send at least 25 Hz
		Send = Send || (GameClient()->m_Snap.m_pLocalCharacter && GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_NINJA && (m_aInputData[g_Config.m_ClDummy].m_Direction || m_aInputData[g_Config.m_ClDummy].m_Jump || m_aInputData[g_Config.m_ClDummy].m_Hook));
	}

	// copy and return size
	m_aLastData[g_Config.m_ClDummy] = m_aInputData[g_Config.m_ClDummy];

	if(!Send)
		return 0;

	m_LastSendTime = time_get();
	mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));
	return sizeof(m_aInputData[0]);
}

void CControls::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_MacroRewindHeld && !m_MacroRecording && !m_vMacroFrames.empty() &&
		(m_MacroPracticeActive || m_MacroPlaybackLive) &&
		!GameClient()->m_Chat.IsActive() && !GameClient()->m_Menus.IsActive())
	{
		const int64_t Now = time_get();
		if(Now >= m_MacroRewindNextRepeat)
		{
			SeekMacro(-MacroSeekStep(), false);
			const int64_t HeldFor = Now - m_MacroRewindHeldSince;
			const int64_t RepeatDelay = HeldFor >= time_freq() ? time_freq() / 16 :
				HeldFor >= time_freq() / 2 ? time_freq() / 8 : time_freq() / 4;
			m_MacroRewindNextRepeat = Now + RepeatDelay;
		}
	}

	if(g_Config.m_ClAutoswitchWeaponsOutOfAmmo && !GameClient()->m_GameInfo.m_UnlimitedAmmo && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Keep track of ammo count, we know weapon ammo only when we switch to that weapon, this is tracked on server and protocol does not track that
		m_aAmmoCount[std::max(0, GameClient()->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS)] = GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount;
		// Autoswitch weapon if we're out of ammo
		if(m_aInputData[g_Config.m_ClDummy].m_Fire % 2 != 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount == 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_HAMMER &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_NINJA)
		{
			int Weapon;
			for(Weapon = WEAPON_LASER; Weapon > WEAPON_GUN; Weapon--)
			{
				if(Weapon == GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
					continue;
				if(m_aAmmoCount[Weapon] > 0)
					break;
			}
			if(Weapon != GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
				m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = Weapon + 1;
		}
	}

	// The camera and cursor follow the practice tee while the real server tee
	// remains translucent at its original position.
	const vec2 LocalCharacterPos = m_MacroPracticeActive ? MacroPracticePosition() : GameClient()->m_LocalCharacterPos;
	if(GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// make sure to compensate for smooth dyncam to ensure the cursor stays still in world space if zoomed
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy];
		float Zoom = GameClient()->m_Camera.m_Zoom;
		m_aTargetPos[g_Config.m_ClDummy] = LocalCharacterPos + m_aMousePos[g_Config.m_ClDummy] - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aMousePos[g_Config.m_ClDummy];
	}
	else
	{
		m_aTargetPos[g_Config.m_ClDummy] = m_aMousePos[g_Config.m_ClDummy];
	}

	if(g_Config.m_ClAutoHookShowRadius && (GameClient()->m_Snap.m_pLocalCharacter || m_MacroPracticeActive))
	{
		const float Radius = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		const float ConeAngle = std::clamp((float)g_Config.m_ClAutoHookAngle, 10.0f, 360.0f);
		const float AimAngle = std::atan2(m_aMousePos[g_Config.m_ClDummy].y, m_aMousePos[g_Config.m_ClDummy].x);
		const float HalfAngle = ConeAngle * 0.5f * pi / 180.0f;
		const int DotCount = std::max(2, round_to_int(72.0f * ConeAngle / 360.0f));
		const vec2 Center = m_MacroPracticeActive ? MacroPracticePosition() :
			vec2((float)GameClient()->m_Snap.m_pLocalCharacter->m_X, (float)GameClient()->m_Snap.m_pLocalCharacter->m_Y);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.25f, 0.80f, 1.0f, 0.65f);
		for(int i = 0; i < 72; ++i)
		{
			const float Angle = (2.0f * pi * i) / 72.0f;
			IGraphics::CQuadItem Dot(Center.x + std::cos(Angle) * Radius - 1.5f,
				Center.y + std::sin(Angle) * Radius - 1.5f, 3.0f, 3.0f);
			Graphics()->QuadsDrawTL(&Dot, 1);
		}
		// Draw a second, thinner arc for the current search cone.
		if(ConeAngle < 359.9f)
		{
			for(int i = 0; i < DotCount; ++i)
			{
				const float Angle = AimAngle - HalfAngle + (2.0f * HalfAngle * i) / (DotCount - 1);
				IGraphics::CQuadItem Dot(Center.x + std::cos(Angle) * Radius - 0.8f,
					Center.y + std::sin(Angle) * Radius - 0.8f, 1.6f, 1.6f);
				Graphics()->QuadsDrawTL(&Dot, 1);
			}
		}

		// Draw the two edges of the circular aiming sector, making the
		// independent radius and angle limits visible at the same time.
		if(ConeAngle < 359.9f)
		{
			for(int Edge = 0; Edge < 2; ++Edge)
			{
				const float Angle = AimAngle + (Edge == 0 ? -HalfAngle : HalfAngle);
				for(int Step = 1; Step < 12; ++Step)
				{
					const float Distance = Radius * Step / 12.0f;
					IGraphics::CQuadItem Dot(Center.x + std::cos(Angle) * Distance - 1.0f,
						Center.y + std::sin(Angle) * Distance - 1.0f, 2.0f, 2.0f);
					Graphics()->QuadsDrawTL(&Dot, 1);
				}
			}
		}
		Graphics()->QuadsEnd();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(m_MacroPracticeActive)
	{
		const CScreenRect PreviousScreen = Graphics()->GetScreen();
		const float ScreenWidth = Graphics()->ScreenWidth();
		const float ScreenHeight = Graphics()->ScreenHeight();
		Graphics()->MapScreenToSize(ScreenWidth, ScreenHeight);

		char aTickLabel[128];
		if(m_MacroRecording)
			str_format(aTickLabel, sizeof(aTickLabel), "МАКРОС  |  ТИК %d  |  %d ТИК = 1С",
				(int)m_vMacroFrames.size(), SERVER_TICK_SPEED);
		else if(m_MacroPlaying)
			str_format(aTickLabel, sizeof(aTickLabel), "МАКРОС  |  ТИК %d / %d  |  %d ТИК = 1С",
				(int)m_MacroPlaybackIndex, (int)m_vMacroFrames.size(), SERVER_TICK_SPEED);
		else
			str_format(aTickLabel, sizeof(aTickLabel), "FP-ТРЕНИРОВКА  |  ТИК %d", m_MacroPracticeTick);

		const float BoxWidth = std::min(640.0f, ScreenWidth - 32.0f);
		const float BoxHeight = 92.0f;
		const float BoxX = (ScreenWidth - BoxWidth) * 0.5f;
		const float BoxY = 76.0f;
		Graphics()->DrawRect(BoxX, BoxY, BoxWidth, BoxHeight,
			ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(BoxX + 4.0f, BoxY + 4.0f, BoxWidth - 8.0f, BoxHeight - 8.0f,
			ColorRGBA(0.42f, 0.42f, 0.42f, 0.88f), IGraphics::CORNER_ALL, 2.0f);

		const char *pClientName = "КЛИЕНТ 1";
		const float ClientFontSize = 15.0f;
		const float ClientWidth = TextRender()->TextWidth(ClientFontSize, pClientName);
		TextRender()->TextColor(0.55f, 0.9f, 1.0f, 1.0f);
		TextRender()->Text((ScreenWidth - ClientWidth) * 0.5f, BoxY + 10.0f, ClientFontSize, pClientName);

		const float FontSize = ScreenWidth < 700.0f ? 22.0f : 30.0f;
		const float Width = TextRender()->TextWidth(FontSize, aTickLabel);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text((ScreenWidth - Width) * 0.5f, BoxY + 42.0f, FontSize, aTickLabel);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Graphics()->MapScreen(PreviousScreen);
	}
}

bool CControls::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(GameClient()->IsWorldPaused())
		return false;

	if(CursorType == IInput::CURSOR_JOYSTICK && g_Config.m_InpControllerAbsolute && GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 AbsoluteDirection;
		if(Input()->GetActiveJoystick()->Absolute(&AbsoluteDirection.x, &AbsoluteDirection.y))
		{
			m_aMousePos[g_Config.m_ClDummy] = AbsoluteDirection * GetMaxMouseDistance();
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::ABSOLUTE;
		}
		return true;
	}

	float Factor = 1.0f;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
	{
		Factor = g_Config.m_ClDyncamMousesens / 100.0f;
	}
	else
	{
		switch(CursorType)
		{
		case IInput::CURSOR_MOUSE:
			Factor = g_Config.m_InpMousesens / 100.0f;
			break;
		case IInput::CURSOR_JOYSTICK:
			Factor = g_Config.m_InpControllerSens / 100.0f;
			break;
		default:
			dbg_assert_failed("CControls::OnCursorMove CursorType %d", (int)CursorType);
		}
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		Factor *= GameClient()->m_Camera.m_Zoom;

	m_aMousePos[g_Config.m_ClDummy] += vec2(x, y) * Factor;
	GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::RELATIVE;
	ClampMousePos();
	return true;
}

void CControls::ClampMousePos()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
	{
		m_aMousePos[g_Config.m_ClDummy].x = std::clamp(m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (Collision()->GetWidth() + 201.0f) * 32.0f);
		m_aMousePos[g_Config.m_ClDummy].y = std::clamp(m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (Collision()->GetHeight() + 201.0f) * 32.0f);
	}
	else
	{
		const float MouseMin = GetMinMouseDistance();
		const float MouseMax = GetMaxMouseDistance();

		float MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance < 0.001f)
		{
			m_aMousePos[g_Config.m_ClDummy].x = 0.001f;
			m_aMousePos[g_Config.m_ClDummy].y = 0;
			MouseDistance = 0.001f;
		}
		if(MouseDistance < MouseMin)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMin;
		MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance > MouseMax)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMax;
	}
}

float CControls::GetMinMouseDistance() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamMinDistance : g_Config.m_ClMouseMinDistance;
}

float CControls::GetMaxMouseDistance() const
{
	float CameraMaxDistance = 200.0f;
	float FollowFactor = (g_Config.m_ClDyncam ? g_Config.m_ClDyncamFollowFactor : g_Config.m_ClMouseFollowfactor) / 100.0f;
	float DeadZone = g_Config.m_ClDyncam ? g_Config.m_ClDyncamDeadzone : g_Config.m_ClMouseDeadzone;
	float MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
	return std::min(FollowFactor != 0.0f ? CameraMaxDistance / FollowFactor + DeadZone : MaxDistance, MaxDistance);
}
