/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

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
#include <cmath>
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
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_vMacroFrames.clear();
	std::fill(std::begin(m_aManualHook), std::end(m_aManualHook), false);
	std::fill(std::begin(m_aAutoHookActive), std::end(m_aAutoHookActive), false);
	std::fill(std::begin(m_aAutoHookFireActive), std::end(m_aAutoHookFireActive), false);
	std::fill(std::begin(m_aAutoAvoidActive), std::end(m_aAutoAvoidActive), false);
	std::fill(std::begin(m_aAutoEdgeJumpActive), std::end(m_aAutoEdgeJumpActive), false);
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
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	*pState->m_apVariables[g_Config.m_ClDummy] = Stroke;
	if(pState->m_IsHookAction)
		pState->m_pControls->m_aManualHook[g_Config.m_ClDummy] = Stroke != 0;

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
	}
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	ConKeyInputCounter(pResult, pSet);
	pSet->m_pControls->m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = 0;
}

void CControls::OnConsoleInit()
{
	// game commands
	{
		static CInputState s_State = {this, {&m_aInputDirectionLeft[0], &m_aInputDirectionLeft[1]}, false};
		Console()->Register("+left", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move left");
	}
	{
		static CInputState s_State = {this, {&m_aInputDirectionRight[0], &m_aInputDirectionRight[1]}, false};
		Console()->Register("+right", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move right");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Jump, &m_aInputData[1].m_Jump}, false};
		Console()->Register("+jump", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Jump");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Hook, &m_aInputData[1].m_Hook}, true};
		s_State.m_IsHookAction = true;
		Console()->Register("+hook", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Hook");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Fire, &m_aInputData[1].m_Fire}, true};
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
}

void CControls::ConTpToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN);
}

void CControls::ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
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

void CControls::SendPracticeTeleportToCursor(int Conn)
{
	const vec2 CursorPos = CursorWorldPos();
	CNetMsg_Cl_PracticeTeleport Msg;
	Msg.m_X = round_to_int(CursorPos.x);
	Msg.m_Y = round_to_int(CursorPos.y);
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

namespace
{
	constexpr uint32_t MACRO_FILE_MAGIC = 0x444D4143;
	constexpr uint32_t MACRO_FILE_VERSION = 2;
	constexpr uint32_t MACRO_MAX_FRAMES = 18000;
}

bool CControls::MacroFilePath(const char *pName, char *pBuffer, unsigned BufferSize) const
{
	if(!pName || !pName[0])
		return false;
	char aSafeName[64];
	int Out = 0;
	for(int i = 0; pName[i] && Out < (int)sizeof(aSafeName) - 1; ++i)
	{
		const unsigned char Ch = (unsigned char)pName[i];
		if((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '_' || Ch == '-')
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

bool CControls::StartMacroPractice()
{
	if(m_MacroPracticeActive)
		return true;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(Client()->State() != IClient::STATE_ONLINE || LocalId < 0 || LocalId >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_pLocalCharacter ||
		!GameClient()->m_PredictedWorld.GetCharacterById(LocalId))
	{
		GameClient()->Echo("Macro practice requires an online local tee with prediction available");
		return false;
	}

	m_MacroPracticeWorld.CopyWorld(&GameClient()->m_PredictedWorld);
	if(!m_MacroPracticeWorld.GetCharacterById(LocalId))
	{
		m_MacroPracticeWorld.Clear();
		GameClient()->Echo("Could not create the isolated macro practice world");
		return false;
	}

	m_MacroPracticeLocalId = LocalId;
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeActive = true;
	m_MacroPracticeHasRenderChar = false;

	CCharacter *pPracticeChar = m_MacroPracticeWorld.GetCharacterById(LocalId);
	UpdateMacroPracticeGhost(pPracticeChar);
	GameClient()->Echo("Macro practice started. Your server tee is now idle; the practice tee is local only.");
	return true;
}

void CControls::StopMacroPractice()
{
	m_MacroRecording = false;
	m_MacroPlaying = false;
	m_MacroPracticeActive = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLocalId = -1;
	m_MacroPracticeHasRenderChar = false;
	m_MacroPlaybackIndex = 0;
	m_MacroPracticeWorld.Clear();
	if(GameClient())
		GameClient()->m_Ghost.ClearMacroPracticeCharacter();
}

void CControls::UpdateMacroPracticeGhost(CCharacter *pCharacter)
{
	if(!pCharacter)
		return;

	CNetObj_Character Current{};
	pCharacter->Core()->Write(&Current);
	Current.m_Weapon = pCharacter->GetActiveWeapon();
	Current.m_AttackTick = pCharacter->GetAttackTick();
	Current.m_Tick = m_MacroPracticeWorld.GameTick();

	CNetObj_Character Prev = m_MacroPracticeHasRenderChar ? m_MacroPracticeRenderChar : Current;
	GameClient()->m_Ghost.SetMacroPracticeCharacter(Prev, Current);
	m_MacroPracticeRenderChar = Current;
	m_MacroPracticeHasRenderChar = true;
}

bool CControls::RestoreMacroPracticeFrame(int Index)
{
	if(!m_MacroPracticeActive || Index < 0 || Index >= (int)m_vMacroFrames.size())
		return false;

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
		return false;

	const SMacroFrame &Frame = m_vMacroFrames[Index];
	if(Frame.m_HasCoordinates)
	{
		CCharacterCore Core = pCharacter->GetCore();
		Core.Read(&Frame.m_Core);
		pCharacter->SetCore(Core);
		pCharacter->SetActiveWeapon(Frame.m_Weapon);
		m_MacroPracticeWorld.m_GameTick = Frame.m_Tick;
		m_MacroPracticeTick = Frame.m_Tick;

		CNetObj_Character Current = Frame.m_Character;
		Current.m_AttackTick = Frame.m_AttackTick;
		CNetObj_Character Prev = Index > 0 ? m_vMacroFrames[Index - 1].m_Character : Current;
		GameClient()->m_Ghost.SetMacroPracticeCharacter(Prev, Current);
		m_MacroPracticeRenderChar = Current;
		m_MacroPracticeHasRenderChar = true;
	}
	m_MacroPlaybackIndex = (std::size_t)Index + 1;
	m_MacroPracticeWaiting = false;
	return true;
}

void CControls::SimulateMacroPracticeTick(const CNetObj_PlayerInput &LiveInput)
{
	if(!m_MacroPracticeActive)
		return;

	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
	{
		GameClient()->Echo("Macro practice lost its local tee and has stopped");
		StopMacroPractice();
		return;
	}

	CNetObj_PlayerInput TickInput = LiveInput;
	TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

	if(m_MacroPlaying)
	{
		if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
		{
			m_MacroPlaying = false;
			m_MacroPracticeWaiting = false;
			GameClient()->Echo("Macro playback finished");
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
				m_MacroPracticeWaiting = true;
				return;
			}
		}
		m_MacroPracticeWaiting = false;
		TickInput = Frame.m_Input;
		TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	}

	SMacroFrame NewFrame;
	if(m_MacroRecording)
	{
		NewFrame.m_Input = TickInput;
		pCharacter->Core()->Write(&NewFrame.m_PreCore);
		NewFrame.m_PreWeapon = pCharacter->GetActiveWeapon();
		NewFrame.m_HasCoordinates = 1;
	}

	pCharacter->OnPredictedInput(&TickInput);
	++m_MacroPracticeWorld.m_GameTick;
	m_MacroPracticeWorld.Tick();
	++m_MacroPracticeTick;

	pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
	{
		GameClient()->Echo("Macro practice tee disappeared");
		StopMacroPractice();
		return;
	}

	if(m_MacroRecording)
	{
		pCharacter->Core()->Write(&NewFrame.m_Core);
		NewFrame.m_Weapon = pCharacter->GetActiveWeapon();
		NewFrame.m_AttackTick = pCharacter->GetAttackTick();
		NewFrame.m_Tick = m_MacroPracticeTick;
		NewFrame.m_Core.Write(&NewFrame.m_Character);
		NewFrame.m_Character.m_Weapon = NewFrame.m_Weapon;
		NewFrame.m_Character.m_AttackTick = NewFrame.m_AttackTick;
		NewFrame.m_Character.m_Tick = NewFrame.m_Tick;
		m_vMacroFrames.push_back(NewFrame);
		if(m_vMacroFrames.size() >= MACRO_MAX_FRAMES)
		{
			m_MacroRecording = false;
			GameClient()->Echo("Macro recording reached the 18000-tick limit");
		}
	}
	else if(m_MacroPlaying)
	{
		++m_MacroPlaybackIndex;
	}

	UpdateMacroPracticeGhost(pCharacter);
}

void CControls::StartMacroRecording()
{
	if(!StartMacroPractice())
		return;

	m_MacroPlaying = false;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	m_vMacroFrames.clear();
	m_MacroRecording = true;
	GameClient()->Echo("Macro recording started in the isolated practice world");
}

void CControls::StopMacroRecording()
{
	if(m_MacroRecording)
	{
		m_MacroRecording = false;
		char aBuf[112];
		str_format(aBuf, sizeof(aBuf), "Macro recording stopped (%d practice ticks)", (int)m_vMacroFrames.size());
		GameClient()->Echo(aBuf);
	}
	if(m_MacroPlaying)
		GameClient()->Echo("Macro playback stopped");
	m_MacroPlaying = false;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;
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
		io_write(File, m_vMacroFrames.data(), Count * sizeof(SMacroFrame)) == Count * sizeof(SMacroFrame);
	io_close(File);
	return Written;
}

bool CControls::PlayMacro(const char *pName)
{
	char aPath[128];
	if(!MacroFilePath(pName, aPath, sizeof(aPath)))
		return false;
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	uint32_t Magic = 0;
	uint32_t Version = 0;
	uint32_t Count = 0;
	bool Valid = io_read(File, &Magic, sizeof(Magic)) == sizeof(Magic) &&
		io_read(File, &Version, sizeof(Version)) == sizeof(Version) &&
		io_read(File, &Count, sizeof(Count)) == sizeof(Count) &&
		Magic == MACRO_FILE_MAGIC && (Version == 1 || Version == MACRO_FILE_VERSION) &&
		Count > 0 && Count <= MACRO_MAX_FRAMES;

	std::vector<SMacroFrame> Frames;
	if(Valid && Version == MACRO_FILE_VERSION)
	{
		Frames.resize(Count);
		Valid = io_read(File, Frames.data(), Count * sizeof(SMacroFrame)) == Count * sizeof(SMacroFrame);
	}
	else if(Valid && Version == 1)
	{
		std::vector<SMacroFrameV1> OldFrames(Count);
		Valid = io_read(File, OldFrames.data(), Count * sizeof(SMacroFrameV1)) == Count * sizeof(SMacroFrameV1);
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
			}
		}
	}
	io_close(File);
	if(!Valid || !StartMacroPractice())
		return false;

	// Reset only the local practice tee. The server tee remains idle.
	m_MacroPracticeWorld.CopyWorld(&GameClient()->m_PredictedWorld);
	CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
	if(!pCharacter)
		return false;
	if(Frames[0].m_HasCoordinates)
	{
		CCharacterCore Core = pCharacter->GetCore();
		Core.Read(&Frames[0].m_PreCore);
		pCharacter->SetCore(Core);
		pCharacter->SetActiveWeapon(Frames[0].m_PreWeapon);
		m_MacroPracticeWorld.m_GameTick = Frames[0].m_Tick - 1;
		m_MacroPracticeTick = Frames[0].m_Tick - 1;
	}

	m_vMacroFrames = std::move(Frames);
	m_MacroRecording = false;
	m_MacroPlaybackIndex = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPlaying = true;
	UpdateMacroPracticeGhost(pCharacter);
	GameClient()->Echo("Macro playback started in the isolated practice world");
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
		pSelf->GameClient()->Echo("Could not save macro. Stop recording first and enter a valid name.");
	else
		pSelf->GameClient()->Echo("Macro saved");
}

void CControls::ConMacroPlay(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->PlayMacro(pResult->GetString(0)))
		pSelf->GameClient()->Echo("Could not play macro. Check its name and file.");
}

void CControls::ConMacroDelete(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->DeleteMacro(pResult->GetString(0)))
		pSelf->GameClient()->Echo("Could not delete macro. Check its name.");
	else
		pSelf->GameClient()->Echo("Macro deleted");
}

int CControls::MacroSeekStep() const
{
	return g_Config.m_ClMacroRewindStep;
}

void CControls::SetMacroSeekStep(int Step)
{
	g_Config.m_ClMacroRewindStep = std::clamp(Step, 1, 18000);
}

int CControls::MacroPracticeRate() const
{
	return g_Config.m_ClMacroPracticeRate;
}

void CControls::SetMacroPracticeRate(int Rate)
{
	g_Config.m_ClMacroPracticeRate = std::clamp(Rate, 1, SERVER_TICK_SPEED);
}

bool CControls::SeekMacro(int Offset)
{
	if(!m_MacroPlaying || m_vMacroFrames.empty())
		return false;

	const int Current = std::clamp((int)m_MacroPlaybackIndex - 1, 0, (int)m_vMacroFrames.size() - 1);
	const int Target = std::clamp(Current + Offset, 0, (int)m_vMacroFrames.size() - 1);
	if(!RestoreMacroPracticeFrame(Target))
		return false;

	char aMessage[128];
	str_format(aMessage, sizeof(aMessage), "Macro position: tick %d / %d (step %d ticks)",
		Target + 1, (int)m_vMacroFrames.size(), MacroSeekStep());
	GameClient()->Echo(aMessage);
	return true;
}

bool CControls::OnInput(const IInput::CEvent &Event)
{
	if(!m_MacroPlaying || !m_MacroPracticeActive || GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() ||
		!(Event.m_Flags & IInput::FLAG_PRESS))
		return false;

	// Shift+Comma types '<' and Shift+Period types '>'. They seek by ticks only.
	if(Input()->ShiftIsPressed() && Event.m_Key == KEY_COMMA)
		return SeekMacro(-MacroSeekStep());
	if(Input()->ShiftIsPressed() && Event.m_Key == KEY_PERIOD)
		return SeekMacro(MacroSeekStep());

	if(Event.m_Key == KEY_UP)
		SetMacroSeekStep(std::min(MacroSeekStep() * 2, 18000));
	else if(Event.m_Key == KEY_DOWN)
		SetMacroSeekStep(std::max(MacroSeekStep() / 2, 1));
	else
		return false;

	char aMessage[96];
	str_format(aMessage, sizeof(aMessage), "Macro rewind step: %d ticks (%.2f s)",
		MacroSeekStep(), MacroSeekStep() / 50.0f);
	GameClient()->Echo(aMessage);
	return true;
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
	const bool ManualHook = m_aManualHook[Dummy];
	if(!pLocal)
	{
		m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
		m_aAutoHookActive[Dummy] = false;
		m_aAutoAvoidActive[Dummy] = false;
		m_aAutoHookFireActive[Dummy] = false;
		return;
	}

	const vec2 LocalPos((float)pLocal->m_X, (float)pLocal->m_Y);
	bool FoundTarget = false;
	vec2 BestTarget(0.0f, 0.0f);

	// Manual hook always has priority. Automatic targeting never changes the
	// visible cursor and sends only a replacement input direction to the server.
	if((g_Config.m_ClAutoHook && !ManualHook) || g_Config.m_ClAutoTriplefly)
	{
		const float Range = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		const float RangeSquared = Range * Range;
		float aBestDistances[3] = {RangeSquared + 1.0f, RangeSquared + 1.0f, RangeSquared + 1.0f};
		vec2 aBestTargets[3] = {};
		const vec2 ManualAim = m_aMousePos[Dummy];
		const float AimLength = length(ManualAim);
		const float AimAngle = std::clamp((float)g_Config.m_ClAutoHookAngle, 10.0f, 360.0f);
		const int LocalId = GameClient()->m_Snap.m_LocalClientId;

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(ClientId == LocalId || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				continue;
			if(GameClient()->IsOtherTeam(ClientId) || GameClient()->m_aClients[ClientId].m_Solo ||
				GameClient()->m_aClients[ClientId].m_HookHitDisabled || GameClient()->m_aClients[ClientId].m_CollisionDisabled)
				continue;

			const CNetObj_Character &Character = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
			const vec2 TargetPos((float)Character.m_X, (float)Character.m_Y);
			const vec2 Delta = TargetPos - LocalPos;
			const float DistanceSquared = dot(Delta, Delta);
			if(DistanceSquared < 1.0f || DistanceSquared > RangeSquared)
				continue;

			if(!g_Config.m_ClAutoHookBackhook && AimLength > 1.0f && AimAngle < 359.9f)
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

		const int TargetRank = g_Config.m_ClAutoTriplefly ? 2 : 0;
		if(aBestDistances[TargetRank] <= RangeSquared)
		{
			BestTarget = aBestTargets[TargetRank];
			FoundTarget = true;
			m_aInputData[Dummy].m_TargetX = round_to_int(BestTarget.x);
			m_aInputData[Dummy].m_TargetY = round_to_int(BestTarget.y);
			m_aInputData[Dummy].m_Hook = 1;
			m_aAutoHookActive[Dummy] = true;
			if(g_Config.m_ClAutoHookAimAtTee && !g_Config.m_ClAutoHookBackhook)
				m_aMousePos[Dummy] = BestTarget;
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

	// Auto unfreeze targets the nearest visible frozen teammate with the laser.
	int AutoWeapon = -1;
	if(g_Config.m_ClAutoUnfreezeLaser && !ManualHook)
	{
		const CCharacterCore &Predicted = GameClient()->m_PredictedChar;
		if(Predicted.m_aWeapons[WEAPON_LASER].m_Got &&
			(GameClient()->m_GameInfo.m_UnlimitedAmmo || Predicted.m_aWeapons[WEAPON_LASER].m_Ammo != 0))
		{
			const float Range = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
			float BestFrozenDistance = Range * Range;
			const vec2 ManualAim = m_aMousePos[Dummy];
			const float AimLength = length(ManualAim);
			const float AimAngle = std::clamp((float)g_Config.m_ClAutoHookAngle, 10.0f, 360.0f);
			const int LocalId = GameClient()->m_Snap.m_LocalClientId;
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				if(ClientId == LocalId || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || GameClient()->IsOtherTeam(ClientId) || GameClient()->m_aClients[ClientId].m_LaserHitDisabled)
					continue;
				const bool Frozen = GameClient()->m_aClients[ClientId].m_FreezeEnd != 0 ||
					GameClient()->m_aClients[ClientId].m_LiveFrozen ||
					(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData &&
						GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_FreezeEnd != 0);
				if(!Frozen)
					continue;

				const CNetObj_Character &Character = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
				const vec2 TargetPos((float)Character.m_X, (float)Character.m_Y);
				const vec2 Delta = TargetPos - LocalPos;
				const float DistanceSquared = dot(Delta, Delta);
				if(DistanceSquared < 1.0f || DistanceSquared > BestFrozenDistance)
					continue;
				if(!g_Config.m_ClAutoHookBackhook && AimLength > 1.0f && AimAngle < 359.9f)
				{
					const float AimDot = dot(ManualAim, Delta) / (AimLength * std::sqrt(DistanceSquared));
					if(AimDot < std::cos(AimAngle * 0.5f * pi / 180.0f))
						continue;
				}
				vec2 CollisionPos;
				vec2 BeforeCollision;
				if(Collision()->IntersectLine(LocalPos, TargetPos, &CollisionPos, &BeforeCollision) != 0)
					continue;

				BestFrozenDistance = DistanceSquared;
				BestTarget = Delta;
				FoundTarget = true;
				AutoWeapon = WEAPON_LASER;
			}
			if(AutoWeapon == WEAPON_LASER)
			{
				m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
				m_aAutoHookActive[Dummy] = false;
				m_aInputData[Dummy].m_TargetX = round_to_int(BestTarget.x);
				m_aInputData[Dummy].m_TargetY = round_to_int(BestTarget.y);
				if(g_Config.m_ClAutoHookAimAtTee && !g_Config.m_ClAutoHookBackhook)
					m_aMousePos[Dummy] = BestTarget;
			}
		}
	}

	// Optional weapon fire is aimed at the same selected tee.
	if(AutoWeapon < 0 && FoundTarget && !ManualHook)
	{
		const CCharacterCore &Predicted = GameClient()->m_PredictedChar;
		const bool UnlimitedAmmo = GameClient()->m_GameInfo.m_UnlimitedAmmo;
		const int aWeapons[] = {WEAPON_LASER, WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_GUN};
		const bool aEnabled[] = {
			g_Config.m_ClAutoHookWeaponLaser != 0,
			g_Config.m_ClAutoHookWeaponShotgun != 0,
			g_Config.m_ClAutoHookWeaponGrenade != 0,
			g_Config.m_ClAutoHookWeaponGun != 0};
		for(int i = 0; i < 4; ++i)
		{
			const int Weapon = aWeapons[i];
			if(aEnabled[i] && Predicted.m_aWeapons[Weapon].m_Got &&
				(UnlimitedAmmo || Predicted.m_aWeapons[Weapon].m_Ammo != 0))
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
		m_aAutoHookFireActive[Dummy] = false;
	}

	// Avoid assist: when rising, hook a surface below; when falling, hook a
	// side surface slightly above the tee to reduce downward momentum.
	// Recompute the assist every tick so a previous automatic hook cannot stick.
	if(m_aAutoAvoidActive[Dummy])
	{
		m_aInputData[Dummy].m_Hook = ManualHook ? 1 : 0;
		m_aAutoAvoidActive[Dummy] = false;
	}
	if(!FoundTarget && !ManualHook && g_Config.m_ClAutoAvoid)
	{
		const float VelX = pLocal->m_VelX / 256.0f;
		const float VelY = pLocal->m_VelY / 256.0f;
		vec2 Candidates[2];
		int NumCandidates = 0;
		if(VelY < -2.0f)
		{
			Candidates[NumCandidates++] = vec2(0.0f, 1.0f);
		}
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
	if(!pLocal)
		return;

	const vec2 LocalPos((float)pLocal->m_X, (float)pLocal->m_Y);
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

void CControls::ApplyEdgeJump(int Dummy)
{
	if(!g_Config.m_ClAutoEdgeJump || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		m_aAutoEdgeJumpActive[Dummy] = false;
		return;
	}
	const int Direction = m_aInputData[Dummy].m_Direction;
	if(Direction == 0)
	{
		m_aAutoEdgeJumpActive[Dummy] = false;
		return;
	}
	const CNetObj_Character *pLocal = GameClient()->m_Snap.m_pLocalCharacter;
	const vec2 Pos((float)pLocal->m_X, (float)pLocal->m_Y);
	const bool Grounded = Collision()->IsOnGround(Pos, 14.0f);
	const bool GroundAhead = Collision()->CheckPoint(Pos.x + Direction * 19.0f, Pos.y + 18.0f);
	const bool AtEdge = Grounded && !GroundAhead;
	if(AtEdge && !m_aAutoEdgeJumpActive[Dummy])
	{
		m_aInputData[Dummy].m_Jump = 1;
		m_aAutoEdgeJumpActive[Dummy] = true;
	}
	else if(!AtEdge)
	{
		m_aAutoEdgeJumpActive[Dummy] = false;
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
		const int Dummy = g_Config.m_ClDummy;
		CNetObj_PlayerInput PracticeInput = m_aInputData[Dummy];
		PracticeInput.m_Direction = 0;
		if(m_aInputDirectionLeft[Dummy] && !m_aInputDirectionRight[Dummy])
			PracticeInput.m_Direction = -1;
		if(!m_aInputDirectionLeft[Dummy] && m_aInputDirectionRight[Dummy])
			PracticeInput.m_Direction = 1;
		PracticeInput.m_TargetX = (int)m_aMousePos[Dummy].x;
		PracticeInput.m_TargetY = (int)m_aMousePos[Dummy].y;
		PracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

		if(m_aInputData[Dummy].m_PlayerFlags & PLAYERFLAG_PLAYING)
		{
			m_MacroPracticeAccumulator += MacroPracticeRate() / (float)SERVER_TICK_SPEED;
			int Steps = 0;
			while(m_MacroPracticeAccumulator >= 1.0f && Steps < SERVER_TICK_SPEED)
			{
				SimulateMacroPracticeTick(PracticeInput);
				m_MacroPracticeAccumulator -= 1.0f;
				++Steps;
				if(!m_MacroPracticeActive)
					break;
			}
		}

		CNetObj_PlayerInput ServerInput{};
		ServerInput.m_PlayerFlags = m_aInputData[Dummy].m_PlayerFlags;
		ServerInput.m_TargetX = (int)m_aMousePos[Dummy].x;
		ServerInput.m_TargetY = (int)m_aMousePos[Dummy].y;
		ServerInput.m_Fire = m_aLastData[Dummy].m_Fire;
		if(ServerInput.m_Fire & 1)
			++ServerInput.m_Fire;
		ServerInput.m_NextWeapon = m_aLastData[Dummy].m_NextWeapon;
		ServerInput.m_PrevWeapon = m_aLastData[Dummy].m_PrevWeapon;

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

		ApplyHookAutomation(g_Config.m_ClDummy);
		ApplyEdgeAssist(g_Config.m_ClDummy);
		ApplyEdgeJump(g_Config.m_ClDummy);

		// dummy copy moves
		if(g_Config.m_ClDummyCopyMoves)
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

		if(g_Config.m_ClDummyControl)
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

	// update target pos
	if(GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// make sure to compensate for smooth dyncam to ensure the cursor stays still in world space if zoomed
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy];
		float Zoom = GameClient()->m_Camera.m_Zoom;
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_LocalCharacterPos + m_aMousePos[g_Config.m_ClDummy] - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aMousePos[g_Config.m_ClDummy];
	}
	else
	{
		m_aTargetPos[g_Config.m_ClDummy] = m_aMousePos[g_Config.m_ClDummy];
	}

	if((g_Config.m_ClAutoHook || g_Config.m_ClAutoTriplefly || g_Config.m_ClAutoUnfreezeLaser) && g_Config.m_ClAutoHookShowRadius && GameClient()->m_Snap.m_pLocalCharacter)
	{
		const float Radius = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		const float ConeAngle = std::clamp((float)g_Config.m_ClAutoHookAngle, 10.0f, 360.0f);
		const float AimAngle = std::atan2(m_aMousePos[g_Config.m_ClDummy].y, m_aMousePos[g_Config.m_ClDummy].x);
		const float HalfAngle = ConeAngle * 0.5f * pi / 180.0f;
		const int DotCount = std::max(2, round_to_int(72.0f * ConeAngle / 360.0f));
		const vec2 Center((float)GameClient()->m_Snap.m_pLocalCharacter->m_X, (float)GameClient()->m_Snap.m_pLocalCharacter->m_Y);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.25f, 0.80f, 1.0f, 0.65f);
		for(int i = 0; i < DotCount; ++i)
		{
			const float Angle = AimAngle - HalfAngle + (2.0f * HalfAngle * i) / (DotCount - 1);
			IGraphics::CQuadItem Dot(Center.x + std::cos(Angle) * Radius - 1.5f,
				Center.y + std::sin(Angle) * Radius - 1.5f, 3.0f, 3.0f);
			Graphics()->QuadsDrawTL(&Dot, 1);
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
