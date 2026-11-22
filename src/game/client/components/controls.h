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
	bool MacroSlowHeld() const { return m_MacroSlowHeld; }
	int MacroPosition() const { return m_MacroRecording ? (int)m_vMacroFrames.size() : (int)m_MacroPlaybackIndex; }
	int MacroFrameCount() const { return (int)m_vMacroFrames.size(); }
	struct SMacroInfo
	{
		char m_aName[64]{};
		char m_aMapName[64]{};
		int m_TickCount = 0;
	};
	void GetMacroList(std::vector<SMacroInfo> &vMacros);
	int MacroSeekStep() const;
	void SetMacroSeekStep(int Step);
	int MacroPracticeRate() const;
	void SetMacroPracticeRate(int Rate);
	bool MacroPracticeActive() const { return m_MacroPracticeActive; }
	int MacroPracticeTick() const { return m_MacroPracticeTick; }
	vec2 MacroPracticePosition();
	void OnMacroPracticeDummySwap();
	CGameWorld *MacroPracticeWorld() { return m_MacroPracticeActive ? &m_MacroPracticeWorld : nullptr; }
	float MacroPracticeTickFraction() const { return m_MacroPracticeAccumulator; }
	bool MacroPracticeWaiting() const { return m_MacroPracticeWaiting; }
	bool StartMacroPractice();
	void StopMacroPractice();
	void StartMacroRecording();
	void StopMacroRecording();
	bool SaveMacro(const char *pName);
	bool PlayMacro(const char *pName);
	bool DeleteMacro(const char *pName);

private:
	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
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
	std::vector<SMacroFrame> m_vMacroCorrectionFrames;
	std::size_t m_MacroPlaybackIndex = 0;
	SMacroFrame m_MacroCurrentFrame{};
	bool m_MacroHasCurrentFrame = false;
	bool m_MacroHoldCurrentFrame = false;
	bool m_MacroRecording = false;
	bool m_MacroPlaying = false;
	bool m_MacroPlaybackLive = false;
	bool m_MacroPlaybackOwnsDummy = false;
	bool m_MacroPlaybackOwnsPractice = false;
	int m_MacroPlaybackLastTick = -1;
	bool m_MacroRewindHeld = false;
	int64_t m_MacroRewindHeldSince = 0;
	int64_t m_MacroRewindNextRepeat = 0;
	bool m_MacroSlowHeld = false;
	int m_MacroPlaybackSlowTickCounter = 0;
	int m_MacroLiveFireOffset = 0;
	int m_MacroLiveNextWeaponOffset = 0;
	int m_MacroLivePrevWeaponOffset = 0;
	bool m_MacroPracticeActive = false;
	bool m_MacroPracticeWaiting = false;
	float m_MacroPracticeAccumulator = 0.0f;
	int64_t m_MacroPracticeLastTime = 0;
	int m_MacroPracticeTick = 0;
	int m_MacroPracticeLocalId = -1;
	CNetObj_PlayerInput m_MacroPracticeInput{};
	int m_MacroPracticePendingFireTransitions = 0;
	int m_MacroPracticeFireCounter = 0;
	bool m_MacroPracticePendingJump = false;
	bool m_MacroPracticePendingHook = false;
	CGameWorld m_MacroPracticeWorld;
	CNetObj_Character m_MacroPracticeRenderChar{};
	bool m_MacroPracticeHasRenderChar = false;
	char m_aMacroMapName[64] = "Неизвестная карта";
	char m_aLoadedMacroName[64] = "";
	bool m_aManualHook[NUM_DUMMIES]{};
	bool m_aAutoHookActive[NUM_DUMMIES]{};
	bool m_aAutoHookFireActive[NUM_DUMMIES]{};
	bool m_aAutoAvoidActive[NUM_DUMMIES]{};
	bool m_aAutoEdgeHookActive[NUM_DUMMIES]{};
	void ApplyHookAutomation(int Dummy);
	void ApplyEdgeAssist(int Dummy);
	void ApplyEdgeHook(int Dummy);
	bool MacroFilePath(const char *pName, char *pBuffer, unsigned BufferSize) const;
	static int MacroListCallback(const char *pName, int IsDir, int DirType, void *pUser);
	bool SeekMacro(int Offset, bool ShowMessage = true, bool TeleportLive = false);
	void SimulateMacroPracticeTick(const CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &DummyInput);
	bool RestoreMacroPracticeFrame(int Index);
	void UpdateMacroPracticeGhost(CCharacter *pCharacter);
	void PopulateMacroPracticeWorld();
	bool SwitchMacroPracticeCharacter(int ClientId);
	static void ConMacroRecord(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroStop(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSave(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPlay(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroDelete(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroRewind(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSeek(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSlow(IConsole::IResult *pResult, void *pUserData);
	void SendPracticeTeleport(vec2 Pos, int Conn);
	void SendPracticeTeleportToCursor(int Conn);
};
#endif
