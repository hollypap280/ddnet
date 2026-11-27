/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menu_background.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <iterator>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::RenderSettings(CUIRect MainView)
{
	// render background
	CUIRect Button, TabBar, RestartBar;
	MainView.VSplitRight(120.0f, &MainView, &TabBar);
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);

	const char *apTabs[SETTINGS_LENGTH] = {
		Localize("Language"),
		Localize("General"),
		Localize("Player"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		Localize("Credits"),
		Localize("Игровые функции"),
		Localize("Макросы"),
		Localize("Кнопки")};
	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			g_Config.m_UiSettingsPage = i;
	}

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
		RenderLanguageSettings(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsGeneral(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
		RenderSettingsPlayer(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(MainView);
		else
			RenderSettingsTee(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderSettingsAppearance(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderSettingsGraphics(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderSettingsSound(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsDDNet(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsAssets(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
		RenderSettingsCredits(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GAME)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsGame(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_MACROS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsMacros(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_BUTTONS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsButtons(MainView);
	}
	else
	{
		dbg_assert_failed("ui_settings_page invalid");
	}

	if(NeedRestart)
	{
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
	}
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 0, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 1, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}


void CMenus::RenderSettingsGame(CUIRect MainView)
{
	(void)MainView;
}

void CMenus::RenderSettingsMacros(CUIRect MainView)
{
	static int s_MacroSubPage = 0;
	static CButtonContainer s_MacroTab, s_SettingsTab;
	static CButtonContainer s_RecordButton, s_PlayButton, s_RenameButton, s_DeleteButton;
	static CButtonContainer s_ImportBindsButton, s_CreateFolderButton, s_BackToRootButton;
	static CButtonContainer s_PinButton, s_MoveButton, s_TrajectoryToggle;
	static CButtonContainer s_aMacroButtons[1024];
	static CButtonContainer s_aFolderButtons[256];
	static char s_aSelectedMacro[96] = "";
	static char s_aRenameInputFor[96] = "";
	static char s_aMacroFolder[48] = "";
	static CLineInputBuffered<64> s_RenameInput;
	static CLineInputBuffered<64> s_NewMacroName;
	static CLineInputBuffered<48> s_NewFolderName;
	static CScrollRegion s_MacroListScrollRegion;

	CUIRect TabRow, MacroTab, TabGap, SettingsTab;
	MainView.HSplitTop(28.0f, &TabRow, &MainView);
	TabRow.VSplitLeft(TabRow.w * 0.5f - 2.0f, &MacroTab, &TabGap);
	TabGap.VSplitLeft(4.0f, nullptr, &SettingsTab);
	if(DoButton_Menu(&s_MacroTab, Localize("Макрос"), s_MacroSubPage == 0, &MacroTab))
		s_MacroSubPage = 0;
	if(DoButton_Menu(&s_SettingsTab, Localize("Настройки"), s_MacroSubPage == 1, &SettingsTab))
		s_MacroSubPage = 1;

	CControls &Controls = GameClient()->m_Controls;
	CUIRect Row;
	if(s_MacroSubPage == 0)
	{
		std::vector<CControls::SMacroInfo> vMacros;
		std::vector<std::string> vFolders;
		Controls.GetMacroList(vMacros, s_aMacroFolder);
		Controls.GetMacroFolders(vFolders);

		if(Controls.MacroRecording())
			str_copy(s_aSelectedMacro, Controls.MacroRecordingName());

		bool SelectionInCurrentFolder = false;
		const char *pSelectedSlash = strrchr(s_aSelectedMacro, '/');
		const std::string SelectedFolder = pSelectedSlash ?
			std::string(s_aSelectedMacro, pSelectedSlash - s_aSelectedMacro) : std::string();
		SelectionInCurrentFolder = SelectedFolder == s_aMacroFolder;
		bool SelectionExists = false;
		for(const CControls::SMacroInfo &Info : vMacros)
			if(str_comp(s_aSelectedMacro, Info.m_aName) == 0)
				SelectionExists = true;
		if(!Controls.MacroRecording() && s_aSelectedMacro[0] && SelectionInCurrentFolder && !SelectionExists)
			s_aSelectedMacro[0] = '\0';
		if(!s_aSelectedMacro[0] && !vMacros.empty())
			str_copy(s_aSelectedMacro, vMacros.front().m_aName);

		if(str_comp(s_aRenameInputFor, s_aSelectedMacro) != 0)
		{
			const char *pBaseName = strrchr(s_aSelectedMacro, '/');
			s_RenameInput.Set(pBaseName ? pBaseName + 1 : s_aSelectedMacro);
			str_copy(s_aRenameInputFor, s_aSelectedMacro);
		}
		if(!Controls.MacroRecording() && s_aSelectedMacro[0] && !s_NewMacroName.GetString()[0])
		{
			const char *pBaseName = strrchr(s_aSelectedMacro, '/');
			s_NewMacroName.Set(pBaseName ? pBaseName + 1 : s_aSelectedMacro);
		}

		MainView.HSplitTop(8.0f, nullptr, &MainView);
		if(!Controls.MacroRecording() && !Controls.MacroPlaying())
		{
			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect NameLabel, NameInput;
			Row.VSplitLeft(112.0f, &NameLabel, &NameInput);
			Ui()->DoLabel(&NameLabel, Localize("Имя макроса:"), 12.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_NewMacroName, &NameInput, 12.0f);
		}
		MainView.HSplitTop(34.0f, &Row, &MainView);
		CUIRect RecordButton, ButtonGap, PlayButton;
		Row.VSplitLeft(Row.w * 0.5f - 2.0f, &RecordButton, &ButtonGap);
		ButtonGap.VSplitLeft(4.0f, nullptr, &PlayButton);

		if(Controls.MacroRecording())
		{
			if(DoButton_Menu(&s_RecordButton, Localize("Остановить и сохранить"), 0, &RecordButton))
				Controls.StopMacroRecording();
		}
		else if(Controls.MacroPlaying())
		{
			if(DoButton_Menu(&s_RecordButton, Localize("Остановить"), 0, &RecordButton))
				Controls.StopMacroRecording();
		}
		else if(DoButton_Menu(&s_RecordButton, Localize("Начать запись"), 0, &RecordButton))
		{
			char aMacroName[96];
			if(s_aMacroFolder[0])
				str_format(aMacroName, sizeof(aMacroName), "%s/%s", s_aMacroFolder, s_NewMacroName.GetString());
			else
				str_copy(aMacroName, s_NewMacroName.GetString());
			if(Controls.StartMacroRecording(aMacroName))
			{
				str_copy(s_aSelectedMacro, Controls.MacroRecordingName());
				str_copy(s_aRenameInputFor, "");
				const char *pBaseName = strrchr(s_aSelectedMacro, '/');
				s_RenameInput.Set(pBaseName ? pBaseName + 1 : s_aSelectedMacro);
			}
		}

		const bool CanPlay = !Controls.MacroRecording() && !Controls.MacroPlaying() && s_aSelectedMacro[0];
		if(CanPlay)
		{
			if(DoButton_Menu(&s_PlayButton, Localize("Запустить выбранный"), 0, &PlayButton))
				Controls.PlayMacro(s_aSelectedMacro);
		}
		else
			DoButton_Menu(&s_PlayButton, Localize("Запустить выбранный"), 0, &PlayButton);

		MainView.HSplitTop(24.0f, &Row, &MainView);
		char aStatus[160];
		if(Controls.MacroRecording())
			str_format(aStatus, sizeof(aStatus), "Запись: %s  |  %d тиков",
				Controls.MacroRecordingName(), Controls.MacroFrameCount());
		else if(Controls.MacroPlaying())
			str_format(aStatus, sizeof(aStatus), "Воспроизведение: тик %d / %d",
				Controls.MacroPosition(), Controls.MacroFrameCount());
		else
			str_copy(aStatus, Localize("Управление вернётся к обычным биндам после остановки."));
		Ui()->DoLabel(&Row, aStatus, 12.0f, TEXTALIGN_ML);

		// Folder navigation and creation.
		MainView.HSplitTop(28.0f, &Row, &MainView);
		if(s_aMacroFolder[0])
		{
			char aFolderLabel[96];
			str_format(aFolderLabel, sizeof(aFolderLabel), "Папка: %s", s_aMacroFolder);
			CUIRect FolderLabel, BackButton;
			Row.VSplitRight(100.0f, &FolderLabel, &BackButton);
			Ui()->DoLabel(&FolderLabel, aFolderLabel, 13.0f, TEXTALIGN_ML);
			if(DoButton_Menu(&s_BackToRootButton, Localize("В корень"), 0, &BackButton))
				s_aMacroFolder[0] = '\0';
		}
		else
			Ui()->DoLabel(&Row, Localize("Библиотека макросов / корень"), 13.0f, TEXTALIGN_ML);

		if(!s_aMacroFolder[0] && !Controls.MacroRecording() && !Controls.MacroPlaying())
		{
			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect FolderInput, CreateFolder;
			Row.VSplitRight(112.0f, &FolderInput, &CreateFolder);
			FolderInput.VSplitRight(5.0f, &FolderInput, nullptr);
			Ui()->DoEditBox(&s_NewFolderName, &FolderInput, 12.0f);
			if(DoButton_Menu(&s_CreateFolderButton, Localize("Создать папку"), 0, &CreateFolder))
			{
				if(Controls.CreateMacroFolder(s_NewFolderName.GetString()))
				{
					GameClient()->Echo("Папка создана.");
					str_copy(s_aMacroFolder, s_NewFolderName.GetString());
					s_NewFolderName.Set("");
				}
				else
					GameClient()->Echo("Не удалось создать папку. Проверь название.");
			}
		}

		MainView.HSplitTop(178.0f, &Row, &MainView);
		static CScrollRegionParams ScrollParams;
		ScrollParams.m_ScrollUnit = 27.0f;
		s_MacroListScrollRegion.Begin(&Row, &ScrollParams);
		CUIRect Content = Row;
		if(!s_aMacroFolder[0])
		{
			for(size_t i = 0; i < vFolders.size() && i < std::size(s_aFolderButtons); ++i)
			{
				CUIRect FolderRow;
				Content.HSplitTop(27.0f, &FolderRow, &Content);
				if(!s_MacroListScrollRegion.AddRect(FolderRow))
					continue;
				char aFolderName[96];
				str_format(aFolderName, sizeof(aFolderName), "[ПАПКА] %s", vFolders[i].c_str());
				if(DoButton_Menu(&s_aFolderButtons[i], aFolderName, 0, &FolderRow))
					str_copy(s_aMacroFolder, vFolders[i].c_str());
			}
		}
		for(size_t i = 0; i < vMacros.size() && i < std::size(s_aMacroButtons); ++i)
		{
			CUIRect MacroRow;
			Content.HSplitTop(27.0f, &MacroRow, &Content);
			const bool Selected = str_comp(s_aSelectedMacro, vMacros[i].m_aName) == 0;
			if(!s_MacroListScrollRegion.AddRect(MacroRow, Selected))
				continue;
			char aLabel[180];
			str_format(aLabel, sizeof(aLabel), "%s%s  |  %d тиков  |  %s",
				vMacros[i].m_Pinned ? "★ " : "",
				strrchr(vMacros[i].m_aName, '/') ? strrchr(vMacros[i].m_aName, '/') + 1 : vMacros[i].m_aName,
				vMacros[i].m_TickCount, vMacros[i].m_aMapName);
			if(DoButton_Menu(&s_aMacroButtons[i], aLabel, Selected, &MacroRow))
			{
				str_copy(s_aSelectedMacro, vMacros[i].m_aName);
				const char *pBaseName = strrchr(s_aSelectedMacro, '/');
				s_RenameInput.Set(pBaseName ? pBaseName + 1 : s_aSelectedMacro);
				s_NewMacroName.Set(pBaseName ? pBaseName + 1 : s_aSelectedMacro);
				str_copy(s_aRenameInputFor, s_aSelectedMacro);
			}
		}
		s_MacroListScrollRegion.End();

		if(s_aSelectedMacro[0] && !Controls.MacroRecording() && !Controls.MacroPlaying())
		{
			const bool Pinned = Controls.IsMacroPinned(s_aSelectedMacro);
			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect NameLabel, NameInput;
			Row.VSplitLeft(72.0f, &NameLabel, &NameInput);
			Ui()->DoLabel(&NameLabel, Localize("Название:"), 12.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_RenameInput, &NameInput, 12.0f);

			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect RenameButton, PinButton, DeleteButton, Half;
			Row.VSplitLeft(Row.w / 3.0f, &RenameButton, &Half);
			Half.VSplitLeft(Row.w / 3.0f, &PinButton, &DeleteButton);
			if(DoButton_Menu(&s_RenameButton, Localize("Переименовать"), 0, &RenameButton))
			{
				const char *pSlash = strrchr(s_aSelectedMacro, '/');
				char aNewName[96];
				if(pSlash)
				{
					char aFolder[48];
					const size_t FolderLength = (size_t)(pSlash - s_aSelectedMacro);
					mem_copy(aFolder, s_aSelectedMacro, FolderLength);
					aFolder[FolderLength] = '\0';
					str_format(aNewName, sizeof(aNewName), "%s/%s", aFolder, s_RenameInput.GetString());
				}
				else
					str_copy(aNewName, s_RenameInput.GetString());
				if(Controls.RenameMacro(s_aSelectedMacro, aNewName))
				{
					str_copy(s_aSelectedMacro, aNewName);
					str_copy(s_aRenameInputFor, "");
					GameClient()->Echo("Макрос переименован.");
				}
			}
			if(DoButton_Menu(&s_PinButton, Pinned ? Localize("Снять ⭐") : Localize("Закрепить ⭐"), Pinned, &PinButton))
			{
				if(Controls.ToggleMacroPin(s_aSelectedMacro))
					GameClient()->Echo(Pinned ? "Макрос откреплён." : "Макрос закреплён ⭐.");
			}
			if(DoButton_Menu(&s_DeleteButton, Localize("Удалить"), 0, &DeleteButton))
			{
				if(Controls.DeleteMacro(s_aSelectedMacro))
				{
					GameClient()->Echo("Макрос удалён.");
					s_aSelectedMacro[0] = '\0';
					s_aRenameInputFor[0] = '\0';
				}
				else
					GameClient()->Echo(Pinned ? "Закреплённый макрос нельзя удалить. Сними звезду." : "Не удалось удалить макрос.");
			}

			const char *pSelectedSlash = strrchr(s_aSelectedMacro, '/');
			const std::string SelectedFolder = pSelectedSlash ?
				std::string(s_aSelectedMacro, pSelectedSlash - s_aSelectedMacro) : std::string();
			if(SelectedFolder != s_aMacroFolder)
			{
				MainView.HSplitTop(30.0f, &Row, &MainView);
				const char *pMoveLabel = s_aMacroFolder[0] ? "Переместить в эту папку" : "Переместить в корень";
				if(DoButton_Menu(&s_MoveButton, Localize(pMoveLabel), 0, &Row))
				{
					const char *pBaseName = pSelectedSlash ? pSelectedSlash + 1 : s_aSelectedMacro;
					char aBaseName[96];
					str_copy(aBaseName, pBaseName);
					if(Controls.MoveMacroToFolder(s_aSelectedMacro, s_aMacroFolder))
					{
						if(s_aMacroFolder[0])
							str_format(s_aSelectedMacro, sizeof(s_aSelectedMacro), "%s/%s", s_aMacroFolder, aBaseName);
						else
							str_copy(s_aSelectedMacro, aBaseName);
						str_copy(s_aRenameInputFor, "");
						GameClient()->Echo("Макрос перемещён.");
					}
					else
						GameClient()->Echo("Не удалось переместить макрос в эту папку.");
				}
			}
		}
	}
	else
	{
		MainView.HSplitTop(12.0f, nullptr, &MainView);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMacroMouseSens, &g_Config.m_ClMacroMouseSens, &Row,
			Localize("Чувствительность макроса"), 1, 500, &CUi::ms_LogarithmicScrollbarScale,
			CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Эта сенса используется только при записи; обычная сенса мыши не меняется."), 12.0f, TEXTALIGN_ML);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMacroPracticeRate, &g_Config.m_ClMacroPracticeRate, &Row,
			Localize("Скорость записи (тиков/с)"), 1, 50, &CUi::ms_LogarithmicScrollbarScale,
			CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		MainView.HSplitTop(24.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Можно записывать от 1 тика/с; сохранённый макрос запускается с обычной скоростью."), 11.0f, TEXTALIGN_ML);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		if(DoButton_Menu(&s_ImportBindsButton, Localize("Импортировать мои бинды"), 0, &Row))
			Controls.ImportMacroBinds();
		MainView.HSplitTop(30.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Бинды временно переключаются на управление макросом и восстанавливаются после остановки или выхода."), 12.0f, TEXTALIGN_ML);
		MainView.HSplitTop(30.0f, &Row, &MainView);
		if(DoButton_CheckBox(&s_TrajectoryToggle, Localize("Показывать траекторию гранаты и лазера"), g_Config.m_ClMacroTrajectory, &Row))
			g_Config.m_ClMacroTrajectory = !g_Config.m_ClMacroTrajectory;
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Показывает путь полёта, попадание и отскоки по текущим настройкам карты."), 11.0f, TEXTALIGN_ML);
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Прозрачные серверные персонажи не получают управление из FP."), 12.0f, TEXTALIGN_ML);
	}
}

void CMenus::RenderSettingsButtons(CUIRect MainView)
{
	static int s_ButtonSubPage = 0;
	static CButtonContainer s_MacroTab, s_GameTab;
	static CButtonContainer s_MacroRewindKeyReader, s_MacroRewindKeyClear, s_MacroRewindNow;
	static bool s_DefaultMacroRewindBindInitialized = false;
	CUIRect TabRow, MacroTab, TabGap, GameTab;
	MainView.HSplitTop(28.0f, &TabRow, &MainView);
	TabRow.VSplitLeft(TabRow.w * 0.5f - 2.0f, &MacroTab, &TabGap);
	TabGap.VSplitLeft(4.0f, nullptr, &GameTab);
	if(DoButton_Menu(&s_MacroTab, Localize("Макрос"), s_ButtonSubPage == 0, &MacroTab))
		s_ButtonSubPage = 0;
	if(DoButton_Menu(&s_GameTab, Localize("Игровые функции"), s_ButtonSubPage == 1, &GameTab))
		s_ButtonSubPage = 1;

	if(s_ButtonSubPage == 0)
	{
		CUIRect Row, Label, KeyReader;
		MainView.HSplitTop(14.0f, nullptr, &MainView);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		Row.VSplitLeft(220.0f, &Label, &KeyReader);
		Ui()->DoLabel(&Label, Localize("Отмотать запись макроса"), 13.0f, TEXTALIGN_ML);

		CBindSlot CurrentBind = EMPTY_BIND_SLOT;
		for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT && CurrentBind.m_Key == KEY_UNKNOWN; ++Modifier)
		{
			for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
			{
				if(str_comp(GameClient()->m_Binds.Get(Key, Modifier), "macro_record_rewind") == 0)
				{
					CurrentBind = CBindSlot(Key, Modifier);
					break;
				}
			}
		}

		if(!s_DefaultMacroRewindBindInitialized)
		{
			s_DefaultMacroRewindBindInitialized = true;
			if(CurrentBind.m_Key == KEY_UNKNOWN)
			{
				for(int Key = KEY_F6; Key <= KEY_F24; ++Key)
				{
					if(!GameClient()->m_Binds.Get(Key, KeyModifier::NONE)[0])
					{
						GameClient()->m_Binds.Bind(Key, "macro_record_rewind", false, KeyModifier::NONE);
						CurrentBind = CBindSlot(Key, KeyModifier::NONE);
						break;
					}
				}
			}
		}

		const CKeyBinder::CKeyReaderResult KeyResult = GameClient()->m_KeyBinder.DoKeyReader(
			&s_MacroRewindKeyReader, &s_MacroRewindKeyClear, &KeyReader, CurrentBind, false);
		if(!KeyResult.m_Aborted && KeyResult.m_Bind != CurrentBind)
		{
			// Keep one clear, user-selected key for the recording-only action.
			for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
			{
				for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
				{
					if(str_comp(GameClient()->m_Binds.Get(Key, Modifier), "macro_record_rewind") == 0)
						GameClient()->m_Binds.Bind(Key, "", false, Modifier);
				}
			}
			if(KeyResult.m_Bind.m_Key != KEY_UNKNOWN)
			{
				GameClient()->m_Binds.Bind(KeyResult.m_Bind.m_Key, "macro_record_rewind", false, KeyResult.m_Bind.m_ModifierMask);
				GameClient()->Echo("Клавиша отмотки записи назначена.");
			}
			else
			{
				GameClient()->Echo("Клавиша отмотки записи удалена.");
			}
		}

		MainView.HSplitTop(24.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Нажми на поле справа и выбери клавишу для отмотки записи."), 11.0f, TEXTALIGN_ML);
		MainView.HSplitTop(12.0f, nullptr, &MainView);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ClMacroRewindStep, &g_Config.m_ClMacroRewindStep, &Row,
			Localize("Шаг отмотки (тиков)"), 1, 250, &CUi::ms_LogarithmicScrollbarScale,
			CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		MainView.HSplitTop(24.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Работает только во время записи. Будущее после отмотки удаляется."), 11.0f, TEXTALIGN_ML);
		MainView.HSplitTop(12.0f, nullptr, &MainView);
		MainView.HSplitTop(34.0f, &Row, &MainView);
		const bool CanRewind = GameClient()->m_Controls.MacroRecording();
		if(DoButton_Menu(&s_MacroRewindNow, Localize("Отмотать сейчас"), 0, &Row) && CanRewind)
			GameClient()->m_Controls.RewindMacroRecording();
	}
}
