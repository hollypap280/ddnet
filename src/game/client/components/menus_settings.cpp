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
		Localize("Game"),
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
	static CButtonContainer s_AutoTriplefly, s_AutoUnfreeze, s_AimAtTee, s_Backhook, s_EdgeHook, s_DummyFlight;
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
	if(DoButton_CheckBox(&g_Config.m_ClAutoUnfreezeLaser, Localize("Авторазморозка лазером"), g_Config.m_ClAutoUnfreezeLaser, &Row))
		g_Config.m_ClAutoUnfreezeLaser ^= 1;
	Ui()->DoToolTip(&s_AutoUnfreeze, &Row, Localize("Находит ближайшего видимого замороженного тиммейта и стреляет по нему лазером"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoTriplefly, Localize("Автотрёххук (цепляться за третьего тии)"), g_Config.m_ClAutoTriplefly, &Row))
		g_Config.m_ClAutoTriplefly ^= 1;
	Ui()->DoToolTip(&s_AutoTriplefly, &Row, Localize("Выбирает третьего ближайшего подходящего тии в зоне крюка"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookAimAtTee, Localize("Наводиться на тии (двигает курсор)"), g_Config.m_ClAutoHookAimAtTee, &Row))
	{
		g_Config.m_ClAutoHookAimAtTee ^= 1;
		if(g_Config.m_ClAutoHookAimAtTee)
			g_Config.m_ClAutoHookBackhook = 0;
	}
	Ui()->DoToolTip(&s_AimAtTee, &Row, Localize("Передвигает курсор к выбранному тии во время работы автохука"));

	MainView.HSplitTop(24.0f, &Row, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClAutoHookBackhook, Localize("Бэкхук (не двигает курсор)"), g_Config.m_ClAutoHookBackhook, &Row))
	{
		g_Config.m_ClAutoHookBackhook ^= 1;
		if(g_Config.m_ClAutoHookBackhook)
			g_Config.m_ClAutoHookAimAtTee = 0;
	}
	Ui()->DoToolTip(&s_Backhook, &Row, Localize("Не двигает курсор и разрешает автонаведение за направлением курсора"));

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
	CUIRect Row, Label, NameRect, Button;
	MainView.HSplitTop(28.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("Тренировочный макрос (TAS)"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	static CButtonContainer s_PracticeStart, s_PracticeStop;
	MainView.HSplitTop(26.0f, &Row, &MainView);
	Row.VSplitLeft(145.0f, &Button, &Row);
	if(DoButton_Menu(&s_PracticeStart, Localize("Начать тренировку"), 0, &Button))
		GameClient()->m_Controls.StartMacroPractice();
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(145.0f, &Button, &Row);
	if(DoButton_Menu(&s_PracticeStop, Localize("Остановить тренировку"), 0, &Button))
		GameClient()->m_Controls.StopMacroPractice();

	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_ClMacroPracticeRate, &g_Config.m_ClMacroPracticeRate, &Row,
		Localize("Скорость тренировки (тиков в секунду)"), 1, 50);

	static CLineInputBuffered<64> s_MacroName;
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(120.0f, &Label, &NameRect);
	Ui()->DoLabel(&Label, Localize("Название макроса"), 14.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_MacroName, &NameRect, 14.0f);

	static CButtonContainer s_Record, s_Stop, s_Save, s_Play, s_Delete, s_Confirm, s_Cancel;
	static bool s_ConfirmDelete = false;
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.HSplitTop(26.0f, &Row, &MainView);
	Row.VSplitLeft(145.0f, &Button, &Row);
	if(DoButton_Menu(&s_Record, Localize("Начать запись"), 0, &Button))
		GameClient()->m_Controls.StartMacroRecording();
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(110.0f, &Button, &Row);
	if(DoButton_Menu(&s_Stop, Localize("Остановить"), 0, &Button))
		GameClient()->m_Controls.StopMacroRecording();

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(26.0f, &Row, &MainView);
	Row.VSplitLeft(110.0f, &Button, &Row);
	if(DoButton_Menu(&s_Save, Localize("Сохранить"), 0, &Button))
	{
		if(!s_MacroName.IsEmpty() && !GameClient()->m_Controls.SaveMacro(s_MacroName.GetString()))
			GameClient()->Echo(Localize("Не удалось сохранить макрос. Сначала остановите запись."));
		else if(!s_MacroName.IsEmpty())
			GameClient()->Echo(Localize("Макрос сохранён в папке macros"));
	}
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(110.0f, &Button, &Row);
	if(DoButton_Menu(&s_Play, Localize("Воспроизвести"), 0, &Button))
	{
		if(s_MacroName.IsEmpty() || !GameClient()->m_Controls.PlayMacro(s_MacroName.GetString()))
			GameClient()->Echo(Localize("Не удалось запустить макрос. Проверьте название или файл."));
	}
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(110.0f, &Button, &Row);
	if(DoButton_Menu(&s_Delete, Localize("Удалить"), 0, &Button) && !s_MacroName.IsEmpty())
		s_ConfirmDelete = true;

	if(s_ConfirmDelete)
	{
		MainView.HSplitTop(8.0f, nullptr, &MainView);
		MainView.HSplitTop(24.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Удалить сохранённый макрос? Это действие нельзя отменить."), 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(26.0f, &Row, &MainView);
		Row.VSplitLeft(110.0f, &Button, &Row);
		if(DoButton_Menu(&s_Confirm, Localize("Да, удалить"), 0, &Button))
		{
			if(!s_MacroName.IsEmpty() && !GameClient()->m_Controls.DeleteMacro(s_MacroName.GetString()))
				GameClient()->Echo(Localize("Не удалось удалить макрос. Проверьте его название."));
			else
				GameClient()->Echo(Localize("Макрос удалён"));
			s_ConfirmDelete = false;
		}
		Row.VSplitLeft(8.0f, nullptr, &Row);
		Row.VSplitLeft(110.0f, &Button, &Row);
		if(DoButton_Menu(&s_Cancel, Localize("Отмена"), 0, &Button))
			s_ConfirmDelete = false;
	}
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.HSplitTop(28.0f, &Row, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_ClMacroRewindStep, &g_Config.m_ClMacroRewindStep, &Row,
		Localize("Шаг перемотки (тики)"), 1, 18000);

	char aStatus[192];
	str_format(aStatus, sizeof(aStatus), "%s | %s | %s | tick %d / %d | seek %d | speed %d tick/s",
		GameClient()->m_Controls.MacroPracticeActive() ? Localize("Тренировка включена") : Localize("Тренировка выключена"),
		GameClient()->m_Controls.MacroRecording() ? Localize("Запись") : Localize("Нет записи"),
		GameClient()->m_Controls.MacroPlaying() ? (GameClient()->m_Controls.MacroPracticeWaiting() ? Localize("Ожидание координаты") : Localize("Воспроизведение")) : Localize("Не воспроизводится"),
		GameClient()->m_Controls.MacroPosition(),
		GameClient()->m_Controls.MacroFrameCount(),
		g_Config.m_ClMacroRewindStep,
		g_Config.m_ClMacroPracticeRate);
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	Ui()->DoLabel(&MainView, aStatus, 14.0f, TEXTALIGN_ML);
	MainView.HSplitTop(20.0f, nullptr, &MainView);
	Ui()->DoLabel(&MainView, GameClient()->m_Controls.MacroPracticeWaiting() ?
		Localize("Воспроизведение ждёт, пока тренировочный тии дойдёт до следующих записанных координат.") :
		Localize("Макрос сверяет координаты мира для каждого тика и не выполняет действия просто по таймеру."), 12.0f, TEXTALIGN_ML);
	MainView.HSplitTop(18.0f, nullptr, &MainView);
	Ui()->DoLabel(&MainView, Localize("Клавиша «<» — назад, «>» — вперёд. Стрелка вверх удваивает шаг, вниз уменьшает его вдвое. Скорость тренировки задаётся отдельно: 1 — один тик в секунду, 50 — обычная скорость. Серверный тии стоит на месте, двигается только тренировочный."), 12.0f, TEXTALIGN_ML);
	MainView.HSplitTop(18.0f, nullptr, &MainView);
	Ui()->DoLabel(&MainView, Localize("Макросы хранятся в папке macros внутри папки сохранений клиента (*.dmacro)."), 12.0f, TEXTALIGN_ML);
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
		{ "Авторазморозка лазером", "toggle cl_auto_unfreeze_laser 0 1" },
		{ "Автохук", "toggle cl_auto_hook 0 1" },
		{ "Защита от фриза", "toggle cl_auto_avoid 0 1" },
		{ "Наведение на тии (курсор двигается)", "toggle cl_auto_hook_aim_at_tee 0 1" },
		{ "Бэкхук (курсор неподвижен)", "toggle cl_auto_hook_backhook 0 1" },
		{ "Точная наводка на край", "toggle cl_auto_edge 0 1" },
		{ "Подхват крюком у края", "toggle cl_auto_edge_hook 0 1" },
		{ "Полёт дамми (хаммерфлай)", "toggle cl_dummy_hammer 0 1" },
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
	Ui()->DoLabel(&MainView, Localize("Нажмите на поле клавиши и выберите кнопку. Значок корзины очищает назначение. Нажатая клавиша включает или выключает соответствующую функцию."), 12.0f, TEXTALIGN_ML);
}
