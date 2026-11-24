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
	CControls *pSelf = pState->m_pControls;

	if(pSelf->GameClient()->m_GameInfo.m_BugDDRaceInput && pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	const int Dummy = g_Config.m_ClDummy;

	// Practice controls must never enter the real DDNet input buffers. Those
	// buffers are also consumed by the live prediction world, not only SnapInput.
	if(pSelf->m_MacroPracticeActive)
	{
		if(pState->m_IsDirectionLeft)
		{
			pSelf->m_MacroPracticeDirectionLeft = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingDirectionLeft = true;
		}
		if(pState->m_IsDirectionRight)
		{
			pSelf->m_MacroPracticeDirectionRight = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingDirectionRight = true;
		}
		if(pState->m_IsJumpAction)
		{
			pSelf->m_MacroPracticeInput.m_Jump = Stroke;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingJump = true;
		}
		if(pState->m_IsHookAction)
		{
			pSelf->m_MacroPracticeInput.m_Hook = Stroke;
			pSelf->m_aManualHook[Dummy] = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingHook = true;
		}
		if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
			pSelf->GameClient()->m_Binds.m_HandlingNewBind)
		{
			pSelf->m_aMouseOnAction[Dummy] = true;
		}
		return;
	}

	*pState->m_apVariables[Dummy] = Stroke;
	if(pState->m_IsHookAction)
		pSelf->m_aManualHook[Dummy] = Stroke != 0;

	if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
		pSelf->GameClient()->m_Binds.m_HandlingNewBind)
	{
		pSelf->m_aMouseOnAction[Dummy] = true;
	}
}

void CControls::ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;
	CControls *pSelf = pState->m_pControls;

	if((pSelf->GameClient()->m_GameInfo.m_BugDDRaceInput && pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active) ||
		pSelf->GameClient()->m_Spectator.IsActive())
	{
		return;
	}

	const int Stroke = pResult->GetInteger(0);
	const int Dummy = g_Config.m_ClDummy;
	if(pSelf->m_MacroPracticeActive && pState->m_IsFireAction)
	{
		// Keep the normal DDNet press/release counter, but in a private buffer.
		// This preserves same-tick hammer and gun taps without an extra queue tick.
		int &Fire = pSelf->m_MacroPracticeInput.m_Fire;
		if((Fire & 1) != Stroke)
			++Fire;
		Fire &= INPUT_STATE_MASK;
		if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
			pSelf->GameClient()->m_Binds.m_HandlingNewBind)
		{
			pSelf->m_aMouseOnAction[Dummy] = true;
		}
		return;
	}

	int *pVariable = pState->m_apVariables[Dummy];
	if(((*pVariable) & 1) != Stroke)
	{
		++(*pVariable);
		if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
			pSelf->GameClient()->m_Binds.m_HandlingNewBind)
		{
			pSelf->m_aMouseOnAction[Dummy] = true;
		}
	}
	*pVariable &= INPUT_STATE_MASK;
}

void CControls::ConMacroPracticeInput(IConsole::IResult *pResult, void *pUserData)
{
	SMacroPracticeDummyBind *pBind = (SMacroPracticeDummyBind *)pUserData;
	CControls *pSelf = pBind->m_pControls;
	const bool Pressed = pResult->GetInteger(0) != 0;
	const int Dummy = g_Config.m_ClDummy;
	if(!pSelf->m_MacroPracticeActive || pSelf->m_MacroPlaying)
		return;

	// Compatibility aliases write to the exact same isolated input buffer.
	switch(pBind->m_Action)
	{
	case EMacroPracticeDummyAction::LEFT:
		pSelf->m_MacroPracticeDirectionLeft = Pressed;
		if(Pressed && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
			pSelf->m_MacroPracticePendingDirectionLeft = true;
		break;
	case EMacroPracticeDummyAction::RIGHT:
		pSelf->m_MacroPracticeDirectionRight = Pressed;
		if(Pressed && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
			pSelf->m_MacroPracticePendingDirectionRight = true;
		break;
	case EMacroPracticeDummyAction::JUMP:
		pSelf->m_MacroPracticeInput.m_Jump = Pressed ? 1 : 0;
		if(Pressed && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
			pSelf->m_MacroPracticePendingJump = true;
		break;
	case EMacroPracticeDummyAction::HOOK:
		pSelf->m_MacroPracticeInput.m_Hook = Pressed ? 1 : 0;
		pSelf->m_aManualHook[Dummy] = Pressed;
		if(Pressed && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
			pSelf->m_MacroPracticePendingHook = true;
		break;
	case EMacroPracticeDummyAction::FIRE:
	{
		int &Fire = pSelf->m_MacroPracticeInput.m_Fire;
		if((Fire & 1) != (Pressed ? 1 : 0))
		{
			++Fire;
			Fire &= INPUT_STATE_MASK;
		}
		break;
	}
	}
}

void CControls::ConMacroPracticeDummyInput(IConsole::IResult *pResult, void *pUserData)
{
	SMacroPracticeDummyBind *pBind = (SMacroPracticeDummyBind *)pUserData;
	CControls *pSelf = pBind->m_pControls;
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(!pSelf->m_MacroPracticeActive || pSelf->m_MacroPlaying)
		return;

	if(Pressed && !pSelf->m_MacroPracticeDummyEnabled)
	{
		pSelf->m_MacroPracticeDummyEnabled = true;
		pSelf->m_MacroPracticeDummyInput = {};
		pSelf->m_MacroPracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		pSelf->m_MacroPracticeDummyInput.m_TargetX = 1;
		pSelf->m_MacroPracticeDummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;
	}

	switch(pBind->m_Action)
	{
	case EMacroPracticeDummyAction::LEFT:
		pSelf->m_MacroPracticeDummyLeft = Pressed;
		break;
	case EMacroPracticeDummyAction::RIGHT:
		pSelf->m_MacroPracticeDummyRight = Pressed;
		break;
	case EMacroPracticeDummyAction::JUMP:
		pSelf->m_MacroPracticeDummyInput.m_Jump = Pressed ? 1 : 0;
		break;
	case EMacroPracticeDummyAction::HOOK:
		pSelf->m_MacroPracticeDummyInput.m_Hook = Pressed ? 1 : 0;
		break;
	case EMacroPracticeDummyAction::FIRE:
	{
		int &Fire = pSelf->m_MacroPracticeDummyInput.m_Fire;
		if((Fire & 1) != (Pressed ? 1 : 0))
			Fire = (Fire + 1) & INPUT_STATE_MASK;
		break;
	}
	}
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
	if(!pResult->GetInteger(0))
		return;

	CControls *pSelf = pSet->m_pControls;
	if(pSelf->m_MacroPracticeActive &&
		pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_WantedWeapon)
	{
		if(pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
		{
			pSelf->m_MacroPracticePendingWantedWeapon = pSet->m_Value;
			pSelf->m_MacroPracticeInput.m_WantedWeapon = 0;
		}
		else
		{
			pSelf->m_MacroPracticeInput.m_WantedWeapon = pSet->m_Value;
		}
		return;
	}

	*pSet->m_apVariables[g_Config.m_ClDummy] = pSet->m_Value;
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	CControls *pSelf = pSet->m_pControls;
	const int Stroke = pResult->GetInteger(0);
	const int Dummy = g_Config.m_ClDummy;

	if(pSelf->m_MacroPracticeActive)
	{
		int *pPracticeVariable = nullptr;
		if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_NextWeapon)
			pPracticeVariable = &pSelf->m_MacroPracticeInput.m_NextWeapon;
		else if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_PrevWeapon)
			pPracticeVariable = &pSelf->m_MacroPracticeInput.m_PrevWeapon;

		if(pPracticeVariable && ((*pPracticeVariable & 1) != Stroke))
		{
			++(*pPracticeVariable);
			*pPracticeVariable &= INPUT_STATE_MASK;
		}
		return;
	}

	int *pVariable = pSet->m_apVariables[Dummy];
	if(((*pVariable) & 1) != Stroke)
		++(*pVariable);
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

	Console()->Register("macro_practice", "", CFGFLAG_CLIENT, ConMacroPractice, this, "Toggle isolated FP practice");
	Console()->Register("macro_pause", "", CFGFLAG_CLIENT, ConMacroPause, this, "Pause or resume live macro playback");
	Console()->Register("macro_record", "", CFGFLAG_CLIENT, ConMacroRecord, this, "Start recording a player input macro");
	Console()->Register("macro_stop", "", CFGFLAG_CLIENT, ConMacroStop, this, "Stop recording or playback of a player input macro");
	Console()->Register("macro_save", "s[name]", CFGFLAG_CLIENT, ConMacroSave, this, "Save the recorded player input macro");
	Console()->Register("macro_play", "s[name]", CFGFLAG_CLIENT, ConMacroPlay, this, "Play a saved player input macro");
	Console()->Register("macro_play_first", "", CFGFLAG_CLIENT, ConMacroPlayFirst, this, "Play the first saved player input macro");
	Console()->Register("macro_delete", "s[name]", CFGFLAG_CLIENT, ConMacroDelete, this, "Delete a saved player input macro");
	Console()->Register("+macro_rewind", "", CFGFLAG_CLIENT, ConMacroRewind, this, "Hold to rewind a macro; repeat speed increases while held");
	Console()->Register("+macro_seek", "", CFGFLAG_CLIENT, ConMacroSeek, this, "Jump forward by the configured number of macro ticks");
	Console()->Register("macro_goto", "i[tick]", CFGFLAG_CLIENT, ConMacroGoto, this, "Jump to an exact macro tick");

	static const SMacroPracticeDummyBind s_PlayerLeft{this, EMacroPracticeDummyAction::LEFT};
	static const SMacroPracticeDummyBind s_PlayerRight{this, EMacroPracticeDummyAction::RIGHT};
	static const SMacroPracticeDummyBind s_PlayerJump{this, EMacroPracticeDummyAction::JUMP};
	static const SMacroPracticeDummyBind s_PlayerHook{this, EMacroPracticeDummyAction::HOOK};
	static const SMacroPracticeDummyBind s_PlayerFire{this, EMacroPracticeDummyAction::FIRE};
	Console()->Register("+macro_left", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_PlayerLeft, "Move the FP practice tee left");
	Console()->Register("+macro_right", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_PlayerRight, "Move the FP practice tee right");
	Console()->Register("+macro_jump", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_PlayerJump, "Jump with the FP practice tee");
	Console()->Register("+macro_hook", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_PlayerHook, "Hook with the FP practice tee");
	Console()->Register("+macro_fire", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_PlayerFire, "Fire with the FP practice tee");

	static const SMacroPracticeDummyBind s_DummyLeft{this, EMacroPracticeDummyAction::LEFT};
	static const SMacroPracticeDummyBind s_DummyRight{this, EMacroPracticeDummyAction::RIGHT};
	static const SMacroPracticeDummyBind s_DummyJump{this, EMacroPracticeDummyAction::JUMP};
	static const SMacroPracticeDummyBind s_DummyHook{this, EMacroPracticeDummyAction::HOOK};
	static const SMacroPracticeDummyBind s_DummyFire{this, EMacroPracticeDummyAction::FIRE};
	Console()->Register("+macro_dummy_left", "", CFGFLAG_CLIENT, ConMacroPracticeDummyInput, (void *)&s_DummyLeft, "Move the practice dummy left");
	Console()->Register("+macro_dummy_right", "", CFGFLAG_CLIENT, ConMacroPracticeDummyInput, (void *)&s_DummyRight, "Move the practice dummy right");
	Console()->Register("+macro_dummy_jump", "", CFGFLAG_CLIENT, ConMacroPracticeDummyInput, (void *)&s_DummyJump, "Make the practice dummy jump");
	Console()->Register("+macro_dummy_hook", "", CFGFLAG_CLIENT, ConMacroPracticeDummyInput, (void *)&s_DummyHook, "Make the practice dummy hook");
	Console()->Register("+macro_dummy_fire", "", CFGFLAG_CLIENT, ConMacroPracticeDummyInput, (void *)&s_DummyFire, "Make the practice dummy fire");
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

void CControls::SendMacroFrameControl(EMacroFrameMode Mode)
{
	if(!m_MacroPlaybackLive)
		return;
	CNetMsg_Cl_MacroFrame Msg{};
	Msg.m_Mode = (int)Mode;
	const int Conn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CControls::SendMacroFrame(int Index, EMacroFrameMode Mode)
{
	if(!m_MacroPlaybackLive || Index < 0 || Index >= (int)m_vMacroFrames.size())
		return;

	const SMacroFrame &Frame = m_vMacroFrames[Index];
	const CNetObj_CharacterCore &Core = Frame.m_PreCore;
	const CNetObj_DDNetCharacter &DDNet = Frame.m_PreDDNetCharacter;
	CNetMsg_Cl_MacroFrame Msg{};
	Msg.m_Mode = (int)Mode;
	Msg.m_RecordedTick = Frame.m_Tick - 1;
	Msg.m_X = Core.m_X;
	Msg.m_Y = Core.m_Y;
	Msg.m_VelX = Core.m_VelX;
	Msg.m_VelY = Core.m_VelY;
	Msg.m_Angle = Core.m_Angle;
	Msg.m_Direction = Core.m_Direction;
	Msg.m_Jumped = Core.m_Jumped;
	Msg.m_HookedPlayer = Core.m_HookedPlayer;
	Msg.m_HookState = Core.m_HookState;
	Msg.m_HookTick = Core.m_HookTick;
	Msg.m_HookX = Core.m_HookX;
	Msg.m_HookY = Core.m_HookY;
	Msg.m_HookDx = Core.m_HookDx;
	Msg.m_HookDy = Core.m_HookDy;
	Msg.m_FreezeTime = Frame.m_PreFreezeTime;
	Msg.m_FrozenLastTick = Frame.m_PreFrozenLastTick != 0;
	Msg.m_Weapon = Frame.m_PreWeapon;
	Msg.m_AttackTick = Frame.m_PreAttackTick;
	Msg.m_InputDirection = Frame.m_Input.m_Direction;
	Msg.m_InputTargetX = Frame.m_Input.m_TargetX;
	Msg.m_InputTargetY = Frame.m_Input.m_TargetY;
	Msg.m_InputJump = Frame.m_Input.m_Jump;
	Msg.m_InputFire = (Frame.m_Input.m_Fire + m_MacroLiveFireOffset) & INPUT_STATE_MASK;
	Msg.m_InputHook = Frame.m_Input.m_Hook;
	Msg.m_InputWantedWeapon = Frame.m_Input.m_WantedWeapon;
	Msg.m_InputNextWeapon = (Frame.m_Input.m_NextWeapon + m_MacroLiveNextWeaponOffset) & INPUT_STATE_MASK;
	Msg.m_InputPrevWeapon = (Frame.m_Input.m_PrevWeapon + m_MacroLivePrevWeaponOffset) & INPUT_STATE_MASK;
	Msg.m_DDNetHasState = Frame.m_HasDDNetState;
	Msg.m_DDNetFlags = Frame.m_HasDDNetState ? DDNet.m_Flags : 0;
	Msg.m_DDNetFreezeEnd = Frame.m_HasDDNetState ? DDNet.m_FreezeEnd : 0;
	Msg.m_DDNetJumps = Frame.m_HasDDNetState ? DDNet.m_Jumps : -1;
	Msg.m_DDNetTeleCheckpoint = Frame.m_HasDDNetState ? DDNet.m_TeleCheckpoint : 0;
	Msg.m_DDNetStrongWeakId = Frame.m_HasDDNetState ? DDNet.m_StrongWeakId : 0;
	Msg.m_DDNetJumpedTotal = Frame.m_HasDDNetState ? DDNet.m_JumpedTotal : -1;
	Msg.m_DDNetNinjaActivationTick = Frame.m_HasDDNetState ? DDNet.m_NinjaActivationTick : -1;
	Msg.m_DDNetFreezeStart = Frame.m_HasDDNetState ? DDNet.m_FreezeStart : -1;
	Msg.m_DDNetTargetX = Frame.m_HasDDNetState ? DDNet.m_TargetX : Frame.m_Input.m_TargetX;
	Msg.m_DDNetTargetY = Frame.m_HasDDNetState ? DDNet.m_TargetY : Frame.m_Input.m_TargetY;
	Msg.m_DDNetTuneZoneOverride = Frame.m_HasDDNetState ? DDNet.m_TuneZoneOverride : -1;

	const int Conn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
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
	// Version 6 writes the current frame layout. Version 5 files are read
	// using either the current layout or the older version-4-compatible one.
	constexpr uint32_t MACRO_FILE_VERSION = 8;
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
	std::fill(std::begin(m_aMacroPracticeSnapshotInitialized), std::end(m_aMacroPracticeSnapshotInitialized), false);
	m_MacroPracticeWorld.CopyWorld(&GameClient()->m_PredictedWorld);
	if(m_MacroPracticeWorld.m_pParent && m_MacroPracticeWorld.m_pParent->m_pChild == &m_MacroPracticeWorld)
		m_MacroPracticeWorld.m_pParent->m_pChild = nullptr;
	m_MacroPracticeWorld.m_pParent = nullptr;
	m_MacroPracticeWorld.m_pChild = nullptr;
	m_MacroPracticeBaselineValid = false;
	m_aMacroPracticeBaselineName[0] = '\0';

	// Fast Practice must simulate the same local gameplay as the normal
	// prediction world, even when the server does not request client prediction.
	// These flags affect only the private copy; the real server tee still gets
	// neutral input while practice is active.
	m_MacroPracticeWorld.m_WorldConfig = GameClient()->m_PredictedWorld.m_WorldConfig;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictWeapons = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictFreeze = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictTiles = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictDDRace = true;
	m_MacroPracticeWorld.m_WorldConfig.m_PredictEvents = true;
	m_MacroPracticeWorld.m_WorldConfig.m_UseTuneZones = true;
	// CopyWorld already cloned the prediction world's team collision rules.

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
	const int Dummy = g_Config.m_ClDummy;
	m_MacroPracticeFireCounter = 0;
	m_MacroPracticeFirePressed = false;
	m_MacroPracticeInput = {};
	m_MacroPracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_MacroPracticeInput.m_TargetX = 1;
	m_MacroPracticeDirectionLeft = false;
	m_MacroPracticeDirectionRight = false;
	m_MacroPracticeNextWeaponPressed = false;
	m_MacroPracticePrevWeaponPressed = false;

	// Keep the real/predicted tee idle while keys are consumed by the private world.
	m_aInputData[Dummy] = {};
	m_aInputData[Dummy].m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_aInputData[Dummy].m_TargetX = 1;
	m_aLastData[Dummy] = m_aInputData[Dummy];
	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
	m_aManualHook[Dummy] = false;

	m_MacroPracticeDummyInput = {};
	m_MacroPracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_MacroPracticeDummyInput.m_TargetX = 1;
	m_MacroPracticeDummyEnabled = false;
	m_MacroPracticeDummyLeft = false;
	m_MacroPracticeDummyRight = false;
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
	m_aInputData[Dummy] = {};
	m_aInputData[Dummy].m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_aInputData[Dummy].m_TargetX = 1;
	m_aLastData[Dummy] = m_aInputData[Dummy];
	m_aManualHook[Dummy] = false;
	m_aAutoHookActive[Dummy] = false;
	m_aAutoHookFireActive[Dummy] = false;
	m_aAutoAvoidActive[Dummy] = false;
	m_aAutoEdgeHookActive[Dummy] = false;
	m_MacroRecording = false;
	m_MacroRecordingPaused = false;
	m_MacroPlaying = false;
	m_MacroPracticeActive = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = 0;
	m_MacroPracticeLocalId = -1;
	m_MacroPracticeStartLocalId = -1;
	std::fill(std::begin(m_aMacroPracticeSnapshotInitialized), std::end(m_aMacroPracticeSnapshotInitialized), false);
	std::fill(std::begin(m_aMacroPracticeStartSnapshotInitialized), std::end(m_aMacroPracticeStartSnapshotInitialized), false);
	m_MacroPracticeBaselineValid = false;
	m_aMacroPracticeBaselineName[0] = '\0';
	m_MacroPlaybackOwnsDummy = false;
	m_MacroPlaybackOwnsPractice = false;
	m_aRecordingMacroName[0] = '\0';
	m_MacroPlaybackLive = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroRewindHeld = false;
	m_MacroRewindHeldSince = 0;
	m_MacroRewindNextRepeat = 0;
	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticeInput = {};
	m_MacroPracticeDummyInput = {};
	m_MacroPracticeDummyEnabled = false;
	m_MacroPracticeDummyLeft = false;
	m_MacroPracticeDummyRight = false;
	m_MacroPracticeDirectionLeft = false;
	m_MacroPracticeDirectionRight = false;
	m_MacroPracticeFirePressed = false;
	m_MacroPracticeNextWeaponPressed = false;
	m_MacroPracticePrevWeaponPressed = false;
	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticeHasRenderChar = false;
	m_MacroPlaybackIndex = 0;
	m_MacroStartFrameIndex = 0;
	m_MacroDisplayedTick = 0;
	m_MacroPracticeWorld.Clear();
	m_MacroPracticeStartWorld.Clear();
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

		// The active practice tee is already owned by the private world.
		// Mark it imported so it cannot be reloaded from the server after a
		// later dummy switch.
		if(ClientId == m_MacroPracticeLocalId)
		{
			m_aMacroPracticeSnapshotInitialized[ClientId] = true;
			continue;
		}

		const bool IsOwnDummy = ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
		if((!g_Config.m_ClMacroPracticeShowOthers && !IsOwnDummy) || !Info.m_Active)
		{
			if(pPracticeCharacter)
				pPracticeCharacter->Destroy();
			m_aMacroPracticeSnapshotInitialized[ClientId] = false;
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

		// A non-controlled local tee is always input-AFK. It is imported only
		// once, then its state is preserved inside the practice world. It can
		// still receive forces from the controlled tee, projectiles and map tiles.
		if(!m_aMacroPracticeSnapshotInitialized[ClientId])
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
			pPracticeCharacter->OnDirectInput(&IdleInput);
			pPracticeCharacter->OnPredictedInput(&IdleInput);
			m_aMacroPracticeSnapshotInitialized[ClientId] = true;
		}

		pPracticeCharacter->m_IsMacroPracticeStatic = true;
		pPracticeCharacter->m_GameTeam = GameTeam;
	}

	m_MacroPracticeWorld.RemoveMarkedEntities();
	m_MacroPracticeWorld.m_GameTick = PracticeTick;

	// While a macro is replaying, the recorded tee id controls the private world.
	if(!m_MacroPlaybackOwnsDummy && !m_MacroPlaying)
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
				m_MacroPracticePendingDirectionLeft = false;
				m_MacroPracticePendingDirectionRight = false;
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
		// Stop feeding this tee live input, but keep its velocity, hook,
		// freeze, weapon and position so it remains part of the timeline.
		pOldCharacter->SetInput(&IdleInput);
		pOldCharacter->m_IsLocal = false;
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
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticeDirectionLeft = false;
	m_MacroPracticeDirectionRight = false;
	m_MacroPracticeInput = IdleInput;
	m_MacroPracticeInput.m_TargetX = 1;
	m_MacroPracticeFireCounter = 0;
	m_MacroPracticeFirePressed = false;
	m_MacroPracticeNextWeaponPressed = false;
	m_MacroPracticePrevWeaponPressed = false;
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
		if(Frame.m_HasDDNetState)
		{
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				Core.m_aWeapons[Weapon].m_Ammo = Frame.m_aWeaponAmmo[Weapon];
		}
		pCharacter->SetCore(Core);
		pCharacter->m_Pos = Core.m_Pos;
		pCharacter->m_PrevPos = Core.m_Pos;
		pCharacter->m_PrevPrevPos = Core.m_Pos;
		pCharacter->SetActiveWeapon(Frame.m_Weapon);
		pCharacter->m_FreezeTime = Frame.m_FreezeTime;
		pCharacter->m_FrozenLastTick = Frame.m_FrozenLastTick != 0;
		m_MacroPracticeWorld.m_GameTick = Frame.m_Tick;
		m_MacroPracticeTick = Frame.m_Tick;

		UpdateMacroPracticeGhost(pCharacter);
	}
	m_MacroPlaybackIndex = (std::size_t)Index + 1;
	m_MacroDisplayedTick = Index + 1;
	m_MacroPracticeWaiting = false;
	return true;
}

void CControls::SimulateMacroPracticeTick(const CNetObj_PlayerInput &LiveInput, const CNetObj_PlayerInput &DummyInput)
{
	(void)DummyInput;
	if(!m_MacroPracticeActive)
		return;

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
				m_MacroPlaybackIndex = std::clamp(m_MacroStartFrameIndex, 0, (int)m_vMacroFrames.size() - 1);
				const SMacroFrame &StartFrame = m_vMacroFrames[m_MacroPlaybackIndex];
				if(StartFrame.m_ClientId >= 0 && StartFrame.m_ClientId != m_MacroPracticeLocalId &&
					!SwitchMacroPracticeCharacter(StartFrame.m_ClientId))
				{
					GameClient()->Echo("Не удалось переключить тренировочного тии в начало цикла.");
					StopMacroRecording();
					return;
				}
				pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
				if(pCharacter && StartFrame.m_HasCoordinates)
				{
					CCharacterCore Core = pCharacter->GetCore();
					Core.Read(&StartFrame.m_PreCore);
					pCharacter->SetCore(Core);
					pCharacter->m_Pos = Core.m_Pos;
					pCharacter->m_PrevPos = Core.m_Pos;
					pCharacter->m_PrevPrevPos = Core.m_Pos;
					pCharacter->SetActiveWeapon(StartFrame.m_PreWeapon);
					pCharacter->m_FreezeTime = StartFrame.m_PreFreezeTime;
					pCharacter->m_FrozenLastTick = StartFrame.m_PreFrozenLastTick != 0;
					m_MacroPracticeWorld.m_GameTick = std::max(0, StartFrame.m_Tick - 1);
					m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
				}
				m_MacroDisplayedTick = m_MacroStartFrameIndex + 1;
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
		if(Frame.m_ClientId >= 0 && Frame.m_ClientId != m_MacroPracticeLocalId &&
			!SwitchMacroPracticeCharacter(Frame.m_ClientId))
		{
			GameClient()->Echo("Не удалось переключить тренировочного тии по записи макроса.");
			StopMacroRecording();
			return;
		}
		pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId);
		if(!pCharacter)
		{
			GameClient()->Echo("Тренировочный тии из макроса не найден.");
			StopMacroRecording();
			return;
		}

		// Restore the exact pre-tick state before replaying this input frame.
		if(Frame.m_HasCoordinates)
		{
			m_MacroPracticeWorld.m_GameTick = std::max(0, Frame.m_Tick - 1);
			m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
			if(Frame.m_HasDDNetState)
			{
				CNetObj_Character PreCharacter{};
				static_cast<CNetObj_CharacterCore &>(PreCharacter) = Frame.m_PreCore;
				PreCharacter.m_Weapon = Frame.m_PreWeapon;
				PreCharacter.m_AttackTick = Frame.m_PreAttackTick;
				PreCharacter.m_Tick = Frame.m_Tick - 1;
				if(Frame.m_PreWeapon >= 0 && Frame.m_PreWeapon < NUM_WEAPONS)
					PreCharacter.m_AmmoCount = Frame.m_aPreWeaponAmmo[Frame.m_PreWeapon];
				CNetObj_DDNetCharacter PreDDNetCharacter = Frame.m_PreDDNetCharacter;
				pCharacter->Read(&PreCharacter, &PreDDNetCharacter, true);
				CCharacterCore RestoredCore = pCharacter->GetCore();
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
					RestoredCore.m_aWeapons[Weapon].m_Ammo = Frame.m_aPreWeaponAmmo[Weapon];
				pCharacter->SetCore(RestoredCore);
				pCharacter->m_Pos = pCharacter->Core()->m_Pos;
			}
			else
			{
				CCharacterCore Core = pCharacter->GetCore();
				Core.Read(&Frame.m_PreCore);
				pCharacter->SetCore(Core);
				pCharacter->m_Pos = Core.m_Pos;
				pCharacter->SetActiveWeapon(Frame.m_PreWeapon);
			}
			pCharacter->m_Pos = pCharacter->Core()->m_Pos;
			pCharacter->m_PrevPos = pCharacter->m_Pos;
			pCharacter->m_PrevPrevPos = pCharacter->m_Pos;
			pCharacter->m_FreezeTime = Frame.m_PreFreezeTime;
			pCharacter->m_FrozenLastTick = Frame.m_PreFrozenLastTick != 0;
		}

		m_MacroPracticeWaiting = false;
		TickInput = Frame.m_Input;
		TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		ReplayRecordedFrame = true;
	}

	if(!ReplayRecordedFrame && MacroPracticeRate() < SERVER_TICK_SPEED)
	{
		// At a slower recording rate preserve short movement/jump/hook taps.
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
		if(m_MacroPracticePendingWantedWeapon > 0)
		{
			TickInput.m_WantedWeapon = m_MacroPracticePendingWantedWeapon;
			m_MacroPracticePendingWantedWeapon = 0;
		}
	}

	SMacroFrame NewFrame;
	if(m_MacroRecording)
	{
		NewFrame.m_Input = TickInput;
		pCharacter->Core()->Write(&NewFrame.m_PreCore);
		NewFrame.m_PreWeapon = pCharacter->GetActiveWeapon();
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			NewFrame.m_aPreWeaponAmmo[Weapon] = pCharacter->Core()->m_aWeapons[Weapon].m_Ammo;
		NewFrame.m_PreFreezeTime = pCharacter->m_FreezeTime;
		NewFrame.m_PreFrozenLastTick = pCharacter->m_FrozenLastTick ? 1 : 0;
		NewFrame.m_PreAttackTick = pCharacter->GetAttackTick();
		NewFrame.m_HasDDNetState = 1;
		const CCharacterCore &PreCore = *pCharacter->Core();
		CNetObj_DDNetCharacter &DDNet = NewFrame.m_PreDDNetCharacter;
		DDNet = {};
		if(PreCore.m_Solo) DDNet.m_Flags |= CHARACTERFLAG_SOLO;
		if(PreCore.m_Jetpack) DDNet.m_Flags |= CHARACTERFLAG_JETPACK;
		if(PreCore.m_CollisionDisabled) DDNet.m_Flags |= CHARACTERFLAG_COLLISION_DISABLED;
		if(PreCore.m_EndlessHook) DDNet.m_Flags |= CHARACTERFLAG_ENDLESS_HOOK;
		if(PreCore.m_EndlessJump) DDNet.m_Flags |= CHARACTERFLAG_ENDLESS_JUMP;
		if(PreCore.m_Super) DDNet.m_Flags |= CHARACTERFLAG_SUPER;
		if(PreCore.m_Invincible) DDNet.m_Flags |= CHARACTERFLAG_INVINCIBLE;
		if(PreCore.m_HammerHitDisabled) DDNet.m_Flags |= CHARACTERFLAG_HAMMER_HIT_DISABLED;
		if(PreCore.m_ShotgunHitDisabled) DDNet.m_Flags |= CHARACTERFLAG_SHOTGUN_HIT_DISABLED;
		if(PreCore.m_GrenadeHitDisabled) DDNet.m_Flags |= CHARACTERFLAG_GRENADE_HIT_DISABLED;
		if(PreCore.m_LaserHitDisabled) DDNet.m_Flags |= CHARACTERFLAG_LASER_HIT_DISABLED;
		if(PreCore.m_HookHitDisabled) DDNet.m_Flags |= CHARACTERFLAG_HOOK_HIT_DISABLED;
		if(PreCore.m_HasTelegunGun) DDNet.m_Flags |= CHARACTERFLAG_TELEGUN_GUN;
		if(PreCore.m_HasTelegunGrenade) DDNet.m_Flags |= CHARACTERFLAG_TELEGUN_GRENADE;
		if(PreCore.m_HasTelegunLaser) DDNet.m_Flags |= CHARACTERFLAG_TELEGUN_LASER;
		if(PreCore.m_LiveFrozen) DDNet.m_Flags |= CHARACTERFLAG_MOVEMENTS_DISABLED;
		if(PreCore.m_IsInFreeze) DDNet.m_Flags |= CHARACTERFLAG_IN_FREEZE;
		if(PreCore.m_aWeapons[WEAPON_HAMMER].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_HAMMER;
		if(PreCore.m_aWeapons[WEAPON_GUN].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_GUN;
		if(PreCore.m_aWeapons[WEAPON_SHOTGUN].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_SHOTGUN;
		if(PreCore.m_aWeapons[WEAPON_GRENADE].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_GRENADE;
		if(PreCore.m_aWeapons[WEAPON_LASER].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_LASER;
		if(PreCore.m_aWeapons[WEAPON_NINJA].m_Got) DDNet.m_Flags |= CHARACTERFLAG_WEAPON_NINJA;
		DDNet.m_FreezeEnd = PreCore.m_DeepFrozen ? -1 : PreCore.m_FreezeEnd;
		DDNet.m_Jumps = PreCore.m_Jumps;
		DDNet.m_TeleCheckpoint = pCharacter->m_TeleCheckpoint;
		DDNet.m_StrongWeakId = pCharacter->GetStrongWeakId();
		DDNet.m_JumpedTotal = PreCore.m_JumpedTotal;
		DDNet.m_NinjaActivationTick = PreCore.m_Ninja.m_ActivationTick;
		DDNet.m_FreezeStart = PreCore.m_FreezeStart;
		DDNet.m_TargetX = PreCore.m_Input.m_TargetX;
		DDNet.m_TargetY = PreCore.m_Input.m_TargetY;
		DDNet.m_TuneZoneOverride = pCharacter->GetTuneZoneOverride();
		NewFrame.m_HasCoordinates = 1;
		NewFrame.m_ClientId = m_MacroPracticeLocalId;
	}

	// Apply the selected input only to the controlled practice tee. The other
	// local tee receives an idle input; it is still simulated so interactions,
	// momentum and map objects are reproduced when the timeline is rebuilt.
	pCharacter->OnDirectInput(&TickInput);
	pCharacter->OnPredictedInput(&TickInput);

	const int OwnId0 = GameClient()->m_aLocalIds[0];
	const int OwnId1 = GameClient()->m_aLocalIds[1];
	const int OtherLocalId = OwnId0 == m_MacroPracticeLocalId ? OwnId1 : OwnId0;
	CCharacter *pPracticeOther = m_MacroPracticeWorld.GetCharacterById(OtherLocalId);
	if(pPracticeOther && pPracticeOther != pCharacter)
	{
		CNetObj_PlayerInput IdleInput{};
		IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		IdleInput.m_TargetX = 1;
		pPracticeOther->OnDirectInput(&IdleInput);
		pPracticeOther->OnPredictedInput(&IdleInput);
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

	if(m_MacroRecording)
	{
		pCharacter->Core()->Write(&NewFrame.m_Core);
		NewFrame.m_Weapon = pCharacter->GetActiveWeapon();
		NewFrame.m_AttackTick = pCharacter->GetAttackTick();
		NewFrame.m_Tick = m_MacroPracticeTick;
		NewFrame.m_FreezeTime = pCharacter->m_FreezeTime;
		NewFrame.m_FrozenLastTick = pCharacter->m_FrozenLastTick ? 1 : 0;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			NewFrame.m_aWeaponAmmo[Weapon] = pCharacter->Core()->m_aWeapons[Weapon].m_Ammo;
		static_cast<CNetObj_CharacterCore &>(NewFrame.m_Character) = NewFrame.m_Core;
		NewFrame.m_Character.m_Weapon = NewFrame.m_Weapon;
		NewFrame.m_Character.m_AttackTick = NewFrame.m_AttackTick;
		NewFrame.m_Character.m_Tick = NewFrame.m_Tick;
		m_vMacroFrames.push_back(NewFrame);
		if(m_vMacroFrames.size() >= MACRO_MAX_FRAMES)
		{
			GameClient()->Echo("Запись достигла ограничения в 18000 тиков. Макрос сохраняется.");
			StopMacroRecording();
			return;
		}
	}
	else if(m_MacroPlaying && ReplayRecordedFrame)
	{
		const int PlayedFrameIndex = (int)m_MacroPlaybackIndex;
		const SMacroFrame &PlayedFrame = m_vMacroFrames[m_MacroPlaybackIndex];
		++m_MacroPlaybackIndex;
		m_MacroDisplayedTick = PlayedFrameIndex + 1;
		if(PlayedFrame.m_HasCoordinates)
		{
			CCharacterCore RecordedCore = pCharacter->GetCore();
			RecordedCore.Read(&PlayedFrame.m_Core);
			if(PlayedFrame.m_HasDDNetState)
			{
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
					RecordedCore.m_aWeapons[Weapon].m_Ammo = PlayedFrame.m_aWeaponAmmo[Weapon];
			}
			pCharacter->SetCore(RecordedCore);
			pCharacter->m_Pos = RecordedCore.m_Pos;
			pCharacter->m_PrevPos = RecordedCore.m_Pos;
			pCharacter->m_PrevPrevPos = RecordedCore.m_Pos;
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
	m_MacroStartFrameIndex = 0;
	m_MacroDisplayedTick = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0f;
	m_MacroPracticeLastTime = time_get();
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();
	m_vMacroFrames.clear();
	m_MacroRecordingPaused = false;
	m_MacroRecording = true;

	// Keep an exact copy of the entire local practice world at the beginning
	// of recording. Rewind restores this snapshot and simulates every frame
	// back to the selected time, including the idle dummy, projectiles and pickups.
	m_MacroPracticeStartWorld.CopyWorld(&m_MacroPracticeWorld);
	m_MacroPracticeStartWorld.m_pParent = nullptr;
	m_MacroPracticeStartWorld.m_pChild = nullptr;
	m_MacroPracticeWorld.m_pParent = nullptr;
	m_MacroPracticeWorld.m_pChild = nullptr;
	m_MacroPracticeStartLocalId = m_MacroPracticeLocalId;
	std::copy(std::begin(m_aMacroPracticeSnapshotInitialized), std::end(m_aMacroPracticeSnapshotInitialized),
		std::begin(m_aMacroPracticeStartSnapshotInitialized));
	m_MacroPracticeBaselineValid = true;
	str_copy(m_aMacroPracticeBaselineName, aRecordingName);

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
		SendMacroFrameControl(EMacroFrameMode::STOP);
	if(m_MacroPlaybackLive)
	{
		const int Dummy = g_Config.m_ClDummy;
		m_aInputData[Dummy].m_Direction = 0;
		m_aInputData[Dummy].m_Jump = 0;
		m_aInputData[Dummy].m_Hook = 0;
		if(m_aInputData[Dummy].m_Fire & 1)
			m_aInputData[Dummy].m_Fire++;
		m_aInputData[Dummy].m_Fire &= INPUT_STATE_MASK;
		m_aInputData[Dummy].m_WantedWeapon = 0;
		m_aInputData[Dummy].m_NextWeapon = 0;
		m_aInputData[Dummy].m_PrevWeapon = 0;
	}

	m_MacroPlaying = false;
	m_MacroRecordingPaused = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackPaused = false;
	m_MacroPlaybackOwnsPractice = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroRewindHeld = false;
	m_MacroRewindHeldSince = 0;
	m_MacroRewindNextRepeat = 0;
	if(WasRecording)
		m_MacroPlaybackIndex = m_vMacroFrames.size();
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPracticeWaiting = false;

	// Keep the isolated world and its starting snapshot after recording. This
	// lets the saved macro play in the same practice world and makes recording
	// rewind/re-record deterministic. A playback-owned world is still cleaned up.
	if(m_MacroPracticeActive && StopOwnedPractice)
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
	{
		Valid = io_read(File, aRecordedMapName, sizeof(aRecordedMapName)) == sizeof(aRecordedMapName);
		aRecordedMapName[sizeof(aRecordedMapName) - 1] = '\0';
	}

	const int64_t HeaderBytes = (int64_t)sizeof(Magic) + sizeof(Version) + sizeof(Count) +
		(Version >= 3 ? (int64_t)sizeof(aRecordedMapName) : 0);
	const int64_t RemainingBytes = Valid && FileLength >= HeaderBytes ? FileLength - HeaderBytes : 0;
	if(Valid && FileLength < HeaderBytes)
		Valid = false;
	const int64_t CurrentFrameBytes = (int64_t)Count * sizeof(SMacroFrame);
	const int64_t Version7FrameBytes = (int64_t)Count * sizeof(SMacroFrameV7);
	const int64_t Version6FrameBytes = (int64_t)Count * sizeof(SMacroFrameV6);
	const int64_t Version5FrameBytes = (int64_t)Count * sizeof(SMacroFrameV4);
	const int64_t Version4FrameBytes = (int64_t)Count * sizeof(SMacroFrameV4);
	const int64_t Version3FrameBytes = (int64_t)Count * sizeof(SMacroFrameV3);
	const int64_t LegacyFrameBytes = (int64_t)Count * sizeof(SMacroFrameV1);
	std::vector<SMacroFrame> Frames;
	auto CopyVersion7Frames = [&](const std::vector<SMacroFrameV7> &OldFrames) {
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
			Frames[i].m_PreFreezeTime = OldFrames[i].m_PreFreezeTime;
			Frames[i].m_FreezeTime = OldFrames[i].m_FreezeTime;
			Frames[i].m_PreFrozenLastTick = OldFrames[i].m_PreFrozenLastTick;
			Frames[i].m_FrozenLastTick = OldFrames[i].m_FrozenLastTick;
			Frames[i].m_PreAttackTick = OldFrames[i].m_PreAttackTick;
			Frames[i].m_HasDDNetState = OldFrames[i].m_HasDDNetState;
			Frames[i].m_PreDDNetCharacter = OldFrames[i].m_PreDDNetCharacter;
			// Older macros did not store ammo for every weapon. Treat those
			// legacy frames as unlimited instead of restoring zero ammo.
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			{
				Frames[i].m_aPreWeaponAmmo[Weapon] = -1;
				Frames[i].m_aWeaponAmmo[Weapon] = -1;
			}
		}
	};
	auto CopyVersion6Frames = [&](const std::vector<SMacroFrameV6> &OldFrames) {
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
			Frames[i].m_PreFreezeTime = OldFrames[i].m_PreFreezeTime;
			Frames[i].m_FreezeTime = OldFrames[i].m_FreezeTime;
			Frames[i].m_PreFrozenLastTick = OldFrames[i].m_PreFrozenLastTick;
			Frames[i].m_FrozenLastTick = OldFrames[i].m_FrozenLastTick;
			Frames[i].m_PreAttackTick = 0;
			Frames[i].m_HasDDNetState = 0;
			Frames[i].m_PreDDNetCharacter = {};
		}
	};
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
			Frames[i].m_PreFreezeTime = 0;
			Frames[i].m_FreezeTime = 0;
			Frames[i].m_PreFrozenLastTick = 0;
			Frames[i].m_FrozenLastTick = 0;
			Frames[i].m_PreAttackTick = 0;
			Frames[i].m_HasDDNetState = 0;
			Frames[i].m_PreDDNetCharacter = {};
		}
	};
	if(Valid && Version >= 8)
	{
		Frames.resize(Count);
		Valid = RemainingBytes == CurrentFrameBytes &&
			io_read(File, Frames.data(), CurrentFrameBytes) == CurrentFrameBytes;
	}
	else if(Valid && Version == 7)
	{
		std::vector<SMacroFrameV7> OldFrames(Count);
		Valid = RemainingBytes == Version7FrameBytes &&
			io_read(File, OldFrames.data(), Version7FrameBytes) == Version7FrameBytes;
		if(Valid)
			CopyVersion7Frames(OldFrames);
	}
	else if(Valid && Version == 6)
	{
		std::vector<SMacroFrameV6> OldFrames(Count);
		Valid = RemainingBytes == Version6FrameBytes &&
			io_read(File, OldFrames.data(), Version6FrameBytes) == Version6FrameBytes;
		if(Valid)
			CopyVersion6Frames(OldFrames);
	}
	else if(Valid && Version == 5)
	{
		if(RemainingBytes == Version6FrameBytes)
		{
			std::vector<SMacroFrameV6> OldFrames(Count);
			Valid = io_read(File, OldFrames.data(), Version6FrameBytes) == Version6FrameBytes;
			if(Valid)
				CopyVersion6Frames(OldFrames);
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

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		GameClient()->Echo("Для воспроизведения нужен активный локальный персонаж.");
		return false;
	}

	char aCurrentMapName[64] = "Неизвестная карта";
	const char *pCurrentMapName = Client()->MapDownloadName();
	if(pCurrentMapName && pCurrentMapName[0])
	{
		const char *pBaseName = pCurrentMapName;
		for(const char *p = pCurrentMapName; *p; ++p)
			if(*p == '/' || *p == '\\')
				pBaseName = p + 1;
		str_copy(aCurrentMapName, pBaseName);
		for(char *p = aCurrentMapName; *p; ++p)
		{
			if(*p == '.')
			{
				*p = '\0';
				break;
			}
		}
	}

	if(aRecordedMapName[0] && aCurrentMapName[0] && str_comp_nocase(aRecordedMapName, aCurrentMapName) != 0)
	{
		char aMessage[192];
		str_format(aMessage, sizeof(aMessage), "Макрос записан на карте «%s», а сейчас загружена «%s». Подключись к исходной карте.",
			aRecordedMapName, aCurrentMapName);
		GameClient()->Echo(aMessage);
		return false;
	}

	// Saved playback always uses the isolated local world. If this is the
	// macro that was just recorded, keep its original full-world baseline so
	// the dummy and all practice entities rewind to the actual recording start.
	const bool OwnsPractice = !m_MacroPracticeActive;
	if(OwnsPractice && !StartMacroPractice())
		return false;

	const bool ReuseRecordingBaseline = m_MacroPracticeBaselineValid &&
		str_comp(m_aMacroPracticeBaselineName, pName) == 0;
	if(!ReuseRecordingBaseline)
	{
		m_MacroPracticeStartWorld.CopyWorld(&m_MacroPracticeWorld);
		m_MacroPracticeStartWorld.m_pParent = nullptr;
		m_MacroPracticeStartWorld.m_pChild = nullptr;
		m_MacroPracticeWorld.m_pParent = nullptr;
		m_MacroPracticeWorld.m_pChild = nullptr;
		m_MacroPracticeStartLocalId = m_MacroPracticeLocalId;
		std::copy(std::begin(m_aMacroPracticeSnapshotInitialized),
			std::end(m_aMacroPracticeSnapshotInitialized),
			std::begin(m_aMacroPracticeStartSnapshotInitialized));
		m_MacroPracticeBaselineValid = true;
		str_copy(m_aMacroPracticeBaselineName, pName);
	}

	const int StartFrameIndex = std::clamp(g_Config.m_ClMacroStartTick - 1, 0, (int)Frames.size() - 1);
	m_vMacroFrames = std::move(Frames);
	m_MacroRecording = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackOwnsPractice = OwnsPractice;
	m_MacroPlaybackOwnsDummy = false;
	m_MacroPlaying = true;
	m_MacroStartFrameIndex = StartFrameIndex;
	// Rebuild all frames before the requested start tick in the same practice
	// world. This restores the dummy, pickups, projectiles and switchers too.
	m_MacroPlaybackIndex = 0;
	m_MacroDisplayedTick = 0;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPlaybackPaused = false;
	m_MacroRecordingPaused = false;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0;
	m_MacroPracticeLastTime = time_get();
	m_MacroPlaybackLastTick = -1;
	if(!RebuildMacroWorldToPosition(StartFrameIndex))
	{
		m_MacroPlaying = false;
		m_MacroPlaybackOwnsPractice = false;
		GameClient()->Echo("Не удалось восстановить начальное состояние мира для макроса.");
		return false;
	}
	m_MacroPlaybackIndex = (std::size_t)StartFrameIndex;
	m_MacroDisplayedTick = StartFrameIndex + 1;

	char aStartMessage[192];
	str_format(aStartMessage, sizeof(aStartMessage), "Макрос «%s» запущен в отдельном тренировочном мире. Скорость: %d тиков/с. /practice не нужен.",
		pName, SERVER_TICK_SPEED);
	GameClient()->Echo(aStartMessage);
	return true;
}
bool CControls::DeleteMacro(const char *pName)
{
	char aPath[128];
	return MacroFilePath(pName, aPath, sizeof(aPath)) && Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
}

void CControls::ConMacroPractice(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->m_MacroRecording)
	{
		pSelf->GameClient()->Echo("Сначала останови запись макроса.");
		return;
	}

	if(pSelf->m_MacroPracticeActive)
	{
		pSelf->StopMacroPractice();
		pSelf->GameClient()->Echo("FP-практика выключена.");
	}
	else
	{
		pSelf->StartMacroPractice();
	}
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

void CControls::ConMacroPlayFirst(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CControls *pSelf = (CControls *)pUserData;
	if(!pSelf->PlayMacro(nullptr))
		pSelf->GameClient()->Echo("Не удалось запустить первый сохранённый макрос. Проверьте, что макрос сохранён.");
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
			pSelf->SeekMacro(-pSelf->MacroSeekStep(), true, true);
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

void CControls::ConMacroGoto(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->GameClient()->m_Chat.IsActive() || pSelf->GameClient()->m_Menus.IsActive())
		return;

	const int TargetTick = pResult->GetInteger(0);
	if(TargetTick < 1)
	{
		pSelf->GameClient()->Echo("Укажи номер тика от 1 до длины макроса.");
		return;
	}
	const int CurrentTick = pSelf->m_MacroRecording ? pSelf->MacroFrameCount() :
		(pSelf->m_MacroDisplayedTick > 0 ? pSelf->m_MacroDisplayedTick : (int)pSelf->m_MacroPlaybackIndex);
	pSelf->SeekMacro(TargetTick - CurrentTick, true, true);
}

void CControls::ToggleMacroPause()
{
	if(!m_MacroPracticeActive)
		return;

	if(m_MacroRecording)
	{
		m_MacroRecordingPaused = !m_MacroRecordingPaused;
		m_MacroPracticeAccumulator = 0.0;
		m_MacroPracticeLastTime = time_get();
		GameClient()->Echo(m_MacroRecordingPaused ? "Запись макроса на паузе." : "Запись макроса продолжена.");
		return;
	}

	if(!m_MacroPlaying)
		return;

	m_MacroPlaybackPaused = !m_MacroPlaybackPaused;
	m_MacroPracticeAccumulator = 0.0;
	if(m_MacroPlaybackPaused)
	{
		GameClient()->Echo("Макрос на паузе.");
	}
	else
	{
		m_MacroPracticeLastTime = time_get();
		GameClient()->Echo("Воспроизведение макроса продолжено.");
	}
}

void CControls::ConMacroPause(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	((CControls *)pUserData)->ToggleMacroPause();
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
	return std::clamp(g_Config.m_ClMacroPracticeRate, 1, 50);
}

bool CControls::RebuildMacroWorldToPosition(int Position)
{
	if(!m_MacroPracticeActive || !m_MacroPracticeBaselineValid ||
		Position < 0 || Position > (int)m_vMacroFrames.size())
	{
		return false;
	}

	const bool WasRecording = m_MacroRecording;
	const bool WasRecordingPaused = m_MacroRecordingPaused;
	const bool WasPlaying = m_MacroPlaying;
	const bool WasPlaybackPaused = m_MacroPlaybackPaused;
	const bool WasPlaybackOwnsPractice = m_MacroPlaybackOwnsPractice;

	// Replace the complete practice world, not just the active tee. This
	// restores the second tee, all moving entities, pickups and switchers.
	m_MacroPracticeWorld.CopyWorld(&m_MacroPracticeStartWorld);
	m_MacroPracticeWorld.m_pParent = nullptr;
	m_MacroPracticeWorld.m_pChild = nullptr;
	m_MacroPracticeStartWorld.m_pParent = nullptr;
	m_MacroPracticeStartWorld.m_pChild = nullptr;
	m_MacroPracticeLocalId = m_MacroPracticeStartLocalId;
	m_MacroPracticeWorld.m_LocalClientId = m_MacroPracticeLocalId;
	std::copy(std::begin(m_aMacroPracticeStartSnapshotInitialized),
		std::end(m_aMacroPracticeStartSnapshotInitialized),
		std::begin(m_aMacroPracticeSnapshotInitialized));
	m_MacroPracticeTick = m_MacroPracticeWorld.GameTick();

	m_MacroPracticePendingFireTransitions = 0;
	m_MacroPracticePendingNextWeaponTransitions = 0;
	m_MacroPracticePendingPrevWeaponTransitions = 0;
	m_MacroPracticePendingWantedWeapon = 0;
	m_MacroPracticePendingJump = false;
	m_MacroPracticePendingHook = false;
	m_MacroPracticePendingDirectionLeft = false;
	m_MacroPracticePendingDirectionRight = false;
	m_MacroPracticeDirectionLeft = false;
	m_MacroPracticeDirectionRight = false;
	m_MacroPracticeFireCounter = 0;
	m_MacroPracticeFirePressed = false;
	m_MacroPracticeInput = {};
	m_MacroPracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_MacroPracticeInput.m_TargetX = 1;
	m_MacroPracticeAccumulator = 0.0;
	m_MacroPracticeWaiting = false;

	// Replay the timeline from its original full-world snapshot. SimulateMacro
	// applies the saved inputs to the cloned CGameWorld and advances every
	// entity in it, so seeking backward restores the dummy and the rest of the
	// practice scene to that exact point instead of leaving them in the future.
	m_MacroRecording = false;
	m_MacroPlaying = true;
	m_MacroPlaybackPaused = false;
	m_MacroPlaybackIndex = 0;
	m_MacroDisplayedTick = 0;
	CNetObj_PlayerInput IdleInput{};
	IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	IdleInput.m_TargetX = 1;
	for(int i = 0; i < Position; ++i)
	{
		SimulateMacroPracticeTick(IdleInput, IdleInput);
		if(!m_MacroPracticeActive)
			return false;
	}

	m_MacroRecording = WasRecording;
	m_MacroRecordingPaused = WasRecording ? true : WasRecordingPaused;
	m_MacroPlaying = WasPlaying;
	m_MacroPlaybackPaused = WasPlaybackPaused;
	m_MacroPlaybackOwnsPractice = WasPlaybackOwnsPractice;
	m_MacroPlaybackIndex = (std::size_t)Position;
	m_MacroDisplayedTick = Position;
	m_MacroPracticeWaiting = false;
	m_MacroPracticeAccumulator = 0.0;
	m_MacroPracticeLastTime = time_get();
	m_MacroPlaybackLastTick = -1;
	UpdateMacroPracticeGhost(m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId));
	return true;
}

bool CControls::SeekMacro(int Offset, bool ShowMessage, bool TeleportLive)
{
	(void)TeleportLive;
	if(!m_MacroPracticeActive || m_vMacroFrames.empty())
		return false;

	if(m_MacroRecording)
	{
		// Recording rewind moves the whole practice world backward and truncates
		// the future. Recording remains paused until the user resumes it.
		const int CurrentPosition = (int)m_vMacroFrames.size();
		const int TargetPosition = std::clamp(CurrentPosition + Offset, 0, CurrentPosition);
		if(TargetPosition == CurrentPosition)
			return false;
		m_MacroRecordingPaused = true;
		if(!RebuildMacroWorldToPosition(TargetPosition))
			return false;
		m_vMacroFrames.resize(TargetPosition);
		m_MacroPlaybackIndex = (std::size_t)TargetPosition;
		m_MacroDisplayedTick = TargetPosition;
		if(ShowMessage)
		{
			char aMessage[128];
			str_format(aMessage, sizeof(aMessage), "Запись отмотана: тик %d / %d. Будущее будет перезаписано.",
				TargetPosition, (int)m_vMacroFrames.size());
			GameClient()->Echo(aMessage);
		}
		return true;
	}

	if(!m_MacroPlaying && !m_MacroPlaybackLive)
		return false;

	const int CurrentIndex = m_MacroDisplayedTick > 0 ? m_MacroDisplayedTick - 1 :
		(m_MacroHasCurrentFrame ? (int)m_MacroPlaybackIndex - 1 : (int)m_MacroPlaybackIndex);
	const int Current = std::clamp(CurrentIndex, 0, (int)m_vMacroFrames.size() - 1);
	const int Target = std::clamp(Current + Offset, 0, (int)m_vMacroFrames.size() - 1);
	if(Target == Current)
		return false;

	if(m_MacroPlaybackLive)
	{
		SendMacroFrame(Target, m_MacroPlaybackPaused ? EMacroFrameMode::RESTORE_PAUSED : EMacroFrameMode::STEP);
		m_MacroPlaybackIndex = (std::size_t)Target + 1;
		m_MacroDisplayedTick = Target + 1;
		m_MacroHasCurrentFrame = false;
		m_MacroPlaybackLastTick = Client()->PredGameTick(g_Config.m_ClDummy);
		m_MacroPracticeAccumulator = 0.0;
		m_MacroPracticeWaiting = false;
	}
	else
	{
		if(!RebuildMacroWorldToPosition(Target + 1))
			return false;
		m_MacroPlaybackIndex = (std::size_t)Target + 1;
		m_MacroDisplayedTick = Target + 1;
	}

	if(ShowMessage)
	{
		char aMessage[128];
		str_format(aMessage, sizeof(aMessage), "Позиция макроса: тик %d / %d (шаг %d тиков)",
			Target + 1, (int)m_vMacroFrames.size(), MacroSeekStep());
		GameClient()->Echo(aMessage);
	}
	return true;
}

bool CControls::OnInput(const IInput::CEvent &)
{
	// Never consume gameplay keys here. Macro seek/rewind is handled by
	// explicit bindable commands so movement binds keep working in practice.
	return false;
}
void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons && m_MacroPracticeActive)
		{
			const int WantedWeapon = pMsg->m_Weapon + 1;
			if(MacroPracticeRate() < SERVER_TICK_SPEED)
				m_MacroPracticePendingWantedWeapon = WantedWeapon;
			else
				m_MacroPracticeInput.m_WantedWeapon = WantedWeapon;
		}
		else if(g_Config.m_ClAutoswitchWeapons)
		{
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = pMsg->m_Weapon + 1;
		}
		// The exact ammo count is known once we switch to the weapon, but
		// a non-zero value is enough here for the regular autoswitch logic.
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
void CControls::OnUpdate()
{
	if(!m_MacroPracticeActive)
		return;

	// The live prediction buffers are intentionally never read or modified here.
	// Standard binds populate m_MacroPracticeInput through the isolated handlers.
	PopulateMacroPracticeWorld();

	const int Dummy = g_Config.m_ClDummy;
	CNetObj_PlayerInput PracticeInput = m_MacroPracticeInput;
	PracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

	vec2 PracticeAim = m_aMousePos[Dummy];
	if(g_Config.m_ClSubTickAiming && m_aMousePosOnAction[Dummy] != vec2(0.0f, 0.0f))
	{
		PracticeAim = m_aMousePosOnAction[Dummy];
		m_aMousePosOnAction[Dummy] = vec2(0.0f, 0.0f);
	}
	PracticeInput.m_TargetX = (int)PracticeAim.x;
	PracticeInput.m_TargetY = (int)PracticeAim.y;
	if(PracticeInput.m_TargetX == 0 && PracticeInput.m_TargetY == 0)
		PracticeInput.m_TargetX = 1;
	PracticeInput.m_Direction = m_MacroPracticeDirectionLeft == m_MacroPracticeDirectionRight ? 0 :
		(m_MacroPracticeDirectionLeft ? -1 : 1);

	const bool SlowPracticeInput = !m_MacroPlaying && MacroPracticeRate() < SERVER_TICK_SPEED;
	if(SlowPracticeInput)
	{
		if(PracticeInput.m_WantedWeapon > 0)
			m_MacroPracticePendingWantedWeapon = PracticeInput.m_WantedWeapon;
		PracticeInput.m_WantedWeapon = 0;
	}

	// The second local tee is deliberately AFK: it gets no copied movement,
	// cursor, weapon or fire input. It still participates in world physics and
	// can be moved by hooks, hammer hits, projectiles and map interactions.
	CNetObj_PlayerInput PracticeDummyInput{};
	PracticeDummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	PracticeDummyInput.m_TargetX = 1;

	const int64_t CurrentTime = time_get();
	if(m_MacroPracticeLastTime == 0)
		m_MacroPracticeLastTime = CurrentTime;

	// Do not simulate while the player is typing or viewing a menu. Paused
	// recording and paused playback both keep an exact, stationary world.
	const bool UiCapturesInput = GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive();
	const bool MacroPaused = (m_MacroRecording && m_MacroRecordingPaused) ||
		(m_MacroPlaying && m_MacroPlaybackPaused);
	if(UiCapturesInput || MacroPaused)
	{
		m_MacroPracticeLastTime = CurrentTime;
		return;
	}

	const int64_t ElapsedTime = std::max<int64_t>(0, CurrentTime - m_MacroPracticeLastTime);
	m_MacroPracticeLastTime = CurrentTime;
	const int PracticeRate = m_MacroPlaying ? SERVER_TICK_SPEED : MacroPracticeRate();
	m_MacroPracticeAccumulator += (double)ElapsedTime / (double)time_freq() * PracticeRate;

	int Steps = 0;
	const int MaxStepsPerUpdate = std::max<int>(SERVER_TICK_SPEED, PracticeRate);
	bool ConsumeWantedWeapon = PracticeInput.m_WantedWeapon > 0;
	while(m_MacroPracticeAccumulator >= 1.0 && Steps < MaxStepsPerUpdate)
	{
		SimulateMacroPracticeTick(PracticeInput, PracticeDummyInput);
		m_MacroPracticeAccumulator -= 1.0;
		++Steps;
		if(ConsumeWantedWeapon)
		{
			// Direct weapon selection is a one-tick event, not a held input.
			PracticeInput.m_WantedWeapon = 0;
			m_MacroPracticeInput.m_WantedWeapon = 0;
			ConsumeWantedWeapon = false;
		}
		if(!m_MacroPracticeActive)
			break;
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
		// The private world advances from the normal client update loop.
		// SnapInput only sends neutral packets to both real server connections.
		const int Dummy = g_Config.m_ClDummy;
		CNetObj_PlayerInput ServerInput{};
		ServerInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		ServerInput.m_TargetX = 1;
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
			MacroLiveInput = true;
			const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
			if(!m_MacroPlaybackPaused)
			{
				if(m_MacroPlaybackLastTick < 0)
					m_MacroPlaybackLastTick = PredTick;
				const int TickDelta = std::max(0, PredTick - m_MacroPlaybackLastTick);
				m_MacroPlaybackLastTick = PredTick;
				m_MacroPracticeAccumulator +=
					(double)TickDelta * (double)MacroPracticeRate() / (double)SERVER_TICK_SPEED;

				// At most one frame is sent per input update. Backlog is drained
				// over later updates so two frames are never collapsed into one server tick.
				if(m_MacroPracticeAccumulator >= 1.0)
				{
					m_MacroPracticeAccumulator -= 1.0;
					if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
					{
						if(g_Config.m_ClMacroLoop && !m_vMacroFrames.empty())
							m_MacroPlaybackIndex = (std::size_t)std::clamp(m_MacroStartFrameIndex, 0, (int)m_vMacroFrames.size() - 1);
						else
							StopMacroRecording();
					}

					if(m_MacroPlaying && m_MacroPlaybackLive && m_MacroPlaybackIndex < m_vMacroFrames.size())
					{
						const int FrameIndex = (int)m_MacroPlaybackIndex;
						SendMacroFrame(FrameIndex, EMacroFrameMode::STEP);
						++m_MacroPlaybackIndex;
						m_MacroDisplayedTick = FrameIndex + 1;
					}
				}
			}

			// The custom macro-frame message controls the server tee. Normal
			// client input remains neutral until macro_stop sends STOP.
			const int PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;
			m_aInputData[g_Config.m_ClDummy] = {};
			m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PlayerFlags;
			m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;
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

		if(m_MacroRewindHeld && !m_vMacroFrames.empty() &&
		(m_MacroPracticeActive || m_MacroPlaybackLive) &&
		!GameClient()->m_Chat.IsActive() && !GameClient()->m_Menus.IsActive())
	{
		const int64_t Now = time_get();
		if(Now >= m_MacroRewindNextRepeat)
		{
			SeekMacro(-MacroSeekStep(), false, true);
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

	RenderMacroHud();
}

void CControls::RenderMacroHud()
{
	if(!m_MacroPracticeActive && !m_MacroPlaybackLive)
		return;

	const CScreenRect PreviousScreen = Graphics()->GetScreen();
	const float ScreenWidth = Graphics()->ScreenWidth();
	const float ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->MapScreenToSize(ScreenWidth, ScreenHeight);

	char aTickLabel[128];
	if(m_MacroRecording)
		str_format(aTickLabel, sizeof(aTickLabel), "%s  |  ТИК %d  |  %d ТИК/С",
			m_MacroRecordingPaused ? "ЗАПИСЬ ПАУЗА" : "ЗАПИСЬ",
			(int)m_vMacroFrames.size(), MacroPracticeRate());
	else if(m_MacroPlaying)
		str_format(aTickLabel, sizeof(aTickLabel), "%s  |  ТИК %d / %d  |  %d ТИК/С",
			m_MacroPlaybackPaused ? "МАКРОС ПАУЗА" : "МАКРОС",
			std::clamp(m_MacroDisplayedTick, 1, (int)m_vMacroFrames.size()),
			(int)m_vMacroFrames.size(), MacroPracticeRate());
	else
		str_format(aTickLabel, sizeof(aTickLabel), "FP-ТРЕНИРОВКА  |  ТИК %d  |  %d ТИК/С",
			m_MacroPracticeTick, MacroPracticeRate());

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
