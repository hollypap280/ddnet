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
	int MacroPosition() const { return (int)m_MacroPlaybackIndex; }
	int MacroFrameCount() const { return (int)m_vMacroFrames.size(); }
	int MacroSeekStep() const;
	void SetMacroSeekStep(int Step);
	int MacroPracticeRate() const;
	void SetMacroPracticeRate(int Rate);
	bool MacroPracticeActive() const { return m_MacroPracticeActive; }
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
	SMacroFrame m_MacroCurrentFrame{};
	bool m_MacroHasCurrentFrame = false;
	bool m_MacroHoldCurrentFrame = false;
	bool m_MacroRecording = false;
	bool m_MacroPlaying = false;
	bool m_MacroPracticeActive = false;
	bool m_MacroPracticeWaiting = false;
	float m_MacroPracticeAccumulator = 0.0f;
	int m_MacroPracticeTick = 0;
	int m_MacroPracticeLocalId = -1;
	CNetObj_PlayerInput m_MacroPracticeInput{};
	CGameWorld m_MacroPracticeWorld;
	CNetObj_Character m_MacroPracticeRenderChar{};
	bool m_MacroPracticeHasRenderChar = false;
	bool m_aManualHook[NUM_DUMMIES]{};
	bool m_aAutoHookActive[NUM_DUMMIES]{};
	bool m_aAutoHookFireActive[NUM_DUMMIES]{};
	bool m_aAutoAvoidActive[NUM_DUMMIES]{};
	bool m_aAutoEdgeJumpActive[NUM_DUMMIES]{};
	void ApplyHookAutomation(int Dummy);
	void ApplyEdgeAssist(int Dummy);
	void ApplyEdgeJump(int Dummy);
	bool MacroFilePath(const char *pName, char *pBuffer, unsigned BufferSize) const;
	bool SeekMacro(int Offset);
	void SimulateMacroPracticeTick(const CNetObj_PlayerInput &Input);
	bool RestoreMacroPracticeFrame(int Index);
	void UpdateMacroPracticeGhost(CCharacter *pCharacter);
	static void ConMacroRecord(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroStop(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroSave(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroPlay(IConsole::IResult *pResult, void *pUserData);
	static void ConMacroDelete(IConsole::IResult *pResult, void *pUserData);
	void SendPracticeTeleportToCursor(int Conn);
};
#endif
