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
#include <string>
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
	m_MacroPlaybackStartPending = false;
	m_MacroPlaybackManualDummyOverride = false;
	m_MacroPlaybackLastTick = -1;
	m_MacroStopAtTickArmed = false;
	m_MacroPlaybackSeekTargetIndex = -1;
	std::fill(std::begin(m_aMacroPlaybackWeaponOverride), std::end(m_aMacroPlaybackWeaponOverride), -1);
	std::fill(std::begin(m_aMacroPickupWeaponOverride), std::end(m_aMacroPickupWeaponOverride), -1);
	std::fill(std::begin(m_aMacroLiveFireOffset), std::end(m_aMacroLiveFireOffset), 0);
	std::fill(std::begin(m_aMacroLiveNextWeaponOffset), std::end(m_aMacroLiveNextWeaponOffset), 0);
	std::fill(std::begin(m_aMacroLivePrevWeaponOffset), std::end(m_aMacroLivePrevWeaponOffset), 0);
	std::fill(std::begin(m_aMacroManualDirectionActive), std::end(m_aMacroManualDirectionActive), false);
	std::fill(std::begin(m_aMacroManualJumpActive), std::end(m_aMacroManualJumpActive), false);
	std::fill(std::begin(m_aMacroManualHookActive), std::end(m_aMacroManualHookActive), false);
	std::fill(std::begin(m_aMacroManualAimActive), std::end(m_aMacroManualAimActive), false);
	std::fill(std::begin(m_aMacroPlaybackConnectionUsed), std::end(m_aMacroPlaybackConnectionUsed), false);
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

	if(!pSelf->m_MacroPracticeActive && pSelf->GameClient()->m_GameInfo.m_BugDDRaceInput && pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	const int Dummy = pSelf->m_MacroPlaybackLive ? pSelf->m_MacroPlaybackDummy : g_Config.m_ClDummy;

	// After the user touches an action, its live state replaces the recorded state,
	// including key release, so manual intervention can cancel a held TAS action.
	if(pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying))
	{
		if(pState->m_IsDirectionLeft || pState->m_IsDirectionRight)
			pSelf->m_aMacroManualDirectionActive[Dummy] = true;
		if(pState->m_IsJumpAction)
			pSelf->m_aMacroManualJumpActive[Dummy] = true;
		if(pState->m_IsHookAction)
			pSelf->m_aMacroManualHookActive[Dummy] = true;
	}

	// Practice controls must never enter the real DDNet input buffers. Those
	// buffers are also consumed by the live prediction world, not only SnapInput.
	if(pSelf->m_MacroPracticeActive)
	{
		bool HandledPracticeInput = false;
		if(pState->m_IsDirectionLeft)
		{
			pSelf->m_MacroPracticeDirectionLeft = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingDirectionLeft = true;
			HandledPracticeInput = true;
		}
		if(pState->m_IsDirectionRight)
		{
			pSelf->m_MacroPracticeDirectionRight = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingDirectionRight = true;
			HandledPracticeInput = true;
		}
		if(pState->m_IsJumpAction)
		{
			pSelf->m_MacroPracticeInput.m_Jump = Stroke;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingJump = true;
			HandledPracticeInput = true;
		}
		if(pState->m_IsHookAction)
		{
			pSelf->m_MacroPracticeInput.m_Hook = Stroke;
			pSelf->m_aManualHook[Dummy] = Stroke != 0;
			if(Stroke && pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingHook = true;
			HandledPracticeInput = true;
		}
		if(HandledPracticeInput)
		{
			if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
				pSelf->GameClient()->m_Binds.m_HandlingNewBind)
			{
				pSelf->m_aMouseOnAction[Dummy] = true;
			}
			return;
		}
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

	if(!pSelf->m_MacroPracticeActive &&
		((pSelf->GameClient()->m_GameInfo.m_BugDDRaceInput && pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active) ||
			pSelf->GameClient()->m_Spectator.IsActive()))
	{
		return;
	}

	const int Stroke = pResult->GetInteger(0);
	const int Dummy = pSelf->m_MacroPlaybackLive ? pSelf->m_MacroPlaybackDummy : g_Config.m_ClDummy;

	if(pSelf->m_MacroPracticeActive && pState->m_IsFireAction)
	{
		// Keep the normal DDNet press/release counter, but in a private buffer.
		// This preserves same-tick hammer and gun taps without an extra queue tick.
		int &Fire = pSelf->m_MacroPracticeInput.m_Fire;
		if((Fire & 1) != Stroke)
		{
			++Fire;
			if(Stroke && !pSelf->m_MacroPlaying)
				++pSelf->m_MacroPracticePendingFireTransitions;
			if(pSelf->m_MacroPlaying)
				++pSelf->m_aMacroLiveFireOffset[Dummy];
		}
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
		if(pSelf->m_MacroPlaybackLive && pState->m_IsFireAction)
			++pSelf->m_aMacroLiveFireOffset[Dummy];
		if(Stroke && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming &&
			pSelf->GameClient()->m_Binds.m_HandlingNewBind)
		{
			pSelf->m_aMouseOnAction[Dummy] = true;
		}
	}
	*pVariable &= INPUT_STATE_MASK;
}


void CControls::ConAutoTriplefly(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CControls *pSelf = (CControls *)pUserData;
	pSelf->SetAutoTriplefly(!pSelf->m_AutoTripleflyBindActive);
}

void CControls::ConAutoTripleflyEnable(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	((CControls *)pUserData)->SetAutoTriplefly(true);
}

void CControls::ConAutoTripleflyDisable(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	((CControls *)pUserData)->SetAutoTriplefly(false);
}

void CControls::SetAutoTriplefly(bool Enable)
{
	if(Enable == m_AutoTripleflyBindActive)
		return;

	if(Enable)
	{
		if(Client()->State() != IClient::STATE_ONLINE ||
			!Client()->DummyConnected() ||
			GameClient()->m_aLocalIds[0] < 0 ||
			GameClient()->m_aLocalIds[1] < 0)
		{
			GameClient()->Echo("Для Triplefly подключи основной тии и дамми.");
			return;
		}

		m_AutoTripleflyPreviousAuto = g_Config.m_ClAutoTriplefly;
		m_AutoTripleflyPreviousDummyCopyMoves = g_Config.m_ClDummyCopyMoves;
		m_AutoTripleflyPreviousDummyHook = g_Config.m_ClDummyHook;

		m_AutoTripleflyBindActive = true;
		g_Config.m_ClAutoTriplefly = 1;
		g_Config.m_ClDummyCopyMoves = 1;
		g_Config.m_ClDummyHook = 0;
		// The helper adds brief hook windows and leaves hammer/fire/jump
		// settings untouched so the user's normal flight continues.
		m_TripleflyFlightActive = false;
		m_TripleflyNextHookTick = -1;
		m_TripleflyHookStartTick = -1;
		m_TripleflyHookEndTick = -1;
		m_TripleflyHookDuration = 3;
		m_TripleflyHookSent = false;
		std::fill(std::begin(m_aTripleflyTargetFound), std::end(m_aTripleflyTargetFound), false);
		std::fill(std::begin(m_aTripleflyTargetClientId), std::end(m_aTripleflyTargetClientId), -1);
		GameClient()->Echo("Triplefly активен, пока удерживается бинд. Начни флайт — дамми сам ищет третьего тии.");
	}
	else
	{
		m_AutoTripleflyBindActive = false;
		g_Config.m_ClAutoTriplefly = m_AutoTripleflyPreviousAuto;
		g_Config.m_ClDummyCopyMoves = m_AutoTripleflyPreviousDummyCopyMoves;
		g_Config.m_ClDummyHook = m_AutoTripleflyPreviousDummyHook;
		m_TripleflyFlightActive = false;
		m_TripleflyNextHookTick = -1;
		m_TripleflyHookStartTick = -1;
		m_TripleflyHookEndTick = -1;
		std::fill(std::begin(m_aTripleflyTargetFound), std::end(m_aTripleflyTargetFound), false);
		std::fill(std::begin(m_aTripleflyTargetClientId), std::end(m_aTripleflyTargetClientId), -1);
		GameClient()->m_DummyInput.m_Hook = 0;
		m_aInputData[!g_Config.m_ClDummy].m_Hook = 0;
		GameClient()->Echo("Triplefly остановлен; настройки дамми восстановлены.");
	}
}

bool CControls::TripleflyHookActive() const
{
	if(!m_AutoTripleflyBindActive || !m_TripleflyFlightActive ||
		m_TripleflyHookStartTick < 0 || m_TripleflyHookEndTick <= m_TripleflyHookStartTick)
		return false;

	const int ActiveTee = g_Config.m_ClDummy;
	if(ActiveTee < 0 || ActiveTee >= NUM_DUMMIES || !m_aTripleflyTargetFound[ActiveTee])
		return false;

	const int HookerConnection = 1 - ActiveTee;
	const int CurrentTick = Client()->GameTick(HookerConnection);
	return CurrentTick >= m_TripleflyHookStartTick && CurrentTick < m_TripleflyHookEndTick;
}
void CControls::ConMacroPracticeInput(IConsole::IResult *pResult, void *pUserData)
{
	SMacroPracticeDummyBind *pBind = (SMacroPracticeDummyBind *)pUserData;
	CControls *pSelf = pBind->m_pControls;
	const bool Pressed = pResult->GetInteger(0) != 0;
	const int Dummy = pSelf->m_MacroPlaybackLive ? pSelf->m_MacroPlaybackDummy : g_Config.m_ClDummy;

	// Weapon selection commands are one-tick events and also work while a
	// macro is playing, where they become manual overrides for that tee.
	int WantedWeapon = 0;
	switch(pBind->m_Action)
	{
	case EMacroPracticeDummyAction::WEAPON_HAMMER: WantedWeapon = WEAPON_HAMMER + 1; break;
	case EMacroPracticeDummyAction::WEAPON_GUN: WantedWeapon = WEAPON_GUN + 1; break;
	case EMacroPracticeDummyAction::WEAPON_SHOTGUN: WantedWeapon = WEAPON_SHOTGUN + 1; break;
	case EMacroPracticeDummyAction::WEAPON_GRENADE: WantedWeapon = WEAPON_GRENADE + 1; break;
	case EMacroPracticeDummyAction::WEAPON_LASER: WantedWeapon = WEAPON_LASER + 1; break;
	default: break;
	}
	if(WantedWeapon > 0)
	{
		if(!Pressed)
			return;
		if(pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying))
			pSelf->m_aMacroPlaybackWeaponOverride[Dummy] = WantedWeapon;
		else if(pSelf->m_MacroPracticeActive)
		{
			if(pSelf->MacroPracticeRate() < SERVER_TICK_SPEED)
				pSelf->m_MacroPracticePendingWantedWeapon = WantedWeapon;
			else
				pSelf->m_MacroPracticeInput.m_WantedWeapon = WantedWeapon;
		}
		else
			pSelf->m_aInputData[Dummy].m_WantedWeapon = WantedWeapon;
		return;
	}

	if(pBind->m_Action == EMacroPracticeDummyAction::NEXT_WEAPON ||
		pBind->m_Action == EMacroPracticeDummyAction::PREV_WEAPON)
	{
		int *pCounter = nullptr;
		if(pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying))
		{
			pCounter = pBind->m_Action == EMacroPracticeDummyAction::NEXT_WEAPON ?
				&pSelf->m_aMacroLiveNextWeaponOffset[Dummy] : &pSelf->m_aMacroLivePrevWeaponOffset[Dummy];
		}
		else if(pSelf->m_MacroPracticeActive)
		{
			pCounter = pBind->m_Action == EMacroPracticeDummyAction::NEXT_WEAPON ?
				&pSelf->m_MacroPracticeInput.m_NextWeapon : &pSelf->m_MacroPracticeInput.m_PrevWeapon;
		}
		else
		{
			pCounter = pBind->m_Action == EMacroPracticeDummyAction::NEXT_WEAPON ?
				&pSelf->m_aInputData[Dummy].m_NextWeapon : &pSelf->m_aInputData[Dummy].m_PrevWeapon;
		}
		if(pCounter && ((*pCounter & 1) != (Pressed ? 1 : 0)))
		{
			++(*pCounter);
			*pCounter &= INPUT_STATE_MASK;
		}
		return;
	}

	if(pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying))
	{
		if(pSelf->m_MacroPlaybackLive)
		{
			switch(pBind->m_Action)
			{
			case EMacroPracticeDummyAction::LEFT:
				pSelf->m_aMacroManualDirectionActive[Dummy] = true;
				pSelf->m_aInputDirectionLeft[Dummy] = Pressed;
				break;
			case EMacroPracticeDummyAction::RIGHT:
				pSelf->m_aMacroManualDirectionActive[Dummy] = true;
				pSelf->m_aInputDirectionRight[Dummy] = Pressed;
				break;
			case EMacroPracticeDummyAction::JUMP:
				pSelf->m_aMacroManualJumpActive[Dummy] = true;
				pSelf->m_aInputData[Dummy].m_Jump = Pressed;
				break;
			case EMacroPracticeDummyAction::HOOK:
				pSelf->m_aMacroManualHookActive[Dummy] = true;
				pSelf->m_aInputData[Dummy].m_Hook = Pressed;
				break;
			case EMacroPracticeDummyAction::FIRE:
			{
				int &Fire = pSelf->m_aInputData[Dummy].m_Fire;
				if((Fire & 1) != (Pressed ? 1 : 0))
				{
					++Fire;
					++pSelf->m_aMacroLiveFireOffset[Dummy];
				}
				Fire &= INPUT_STATE_MASK;
				break;
			}
			default: break;
			}
		}
		else
		{
			switch(pBind->m_Action)
			{
			case EMacroPracticeDummyAction::LEFT:
				pSelf->m_aMacroManualDirectionActive[Dummy] = true;
				pSelf->m_MacroPracticeDirectionLeft = Pressed;
				break;
			case EMacroPracticeDummyAction::RIGHT:
				pSelf->m_aMacroManualDirectionActive[Dummy] = true;
				pSelf->m_MacroPracticeDirectionRight = Pressed;
				break;
			case EMacroPracticeDummyAction::JUMP:
				pSelf->m_aMacroManualJumpActive[Dummy] = true;
				pSelf->m_MacroPracticeInput.m_Jump = Pressed;
				break;
			case EMacroPracticeDummyAction::HOOK:
				pSelf->m_aMacroManualHookActive[Dummy] = true;
				pSelf->m_MacroPracticeInput.m_Hook = Pressed;
				break;
			case EMacroPracticeDummyAction::FIRE:
			{
				int &Fire = pSelf->m_MacroPracticeInput.m_Fire;
				if((Fire & 1) != (Pressed ? 1 : 0))
				{
					++Fire;
					++pSelf->m_aMacroLiveFireOffset[Dummy];
				}
				Fire &= INPUT_STATE_MASK;
				break;
			}
			default: break;
			}
		}
		return;
	}
	if(!pSelf->m_MacroPracticeActive)
	{
		// The import button replaces only plain gameplay binds with these
		// aliases. Outside FP they behave exactly like the original bind.
		switch(pBind->m_Action)
		{
		case EMacroPracticeDummyAction::LEFT: pSelf->m_aInputDirectionLeft[Dummy] = Pressed; break;
		case EMacroPracticeDummyAction::RIGHT: pSelf->m_aInputDirectionRight[Dummy] = Pressed; break;
		case EMacroPracticeDummyAction::JUMP: pSelf->m_aInputData[Dummy].m_Jump = Pressed; break;
		case EMacroPracticeDummyAction::HOOK:
			pSelf->m_aInputData[Dummy].m_Hook = Pressed;
			pSelf->m_aManualHook[Dummy] = Pressed;
			break;
		case EMacroPracticeDummyAction::FIRE:
		{
			int &Fire = pSelf->m_aInputData[Dummy].m_Fire;
			if((Fire & 1) != (Pressed ? 1 : 0))
				Fire = (Fire + 1) & INPUT_STATE_MASK;
			break;
		}
		default: break;
		}
		return;
	}

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
			if(Pressed)
				++pSelf->m_MacroPracticePendingFireTransitions;
		}
		break;
	}
	case EMacroPracticeDummyAction::WEAPON_HAMMER:
	case EMacroPracticeDummyAction::WEAPON_GUN:
	case EMacroPracticeDummyAction::WEAPON_SHOTGUN:
	case EMacroPracticeDummyAction::WEAPON_GRENADE:
	case EMacroPracticeDummyAction::WEAPON_LASER:
	case EMacroPracticeDummyAction::NEXT_WEAPON:
	case EMacroPracticeDummyAction::PREV_WEAPON:
		break;
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
	default:
		break;
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
	if((pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying)) &&
		pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_WantedWeapon)
	{
		const int MacroDummy = pSelf->m_MacroPlaybackLive ? pSelf->m_MacroPlaybackDummy : g_Config.m_ClDummy;
		pSelf->m_aMacroPlaybackWeaponOverride[MacroDummy] = pSet->m_Value;
		if(pSelf->m_MacroPlaybackLive && pSelf->m_MacroPlaybackPaused && pSelf->m_MacroDisplayedTick > 0)
			pSelf->m_MacroPlaybackSeekTargetIndex = pSelf->m_MacroDisplayedTick - 1;
		return;
	}
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
	const int Dummy = pSelf->m_MacroPlaybackLive ? pSelf->m_MacroPlaybackDummy : g_Config.m_ClDummy;

	if(pSelf->m_MacroPlaybackLive || (pSelf->m_MacroPracticeActive && pSelf->m_MacroPlaying))
	{
		int *pMacroOffset = nullptr;
		if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_NextWeapon)
			pMacroOffset = &pSelf->m_aMacroLiveNextWeaponOffset[Dummy];
		else if(pSet->m_apVariables[0] == &pSelf->m_aInputData[0].m_PrevWeapon)
			pMacroOffset = &pSelf->m_aMacroLivePrevWeaponOffset[Dummy];
		int *pVariable = pSet->m_apVariables[Dummy];
		if(pMacroOffset && ((*pVariable & 1) != Stroke))
		{
			++(*pVariable);
			*pVariable &= INPUT_STATE_MASK;
			++(*pMacroOffset);
		}
		return;
	}

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
	Console()->Register("+auto_triplefly", "", CFGFLAG_CLIENT, ConAutoTripleflyEnable, this, "Hold to assist Triplefly with dummy copy and timed third-tee hook");
	Console()->Register("-auto_triplefly", "", CFGFLAG_CLIENT, ConAutoTripleflyDisable, this, "Release to stop Triplefly assist");
	Console()->Register("toggle_auto_triplefly", "", CFGFLAG_CLIENT, ConAutoTriplefly, this, "Toggle Triplefly assist on or off");

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
	Console()->Register("macro_rewind_step", "", CFGFLAG_CLIENT, ConMacroRewindStep, this, "Rewind once by the configured number of macro ticks");
	Console()->Register("macro_forward_step", "", CFGFLAG_CLIENT, ConMacroForwardStep, this, "Advance once by the configured number of macro ticks");
	Console()->Register("macro_import_binds", "", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		(void)pResult;
		((CControls *)pUserData)->ImportMacroBinds();
	}, this, "Import current game input binds into macro practice");
	Console()->Register("macro_goto", "i[tick]", CFGFLAG_CLIENT, ConMacroGoto, this, "Jump to an exact macro tick");
	Console()->Register("macro_stop_at_tick", "", CFGFLAG_CLIENT, ConMacroStopAtTick, this, "Stop macro at the configured tick");
	Console()->Register("macro_goto_configured", "", CFGFLAG_CLIENT, ConMacroGotoConfigured, this, "Jump to the configured macro tick");

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
	static const SMacroPracticeDummyBind s_MacroWeapon1{this, EMacroPracticeDummyAction::WEAPON_HAMMER};
	static const SMacroPracticeDummyBind s_MacroWeapon2{this, EMacroPracticeDummyAction::WEAPON_GUN};
	static const SMacroPracticeDummyBind s_MacroWeapon3{this, EMacroPracticeDummyAction::WEAPON_SHOTGUN};
	static const SMacroPracticeDummyBind s_MacroWeapon4{this, EMacroPracticeDummyAction::WEAPON_GRENADE};
	static const SMacroPracticeDummyBind s_MacroWeapon5{this, EMacroPracticeDummyAction::WEAPON_LASER};
	static const SMacroPracticeDummyBind s_MacroNextWeapon{this, EMacroPracticeDummyAction::NEXT_WEAPON};
	static const SMacroPracticeDummyBind s_MacroPrevWeapon{this, EMacroPracticeDummyAction::PREV_WEAPON};
	Console()->Register("+macro_weapon1", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroWeapon1, "Select hammer in macro practice");
	Console()->Register("+macro_weapon2", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroWeapon2, "Select gun in macro practice");
	Console()->Register("+macro_weapon3", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroWeapon3, "Select shotgun in macro practice");
	Console()->Register("+macro_weapon4", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroWeapon4, "Select grenade in macro practice");
	Console()->Register("+macro_weapon5", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroWeapon5, "Select laser in macro practice");
	Console()->Register("+macro_nextweapon", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroNextWeapon, "Switch to next weapon in macro practice");
	Console()->Register("+macro_prevweapon", "", CFGFLAG_CLIENT, ConMacroPracticeInput, (void *)&s_MacroPrevWeapon, "Switch to previous weapon in macro practice");

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


void CControls::RebaseMacroPlaybackCounters(int Dummy, int PreviousFrameIndex)
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES)
		return;

	CNetObj_PlayerInput PreviousRecordedInput{};
	if(PreviousFrameIndex >= 0 && PreviousFrameIndex < (int)m_vMacroFrames.size())
		PreviousRecordedInput = m_vMacroFrames[PreviousFrameIndex].m_Input;

	// DDNet fire/weapon fields are transition counters, not booleans. Map
	// the recorded timeline onto the live connection's last sent counters so
	// replaying the same file again cannot reuse stale counter values.
	m_aMacroLiveFireOffset[Dummy] =
		(m_aLastData[Dummy].m_Fire - PreviousRecordedInput.m_Fire) & INPUT_STATE_MASK;
	m_aMacroLiveNextWeaponOffset[Dummy] =
		(m_aLastData[Dummy].m_NextWeapon - PreviousRecordedInput.m_NextWeapon) & INPUT_STATE_MASK;
	m_aMacroLivePrevWeaponOffset[Dummy] =
		(m_aLastData[Dummy].m_PrevWeapon - PreviousRecordedInput.m_PrevWeapon) & INPUT_STATE_MASK;
}

void CControls::SendMacroFrame(int Index, EMacroFrameMode Mode)
{
	(void)Mode;
	if(!m_MacroPlaybackLive || Index < 0 || Index >= (int)m_vMacroFrames.size())
		return;

	const SMacroFrame &Frame = m_vMacroFrames[Index];

	// A recording can switch between the main tee and dummy. Select the
	// matching normal DDNet connection, then let SnapInput send only actions.
	if(!m_MacroPlaybackManualDummyOverride && Frame.m_ClientId >= 0)
	{
		int RequestedDummy = -1;
		if(Frame.m_ClientId == GameClient()->m_aLocalIds[0])
			RequestedDummy = 0;
		else if(Frame.m_ClientId == GameClient()->m_aLocalIds[1])
			RequestedDummy = 1;

		if(RequestedDummy >= 0 && RequestedDummy != m_MacroPlaybackDummy)
		{
			m_MacroPlaybackDummy = RequestedDummy;
			g_Config.m_ClDummy = RequestedDummy;
			RebaseMacroPlaybackCounters(RequestedDummy, Index - 1);
			m_MacroPlaybackLastTick = Client()->GameTick(m_MacroPlaybackDummy);
			m_MacroPracticeAccumulator = 0.0;
		}
	}

	// Do not restore saved coordinates, velocity, freeze, ammo or weapon state.
	// SnapInput forwards only this frame's ordinary controls to the server.
	m_MacroCurrentFrame = Frame;
	m_MacroHasCurrentFrame = true;
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
	// Version 10 also stores the second local tee in each frame.
	// Version 9 remains readable through SMacroFrameV9.
	constexpr uint32_t MACRO_FILE_VERSION = 10;
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
	m_MacroPracticeFireCounter = 0;
	m_MacroPracticeFirePressed = false;
	m_MacroPracticeInput = {};
	m_MacroPracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	m_MacroPracticeInput.m_TargetX = 1;
	m_MacroPracticeDirectionLeft = false;
	m_MacroPracticeDirectionRight = false;
	m_MacroPracticeNextWeaponPressed = false;
	m_MacroPracticePrevWeaponPressed = false;

	// Both real server connections and both client-side prediction inputs
	// stay idle for the entire practice session. Every gameplay bind writes
	// only to m_MacroPracticeInput until practice is stopped.
	for(int InputDummy = 0; InputDummy < NUM_DUMMIES; ++InputDummy)
	{
		m_aInputData[InputDummy] = {};
		m_aInputData[InputDummy].m_PlayerFlags = PLAYERFLAG_PLAYING;
		m_aInputData[InputDummy].m_TargetX = 1;
		m_aLastData[InputDummy] = m_aInputData[InputDummy];
		m_aInputDirectionLeft[InputDummy] = 0;
		m_aInputDirectionRight[InputDummy] = 0;
		m_aManualHook[InputDummy] = false;
	}
	GameClient()->m_DummyInput = {};
	GameClient()->m_DummyInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	GameClient()->m_DummyInput.m_TargetX = 1;

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
	m_MacroPracticeFireCounter = 0;
	m_MacroPracticePendingFireTransitions = 0;
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
	m_MacroPlaybackSeekTargetIndex = -1;
	std::fill(std::begin(m_aMacroPlaybackWeaponOverride), std::end(m_aMacroPlaybackWeaponOverride), -1);
	std::fill(std::begin(m_aMacroPickupWeaponOverride), std::end(m_aMacroPickupWeaponOverride), -1);
	std::fill(std::begin(m_aMacroLiveFireOffset), std::end(m_aMacroLiveFireOffset), 0);
	std::fill(std::begin(m_aMacroLiveNextWeaponOffset), std::end(m_aMacroLiveNextWeaponOffset), 0);
	std::fill(std::begin(m_aMacroLivePrevWeaponOffset), std::end(m_aMacroLivePrevWeaponOffset), 0);
	std::fill(std::begin(m_aMacroManualDirectionActive), std::end(m_aMacroManualDirectionActive), false);
	std::fill(std::begin(m_aMacroManualJumpActive), std::end(m_aMacroManualJumpActive), false);
	std::fill(std::begin(m_aMacroManualHookActive), std::end(m_aMacroManualHookActive), false);
	std::fill(std::begin(m_aMacroManualAimActive), std::end(m_aMacroManualAimActive), false);
	std::fill(std::begin(m_aMacroPlaybackConnectionUsed), std::end(m_aMacroPlaybackConnectionUsed), false);
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
	if(ClientId == GameClient()->m_aLocalIds[0])
		g_Config.m_ClDummy = 0;
	else if(ClientId == GameClient()->m_aLocalIds[1])
		g_Config.m_ClDummy = 1;
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
		if(Frame.m_HasWeaponInventoryState)
		{
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			{
				Core.m_aWeapons[Weapon].m_Got = Frame.m_aWeaponGot[Weapon] != 0;
				Core.m_aWeapons[Weapon].m_Ammo = Frame.m_aWeaponAmmo[Weapon];
			}
		}
		else if(Frame.m_HasDDNetState)
		{
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				Core.m_aWeapons[Weapon].m_Ammo = Frame.m_aWeaponAmmo[Weapon];
		}
		pCharacter->SetCore(Core);
		pCharacter->m_Pos = Core.m_Pos;
		pCharacter->m_PrevPos = Core.m_Pos;
		pCharacter->m_PrevPrevPos = Core.m_Pos;
		pCharacter->SetActiveWeapon(Frame.m_Weapon);
		pCharacter->SetAttackTick(Frame.m_AttackTick);
		pCharacter->m_FreezeTime = Frame.m_FreezeTime;
		pCharacter->m_FrozenLastTick = Frame.m_FrozenLastTick != 0;

		// Restore the other locally controlled tee from the same historical
		// frame. It must not keep its present-day position or weapon state.
		if(Frame.m_SecondaryHasState)
		{
			if(CCharacter *pSecondary = m_MacroPracticeWorld.GetCharacterById(Frame.m_SecondaryClientId))
			{
				CCharacterCore SecondaryCore = pSecondary->GetCore();
				SecondaryCore.Read(&Frame.m_SecondaryCore);
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				{
					SecondaryCore.m_aWeapons[Weapon].m_Got = Frame.m_aSecondaryWeaponGot[Weapon] != 0;
					SecondaryCore.m_aWeapons[Weapon].m_Ammo = Frame.m_aSecondaryWeaponAmmo[Weapon];
				}
				pSecondary->SetCore(SecondaryCore);
				pSecondary->m_Pos = SecondaryCore.m_Pos;
				pSecondary->m_PrevPos = SecondaryCore.m_Pos;
				pSecondary->m_PrevPrevPos = SecondaryCore.m_Pos;
				pSecondary->SetActiveWeapon(Frame.m_SecondaryWeapon);
				pSecondary->SetAttackTick(Frame.m_SecondaryAttackTick);
				pSecondary->m_FreezeTime = Frame.m_SecondaryFreezeTime;
				pSecondary->m_FrozenLastTick = Frame.m_SecondaryFrozenLastTick != 0;
			}
		}
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
				if(!RebuildMacroWorldToPosition(m_MacroStartFrameIndex))
				{
					m_MacroPlaying = false;
					GameClient()->Echo("Не удалось повторить макрос: не удалось восстановить FP-мир.");
				}
				return;
			}
			m_MacroPlaying = false;
			m_MacroPlaybackPaused = false;
			m_MacroPlaybackOwnsPractice = false;
			m_MacroPracticeWaiting = false;
			m_MacroPracticeInput = {};
			m_MacroPracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			m_MacroPracticeInput.m_TargetX = 1;
			m_MacroPracticeDirectionLeft = false;
			m_MacroPracticeDirectionRight = false;
			CNetObj_PlayerInput IdleInput{};
			IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			IdleInput.m_TargetX = 1;
			pCharacter->OnPredictedInput(&IdleInput);
			pCharacter->OnDirectInput(&IdleInput);
			pCharacter->SetInput(&IdleInput);
			GameClient()->Echo("Макрос завершён. FP-мир остался на последней позиции — можешь продолжать вручную.");
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

		// Each frame is input only. The next local DDNet physics tick
		// calculates position, velocity and collisions without coordinate teleports.
		m_MacroPracticeWaiting = false;
		TickInput = Frame.m_Input;
		TickInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

		// Manual input replaces only the action that the player has touched.
		const int MacroDummy = g_Config.m_ClDummy;
		TickInput.m_Fire = (TickInput.m_Fire + m_aMacroLiveFireOffset[MacroDummy]) & INPUT_STATE_MASK;
		TickInput.m_NextWeapon = (TickInput.m_NextWeapon + m_aMacroLiveNextWeaponOffset[MacroDummy]) & INPUT_STATE_MASK;
		TickInput.m_PrevWeapon = (TickInput.m_PrevWeapon + m_aMacroLivePrevWeaponOffset[MacroDummy]) & INPUT_STATE_MASK;
		if(m_aMacroManualDirectionActive[MacroDummy])
			TickInput.m_Direction = m_MacroPracticeDirectionLeft == m_MacroPracticeDirectionRight ? 0 :
				(m_MacroPracticeDirectionLeft ? -1 : 1);
		if(m_aMacroManualJumpActive[MacroDummy])
			TickInput.m_Jump = m_MacroPracticeInput.m_Jump;
		if(m_aMacroManualHookActive[MacroDummy])
			TickInput.m_Hook = m_MacroPracticeInput.m_Hook;
		if(m_aMacroManualAimActive[MacroDummy])
		{
			TickInput.m_TargetX = LiveInput.m_TargetX;
			TickInput.m_TargetY = LiveInput.m_TargetY;
		}
		if(m_aMacroPlaybackWeaponOverride[MacroDummy] > 0)
			TickInput.m_WantedWeapon = m_aMacroPlaybackWeaponOverride[MacroDummy];
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

	if(!ReplayRecordedFrame)
	{
		if(m_MacroPracticePendingFireTransitions > 0)
		{
			int NextFire = m_MacroPracticeFireCounter;
			NextFire = (NextFire + ((NextFire & 1) ? 2 : 1)) & INPUT_STATE_MASK;
			TickInput.m_Fire = NextFire;
			m_MacroPracticeFireCounter = NextFire;
			--m_MacroPracticePendingFireTransitions;
		}
		else
		{
			// Keep a held fire pressed, or release it on the first simulated
			// tick after the physical key was released.
			const int WantedFireState = LiveInput.m_Fire & 1;
			if((m_MacroPracticeFireCounter & 1) != WantedFireState)
				m_MacroPracticeFireCounter = (m_MacroPracticeFireCounter + 1) & INPUT_STATE_MASK;
			TickInput.m_Fire = m_MacroPracticeFireCounter;
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
	pCharacter->OnPredictedInput(&TickInput);
	pCharacter->OnDirectInput(&TickInput);

	const int OwnId0 = GameClient()->m_aLocalIds[0];
	const int OwnId1 = GameClient()->m_aLocalIds[1];
	const int OtherLocalId = OwnId0 == m_MacroPracticeLocalId ? OwnId1 : OwnId0;
	CCharacter *pPracticeOther = m_MacroPracticeWorld.GetCharacterById(OtherLocalId);
	if(pPracticeOther && pPracticeOther != pCharacter)
	{
		CNetObj_PlayerInput IdleInput{};
		IdleInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
		IdleInput.m_TargetX = 1;
		pPracticeOther->OnPredictedInput(&IdleInput);
		pPracticeOther->OnDirectInput(&IdleInput);
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
		NewFrame.m_HasWeaponInventoryState = 1;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			NewFrame.m_aWeaponGot[Weapon] = pCharacter->Core()->m_aWeapons[Weapon].m_Got ? 1 : 0;
			NewFrame.m_aWeaponAmmo[Weapon] = pCharacter->Core()->m_aWeapons[Weapon].m_Ammo;
		}
		static_cast<CNetObj_CharacterCore &>(NewFrame.m_Character) = NewFrame.m_Core;
		NewFrame.m_Character.m_Weapon = NewFrame.m_Weapon;
		NewFrame.m_Character.m_AttackTick = NewFrame.m_AttackTick;
		NewFrame.m_Character.m_Tick = NewFrame.m_Tick;

		if(CCharacter *pSecondary = m_MacroPracticeWorld.GetCharacterById(OtherLocalId))
		{
			NewFrame.m_SecondaryHasState = 1;
			NewFrame.m_SecondaryClientId = OtherLocalId;
			pSecondary->Core()->Write(&NewFrame.m_SecondaryCore);
			NewFrame.m_SecondaryWeapon = pSecondary->GetActiveWeapon();
			NewFrame.m_SecondaryAttackTick = pSecondary->GetAttackTick();
			NewFrame.m_SecondaryFreezeTime = pSecondary->m_FreezeTime;
			NewFrame.m_SecondaryFrozenLastTick = pSecondary->m_FrozenLastTick ? 1 : 0;
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			{
				NewFrame.m_aSecondaryWeaponGot[Weapon] = pSecondary->Core()->m_aWeapons[Weapon].m_Got ? 1 : 0;
				NewFrame.m_aSecondaryWeaponAmmo[Weapon] = pSecondary->Core()->m_aWeapons[Weapon].m_Ammo;
			}
		}
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
		++m_MacroPlaybackIndex;
		m_MacroDisplayedTick = PlayedFrameIndex + 1;
		if(m_MacroStopAtTickArmed && m_MacroDisplayedTick >= g_Config.m_ClMacroStopTick)
		{
			m_MacroStopAtTickArmed = false;
			m_MacroPlaying = false;
			m_MacroPlaybackPaused = false;
			m_MacroPracticeInput = {};
			m_MacroPracticeInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			m_MacroPracticeInput.m_TargetX = 1;
			m_MacroPracticeDirectionLeft = false;
			m_MacroPracticeDirectionRight = false;
			GameClient()->Echo("Макрос остановлен на выбранном тике. FP-мир оставлен для ручной игры.");
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
	m_MacroRecordingRate = MacroPracticeRate();
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
		const int Dummy = m_MacroPlaybackDummy;
		m_aInputData[Dummy].m_Direction = 0;
		m_aInputData[Dummy].m_Jump = 0;
		m_aInputData[Dummy].m_Hook = 0;
		if(m_aInputData[Dummy].m_Fire & 1)
			m_aInputData[Dummy].m_Fire++;
		m_aInputData[Dummy].m_Fire &= INPUT_STATE_MASK;
		if(m_aInputData[Dummy].m_NextWeapon & 1)
			m_aInputData[Dummy].m_NextWeapon++;
		if(m_aInputData[Dummy].m_PrevWeapon & 1)
			m_aInputData[Dummy].m_PrevWeapon++;
		m_aInputData[Dummy].m_WantedWeapon = 0;
		m_aInputData[Dummy].m_NextWeapon &= INPUT_STATE_MASK;
		m_aInputData[Dummy].m_PrevWeapon &= INPUT_STATE_MASK;
	}

	m_MacroPlaying = false;
	m_MacroRecordingPaused = false;
	m_MacroPlaybackLive = false;
	m_MacroPlaybackStartPending = false;
	m_MacroPlaybackManualDummyOverride = false;
	m_MacroPlaybackPaused = false;
	m_MacroStopAtTickArmed = false;
	m_MacroPlaybackSeekTargetIndex = -1;
	std::fill(std::begin(m_aMacroPlaybackWeaponOverride), std::end(m_aMacroPlaybackWeaponOverride), -1);
	std::fill(std::begin(m_aMacroPickupWeaponOverride), std::end(m_aMacroPickupWeaponOverride), -1);
	std::fill(std::begin(m_aMacroLiveFireOffset), std::end(m_aMacroLiveFireOffset), 0);
	std::fill(std::begin(m_aMacroLiveNextWeaponOffset), std::end(m_aMacroLiveNextWeaponOffset), 0);
	std::fill(std::begin(m_aMacroLivePrevWeaponOffset), std::end(m_aMacroLivePrevWeaponOffset), 0);
	std::fill(std::begin(m_aMacroManualDirectionActive), std::end(m_aMacroManualDirectionActive), false);
	std::fill(std::begin(m_aMacroManualJumpActive), std::end(m_aMacroManualJumpActive), false);
	std::fill(std::begin(m_aMacroManualHookActive), std::end(m_aMacroManualHookActive), false);
	std::fill(std::begin(m_aMacroManualAimActive), std::end(m_aMacroManualAimActive), false);
	std::fill(std::begin(m_aMacroPlaybackConnectionUsed), std::end(m_aMacroPlaybackConnectionUsed), false);

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

	if(WasRecording && m_MacroPracticeActive)
	{
		StopMacroPractice();
		GameClient()->Echo("Запись сохранена. FP выключена, управление возвращено обычному DDNet.");
	}
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
	const uint32_t RecordedRate = (uint32_t)std::clamp(m_MacroRecordingRate, 1, (int)SERVER_TICK_SPEED);
	const bool Written = io_write(File, &Magic, sizeof(Magic)) == sizeof(Magic) &&
		io_write(File, &Version, sizeof(Version)) == sizeof(Version) &&
		io_write(File, &Count, sizeof(Count)) == sizeof(Count) &&
		io_write(File, m_aMacroMapName, sizeof(m_aMacroMapName)) == sizeof(m_aMacroMapName) &&
		io_write(File, &RecordedRate, sizeof(RecordedRate)) == sizeof(RecordedRate) &&
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
	uint32_t RecordedRate = 0;
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
	if(Valid && Version >= 9)
	{
		Valid = io_read(File, &RecordedRate, sizeof(RecordedRate)) == sizeof(RecordedRate) &&
			RecordedRate >= 1 && RecordedRate <= SERVER_TICK_SPEED;
	}

	const int64_t HeaderBytes = (int64_t)sizeof(Magic) + sizeof(Version) + sizeof(Count) +
		(Version >= 3 ? (int64_t)sizeof(aRecordedMapName) : 0) +
		(Version >= 9 ? (int64_t)sizeof(RecordedRate) : 0);
	const int64_t RemainingBytes = Valid && FileLength >= HeaderBytes ? FileLength - HeaderBytes : 0;
	if(Valid && FileLength < HeaderBytes)
		Valid = false;
	const int64_t CurrentFrameBytes = (int64_t)Count * sizeof(SMacroFrame);
	const int64_t Version9FrameBytes = (int64_t)Count * sizeof(SMacroFrameV9);
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
	if(Valid && Version >= 10)
	{
		Frames.resize(Count);
		Valid = RemainingBytes == CurrentFrameBytes &&
			io_read(File, Frames.data(), CurrentFrameBytes) == CurrentFrameBytes;
	}
	else if(Valid && Version >= 8)
	{
		std::vector<SMacroFrameV9> OldFrames(Count);
		Valid = RemainingBytes == Version9FrameBytes &&
			io_read(File, OldFrames.data(), Version9FrameBytes) == Version9FrameBytes;
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
				Frames[i].m_ClientId = OldFrames[i].m_ClientId;
				Frames[i].m_PreFreezeTime = OldFrames[i].m_PreFreezeTime;
				Frames[i].m_FreezeTime = OldFrames[i].m_FreezeTime;
				Frames[i].m_PreFrozenLastTick = OldFrames[i].m_PreFrozenLastTick;
				Frames[i].m_FrozenLastTick = OldFrames[i].m_FrozenLastTick;
				Frames[i].m_PreAttackTick = OldFrames[i].m_PreAttackTick;
				Frames[i].m_HasDDNetState = OldFrames[i].m_HasDDNetState;
				Frames[i].m_PreDDNetCharacter = OldFrames[i].m_PreDDNetCharacter;
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				{
					Frames[i].m_aPreWeaponAmmo[Weapon] = OldFrames[i].m_aPreWeaponAmmo[Weapon];
					Frames[i].m_aWeaponAmmo[Weapon] = OldFrames[i].m_aWeaponAmmo[Weapon];
				}
			}
		}
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
	// A saved frame is one recorded practice step. Replay exactly one frame
	// per real DDNet server tick (20 ms at the standard 50 tick/s rate).
	const int FasterPlaybackRate = SERVER_TICK_SPEED;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("Для запуска макроса сначала подключись к серверу.");
		return false;
	}
	if(m_MacroRecording)
	{
		GameClient()->Echo("Сначала останови текущую запись макроса.");
		return false;
	}
	if(m_MacroPlaying)
		StopMacroRecording();

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

	// Recording happens in an isolated FP copy, but playback uses the
	// ordinary live tee and normal DDNet input packets. Never restore a saved
	// position or character state on the server.
	const int StartFrameIndex = std::clamp(g_Config.m_ClMacroGotoTick - 1, 0, (int)Frames.size() - 1);
	const SMacroFrame &FirstFrame = Frames[StartFrameIndex];
	int PlaybackDummy = g_Config.m_ClDummy;
	if(FirstFrame.m_ClientId == GameClient()->m_aLocalIds[0])
		PlaybackDummy = 0;
	else if(FirstFrame.m_ClientId == GameClient()->m_aLocalIds[1])
		PlaybackDummy = 1;

	const int PlaybackLocalId = GameClient()->m_aLocalIds[PlaybackDummy];
	if(PlaybackLocalId < 0 || PlaybackLocalId >= MAX_CLIENTS ||
		!GameClient()->m_PredictedWorld.GetCharacterById(PlaybackLocalId))
	{
		GameClient()->Echo("Для обычного воспроизведения нужен доступный локальный тии (основной или дамми).");
		return false;
	}

	// Leave FP before playback so the real server character takes control again.
	if(m_MacroPracticeActive)
		StopMacroPractice();

	g_Config.m_ClDummy = PlaybackDummy;
	m_vMacroFrames = std::move(Frames);
	m_MacroRecording = false;
	m_MacroRecordingPaused = false;
	m_MacroPlaybackLive = true;
	m_MacroPlaybackStartPending = true;
	m_MacroPlaybackManualDummyOverride = false;
	m_MacroPlaybackRate = FasterPlaybackRate;
	m_MacroPlaybackDummy = PlaybackDummy;
	m_MacroPlaybackOwnsPractice = false;
	m_MacroPlaybackOwnsDummy = false;
	m_MacroPlaying = true;
	m_MacroStartFrameIndex = StartFrameIndex;
	m_MacroPlaybackIndex = (std::size_t)StartFrameIndex;
	m_MacroDisplayedTick = StartFrameIndex + 1;
	m_MacroCurrentFrame = {};
	m_MacroHasCurrentFrame = false;
	m_MacroHoldCurrentFrame = false;
	m_MacroPlaybackPaused = false;
	m_MacroStopAtTickArmed = false;
	m_MacroPlaybackSeekTargetIndex = -1;
	m_MacroPlaybackLastTick = Client()->GameTick(PlaybackDummy);
	std::fill(std::begin(m_aMacroPlaybackConnectionUsed), std::end(m_aMacroPlaybackConnectionUsed), false);
	std::fill(std::begin(m_aMacroPlaybackWeaponOverride), std::end(m_aMacroPlaybackWeaponOverride), -1);
	std::fill(std::begin(m_aMacroPickupWeaponOverride), std::end(m_aMacroPickupWeaponOverride), -1);
	std::fill(std::begin(m_aMacroLiveFireOffset), std::end(m_aMacroLiveFireOffset), 0);
	std::fill(std::begin(m_aMacroLiveNextWeaponOffset), std::end(m_aMacroLiveNextWeaponOffset), 0);
	std::fill(std::begin(m_aMacroLivePrevWeaponOffset), std::end(m_aMacroLivePrevWeaponOffset), 0);
	std::fill(std::begin(m_aMacroManualDirectionActive), std::end(m_aMacroManualDirectionActive), false);
	std::fill(std::begin(m_aMacroManualJumpActive), std::end(m_aMacroManualJumpActive), false);
	std::fill(std::begin(m_aMacroManualHookActive), std::end(m_aMacroManualHookActive), false);
	std::fill(std::begin(m_aMacroManualAimActive), std::end(m_aMacroManualAimActive), false);

	// Send a neutral input to the selected real tee before starting the next
	// run. This guarantees that old held fire/hook/weapon counters are released.
	CNetObj_PlayerInput NeutralInput{};
	NeutralInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
	NeutralInput.m_TargetX = 1;
	NeutralInput.m_Fire = m_aLastData[PlaybackDummy].m_Fire;
	if(NeutralInput.m_Fire & 1)
		++NeutralInput.m_Fire;
	NeutralInput.m_Fire &= INPUT_STATE_MASK;
	NeutralInput.m_NextWeapon = m_aLastData[PlaybackDummy].m_NextWeapon;
	if(NeutralInput.m_NextWeapon & 1)
		++NeutralInput.m_NextWeapon;
	NeutralInput.m_NextWeapon &= INPUT_STATE_MASK;
	NeutralInput.m_PrevWeapon = m_aLastData[PlaybackDummy].m_PrevWeapon;
	if(NeutralInput.m_PrevWeapon & 1)
		++NeutralInput.m_PrevWeapon;
	NeutralInput.m_PrevWeapon &= INPUT_STATE_MASK;
	m_aInputData[PlaybackDummy] = NeutralInput;
	m_aInputDirectionLeft[PlaybackDummy] = 0;
	m_aInputDirectionRight[PlaybackDummy] = 0;
	m_aManualHook[PlaybackDummy] = false;

	m_MacroPlaybackRate = FasterPlaybackRate;
	m_MacroPracticeAccumulator = 0.0;
	m_MacroPracticeLastTime = time_get();

	// Wait for the neutral release packet to reach the server before rebasing
	// the recorded fire/weapon counters and sending the first macro frame.

	char aStartMessage[220];
	str_format(aStartMessage, sizeof(aStartMessage),
		"Макрос «%s» запущен в обычном DDNet: отправляются только нажатия, без FP и телепортации. Стартовая позиция должна совпадать с началом записи. Скорость: %d тиков/с.",
		pName, m_MacroPlaybackRate);
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

void CControls::ConMacroRewindStep(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->GameClient()->m_Chat.IsActive() || pSelf->GameClient()->m_Menus.IsActive())
		return;
	pSelf->SeekMacro(-pSelf->MacroSeekStep(), true, true);
}

void CControls::ConMacroForwardStep(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
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
		(pSelf->m_MacroDisplayedTick > 0 ? pSelf->m_MacroDisplayedTick : (int)pSelf->m_MacroPlaybackIndex + 1);
	pSelf->SeekMacro(TargetTick - CurrentTick, true, true);
}

void CControls::ConMacroStopAtTick(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CControls *pSelf = (CControls *)pUserData;
	if((!pSelf->m_MacroPracticeActive && !pSelf->m_MacroPlaybackLive) || !pSelf->m_MacroPlaying)
	{
		pSelf->GameClient()->Echo("Сначала запусти сохранённый макрос.");
		return;
	}

	const int StopTick = g_Config.m_ClMacroStopTick;
	if(StopTick < 1 || StopTick > (int)pSelf->m_vMacroFrames.size())
	{
		pSelf->GameClient()->Echo("Укажи тик остановки от 1 до длины макроса в разделе «Кнопки».");
		return;
	}

	// Stop locally after the selected FP frame has completed its physics tick.
	pSelf->m_MacroStopAtTickArmed = true;
	char aMessage[128];
	str_format(aMessage, sizeof(aMessage), "Макрос остановится на тике %d.", StopTick);
	pSelf->GameClient()->Echo(aMessage);
}

void CControls::ConMacroGotoConfigured(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->GameClient()->m_Chat.IsActive() || pSelf->GameClient()->m_Menus.IsActive())
		return;
	if((!pSelf->m_MacroPracticeActive && !pSelf->m_MacroPlaybackLive) || pSelf->m_vMacroFrames.empty())
	{
		pSelf->GameClient()->Echo("Сначала начни запись или запусти сохранённый макрос.");
		return;
	}

	const int TargetTick = g_Config.m_ClMacroGotoTick;
	if(TargetTick < 1 || TargetTick > (int)pSelf->m_vMacroFrames.size())
	{
		pSelf->GameClient()->Echo("Укажи тик перехода в пределах длины макроса.");
		return;
	}
	const int CurrentTick = pSelf->m_MacroRecording ? pSelf->MacroFrameCount() :
		(pSelf->m_MacroDisplayedTick > 0 ? pSelf->m_MacroDisplayedTick : (int)pSelf->m_MacroPlaybackIndex + 1);
	if(!pSelf->SeekMacro(TargetTick - CurrentTick, true, true))
		pSelf->GameClient()->Echo("Не удалось восстановить выбранный тик. Проверь, что макрос содержит данные состояния.");
}

int CControls::ImportMacroBinds()
{
	struct SImportEntry
	{
		const char *m_pOriginal;
		const char *m_pMacro;
	};
	static constexpr SImportEntry s_aEntries[] = {
		{"+left", "+macro_left"},
		{"+right", "+macro_right"},
		{"+jump", "+macro_jump"},
		{"+hook", "+macro_hook"},
		{"+fire", "+macro_fire"},
		{"+weapon1", "+macro_weapon1"},
		{"+weapon2", "+macro_weapon2"},
		{"+weapon3", "+macro_weapon3"},
		{"+weapon4", "+macro_weapon4"},
		{"+weapon5", "+macro_weapon5"},
		{"+nextweapon", "+macro_nextweapon"},
		{"+prevweapon", "+macro_prevweapon"},
	};

	// Replace supported gameplay commands inside a compound bind while
	// preserving other commands, whitespace, quoted strings and escapes.
	auto ConvertBind = [&](const char *pCommand) {
		const std::string Original = pCommand ? pCommand : "";
		std::string Converted;
		size_t SegmentStart = 0;
		bool InQuotes = false;
		bool Escaped = false;

		auto AppendSegment = [&](size_t SegmentEnd) {
			size_t TokenStart = SegmentStart;
			while(TokenStart < SegmentEnd && std::isspace((unsigned char)Original[TokenStart]))
				++TokenStart;
			size_t TokenEnd = SegmentEnd;
			while(TokenEnd > TokenStart && std::isspace((unsigned char)Original[TokenEnd - 1]))
				--TokenEnd;

			const SImportEntry *pMatched = nullptr;
			for(const SImportEntry &Entry : s_aEntries)
			{
				if(Original.compare(TokenStart, TokenEnd - TokenStart, Entry.m_pOriginal) == 0)
				{
					pMatched = &Entry;
					break;
				}
			}

			if(pMatched)
			{
				Converted.append(Original, SegmentStart, TokenStart - SegmentStart);
				Converted += pMatched->m_pMacro;
				Converted.append(Original, TokenEnd, SegmentEnd - TokenEnd);
			}
			else
			{
				Converted.append(Original, SegmentStart, SegmentEnd - SegmentStart);
			}
		};

		for(size_t i = 0; i <= Original.size(); ++i)
		{
			const bool AtEnd = i == Original.size();
			const char Ch = AtEnd ? ';' : Original[i];
			if(!AtEnd && Escaped)
			{
				Escaped = false;
				continue;
			}
			if(!AtEnd && Ch == '\\')
			{
				Escaped = true;
				continue;
			}
			if(!AtEnd && Ch == '"')
				InQuotes = !InQuotes;

			if(AtEnd || (Ch == ';' && !InQuotes))
			{
				AppendSegment(i);
				if(!AtEnd)
					Converted.push_back(';');
				SegmentStart = i + 1;
			}
		}
		return Converted;
	};

	int Imported = 0;
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
	{
		for(int KeyId = KEY_FIRST; KeyId < KEY_LAST; ++KeyId)
		{
			const CBindSlot Slot(KeyId, Modifier);
			const char *pCurrentCommand = GameClient()->m_Binds.Get(Slot);
			const std::string Converted = ConvertBind(pCurrentCommand);
			if(Converted != pCurrentCommand)
			{
				GameClient()->m_Binds.Bind(KeyId, Converted.c_str(), false, Modifier);
				++Imported;
			}
		}
	}

	char aMessage[160];
	str_format(aMessage, sizeof(aMessage),
		"Импортировано игровых биндов для макроса: %d. Составные команды сохранены; вне FP всё работает как обычно.",
		Imported);
	GameClient()->Echo(aMessage);
	return Imported;
}

void CControls::ToggleMacroPause()
{
	if(!m_MacroPracticeActive && !m_MacroPlaybackLive)
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
	if(m_MacroPlaybackLive)
		m_MacroPlaybackLastTick = Client()->GameTick(m_MacroPlaybackDummy);
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
	if(m_MacroPlaybackLive && m_MacroPlaying)
	{
		const int RequestedDummy = g_Config.m_ClDummy;
		if(RequestedDummy >= 0 && RequestedDummy < NUM_DUMMIES &&
			GameClient()->m_aLocalIds[RequestedDummy] >= 0)
		{
			m_MacroPlaybackDummy = RequestedDummy;
			m_MacroPlaybackManualDummyOverride = true;
			m_MacroPlaybackLastTick = Client()->GameTick(RequestedDummy);
			m_MacroPracticeAccumulator = 0.0;
			if(m_MacroHasCurrentFrame)
				RebaseMacroPlaybackCounters(RequestedDummy, m_MacroDisplayedTick - 1);
			else
				RebaseMacroPlaybackCounters(RequestedDummy, m_MacroStartFrameIndex - 1);
			GameClient()->Echo("Макрос продолжает воспроизведение на выбранном тии.");
		}
		return;
	}

	if(!m_MacroPracticeActive || m_MacroPlaying)
		return;
	const int RequestedLocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(RequestedLocalId >= 0 && RequestedLocalId < MAX_CLIENTS)
		SwitchMacroPracticeCharacter(RequestedLocalId);
}
void CControls::OnMacroObserverInputSent(const CNetObj_PlayerInput &Input)
{
	const int ObserverDummy = 1 - g_Config.m_ClDummy;
	if(ObserverDummy >= 0 && ObserverDummy < NUM_DUMMIES)
		m_aLastData[ObserverDummy] = Input;
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

	std::fill(std::begin(m_aMacroPlaybackWeaponOverride), std::end(m_aMacroPlaybackWeaponOverride), -1);
	std::fill(std::begin(m_aMacroPickupWeaponOverride), std::end(m_aMacroPickupWeaponOverride), -1);
	std::fill(std::begin(m_aMacroLiveFireOffset), std::end(m_aMacroLiveFireOffset), 0);
	std::fill(std::begin(m_aMacroLiveNextWeaponOffset), std::end(m_aMacroLiveNextWeaponOffset), 0);
	std::fill(std::begin(m_aMacroLivePrevWeaponOffset), std::end(m_aMacroLivePrevWeaponOffset), 0);
	std::fill(std::begin(m_aMacroManualDirectionActive), std::end(m_aMacroManualDirectionActive), false);
	std::fill(std::begin(m_aMacroManualJumpActive), std::end(m_aMacroManualJumpActive), false);
	std::fill(std::begin(m_aMacroManualHookActive), std::end(m_aMacroManualHookActive), false);
	std::fill(std::begin(m_aMacroManualAimActive), std::end(m_aMacroManualAimActive), false);
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
	if((!m_MacroPracticeActive && !m_MacroPlaybackLive) || m_vMacroFrames.empty())
		return false;

	if(m_MacroRecording)
	{
		// Recording rewind moves the whole practice world backward and truncates
		// the future. Recording remains paused until the user resumes it.
		const int CurrentPosition = (int)m_vMacroFrames.size();
		const int TargetPosition = std::clamp(CurrentPosition + Offset, 0, CurrentPosition);
		if(TargetPosition == CurrentPosition)
		{
			if(ShowMessage)
			{
				char aMessage[96];
				str_format(aMessage, sizeof(aMessage), "Запись уже находится на тике %d.", TargetPosition);
				GameClient()->Echo(aMessage);
			}
			return true;
		}
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

	const bool WasPlayingBeforeSeek = m_MacroPlaying;
	if(!m_MacroPlaying && !m_MacroPlaybackLive &&
		(!m_MacroPracticeActive || !m_MacroPracticeBaselineValid))
		return false;

	const int CurrentIndex = m_MacroPlaybackLive && m_MacroPlaybackSeekTargetIndex >= 0 ?
		m_MacroPlaybackSeekTargetIndex :
		(m_MacroDisplayedTick > 0 ? m_MacroDisplayedTick - 1 :
			(m_MacroHasCurrentFrame ? (int)m_MacroPlaybackIndex - 1 : (int)m_MacroPlaybackIndex));
	const int Current = std::clamp(CurrentIndex, 0, (int)m_vMacroFrames.size() - 1);
	const int Target = std::clamp(Current + Offset, 0, (int)m_vMacroFrames.size() - 1);
	if(Target == Current)
	{
		if(ShowMessage)
		{
			char aMessage[96];
			str_format(aMessage, sizeof(aMessage), "Макрос уже находится на тике %d.", Target + 1);
			GameClient()->Echo(aMessage);
		}
		return true;
	}

	if(m_MacroPlaybackLive)
	{
		// The server cannot be rewound by a client. Switch to the isolated FP
		// world and restore the exact saved tee state there instead.
		const int RecordedClientId = m_vMacroFrames[Target].m_ClientId;
		if(RecordedClientId == GameClient()->m_aLocalIds[0])
			g_Config.m_ClDummy = 0;
		else if(RecordedClientId == GameClient()->m_aLocalIds[1])
			g_Config.m_ClDummy = 1;

		StopMacroRecording();
		if(!StartMacroPractice())
			return false;
		if(!RestoreMacroPracticeFrame(Target))
		{
			StopMacroPractice();
			GameClient()->Echo("Не удалось восстановить сохранённое состояние тии на выбранном тике.");
			return false;
		}
		m_MacroPlaybackLive = false;
		m_MacroPlaying = true;
		m_MacroPlaybackPaused = true;
		m_MacroPlaybackOwnsPractice = true;
		m_MacroPlaybackIndex = (std::size_t)Target + 1;
		m_MacroDisplayedTick = Target + 1;
		m_MacroPracticeAccumulator = 0.0;
		m_MacroPracticeLastTime = time_get();
	}
	else
	{
		if(m_MacroPracticeBaselineValid)
		{
			if(!RebuildMacroWorldToPosition(Target + 1))
				return false;
		}
		else if(!RestoreMacroPracticeFrame(Target))
			return false;

		m_MacroPlaybackIndex = (std::size_t)Target + 1;
		m_MacroDisplayedTick = Target + 1;
		m_MacroPlaying = true;
		m_MacroPlaybackPaused = true;
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
bool CControls::SeekMacroBy(int Offset)
{
	return SeekMacro(Offset, true, true);
}

void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons && m_MacroPlaybackLive)
		{
			m_aMacroPickupWeaponOverride[m_MacroPlaybackDummy] = pMsg->m_Weapon + 1;
		}
		else if(g_Config.m_ClAutoswitchWeapons && m_MacroPracticeActive)
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
	// Use the same predicted core for flight detection and hook target scoring.
	// Snapshot velocity can lag behind the driver's real direction by a tick.
	const vec2 LocalPos = LocalCore.m_Pos;
	const vec2 LocalVel = LocalCore.m_Vel;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const int DummyId = GameClient()->m_aLocalIds[!g_Config.m_ClDummy];
	const bool PracticeActive = pPracticeLocal != nullptr;

	if(m_AutoTripleflyBindActive)
	{
		const int PreviousTargetClientId = m_aTripleflyTargetClientId[Dummy];
		m_aTripleflyTargetFound[Dummy] = false;
		m_aTripleflyTargetClientId[Dummy] = -1;
		m_aTripleflyAimX[Dummy] = 1;
		m_aTripleflyAimY[Dummy] = 0;

		const float SpeedX = std::abs(LocalVel.x);
		const float SpeedY = std::abs(LocalVel.y);
		const float FlightSpeed = length(LocalVel);

		// The hook window is shorter when the driver is moving sideways or
		// downward, and longer while climbing so the third tee is not dropped.
		if(LocalVel.y < -std::max(2.0f, SpeedX * 0.65f))
			m_TripleflyHookDuration = 5; // upward
		else if(LocalVel.y > std::max(2.0f, SpeedX * 0.65f))
			m_TripleflyHookDuration = 1; // downward
		else if(SpeedX > std::max(2.0f, SpeedY * 1.25f))
			m_TripleflyHookDuration = 2; // sideways
		else
			m_TripleflyHookDuration = 3; // diagonal / transitioning

		const int HookerTick = Client()->GameTick(!Dummy);
		if(!m_TripleflyFlightActive && FlightSpeed >= 3.0f)
		{
			// The player starts the flight manually. As soon as real movement
			// begins, the dummy may hook the third tee; no extra Fire press or
			// dummy hammer pulse is required to start the assist.
			m_TripleflyFlightActive = true;
			m_TripleflyNextHookTick = HookerTick;
			m_TripleflyHookStartTick = -1;
			m_TripleflyHookEndTick = -1;
		}
		else if(m_TripleflyFlightActive && FlightSpeed <= 1.5f)
		{
			// A clear stop ends the hook cycle; the next flight starts fresh.
			m_TripleflyFlightActive = false;
			m_TripleflyNextHookTick = -1;
			m_TripleflyHookStartTick = -1;
			m_TripleflyHookEndTick = -1;
		}

		const bool HookerAvailable = DummyId >= 0 && DummyId < MAX_CLIENTS &&
			(PracticeActive || GameClient()->m_Snap.m_aCharacters[DummyId].m_Active);
		const CCharacter *pPracticeHooker = PracticeActive ? m_MacroPracticeWorld.GetCharacterById(DummyId) : nullptr;
		if(!HookerAvailable || (PracticeActive && !pPracticeHooker))
		{
			m_aTripleflyTargetFound[Dummy] = false;
			m_aTripleflyTargetClientId[Dummy] = -1;
			m_TripleflyHookStartTick = -1;
			m_TripleflyHookEndTick = -1;
			m_aAutoHookActive[Dummy] = false;
			m_aAutoHookFireActive[Dummy] = false;
			m_aAutoAvoidActive[Dummy] = false;
			return;
		}

		const CCharacterCore &HookerCore = pPracticeHooker ? *pPracticeHooker->Core() :
			GameClient()->m_aClients[DummyId].m_Predicted;
		const vec2 HookerPos = HookerCore.m_Pos;
		const vec2 HookerVel = HookerCore.m_Vel;
		const float Range = std::clamp((float)g_Config.m_ClAutoHookRange, 64.0f, 360.0f);
		const float RangeSquared = Range * Range;
		float BestScore = RangeSquared * 2.0f;
		int BestClientId = -1;
		vec2 BestAim(1.0f, 0.0f);

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			// The third tee is separate from both locally controlled tees.
			if(ClientId == LocalId || ClientId == DummyId ||
				!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				continue;
			if(GameClient()->IsOtherTeam(ClientId) || GameClient()->m_aClients[ClientId].m_Solo ||
				GameClient()->m_aClients[ClientId].m_HookHitDisabled)
				continue;

			const CCharacter *pPracticeTarget = PracticeActive ? m_MacroPracticeWorld.GetCharacterById(ClientId) : nullptr;
			if(PracticeActive && !pPracticeTarget)
				continue;

			const CCharacterCore &TargetCore = pPracticeTarget ? *pPracticeTarget->Core() :
				GameClient()->m_aClients[ClientId].m_Predicted;
			const vec2 TargetPos = TargetCore.m_Pos;
			const vec2 Delta = TargetPos - HookerPos;
			const float DistanceSquared = dot(Delta, Delta);
			if(DistanceSquared < 1.0f || DistanceSquared > RangeSquared)
				continue;

			// Do not pick a target if the controlled tee is physically
			// between the dummy hooker and the third tee: the hook would grab
			// the driver instead of the intended teammate.
			const vec2 DriverDelta = LocalPos - HookerPos;
			const float DriverProjection = dot(DriverDelta, Delta) / DistanceSquared;
			if(DriverProjection > 0.0f && DriverProjection < 1.0f)
			{
				const vec2 ClosestPoint = HookerPos + Delta * DriverProjection;
				if(length(ClosestPoint - LocalPos) < 36.0f)
					continue;
			}

			vec2 CollisionPos;
			vec2 BeforeCollision;
			if(Collision()->IntersectLineTeleHook(HookerPos, TargetPos, &CollisionPos, &BeforeCollision) != 0)
				continue;

			float Score = DistanceSquared;
			if(FlightSpeed > 2.0f)
			{
				const float AlongFlight = dot(normalize(Delta), normalize(LocalVel));
				Score *= 1.0f - 0.20f * AlongFlight;
			}
			if(ClientId == PreviousTargetClientId)
				Score *= 0.85f;
			if(Score >= BestScore)
				continue;

			const float HookSpeed = std::max(1.0f, (float)HookerCore.m_Tuning.m_HookFireSpeed);
			const float FlightTicks = std::clamp(std::sqrt(DistanceSquared) / HookSpeed, 1.0f, 6.0f);
			vec2 Lead = (TargetCore.m_Vel - HookerVel) * FlightTicks;
			const float LeadLength = length(Lead);
			if(LeadLength > 96.0f)
				Lead *= 96.0f / LeadLength;

			BestScore = Score;
			BestClientId = ClientId;
			BestAim = Delta + Lead;
		}

		m_aTripleflyTargetFound[Dummy] = BestClientId >= 0;
		m_aTripleflyTargetClientId[Dummy] = BestClientId;
		m_aTripleflyAimX[Dummy] = round_to_int(BestAim.x);
		m_aTripleflyAimY[Dummy] = round_to_int(BestAim.y);

		if(!m_aTripleflyTargetFound[Dummy])
		{
			m_TripleflyHookStartTick = -1;
			m_TripleflyHookEndTick = -1;
			m_TripleflyNextHookTick = HookerTick + 3;
		}
		else if(m_TripleflyFlightActive && m_TripleflyNextHookTick >= 0 &&
			HookerTick >= m_TripleflyNextHookTick &&
			(HookerTick >= m_TripleflyHookEndTick || m_TripleflyHookStartTick < 0))
		{
			m_TripleflyHookStartTick = HookerTick + 1;
			m_TripleflyHookEndTick = m_TripleflyHookStartTick + m_TripleflyHookDuration;
			m_TripleflyNextHookTick = HookerTick + 18;
		}
		else if(m_TripleflyHookStartTick >= 0 && HookerTick >= m_TripleflyHookStartTick &&
			HookerTick < m_TripleflyHookEndTick)
		{
			// Re-evaluate duration as the flight direction changes. A turn down
			// or sideways can shorten the current hook immediately.
			m_TripleflyHookEndTick = m_TripleflyHookStartTick + m_TripleflyHookDuration;
		}

		m_aAutoHookActive[Dummy] = false;
		m_aAutoHookFireActive[Dummy] = false;
		m_aAutoAvoidActive[Dummy] = false;
		return;
	}
	bool FoundTarget = false;
	vec2 BestTarget(0.0f, 0.0f);

	// Manual hook always has priority. During macro practice the same
	// automation works from the private tee's position and mirrored teammates.
	if(g_Config.m_ClAutoHook && !ManualHook)
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
			if(ClientId == LocalId || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
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
		{
			m_MacroPracticePendingWantedWeapon = PracticeInput.m_WantedWeapon;
			m_MacroPracticeInput.m_WantedWeapon = 0;
		}
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
	if(UiCapturesInput)
	{
		// DDNet intentionally suppresses bind release callbacks while a menu,
		// chat or console is open. Clear the private held-input state here so
		// a movement/hook/fire bind cannot remain stuck after closing the UI.
		m_MacroPracticeDirectionLeft = false;
		m_MacroPracticeDirectionRight = false;
		m_MacroPracticePendingDirectionLeft = false;
		m_MacroPracticePendingDirectionRight = false;
		m_MacroPracticePendingJump = false;
		m_MacroPracticePendingHook = false;
		m_MacroPracticePendingWantedWeapon = 0;
		m_MacroPracticeInput.m_Jump = 0;
		m_MacroPracticeInput.m_Hook = 0;
		m_MacroPracticeInput.m_WantedWeapon = 0;
		if(m_MacroPracticeInput.m_Fire & 1)
			++m_MacroPracticeInput.m_Fire;
		m_MacroPracticeInput.m_Fire &= INPUT_STATE_MASK;
		m_MacroPracticePendingFireTransitions = 0;
		m_MacroPracticeFireCounter = m_MacroPracticeInput.m_Fire & INPUT_STATE_MASK;
		if(m_MacroPracticeInput.m_NextWeapon & 1)
			++m_MacroPracticeInput.m_NextWeapon;
		if(m_MacroPracticeInput.m_PrevWeapon & 1)
			++m_MacroPracticeInput.m_PrevWeapon;
		m_aManualHook[Dummy] = false;
		m_MacroPracticeLastTime = CurrentTime;
		return;
	}
	if(MacroPaused)
	{
		m_MacroPracticeLastTime = CurrentTime;
		return;
	}

	const int64_t ElapsedTime = std::max<int64_t>(0, CurrentTime - m_MacroPracticeLastTime);
	m_MacroPracticeLastTime = CurrentTime;
	// Saved macro frames are replayed at the real server tick rate. The
	// configurable slow clock applies only while creating a recording.
	const int PracticeRate = m_MacroPlaying ? (int)SERVER_TICK_SPEED : MacroPracticeRate();
	if(m_MacroPlaying)
		m_MacroPlaybackRate = SERVER_TICK_SPEED;
	m_MacroPracticeAccumulator += (double)ElapsedTime / (double)time_freq() * PracticeRate;

	int Steps = 0;
	const int MaxStepsPerUpdate = std::max<int>(SERVER_TICK_SPEED, PracticeRate);
	bool ConsumeWantedWeapon = PracticeInput.m_WantedWeapon > 0;
	while(m_MacroPracticeAccumulator >= 1.0 && Steps < MaxStepsPerUpdate)
	{
		const bool WasPlaying = m_MacroPlaying;
		SimulateMacroPracticeTick(PracticeInput, PracticeDummyInput);
		m_MacroPracticeAccumulator = std::max(0.0, m_MacroPracticeAccumulator - 1.0);
		++Steps;
		if(ConsumeWantedWeapon)
		{
			// Direct weapon selection is a one-tick event, not a held input.
			PracticeInput.m_WantedWeapon = 0;
			m_MacroPracticeInput.m_WantedWeapon = 0;
			ConsumeWantedWeapon = false;
		}
		if(!m_MacroPracticeActive || (WasPlaying && !m_MacroPlaying))
		{
			m_MacroPracticeAccumulator = 0.0;
			break;
		}
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


		if(!m_MacroPlaybackLive)
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
		if(m_AutoTripleflyBindActive)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;
			if(m_aTripleflyTargetFound[g_Config.m_ClDummy])
			{
				pDummyInput->m_TargetX = m_aTripleflyAimX[g_Config.m_ClDummy];
				pDummyInput->m_TargetY = m_aTripleflyAimY[g_Config.m_ClDummy];
			}
			pDummyInput->m_Hook = (m_aTripleflyTargetFound[g_Config.m_ClDummy] && TripleflyHookActive()) ? 1 : 0;
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


		// Live playback is deliberately input-only: the server simulates its
		// own tee, weapons, pickups, freeze and map collisions as usual.
		const int MacroDummy = g_Config.m_ClDummy;
		if(m_MacroPlaybackLive && m_MacroPlaying && !m_MacroPlaybackStartPending &&
			!m_MacroPlaybackPaused && m_MacroHasCurrentFrame && MacroDummy == m_MacroPlaybackDummy)
		{
			CNetObj_PlayerInput MacroInput = m_MacroCurrentFrame.m_Input;
			MacroInput.m_PlayerFlags = m_aInputData[MacroDummy].m_PlayerFlags;
			if(m_MacroCurrentFrame.m_HasDDNetState &&
				m_MacroCurrentFrame.m_Weapon != m_MacroCurrentFrame.m_PreWeapon)
			{
				// Repeat weapon changes caused by pickups as well as explicit key
				// presses. The server still validates weapon ownership/ammo.
				MacroInput.m_WantedWeapon = m_MacroCurrentFrame.m_Weapon + 1;
			}
			MacroInput.m_Fire = (MacroInput.m_Fire + m_aMacroLiveFireOffset[MacroDummy]) & INPUT_STATE_MASK;
			MacroInput.m_NextWeapon = (MacroInput.m_NextWeapon + m_aMacroLiveNextWeaponOffset[MacroDummy]) & INPUT_STATE_MASK;
			MacroInput.m_PrevWeapon = (MacroInput.m_PrevWeapon + m_aMacroLivePrevWeaponOffset[MacroDummy]) & INPUT_STATE_MASK;

			if(m_aMacroManualDirectionActive[MacroDummy])
				MacroInput.m_Direction = m_aInputDirectionLeft[MacroDummy] == m_aInputDirectionRight[MacroDummy] ? 0 :
					(m_aInputDirectionLeft[MacroDummy] ? -1 : 1);
			if(m_aMacroManualJumpActive[MacroDummy])
				MacroInput.m_Jump = m_aInputData[MacroDummy].m_Jump;
			if(m_aMacroManualHookActive[MacroDummy])
				MacroInput.m_Hook = m_aInputData[MacroDummy].m_Hook;
			if(m_aMacroManualAimActive[MacroDummy])
			{
				MacroInput.m_TargetX = (int)m_aMousePos[MacroDummy].x;
				MacroInput.m_TargetY = (int)m_aMousePos[MacroDummy].y;
			}
			if(m_aMacroPlaybackWeaponOverride[MacroDummy] > 0)
				MacroInput.m_WantedWeapon = m_aMacroPlaybackWeaponOverride[MacroDummy];
			if(m_aMacroPickupWeaponOverride[MacroDummy] > 0)
			{
				MacroInput.m_WantedWeapon = m_aMacroPickupWeaponOverride[MacroDummy];
				m_aMacroPickupWeaponOverride[MacroDummy] = -1;
			}

			m_aInputData[MacroDummy] = MacroInput;
		}

		// check if we need to send input
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Direction != m_aLastData[g_Config.m_ClDummy].m_Direction;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_TargetX != m_aLastData[g_Config.m_ClDummy].m_TargetX;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_TargetY != m_aLastData[g_Config.m_ClDummy].m_TargetY;
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

	// Send a neutral release packet before the first frame of every run.
	if(m_MacroPlaybackLive && m_MacroPlaying && m_MacroPlaybackStartPending &&
		Client()->State() == IClient::STATE_ONLINE &&
		!GameClient()->m_Chat.IsActive() && !GameClient()->m_Menus.IsActive())
	{
		const int ServerTick = Client()->GameTick(m_MacroPlaybackDummy);
		if(ServerTick > m_MacroPlaybackLastTick)
		{
			m_MacroPlaybackLastTick = ServerTick;
			m_MacroPlaybackStartPending = false;
			RebaseMacroPlaybackCounters(m_MacroPlaybackDummy, m_MacroStartFrameIndex - 1);
			SendMacroFrame(m_MacroStartFrameIndex, EMacroFrameMode::STEP);
			m_MacroPlaybackIndex = (std::size_t)m_MacroStartFrameIndex + 1;
			m_MacroDisplayedTick = m_MacroStartFrameIndex + 1;
			m_MacroPracticeAccumulator = 0.0;
		}
	}

	// Pace live input playback from confirmed server ticks. Each selected frame
	// is forwarded through SnapInput as ordinary DDNet controls.
	if(m_MacroPlaybackLive && m_MacroPlaying && !m_MacroPlaybackStartPending &&
		Client()->State() == IClient::STATE_ONLINE)
	{
		const int ServerTick = Client()->GameTick(m_MacroPlaybackDummy);
		if(GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive())
		{
			m_MacroPlaybackLastTick = ServerTick;
			m_MacroPracticeAccumulator = 0.0;
		}
		else
		{
			if(m_MacroPlaybackLastTick < 0)
				m_MacroPlaybackLastTick = ServerTick;
			const int TickDelta = std::max(0, ServerTick - m_MacroPlaybackLastTick);
			if(TickDelta > 0)
			{
				m_MacroPlaybackLastTick = ServerTick;

				if(m_MacroStopAtTickArmed && m_MacroDisplayedTick >= g_Config.m_ClMacroStopTick)
				{
					GameClient()->Echo("Макрос остановлен на выбранном тике; управление возвращено обычному DDNet.");
					StopMacroRecording();
				}
				else if(m_MacroPlaybackSeekTargetIndex >= 0)
				{
					const int TargetIndex = std::clamp(m_MacroPlaybackSeekTargetIndex, 0, (int)m_vMacroFrames.size() - 1);
					m_MacroPlaybackSeekTargetIndex = -1;
					RebaseMacroPlaybackCounters(m_MacroPlaybackDummy, TargetIndex - 1);
					SendMacroFrame(TargetIndex, m_MacroPlaybackPaused ? EMacroFrameMode::RESTORE_PAUSED : EMacroFrameMode::STEP);
					m_MacroPlaybackIndex = (std::size_t)TargetIndex + 1;
					m_MacroDisplayedTick = TargetIndex + 1;
					m_MacroPracticeAccumulator = 0.0;
				}
				else if(!m_MacroPlaybackPaused)
				{
					m_MacroPracticeAccumulator +=
						(double)TickDelta * (double)m_MacroPlaybackRate / (double)SERVER_TICK_SPEED;

					if(m_MacroPracticeAccumulator >= 1.0 && m_MacroPlaying && m_MacroPlaybackLive)
					{
						// Queue at most one input frame per confirmed server tick.
						// Timing debt is carried forward instead of collapsing multiple
						// Fire/Hook/weapon transitions into one packet after a hitch.
						m_MacroPracticeAccumulator -= 1.0;
						if(m_MacroPlaybackIndex >= m_vMacroFrames.size())
						{
							if(g_Config.m_ClMacroLoop && !m_vMacroFrames.empty())
							{
								m_MacroPlaybackIndex = (std::size_t)std::clamp(m_MacroStartFrameIndex, 0, (int)m_vMacroFrames.size() - 1);
								RebaseMacroPlaybackCounters(m_MacroPlaybackDummy, (int)m_vMacroFrames.size() - 1);
							}
							else
							{
								// Stop live input but keep the loaded frame history so
								// rewind/goto can still open it in the local FP world.
								const int FinishedDummy = m_MacroPlaybackDummy;
								m_aInputDirectionLeft[FinishedDummy] = false;
								m_aInputDirectionRight[FinishedDummy] = false;
								m_aInputData[FinishedDummy].m_Direction = 0;
								m_aInputData[FinishedDummy].m_Jump = 0;
								m_aInputData[FinishedDummy].m_Hook = 0;
								if(m_aInputData[FinishedDummy].m_Fire & 1)
									++m_aInputData[FinishedDummy].m_Fire;
								if(m_aInputData[FinishedDummy].m_NextWeapon & 1)
									++m_aInputData[FinishedDummy].m_NextWeapon;
								if(m_aInputData[FinishedDummy].m_PrevWeapon & 1)
									++m_aInputData[FinishedDummy].m_PrevWeapon;
								m_aInputData[FinishedDummy].m_Fire &= INPUT_STATE_MASK;
								m_aInputData[FinishedDummy].m_NextWeapon &= INPUT_STATE_MASK;
								m_aInputData[FinishedDummy].m_PrevWeapon &= INPUT_STATE_MASK;
								m_aInputData[FinishedDummy].m_WantedWeapon = 0;
								m_MacroPlaying = false;
								m_MacroPlaybackPaused = true;
								m_MacroPlaybackStartPending = false;
								m_MacroPlaybackLastTick = -1;
								GameClient()->Echo("Макрос завершён. История сохранена: можно перемотать её в локальную FP-тренировку.");
							}
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
			}
		}
	}
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
			(int)m_vMacroFrames.size(), m_MacroPlaybackRate);
	else if(m_MacroPlaybackLive)
		str_format(aTickLabel, sizeof(aTickLabel), "МАКРОС ЗАВЕРШЁН  |  ТИК %d / %d  |  %d ТИК/С",
			std::clamp(m_MacroDisplayedTick, 1, (int)m_vMacroFrames.size()),
			(int)m_vMacroFrames.size(), m_MacroPlaybackRate);
	else
		str_format(aTickLabel, sizeof(aTickLabel), "FP-ТРЕНИРОВКА  |  ТИК %d  |  %d ТИК/С",
			m_MacroPracticeTick, MacroPracticeRate());

	const float BoxWidth = std::min(640.0f, ScreenWidth - 32.0f);
	const float BoxHeight = 118.0f;
	const float BoxX = (ScreenWidth - BoxWidth) * 0.5f;
	const float BoxY = 76.0f;
	Graphics()->DrawRect(BoxX, BoxY, BoxWidth, BoxHeight,
		ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 2.0f);
	Graphics()->DrawRect(BoxX + 4.0f, BoxY + 4.0f, BoxWidth - 8.0f, BoxHeight - 8.0f,
		ColorRGBA(0.42f, 0.42f, 0.42f, 0.88f), IGraphics::CORNER_ALL, 2.0f);

	const char *pClientName = "КЛИЕНТ 10";
	const float ClientFontSize = 15.0f;
	const float ClientWidth = TextRender()->TextWidth(ClientFontSize, pClientName);
	TextRender()->TextColor(0.55f, 0.9f, 1.0f, 1.0f);
	TextRender()->Text((ScreenWidth - ClientWidth) * 0.5f, BoxY + 7.0f, ClientFontSize, pClientName);

	SMacroFrame DisplayFrame{};
	const SMacroFrame *pDisplayFrame = nullptr;
	if(m_MacroRecording && !m_vMacroFrames.empty())
		pDisplayFrame = &m_vMacroFrames.back();
	else if(m_MacroPlaying && !m_vMacroFrames.empty())
	{
		const int FrameIndex = std::clamp(m_MacroDisplayedTick - 1, 0, (int)m_vMacroFrames.size() - 1);
		pDisplayFrame = &m_vMacroFrames[FrameIndex];
	}
	else if(m_MacroHasCurrentFrame)
		pDisplayFrame = &m_MacroCurrentFrame;
	else if(m_MacroPracticeActive)
	{
		DisplayFrame.m_Input = m_MacroPracticeInput;
		if(const CCharacter *pCharacter = m_MacroPracticeWorld.GetCharacterById(m_MacroPracticeLocalId))
			DisplayFrame.m_Weapon = pCharacter->GetActiveWeapon();
		pDisplayFrame = &DisplayFrame;
	}

	char aActionLabel[192];
	str_copy(aActionLabel, "ДЕЙСТВИЕ: ОЖИДАНИЕ");
	if(pDisplayFrame)
	{
		const CNetObj_PlayerInput &Input = pDisplayFrame->m_Input;
		const char *pDirection = Input.m_Direction < 0 ? "ВЛЕВО" : Input.m_Direction > 0 ? "ВПРАВО" : "СТОЮ";
		const int Weapon = std::clamp(pDisplayFrame->m_Weapon, (int)WEAPON_HAMMER, (int)WEAPON_NINJA);
		const char *pWeapon = "ОРУЖИЕ";
		switch(Weapon)
		{
		case WEAPON_HAMMER: pWeapon = "МОЛОТ"; break;
		case WEAPON_GUN: pWeapon = "ПИСТОЛЕТ"; break;
		case WEAPON_SHOTGUN: pWeapon = "ДРОБОВИК"; break;
		case WEAPON_GRENADE: pWeapon = "ГРАНАТА"; break;
		case WEAPON_LASER: pWeapon = "ЛАЗЕР"; break;
		case WEAPON_NINJA: pWeapon = "НИНДЗЯ"; break;
		}
		str_format(aActionLabel, sizeof(aActionLabel), "ДЕЙСТВИЕ: %s | %s", pDirection, pWeapon);
		if(Input.m_Jump)
		{
			char aTemp[sizeof(aActionLabel)];
			str_copy(aTemp, aActionLabel);
			str_format(aActionLabel, sizeof(aActionLabel), "%s | ПРЫЖОК", aTemp);
		}
		if(Input.m_Hook)
		{
			char aTemp[sizeof(aActionLabel)];
			str_copy(aTemp, aActionLabel);
			str_format(aActionLabel, sizeof(aActionLabel), "%s | ХУК", aTemp);
		}
		if(Input.m_Fire & 1)
		{
			char aTemp[sizeof(aActionLabel)];
			str_copy(aTemp, aActionLabel);
			str_format(aActionLabel, sizeof(aActionLabel), "%s | ОГОНЬ", aTemp);
		}
	}

	const float FontSize = ScreenWidth < 700.0f ? 22.0f : 30.0f;
	const float Width = TextRender()->TextWidth(FontSize, aTickLabel);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->Text((ScreenWidth - Width) * 0.5f, BoxY + 34.0f, FontSize, aTickLabel);
	const float ActionFontSize = ScreenWidth < 700.0f ? 12.0f : 16.0f;
	const float ActionWidth = TextRender()->TextWidth(ActionFontSize, aActionLabel);
	TextRender()->TextColor(0.9f, 0.9f, 0.9f, 1.0f);
	TextRender()->Text((ScreenWidth - ActionWidth) * 0.5f, BoxY + 80.0f, ActionFontSize, aActionLabel);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(PreviousScreen);
}

bool CControls::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(GameClient()->IsWorldPaused())
		return false;

	const int Dummy = m_MacroPlaybackLive ? m_MacroPlaybackDummy : g_Config.m_ClDummy;

	if(CursorType == IInput::CURSOR_JOYSTICK && g_Config.m_InpControllerAbsolute && GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 AbsoluteDirection;
		if(Input()->GetActiveJoystick()->Absolute(&AbsoluteDirection.x, &AbsoluteDirection.y))
		{
			m_aMousePos[Dummy] = AbsoluteDirection * GetMaxMouseDistance();
			if(m_MacroPlaybackLive || (m_MacroPracticeActive && m_MacroPlaying))
				m_aMacroManualAimActive[Dummy] = true;
			GameClient()->m_Controls.m_aMouseInputType[Dummy] = CControls::EMouseInputType::ABSOLUTE;
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

	m_aMousePos[Dummy] += vec2(x, y) * Factor;
	if(m_MacroPlaybackLive || (m_MacroPracticeActive && m_MacroPlaying))
		m_aMacroManualAimActive[Dummy] = true;
	GameClient()->m_Controls.m_aMouseInputType[Dummy] = CControls::EMouseInputType::RELATIVE;
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
