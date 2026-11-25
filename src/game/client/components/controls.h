/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <cstddef>
#include <vector>

#include <game/client/component.h>
#include <game/client/prediction/gameworld.h>

class CCharacter;

class CControls : public CComponent
{
public:
	float GetMinMouseDistance() const;
	float GetMaxMouseDistance() const;

	enum class EMouseInputType
	{
		ABSOLUTE,
		RELATIVE,
		AUTOMATED,
	};

	bool m_aMouseOnAction[NUM_DUMMIES];
	vec2 m_aMousePos[NUM_DUMMIES];
	vec2 m_aMousePosOnAction[NUM_DUMMIES];
	vec2 m_aTargetPos[NUM_DUMMIES];

	EMouseInputType m_aMouseInputType[NUM_DUMMIES];

	int m_aAmmoCount[NUM_WEAPONS];

	int64_t m_LastSendTime;
	CNetObj_PlayerInput m_aInputData[NUM_DUMMIES];
	CNetObj_PlayerInput m_aLastData[NUM_DUMMIES];
	int m_aInputDirectionLeft[NUM_DUMMIES];
	int m_aInputDirectionRight[NUM_DUMMIES];
	int m_aShowHookColl[NUM_DUMMIES];

	CControls();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnUpdate() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnConsoleInit() override;
	bool OnInput(const IInput::CEvent &Event) override;
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	void ResetInput(int Dummy);

	bool MacroRecording() const { return m_MacroRecording; }
	bool MacroPlaying() const { return m_MacroPlaying; }
	bool MacroPlaybackLive() const { return m_MacroPlaybackLive; }
	bool MacroPaused() const { return m_MacroRecording ? m_MacroRecordingPaused : m_MacroPlaybackPaused; }
	int MacroPosition() const { return m_MacroRecording ? (int)m_vMacroFrames.size() : m_MacroPlaying ? m_MacroDisplayedTick : (int)m_MacroPlaybackIndex; }
	bool SeekMacroBy(int Offset);
	void ToggleMacroPause();
	int MacroFrameCount() const { return (int)m_vMacroFrames.size(); }
	const char *MacroRecordingName() const { return m_aRecordingMacroName; }
	struct SMacroInfo
	{
		char m_aName[64]{};
		char m_aMapName[64]{};
		int m_TickCount = 0;
	};
	void GetMacroList(std::vector<SMacroInfo> &vMacros);
	int MacroSeekStep() const;
	void SetMacroSeekStep(int Step);
	int MacroPlaybackRate() const { return m_MacroPlaybackRate; }
	int MacroPracticeRate() const;
	bool MacroPracticeActive() const { return m_MacroPracticeActive; }
	int MacroPracticeTick() const { return m_MacroPracticeTick; }
	vec2 MacroPracticePosition();
	void OnMacroPracticeDummySwap();
	void OnMacroObserverInputSent(const CNetObj_PlayerInput &Input);
	bool TripleflyAssistActive() const { return m_AutoTripleflyBindActive; }
	void OnTripleflyHammerSent();
	bool TripleflyHookActive() const;
	CGameWorld *MacroPracticeWorld() { return m_MacroPracticeActive ? &m_MacroPracticeWorld : nullptr; }
	float MacroPracticeTickFraction() const { return (float)m_MacroPracticeAccumulator; }
	bool MacroPracticeWaiting() const { return m_MacroPracticeWaiting; }
	void RenderMacroHud();
	bool StartMacroPractice();
	void StopMacroPractice();
	void ResetMacroPracticeCharacter();
	bool StartMacroRecording(const char *pName = nullptr);
	void StopMacroRecording();
	bool SaveMacro(const char *pName);
	bool PlayMacro(const char *pName);
	bool DeleteMacro(const char *pName);

private:
	enum class EMacroFrameMode { PAUSE = 0, STEP = 1, RESTORE_PAUSED = 2, STOP = 3 };
	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
	static void ConAutoTriplefly(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConTpToCursor(IConsole::IResult *pResult, void *pUserData);
	static void ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData);
	vec2 CursorWorldPos() const;
	void TeleportMacroPracticeCharacter(int ClientId, vec2 Pos);

	struct SMacroFrame
	{
		CNetObj_PlayerInput m_Input{};
		CNetObj_CharacterCore m_PreCore{};
		CNetObj_CharacterCore m_Core{};
		CNetObj_Character m_Character{};
		int m_PreWeapon = WEAPON_HAMMER;
		int m_Weapon = WEAPON_HAMMER;
		int m_AttackTick = 0;
		int m_Tick = 0;
		int m_HasCoordinates = 1;
		// Tee controlled in this private-world frame, or -1 in older macros.
		int m_ClientId = -1;
		// Freeze state is separate from CCharacterCore and must be replayed too.
		int m_PreFreezeTime = 0;
		int m_FreezeTime = 0;
		int m_PreFrozenLastTick = 0;
		int m_FrozenLastTick = 0;
		int m_PreAttackTick = 0;
		int m_HasDDNetState = 0;
		CNetObj_DDNetCharacter m_PreDDNetCharacter{};
		int m_aPreWeaponAmmo[NUM_WEAPONS]{};
		int m_aWeaponAmmo[NUM_WEAPONS]{};
	};
	struct SMacroFrameV7
	{
		CNetObj_PlayerInput m_Input{};
		CNetObj_CharacterCore m_PreCore{};
		CNetObj_CharacterCore m_Core{};
		CNetObj_Character m_Character{};
		int m_PreWeapon = WEAPON_HAMMER;
		int m_Weapon = WEAPON_HAMMER;
		int m_AttackTick = 0;
		int m_Tick = 0;
		int m_HasCoordinates = 1;
		int m_ClientId = -1;
		int m_PreFreezeTime = 0;
		int m_FreezeTime = 0;
		int m_PreFrozenLastTick = 0;
		int m_FrozenLastTick = 0;
		int m_PreAttackTick = 0;
		int m_HasDDNetState = 0;
		CNetObj_DDNetCharacter m_PreDDNetCharacter{};
	};
	struct SMacroFrameV6
	{
		CNetObj_PlayerInput m_Input{};
		CNetObj_CharacterCore m_PreCore{};
		CNetObj_CharacterCore m_Core{};
		CNetObj_Character m_Character{};
		int m_PreWeapon = WEAPON_HAMMER;
		int m_Weapon = WEAPON_HAMMER;
		int m_AttackTick = 0;
		int m_Tick = 0;
		int m_HasCoordinates = 1;
		int m_ClientId = -1;
		int m_PreFreezeTime = 0;
		int m_FreezeTime = 0;
		int m_PreFrozenLastTick = 0;
		int m_FrozenLastTick = 0;
	};
	struct SMacroFrameV4
	{
		CNetObj_PlayerInput m_Input{};
		CNetObj_CharacterCore m_PreCore{};
		CNetObj_CharacterCore m_Core{};
		CNetObj_Character m_Character{};
		int m_PreWeapon = WEAPON_HAMMER;
		int m_Weapon = WEAPON_HAMMER;
		int m_AttackTick = 0;
		int m_Tick = 0;
		int m_HasCoordinates = 1;
		int m_ClientId = -1;
	};
	struct SMacroFrameV3
	{
		CNetObj_PlayerInput m_Input{};
		CNetObj_CharacterCore m_PreCore{};
		CNetObj_CharacterCore m_Core{};
		CNetObj_Character m_Character{};
		int m_PreWeapon = WEAPON_HAMMER;
		int m_Weapon = WEAPON_HAMMER;
		int m_AttackTick = 0;
		int m_Tick = 0;
		int m_HasCoordinates = 1;
	};
	struct SMacroFrameV1
	{
		int m_Direction;
		int m_Jump;
		int m_Fire;
		int m_Hook;
		int m_WantedWeapon;
		int m_NextWeapon;
		int m_PrevWeapon;
		int m_TargetX;
		int m_TargetY;
	};
	std::vector<SMacroFrame> m_vMacroFrames;
	std::size_t m_MacroPlaybackIndex = 0;
	int m_MacroStartFrameIndex = 0;
	int m_MacroDisplayedTick = 0;
	SMacroFrame m_MacroCurrentFrame{};
	bool m_MacroHasCurrentFrame = false;
	bool m_MacroHoldCurrentFrame = false;
	bool m_MacroRecording = false;
	bool m_MacroRecordingPaused = false;
	bool m_MacroPlaying = false;
	bool m_MacroPlaybackLive = false;
	bool m_MacroPlaybackStartPending = false;
	bool m_MacroPlaybackManualDummyOverride = false;
	bool m_MacroPlaybackPaused = false;
	bool m_MacroStopAtTickArmed = false;
	int m_MacroPlaybackSeekTargetIndex = -1;
	int m_aMacroPlaybackWeaponOverride[NUM_DUMMIES] = {-1, -1};
	int m_MacroRecordingRate = 50;
	int m_MacroPlaybackRate = 50;
	int m_MacroPlaybackDummy = 0;
	bool m_aMacroPlaybackConnectionUsed[NUM_DUMMIES]{};
	bool m_MacroPlaybackOwnsDummy = false;
	bool m_MacroPlaybackOwnsPractice = false;
	int m_MacroPlaybackLastTick = -1;
	bool m_MacroRewindHeld = false;
	int64_t m_MacroRewindHeldSince = 0;
	int64_t m_MacroRewindNextRepeat = 0;
	int m_aMacroLiveFireOffset[NUM_DUMMIES]{};
	int m_aMacroLiveNextWeaponOffset[NUM_DUMMIES]{};
	int m_aMacroLivePrevWeaponOffset[NUM_DUMMIES]{};
	bool m_aMacroManualDirectionActive[NUM_DUMMIES]{};
	bool m_aMacroManualJumpActive[NUM_DUMMIES]{};
	bool m_aMacroManualHookActive[NUM_DUMMIES]{};
	bool m_aMacroManualAimActive[NUM_DUMMIES]{};
	bool m_MacroPracticeActive = false;
	bool m_MacroPracticeWaiting = false;
	double m_MacroPracticeAccumulator = 0.0;
	int64_t m_MacroPracticeLastTime = 0;
	int m_MacroPracticeTick = 0;
	int m_MacroPracticeLocalId = -1;
	int m_MacroPracticeStartTick = 0;
	CNetObj_CharacterCore m_MacroPracticeStartCore{};
	int m_MacroPracticeStartWeapon = WEAPON_HAMMER;
	int m_MacroPracticeStartFreezeTime = 0;
	bool m_MacroPracticeStartFrozenLastTick = false;
	CNetObj_PlayerInput m_MacroPracticeInput{};
	CNetObj_PlayerInput m_MacroPracticeDummyInput{};
	bool m_MacroPracticeDummyEnabled = false;
	bool m_MacroPracticeDummyLeft = false;
	bool m_MacroPracticeDummyRight = false;
	int m_MacroPracticePendingFireTransitions = 0;
	int m_MacroPracticePendingNextWeaponTransitions = 0;
	int m_MacroPracticePendingPrevWeaponTransitions = 0;
	int m_MacroPracticePendingWantedWeapon = 0;
	int m_MacroPracticeFireCounter = 0;
	bool m_MacroPracticePendingJump = false;
	bool m_MacroPracticePendingHook = false;
	bool m_MacroPracticePendingDirectionLeft = false;
	bool m_MacroPracticePendingDirectionRight = false;
	bool m_MacroPracticeDirectionLeft = false;
	bool m_MacroPracticeDirectionRight = false;
	bool m_MacroPracticeFirePressed = false;
	bool m_MacroPracticeNextWeaponPressed = false;
	bool m_MacroPracticePrevWeaponPressed = false;
	CGameWorld m_MacroPracticeWorld;
	// Exact starting snapshot used to rebuild the entire practice world when
	// rewinding during recording or seeking into a saved macro.
	CGameWorld m_MacroPracticeStartWorld;
	int m_MacroPracticeStartLocalId = -1;
	bool m_aMacroPracticeStartSnapshotInitialized[MAX_CLIENTS]{};
	bool m_MacroPracticeBaselineValid = false;
	char m_aMacroPracticeBaselineName[64] = "";
	CNetObj_Character m_MacroPracticeRenderChar{};
	bool m_MacroPracticeHasRenderChar = false;
	// Tracks one-time import from the live snapshot independently from whether
	// a local dummy is currently allowed to move in the private practice world.
	bool m_aMacroPracticeSnapshotInitialized[MAX_CLIENTS]{};
	char m_aMacroMapName[64] = "Неизвестная карта";
	char m_aRecordingMacroName[64] = "";
	bool m_aManualHook[NUM_DUMMIES]{};
	bool m_aAutoHookActive[NUM_DUMMIES]{};
	bool m_aAutoHookFireActive[NUM_DUMMIES]{};
	bool m_aAutoAvoidActive[NUM_DUMMIES]{};
	bool m_aAutoEdgeHookActive[NUM_DUMMIES]{};

	// Triplefly assist keeps the dummy copying movement and aims its hook
	// toward the third tee. Hook pulses are timed just after a main-tee hit.
	bool m_AutoTripleflyBindActive = false;
	int m_AutoTripleflyPreviousAuto = 0;
	int m_AutoTripleflyPreviousDummyCopyMoves = 0;
	int m_AutoTripleflyPreviousDummyControl = 0;
	int m_AutoTripleflyPreviousDummyHook = 0;
	int m_AutoTripleflyPreviousDummyFire = 0;
	int m_AutoTripleflyPreviousDummyJump = 0;
	int m_AutoTripleflyPreviousDummyHammer = 0;
	int m_TripleflyHookStartTick = -1;
	int m_TripleflyHookEndTick = -1;
	int m_TripleflyHookDuration = 4;
	int m_aTripleflyTargetClientId[NUM_DUMMIES] = {-1, -1};
	int m_aTripleflyAimX[NUM_DUMMIES]{};
	int m_aTripleflyAimY[NUM_DUMMIES]{};
	int m_aTripleflyRecommendedHookTicks[NUM_DUMMIES]{};
	bool m_aTripleflyTargetFound[NUM_DUMMIES]{};

	int m_aMacroPickupWeaponOverride[NUM_DUMMIES] = {-1, -1};
	void ApplyHookAutomation(int Dummy);
	void ApplyEdgeAssist(int Dummy);
	void ApplyEdgeHook(int Dummy);
	bool MacroFilePath(const char *pName, char *pBuffer, unsigned BufferSize) const;
	static int MacroListCallback(const char *pName, int IsDir, int DirType, void *pUser);
	bool SeekMacro(int Offset, bool ShowMessage = true, bool TeleportLive = false);
	bool RebuildMacroWorldToPosition(int Position);
	void RebaseMacroPlaybackCounters(int Dummy, int PreviousFrameIndex);
	void SimulateMacroPracticeTick(const CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &DummyInput);
	bool RestoreMacroPracticeFrame(int Index);
	void UpdateMacroPracticeGhost(CCharacter *pCharacter);
	void PopulateMacroPracticeWorld();
	bool SwitchMacroPracticeCharacter(int ClientId);
	static void ConMacroPractice(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPause(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroRecord(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroStop(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSave(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPlay(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroDelete(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPlayFirst(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroRewind(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSeek(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroRewindStep(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroForwardStep(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroGoto(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroStopAtTick(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroGotoConfigured(IConsole::IResult *pResult, void *pUserData);
	enum class EMacroPracticeDummyAction
	{
		LEFT,
		RIGHT,
		JUMP,
		HOOK,
		FIRE,
	};
	struct SMacroPracticeDummyBind
	{
		CControls *m_pControls;
		EMacroPracticeDummyAction m_Action;
	};
	static void ConMacroPracticeDummyInput(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPracticeInput(IConsole::IResult *pResult, void *pUserData);
	void SendMacroFrame(int Index, EMacroFrameMode Mode);
	void SendPracticeTeleport(vec2 Pos, int Conn);
	void SendPracticeTeleportToCursor(int Conn);
};
#endif
