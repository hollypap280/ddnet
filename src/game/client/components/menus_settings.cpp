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
		Localize("Клавиши")};
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
	static CButtonContainer s_ImportBindsButton;
	static CButtonContainer s_aMacroButtons[8];
	static char s_aSelectedMacro[64] = "";
	static char s_aRenameInputFor[64] = "";
	static CLineInputBuffered<64> s_RenameInput;
	static CLineInputBuffered<64> s_NewMacroName;
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
		Controls.GetMacroList(vMacros);

		bool SelectionExists = false;
		for(const CControls::SMacroInfo &Info : vMacros)
			if(str_comp(s_aSelectedMacro, Info.m_aName) == 0)
				SelectionExists = true;
		if(!SelectionExists)
		{
			str_copy(s_aSelectedMacro, vMacros.empty() ? "" : vMacros.front().m_aName);
			str_copy(s_aRenameInputFor, "");
		}
		if(str_comp(s_aRenameInputFor, s_aSelectedMacro) != 0)
		{
			s_RenameInput.Set(s_aSelectedMacro);
			str_copy(s_aRenameInputFor, s_aSelectedMacro);
		}

		MainView.HSplitTop(8.0f, nullptr, &MainView);
		if(!Controls.MacroRecording() && !Controls.MacroPlaying())
		{
			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect NameLabel, NameInput;
			Row.VSplitLeft(112.0f, &NameLabel, &NameInput);
			Ui()->DoLabel(&NameLabel, Localize("Имя до записи:"), 12.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_NewMacroName, &NameInput, 12.0f);
		}
		MainView.HSplitTop(34.0f, &Row, &MainView);
		CUIRect RecordButton, ButtonGap, PlayButton;
		Row.VSplitLeft(Row.w * 0.5f - 2.0f, &RecordButton, &ButtonGap);
		ButtonGap.VSplitLeft(4.0f, nullptr, &PlayButton);

		if(Controls.MacroRecording())
		{
			if(DoButton_Menu(&s_RecordButton, Localize("Остановить и сохранить"), 0, &RecordButton))
			{
				char aSavedName[64];
				str_copy(aSavedName, Controls.MacroRecordingName());
				Controls.StopMacroRecording();
				s_NewMacroName.Set("");
				str_copy(s_aSelectedMacro, aSavedName);
				s_RenameInput.Set(aSavedName);
				str_copy(s_aRenameInputFor, aSavedName);
			}
		}
		else if(Controls.MacroPlaying())
		{
			if(DoButton_Menu(&s_RecordButton, Localize("Остановить"), 0, &RecordButton))
				Controls.StopMacroRecording();
		}
		else if(DoButton_Menu(&s_RecordButton, Localize("Начать"), 0, &RecordButton))
		{
			Controls.StartMacroRecording(s_NewMacroName.GetString());
		}

		const bool CanPlay = !Controls.MacroRecording() && !Controls.MacroPlaying() && s_aSelectedMacro[0];
		if(CanPlay)
		{
			if(DoButton_Menu(&s_PlayButton, Localize("Запустить"), 0, &PlayButton))
				Controls.PlayMacro(s_aSelectedMacro);
		}
		else
		{
			DoButton_Menu(&s_PlayButton, Localize("Запустить"), 0, &PlayButton);
		}

		MainView.HSplitTop(24.0f, &Row, &MainView);
		char aStatus[160];
		if(Controls.MacroRecording())
			str_format(aStatus, sizeof(aStatus), "Запись: %s  |  %d тиков",
				Controls.MacroRecordingName(), Controls.MacroFrameCount());
		else if(Controls.MacroPlaying())
			str_format(aStatus, sizeof(aStatus), "Воспроизведение: тик %d / %d",
				Controls.MacroPosition(), Controls.MacroFrameCount());
		else
			str_copy(aStatus, Localize("Управление и прицел сохраняются автоматически."));
		Ui()->DoLabel(&Row, aStatus, 13.0f, TEXTALIGN_ML);

		MainView.HSplitTop(26.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Сохранённые макросы"), 14.0f, TEXTALIGN_ML);
		const int VisibleMacros = std::min((int)vMacros.size(), (int)std::size(s_aMacroButtons));
		for(int i = 0; i < VisibleMacros; ++i)
		{
			MainView.HSplitTop(27.0f, &Row, &MainView);
			char aLabel[160];
			str_format(aLabel, sizeof(aLabel), "%s  |  %d тиков  |  %s",
				vMacros[i].m_aName, vMacros[i].m_TickCount, vMacros[i].m_aMapName);
			const bool Selected = str_comp(s_aSelectedMacro, vMacros[i].m_aName) == 0;
			if(DoButton_Menu(&s_aMacroButtons[i], aLabel, Selected, &Row))
			{
				str_copy(s_aSelectedMacro, vMacros[i].m_aName);
				s_RenameInput.Set(s_aSelectedMacro);
				str_copy(s_aRenameInputFor, s_aSelectedMacro);
			}
		}
		if((int)vMacros.size() > VisibleMacros)
		{
			MainView.HSplitTop(18.0f, &Row, &MainView);
			Ui()->DoLabel(&Row, Localize("Показаны первые 8 макросов."), 11.0f, TEXTALIGN_ML);
		}

		if(s_aSelectedMacro[0] && !Controls.MacroRecording() && !Controls.MacroPlaying())
		{
			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect NameLabel, NameInput;
			Row.VSplitLeft(72.0f, &NameLabel, &NameInput);
			Ui()->DoLabel(&NameLabel, Localize("Название:"), 12.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_RenameInput, &NameInput, 12.0f);

			MainView.HSplitTop(30.0f, &Row, &MainView);
			CUIRect RenameButton, ActionGap, DeleteButton;
			Row.VSplitLeft(Row.w * 0.5f - 2.0f, &RenameButton, &ActionGap);
			ActionGap.VSplitLeft(4.0f, nullptr, &DeleteButton);
			if(DoButton_Menu(&s_RenameButton, Localize("Переименовать"), 0, &RenameButton))
			{
				if(Controls.RenameMacro(s_aSelectedMacro, s_RenameInput.GetString()))
				{
					str_copy(s_aSelectedMacro, s_RenameInput.GetString());
					str_copy(s_aRenameInputFor, s_aSelectedMacro);
					GameClient()->Echo("Макрос переименован.");
				}
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
					GameClient()->Echo("Не удалось удалить макрос.");
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
		if(DoButton_Menu(&s_ImportBindsButton, Localize("Импортировать мои бинды"), 0, &Row))
			Controls.ImportMacroBinds();
		MainView.HSplitTop(30.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Импортируются бинды движения, прыжка, хука, огня и выбора оружия."), 12.0f, TEXTALIGN_ML);
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Составные бинды сохраняют остальные команды и работают в обычной игре."), 12.0f, TEXTALIGN_ML);
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Бинды автоматически подключаются при начале записи и запуске макроса."), 12.0f, TEXTALIGN_ML);
		MainView.HSplitTop(28.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Прозрачные серверные персонажи не получают управление из FP."), 12.0f, TEXTALIGN_ML);
	}
}

void CMenus::RenderSettingsButtons(CUIRect MainView)
{
	static int s_ButtonSubPage = 0;
	static CButtonContainer s_MacroTab, s_GameTab;
	CUIRect TabRow, MacroTab, TabGap, GameTab;
	MainView.HSplitTop(28.0f, &TabRow, &MainView);
	TabRow.VSplitLeft(TabRow.w * 0.5f - 2.0f, &MacroTab, &TabGap);
	TabGap.VSplitLeft(4.0f, nullptr, &GameTab);
	if(DoButton_Menu(&s_MacroTab, Localize("Макросы"), s_ButtonSubPage == 0, &MacroTab))
		s_ButtonSubPage = 0;
	if(DoButton_Menu(&s_GameTab, Localize("Игровые функции"), s_ButtonSubPage == 1, &GameTab))
		s_ButtonSubPage = 1;
}
