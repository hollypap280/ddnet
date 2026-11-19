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
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
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
	ResetInput(0);
	ResetInput(1);

	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);

	m_LastSendTime = 0;
	m_MacroRecording = false;
	m_MacroPlaying = false;
	m_MacroPlaybackIndex = 0;
	m_vMacroFrames.clear();
	std::fill(std::begin(m_aAutoHookActive), std::end(m_aAutoHookActive), false);
	std::fill(std::begin(m_aAutoAvoidActive), std::end(m_aAutoAvoidActive), false);
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
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	*pState->m_apVariables[g_Config.m_ClDummy] = Stroke;

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
	constexpr uint32_t MACRO_FILE_VERSION = 1;
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

void CControls::StartMacroRecording()
{
	m_MacroPlaying = false;
	m_MacroPlaybackIndex = 0;
	m_vMacroFrames.clear();
	m_MacroRecording = true;
	GameClient()->Echo("Macro recording started");
}

void CControls::StopMacroRecording()
{
	if(m_MacroRecording)
	{
		m_MacroRecording = false;
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "Macro recording stopped (%d frames)", (int)m_vMacroFrames.size());
		GameClient()->Echo(aBuf);
	}
	m_MacroPlaying = false;
	m_MacroPlaybackIndex = 0;
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
		Magic == MACRO_FILE_MAGIC && Version == MACRO_FILE_VERSION &&
		Count > 0 && Count <= MACRO_MAX_FRAMES;
	std::vector<SMacroFrame> Frames;
	if(Valid)
	{
		Frames.resize(Count);
		Valid = io_read(File, Frames.data(), Count * sizeof(SMacroFrame)) == Count * sizeof(SMacroFrame);
	}
	io_close(File);
	if(!Valid)
		return false;
	m_vMacroFrames = std::move(Frames);
	m_MacroRecording = false;
	m_MacroPlaybackIndex = 0;
	m_MacroPlaying = true;
	GameClient()->Echo("Macro playback started");
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
	if(!pLocal)
		return;

	const vec2 LocalPos((float)pLocal->m_X, (float)pLocal->m_Y);
	bool FoundTarget = false;

	if(g_Config.m_ClAutoHook)
	{
		const float Range = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		float BestDistanceSquared = Range * Range;
		const vec2 ManualAim = m_aMousePos[Dummy];
		const float AimLength = length(ManualAim);
		vec2 BestTarget(0.0f, 0.0f);
		const int LocalId = GameClient()->m_Snap.m_LocalClientId;

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(ClientId == LocalId || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				continue;

			const CNetObj_Character &Character = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
			const vec2 Delta((float)Character.m_X - LocalPos.x, (float)Character.m_Y - LocalPos.y);
			const float DistanceSquared = dot(Delta, Delta);
			if(DistanceSquared < 1.0f || DistanceSquared > BestDistanceSquared)
				continue;

			// Normal mode keeps a broad forward cone. 360° mode also considers
			// targets behind the tee and never changes the visible cursor position.
			if(!g_Config.m_ClAutoHook360 && AimLength > 1.0f)
			{
				const float AimDot = dot(ManualAim, Delta) / (AimLength * std::sqrt(DistanceSquared));
				if(AimDot < 0.35f)
					continue;
			}

			BestDistanceSquared = DistanceSquared;
			BestTarget = Delta;
			FoundTarget = true;
		}

		if(FoundTarget)
		{
			m_aInputData[Dummy].m_TargetX = round_to_int(BestTarget.x);
			m_aInputData[Dummy].m_TargetY = round_to_int(BestTarget.y);
			m_aInputData[Dummy].m_Hook = 1;
			m_aAutoHookActive[Dummy] = true;
		}
		else if(m_aAutoHookActive[Dummy])
		{
			m_aInputData[Dummy].m_Hook = 0;
			m_aAutoHookActive[Dummy] = false;
		}
	}
	else if(m_aAutoHookActive[Dummy])
	{
		m_aInputData[Dummy].m_Hook = 0;
		m_aAutoHookActive[Dummy] = false;
	}

	// Avoid assist: rising tees search below; falling tees search for a side
	// surface slightly above them to reduce downward speed.
	if(!FoundTarget && g_Config.m_ClAutoAvoid && m_aInputData[Dummy].m_Hook == 0)
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
			if(Collision()->IntersectLine(LocalPos, LocalPos + Candidates[i] * 260.0f, &CollisionPos, &BeforeCollision) == 0)
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
		else if(m_aAutoAvoidActive[Dummy])
		{
			m_aInputData[Dummy].m_Hook = 0;
			m_aAutoAvoidActive[Dummy] = false;
		}
	}
	else if(m_aAutoAvoidActive[Dummy] && !g_Config.m_ClAutoAvoid)
	{
		m_aInputData[Dummy].m_Hook = 0;
		m_aAutoAvoidActive[Dummy] = false;
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

		// Macro playback has priority over live input.
		if(m_MacroPlaying)
		{
			if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
			{
				m_MacroPlaying = false;
				m_aInputData[g_Config.m_ClDummy].m_Direction = 0;
				m_aInputData[g_Config.m_ClDummy].m_Jump = 0;
				m_aInputData[g_Config.m_ClDummy].m_Hook = 0;
				if(m_aInputData[g_Config.m_ClDummy].m_Fire & 1)
					m_aInputData[g_Config.m_ClDummy].m_Fire++;
				Send = true;
			}
			else
			{
				const SMacroFrame &Frame = m_vMacroFrames[m_MacroPlaybackIndex++];
				CNetObj_PlayerInput &Input = m_aInputData[g_Config.m_ClDummy];
				Input.m_Direction = Frame.m_Direction;
				Input.m_Jump = Frame.m_Jump;
				Input.m_Fire = Frame.m_Fire;
				Input.m_Hook = Frame.m_Hook;
				Input.m_WantedWeapon = Frame.m_WantedWeapon;
				Input.m_NextWeapon = Frame.m_NextWeapon;
				Input.m_PrevWeapon = Frame.m_PrevWeapon;
				Input.m_TargetX = Frame.m_TargetX;
				Input.m_TargetY = Frame.m_TargetY;
				Send = true;
			}
		}

		if(m_MacroRecording && !m_MacroPlaying && m_vMacroFrames.size() < 18000)
		{
			const CNetObj_PlayerInput &Input = m_aInputData[g_Config.m_ClDummy];
			m_vMacroFrames.push_back({Input.m_Direction, Input.m_Jump, Input.m_Fire, Input.m_Hook,
				Input.m_WantedWeapon, Input.m_NextWeapon, Input.m_PrevWeapon, Input.m_TargetX, Input.m_TargetY});
		}
		else if(m_MacroRecording && m_vMacroFrames.size() >= 18000)
		{
			StopMacroRecording();
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

	if(g_Config.m_ClAutoHook && g_Config.m_ClAutoHookShowRadius && GameClient()->m_Snap.m_pLocalCharacter)
	{
		const float Radius = std::clamp((float)g_Config.m_ClAutoHookRange, 32.0f, 360.0f);
		const vec2 Center((float)GameClient()->m_Snap.m_pLocalCharacter->m_X, (float)GameClient()->m_Snap.m_pLocalCharacter->m_Y);
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
