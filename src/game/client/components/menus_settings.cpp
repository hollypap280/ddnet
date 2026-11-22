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
	CUIRect Row, Label;
	MainView.HSplitTop(28.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Игровые функции"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	static CButtonContainer s_AutoHook, s_Angle, s_Radius, s_Avoid, s_Edge;
	static CButtonContainer s_Gun, s_Shotgun, s_Grenade, s_Laser;
	static CButtonContainer s_AutoTriplefly, s_EdgeHook, s_DummyFlight;
	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHook, Localize("Автохук"), g_Config.m_ClAutoHook, &Row))
		g_Config.m_ClAutoHook ^= 1;
	Ui()->DoToolTip(&s_AutoHook, &Row, Localize("Автоматически цепляется за ближайшего видимого тии в заданном радиусе"));

	MainView.HSplitTop(28.0f, &Row, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_ClAutoHookAngle, &g_Config.m_ClAutoHookAngle, &Row, Localize("Угол автохука (градусы)"), 10, 360);
	Ui()->DoToolTip(&s_Angle, &Row, Localize("Меняет угол поиска независимо от дальности. 250° исключает направление строго за спиной, 360° разрешает полный круг."));

	MainView.HSplitTop(28.0f, &Row, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_ClAutoHookRange, &g_Config.m_ClAutoHookRange, &Row, Localize("Дальность автохука"), 32, 360);

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookShowRadius, Localize("Показывать круг зоны крюка"), g_Config.m_ClAutoHookShowRadius, &Row))
		g_Config.m_ClAutoHookShowRadius ^= 1;
	Ui()->DoToolTip(&s_Radius, &Row, Localize("Показывает вокруг тии круг максимальной дальности крюка"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoAvoid, Localize("Защита от фриза"), g_Config.m_ClAutoAvoid, &Row))
		g_Config.m_ClAutoAvoid ^= 1;
	Ui()->DoToolTip(&s_Avoid, &Row, Localize("При подъёме или падении автоматически ищет стену для крюка, чтобы не сорваться во фриз"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoEdge, Localize("Точная наводка на край"), g_Config.m_ClAutoEdge, &Row))
		g_Config.m_ClAutoEdge ^= 1;
	Ui()->DoToolTip(&s_Edge, &Row, Localize("Пока зажат крюк, ищет точку зацепа рядом с прицелом, чтобы точнее цепляться за углы"));

	MainView.HSplitTop(22.0f, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("Прицеливание оружием в той же зоне"), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookWeaponGun, Localize("Автовыстрел: пистолет"), g_Config.m_ClAutoHookWeaponGun, &Row))
		g_Config.m_ClAutoHookWeaponGun ^= 1;
	Ui()->DoToolTip(&s_Gun, &Row, Localize("Стреляет выбранным оружием по тии, на которого навёлся автохук"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookWeaponShotgun, Localize("Автовыстрел: дробовик"), g_Config.m_ClAutoHookWeaponShotgun, &Row))
		g_Config.m_ClAutoHookWeaponShotgun ^= 1;
	Ui()->DoToolTip(&s_Shotgun, &Row, Localize("Стреляет выбранным оружием по тии, на которого навёлся автохук"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookWeaponGrenade, Localize("Автовыстрел: гранатомёт"), g_Config.m_ClAutoHookWeaponGrenade, &Row))
		g_Config.m_ClAutoHookWeaponGrenade ^= 1;
	Ui()->DoToolTip(&s_Grenade, &Row, Localize("Стреляет выбранным оружием по тии, на которого навёлся автохук"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookWeaponLaser, Localize("Автовыстрел: лазер"), g_Config.m_ClAutoHookWeaponLaser, &Row))
		g_Config.m_ClAutoHookWeaponLaser ^= 1;
	Ui()->DoToolTip(&s_Laser, &Row, Localize("Стреляет выбранным оружием по тии, на которого навёлся автохук"));


	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoTriplefly, Localize("Автотрёххук (цепляться за третьего тии)"), g_Config.m_ClAutoTriplefly, &Row))
		g_Config.m_ClAutoTriplefly ^= 1;
	Ui()->DoToolTip(&s_AutoTriplefly, &Row, Localize("Выбирает третьего ближайшего подходящего тии в зоне крюка"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoEdgeHook, Localize("Подхват крюком у края (без прыжка)"), g_Config.m_ClAutoEdgeHook, &Row))
		g_Config.m_ClAutoEdgeHook ^= 1;
	Ui()->DoToolTip(&s_EdgeHook, &Row, Localize("Автоматически цепляется крюком за поверхность у края платформы. Прыжок не нажимается."));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClDummyHammer, Localize("Полёт дамми (обычный хаммерфлай)"), g_Config.m_ClDummyHammer, &Row))
		g_Config.m_ClDummyHammer ^= 1;
	Ui()->DoToolTip(&s_DummyFlight, &Row, Localize("Использует обычный ввод хаммерфлая для дамми"));

}

void CMenus::RenderSettingsMacros(CUIRect MainView)
{
	CUIRect Row, Label, Button, NameRect, SearchRect;

	static CLineInputBuffered<64> s_MacroName;
	static CLineInputBuffered<64> s_MacroSearch;
	static CLineInputNumber s_RewindStep;
	static bool s_RewindStepInitialized = false;
	static std::vector<CControls::SMacroInfo> s_vMacros;
	static std::vector<CButtonContainer> s_vMacroButtons;
	static bool s_MacroListLoaded = false;
	static char s_aSelectedMacro[64]{};
	static int s_MacroSubPage = 0;
	static CButtonContainer s_MacroTab, s_SettingsTab;
	static CButtonContainer s_ShowOthers;
	static CButtonContainer s_RecordToggle;
	static CButtonContainer s_PlayToggle, s_Delete, s_Refresh;
	static CButtonContainer s_MacroLoop;
	static CScrollRegion s_MacroListScrollRegion;
	static CScrollRegion s_MacroSettingsScrollRegion;

	CUIRect TabRow, MacroTab, TabGap, SettingsTab;
	MainView.HSplitTop(28.0f, &TabRow, &MainView);
	TabRow.VSplitLeft(TabRow.w * 0.5f - 2.0f, &MacroTab, &TabGap);
	TabGap.VSplitLeft(4.0f, nullptr, &SettingsTab);
	if(DoButton_Menu(&s_MacroTab, Localize("Макрос"), s_MacroSubPage == 0, &MacroTab))
		s_MacroSubPage = 0;
	if(DoButton_Menu(&s_SettingsTab, Localize("Настройки"), s_MacroSubPage == 1, &SettingsTab))
		s_MacroSubPage = 1;

	auto RefreshMacroList = [&]() {
		GameClient()->m_Controls.GetMacroList(s_vMacros);
		s_vMacroButtons.resize(std::max(s_vMacroButtons.size(), s_vMacros.size()));
		s_MacroListLoaded = true;

		const auto MatchesSearch = [&](const CControls::SMacroInfo &Info) {
			return s_MacroSearch.IsEmpty() ||
				str_utf8_find_nocase(Info.m_aName, s_MacroSearch.GetString()) ||
				str_utf8_find_nocase(Info.m_aMapName, s_MacroSearch.GetString());
		};
		bool SelectedIsVisible = false;
		const CControls::SMacroInfo *pFirstVisible = nullptr;
		for(const CControls::SMacroInfo &Info : s_vMacros)
		{
			if(!MatchesSearch(Info))
				continue;
			if(!pFirstVisible)
				pFirstVisible = &Info;
			if(str_comp(s_aSelectedMacro, Info.m_aName) == 0)
				SelectedIsVisible = true;
		}
		if(!SelectedIsVisible)
		{
			if(pFirstVisible)
				str_copy(s_aSelectedMacro, pFirstVisible->m_aName);
			else
				s_aSelectedMacro[0] = '\\0';
		}
	};
	if(!s_MacroListLoaded)
		RefreshMacroList();


	if(s_MacroSubPage == 0)
	{
		const CControls::SMacroInfo *pFirstVisibleMacro = nullptr;
		bool SelectedIsVisible = false;
		for(const CControls::SMacroInfo &Info : s_vMacros)
		{
			if(!s_MacroSearch.IsEmpty() &&
				!str_utf8_find_nocase(Info.m_aName, s_MacroSearch.GetString()) &&
				!str_utf8_find_nocase(Info.m_aMapName, s_MacroSearch.GetString()))
				continue;
			if(!pFirstVisibleMacro)
				pFirstVisibleMacro = &Info;
			if(str_comp(s_aSelectedMacro, Info.m_aName) == 0)
				SelectedIsVisible = true;
		}
		if(!SelectedIsVisible)
		{
			if(pFirstVisibleMacro)
				str_copy(s_aSelectedMacro, pFirstVisibleMacro->m_aName);
			else
				s_aSelectedMacro[0] = '\0';
		}

		// Keep all recording and playback controls visible. Only the macro
		// library below is scrollable, so its length cannot hide Stop.
		CUIRect Header;
		MainView.HSplitTop(24.0f, &Header, &MainView);
		Ui()->DoLabel(&Header, Localize("Запись и воспроизведение"), 17.0f, TEXTALIGN_ML);

		CUIRect NameRow;
		MainView.HSplitTop(27.0f, &NameRow, &MainView);
		NameRow.VSplitLeft(78.0f, &Label, &NameRect);
		Ui()->DoLabel(&Label, Localize("Название"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_MacroName, &NameRect, 13.0f);

		const bool Recording = GameClient()->m_Controls.MacroRecording();
		const bool Playing = GameClient()->m_Controls.MacroPlaying();
		const bool PracticeActive = GameClient()->m_Controls.MacroPracticeActive();

		CUIRect RecordingRow;
		MainView.HSplitTop(28.0f, &RecordingRow, &MainView);
		RecordingRow.VSplitLeft(142.0f, &Button, &Row);
		const char *pRecordLabel = Recording ? Localize("Завершить и сохранить") :
			Playing ? Localize("Сначала останови макрос") : Localize("Начать макрос");
		if(DoButton_Menu(&s_RecordToggle, pRecordLabel, Recording, &Button))
		{
			if(Recording)
			{
				char aCompletedMacroName[64]{};
				str_copy(aCompletedMacroName, GameClient()->m_Controls.MacroRecordingName());
				GameClient()->m_Controls.StopMacroRecording();
				if(aCompletedMacroName[0])
					str_copy(s_aSelectedMacro, aCompletedMacroName);
				RefreshMacroList();
			}
			else if(Playing)
			{
				GameClient()->Echo(Localize("Сначала останови воспроизведение"));
			}
			else
			{
				const char *pName = s_MacroName.IsEmpty() ? nullptr : s_MacroName.GetString();
				GameClient()->m_Controls.StartMacroRecording(pName);
			}
		}
		const char *pPracticeHint = Recording ? GameClient()->m_Controls.MacroRecordingName() :
			PracticeActive ? Localize("FP-тренировка активна") : Localize("Настоящий тии остаётся неподвижен на сервере");
		Ui()->DoLabel(&Row, pPracticeHint, 11.0f, TEXTALIGN_ML);

		CUIRect PlaybackRow;
		MainView.HSplitTop(28.0f, &PlaybackRow, &MainView);
		PlaybackRow.VSplitLeft(142.0f, &Button, &Row);
		if(DoButton_Menu(&s_PlayToggle,
			Playing ? Localize("Остановить макрос") : Localize("Запустить выбранный"),
			Playing, &Button))
		{
			if(GameClient()->m_Controls.MacroPlaying())
			{
				GameClient()->m_Controls.StopMacroRecording();
			}
			else if(GameClient()->m_Controls.MacroRecording())
			{
				GameClient()->Echo(Localize("Сначала останови и сохрани запись"));
			}
			else
			{
				RefreshMacroList();
				char aPlaybackName[64]{};
				if(s_aSelectedMacro[0])
					str_copy(aPlaybackName, s_aSelectedMacro);
				else if(!s_MacroName.IsEmpty())
					str_copy(aPlaybackName, s_MacroName.GetString());

				if(!aPlaybackName[0])
					GameClient()->Echo(Localize("Сначала выбери макрос в библиотеке"));
				else
					GameClient()->m_Controls.PlayMacro(aPlaybackName);
			}
		}
		Row.VSplitLeft(6.0f, nullptr, &Row);
		Row.VSplitLeft(112.0f, &Button, nullptr);
		if(DoButton_Menu(&s_Delete, Localize("Удалить выбранный"), 0, &Button) && s_aSelectedMacro[0])
		{
			if(GameClient()->m_Controls.MacroPlaying())
				GameClient()->m_Controls.StopMacroRecording();
			if(GameClient()->m_Controls.DeleteMacro(s_aSelectedMacro))
			{
				GameClient()->Echo(Localize("Макрос удалён"));
				s_aSelectedMacro[0] = '\0';
				RefreshMacroList();
			}
			else
				GameClient()->Echo(Localize("Не удалось удалить макрос."));
		}

		CUIRect StatusRow;
		MainView.HSplitTop(20.0f, &StatusRow, &MainView);
		char aStatus[160];
		if(GameClient()->m_Controls.MacroRecording())
			str_format(aStatus, sizeof(aStatus), "ЗАПИСЬ  |  %d тиков", GameClient()->m_Controls.MacroFrameCount());
		else if(GameClient()->m_Controls.MacroPlaying())
			str_format(aStatus, sizeof(aStatus), "ВОСПРОИЗВЕДЕНИЕ  |  %d / %d тиков",
				GameClient()->m_Controls.MacroPosition(), GameClient()->m_Controls.MacroFrameCount());
		else
			str_format(aStatus, sizeof(aStatus), "ГОТОВО  |  %d тиков", GameClient()->m_Controls.MacroFrameCount());
		Ui()->DoLabel(&StatusRow, aStatus, 12.0f, TEXTALIGN_ML);

		CUIRect LoopRow;
		MainView.HSplitTop(24.0f, &LoopRow, &MainView);
		if(DoButton_CheckBox(&s_MacroLoop, Localize("Повторять макрос по кругу"),
			g_Config.m_ClMacroLoop, &LoopRow))
			g_Config.m_ClMacroLoop ^= 1;

		static CButtonContainer s_MacroTraining;
		CUIRect MacroTrainingRow;
		MainView.HSplitTop(24.0f, &MacroTrainingRow, &MainView);
		if(DoButton_CheckBox(&s_MacroTraining, Localize("Обучать макрос моими исправлениями"),
			g_Config.m_ClMacroTraining, &MacroTrainingRow))
			g_Config.m_ClMacroTraining ^= 1;

		CUIRect MacroTrainingInfo;
		MainView.HSplitTop(30.0f, &MacroTrainingInfo, &MainView);
		Ui()->DoLabel(&MacroTrainingInfo,
			Localize("Когда макрос ждёт совпадения позиции, веди тии вручную. Этот участок добавится в макрос для следующих запусков."),
			11.0f, TEXTALIGN_ML);

		CUIRect SearchRow;
		MainView.HSplitTop(25.0f, &SearchRow, &MainView);
		SearchRow.VSplitRight(76.0f, &SearchRect, &Button);
		SearchRect.VSplitLeft(62.0f, &Label, &SearchRect);
		Ui()->DoLabel(&Label, Localize("Поиск"), 12.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_MacroSearch, &SearchRect, 13.0f);
		if(DoButton_Menu(&s_Refresh, Localize("Обновить"), 0, &Button))
			RefreshMacroList();

		CUIRect ListHeader;
		MainView.HSplitTop(21.0f, &ListHeader, &MainView);
		char aListHeader[128];
		str_format(aListHeader, sizeof(aListHeader), "%s: %s",
			Localize("Библиотека"), s_aSelectedMacro[0] ? s_aSelectedMacro : Localize("выбери макрос"));
		Ui()->DoLabel(&ListHeader, aListHeader, 12.0f, TEXTALIGN_ML);

		CScrollRegionParams ListParams;
		ListParams.m_ScrollbarThickness = 10.0f;
		ListParams.m_ScrollbarMargin = 2.0f;
		ListParams.m_ScrollUnit = 42.0f;
		s_MacroListScrollRegion.Begin(&MainView, &ListParams);

		CUIRect Content = MainView;
		int VisibleMacroCount = 0;
		for(std::size_t i = 0; i < s_vMacros.size(); ++i)
		{
			const CControls::SMacroInfo &Info = s_vMacros[i];
			if(!s_MacroSearch.IsEmpty() &&
				!str_utf8_find_nocase(Info.m_aName, s_MacroSearch.GetString()) &&
				!str_utf8_find_nocase(Info.m_aMapName, s_MacroSearch.GetString()))
				continue;

			++VisibleMacroCount;
			CUIRect MacroRow;
			Content.HSplitTop(26.0f, &MacroRow, &Content);
			if(!s_MacroListScrollRegion.AddRect(MacroRow))
				continue;

			char aEntry[196];
			str_format(aEntry, sizeof(aEntry), "%s  |  %s  |  %d %s",
				Info.m_aName, Info.m_aMapName, Info.m_TickCount, Localize("тиков"));
			if(DoButton_Menu(&s_vMacroButtons[i], aEntry,
				str_comp(s_aSelectedMacro, Info.m_aName) == 0, &MacroRow))
				str_copy(s_aSelectedMacro, Info.m_aName);
		}
		if(VisibleMacroCount == 0)
		{
			CUIRect EmptyRow;
			Content.HSplitTop(24.0f, &EmptyRow, &Content);
			if(s_MacroListScrollRegion.AddRect(EmptyRow))
				Ui()->DoLabel(&EmptyRow, Localize("Макросов не найдено"), 12.0f, TEXTALIGN_ML);
		}
		s_MacroListScrollRegion.End();
	}
	else
	{
		CScrollRegionParams SettingsParams;
		SettingsParams.m_ScrollbarThickness = 10.0f;
		SettingsParams.m_ScrollbarMargin = 2.0f;
		SettingsParams.m_ScrollUnit = 42.0f;
		s_MacroSettingsScrollRegion.Begin(&MainView, &SettingsParams);

		CUIRect Content = MainView;
		auto AddSettingRow = [&](float Height, CUIRect &Rect) {
			Content.HSplitTop(Height, &Rect, &Content);
			return s_MacroSettingsScrollRegion.AddRect(Rect);
		};
		auto AddSection = [&](const char *pTitle) {
			CUIRect Section;
			if(AddSettingRow(24.0f, Section))
			{
				Section.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_ALL, 4.0f);
				Section.Margin(6.0f, &Section);
				Ui()->DoLabel(&Section, Localize(pTitle), 13.0f, TEXTALIGN_ML);
			}
		};

		CUIRect Header;
		if(AddSettingRow(27.0f, Header))
			Ui()->DoLabel(&Header, Localize("Настройки тренировочного мира"), 17.0f, TEXTALIGN_ML);

		AddSection("FP-ПРАКТИКА");
		const bool PracticeActive = GameClient()->m_Controls.MacroPracticeActive();
		CUIRect PracticeStatus;
		if(AddSettingRow(21.0f, PracticeStatus))
			Ui()->DoLabel(&PracticeStatus,
				PracticeActive ? Localize("Состояние: FP-тренировка включена") : Localize("Состояние: FP-тренировка выключена"),
				12.0f, TEXTALIGN_ML);

		CUIRect PracticeInfo;
		if(AddSettingRow(44.0f, PracticeInfo))
			Ui()->DoLabel(&PracticeInfo,
				Localize("Нажми «Начать макрос» во вкладке Макрос: игра переключится на локальную тренировочную копию, а настоящий персонаж останется неподвижен на сервере. При завершении запись сохранится автоматически, а копия исчезнет."),
				11.0f, TEXTALIGN_ML);

		CUIRect ShowOthersRow;
		if(AddSettingRow(29.0f, ShowOthersRow) &&
			DoButton_CheckBox(&s_ShowOthers,
				Localize("Другие тии: AFK, но двигаются от крюка, ударов и столкновений"),
				g_Config.m_ClMacroPracticeShowOthers, &ShowOthersRow))
			g_Config.m_ClMacroPracticeShowOthers ^= 1;

		CUIRect PracticeRateRow;
		if(AddSettingRow(27.0f, PracticeRateRow))
			Ui()->DoScrollbarOption(&g_Config.m_ClMacroPracticeRate, &g_Config.m_ClMacroPracticeRate,
				&PracticeRateRow, Localize("Скорость тренировки (тиков в секунду)"), 1, 50);

		AddSection("ПЕРЕМОТКА И ЗАМЕДЛЕНИЕ");
		if(!s_RewindStepInitialized)
		{
			s_RewindStep.SetInteger(g_Config.m_ClMacroRewindStep);
			s_RewindStepInitialized = true;
		}
		CUIRect StepRow;
		if(AddSettingRow(27.0f, StepRow))
		{
			StepRow.VSplitLeft(185.0f, &Label, &NameRect);
			Ui()->DoLabel(&Label, Localize("Шаг перехода (тиков)"), 12.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_RewindStep, &NameRect, 13.0f);
			if(s_RewindStep.WasChanged())
			{
				const int Step = s_RewindStep.GetInteger();
				if(Step >= 1 && Step <= 18000)
					g_Config.m_ClMacroRewindStep = Step;
				else
					s_RewindStep.SetInteger(g_Config.m_ClMacroRewindStep);
			}
		}
		CUIRect SlowRow;
		if(AddSettingRow(27.0f, SlowRow))
			Ui()->DoScrollbarOption(&g_Config.m_ClMacroSlowFactor, &g_Config.m_ClMacroSlowFactor,
				&SlowRow, Localize("Замедление при удержании клавиши"), 2, 10);

		AddSection("КОМАНДЫ В ТРЕНИРОВКЕ");
		CUIRect InfoRow;
		if(AddSettingRow(48.0f, InfoRow))
			Ui()->DoLabel(&InfoRow,
				Localize("Обычные бинды работают в FP-тренировке. Движение выполняется в локальном мире с выбранной скоростью; на сервер отправляется только нейтральный ввод."),
				12.0f, TEXTALIGN_ML);

		s_MacroSettingsScrollRegion.End();
	}
}

void CMenus::RenderSettingsButtons(CUIRect MainView)
{
	CUIRect Row, Label, KeyRect;
	MainView.HSplitTop(28.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Назначение клавиш для функций"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	struct SHotkey
	{
		const char *m_pLabel;
		const char *m_pCommand;
	};
	static const SHotkey s_aHotkeys[] = {
		{ "Автотрёххук", "toggle cl_auto_triplefly 0 1" },
		{ "Автохук", "toggle cl_auto_hook 0 1" },
		{ "Защита от фриза", "toggle cl_auto_avoid 0 1" },
		{ "Точная наводка на край", "toggle cl_auto_edge 0 1" },
		{ "Подхват крюком у края", "toggle cl_auto_edge_hook 0 1" },
		{ "Полёт дамми (хаммерфлай)", "toggle cl_dummy_hammer 0 1" },
		{ "Перемотка макроса назад (удерживать)", "+macro_rewind" },
		{ "Телепорт макроса вперёд (одно нажатие)", "+macro_seek" },
		{ "Замедлить макрос (удерживать)", "+macro_slow" },
	};
	static CButtonContainer s_aKeyReaders[std::size(s_aHotkeys)];
	static CButtonContainer s_aKeyClearButtons[std::size(s_aHotkeys)];

	for(std::size_t i = 0; i < std::size(s_aHotkeys); ++i)
	{
		MainView.HSplitTop(26.0f, &Row, &MainView);
		Row.VSplitLeft(250.0f, &Label, &KeyRect);
		Ui()->DoLabel(&Label, Localize(s_aHotkeys[i].m_pLabel), 14.0f, TEXTALIGN_ML);

		CBindSlot CurrentBind = EMPTY_BIND_SLOT;
		bool Found = false;
		for(int ModifierMask = 0; ModifierMask < KeyModifier::COMBINATION_COUNT && !Found; ++ModifierMask)
		{
			for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
			{
				const CBindSlot Candidate(Key, ModifierMask);
				if(str_comp(GameClient()->m_Binds.Get(Candidate), s_aHotkeys[i].m_pCommand) == 0)
				{
					CurrentBind = Candidate;
					Found = true;
					break;
				}
			}
		}

		const CKeyBinder::CKeyReaderResult Result = GameClient()->m_KeyBinder.DoKeyReader(
			&s_aKeyReaders[i], &s_aKeyClearButtons[i], &KeyRect, CurrentBind, false);
		if(Result.m_Aborted || Result.m_Bind == CurrentBind)
			continue;

		if(CurrentBind.m_Key != KEY_UNKNOWN || Result.m_Bind.m_Key == KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(CurrentBind.m_Key, "", false, CurrentBind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, s_aHotkeys[i].m_pCommand, false, Result.m_Bind.m_ModifierMask);
	}

	MainView.HSplitTop(12.0f, nullptr, &MainView);
	Ui()->DoLabel(&MainView, Localize("Нажмите на поле клавиши и выберите кнопку. Телепорт вперёд срабатывает один раз на нажатие и в основном мире требует /practice; перемотка назад повторяется при удержании; замедление действует, пока клавиша зажата."), 12.0f, TEXTALIGN_ML);
}
