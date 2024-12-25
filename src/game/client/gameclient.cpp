/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <chrono>
#include <limits>

#include <engine/client/checksum.h>
#include <engine/client/enums.h>
#include <engine/demo.h>
#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <game/generated/client_data.h>
#include <game/generated/client_data7.h>
#include <game/generated/protocol.h>

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include "gameclient.h"
#include "lineinput.h"
#include "race.h"
#include "render.h"

#include <game/localization.h>
#include <game/mapitems.h>
#include <game/version.h>

#include <game/generated/protocol7.h>
#include <game/generated/protocolglue.h>

#include "components/background.h"
#include "components/binds.h"
#include "components/broadcast.h"
#include "components/camera.h"
#include "components/chat.h"
#include "components/console.h"
#include "components/controls.h"
#include "components/countryflags.h"
#include "components/damageind.h"
#include "components/debughud.h"
#include "components/effects.h"
#include "components/emoticon.h"
#include "components/freezebars.h"
#include "components/ghost.h"
#include "components/hud.h"
#include "components/infomessages.h"
#include "components/items.h"
#include "components/mapimages.h"
#include "components/maplayers.h"
#include "components/mapsounds.h"
#include "components/menu_background.h"
#include "components/menus.h"
#include "components/motd.h"
#include "components/nameplates.h"
#include "components/particles.h"
#include "components/players.h"
#include "components/race_demo.h"
#include "components/scoreboard.h"
#include "components/skins.h"
#include "components/skins7.h"
#include "components/sounds.h"
#include "components/spectator.h"
#include "components/statboard.h"
#include "components/voting.h"
#include "prediction/entities/character.h"
#include "prediction/entities/projectile.h"

using namespace std::chrono_literals;

const char *CGameClient::Version() const { return GAME_VERSION; }
const char *CGameClient::NetVersion() const { return GAME_NETVERSION; }
const char *CGameClient::NetVersion7() const { return GAME_NETVERSION7; }
int CGameClient::DDNetVersion() const { return DDNET_VERSION_NUMBER; }
const char *CGameClient::DDNetVersionStr() const { return m_aDDNetVersionStr; }
int CGameClient::ClientVersion7() const { return CLIENT_VERSION7; }
const char *CGameClient::GetItemName(int Type) const { return m_NetObjHandler.GetObjName(Type); }

void CGameClient::OnConsoleInit()
{
    m_pEngine = Kernel()->RequestInterface<IEngine>();
    m_pClient = Kernel()->RequestInterface<IClient>();
    m_pTextRender = Kernel()->RequestInterface<ITextRender>();
    m_pSound = Kernel()->RequestInterface<ISound>();
    m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
    m_pConfig = m_pConfigManager->Values();
    m_pInput = Kernel()->RequestInterface<IInput>();
    m_pConsole = Kernel()->RequestInterface<IConsole>();
    m_pStorage = Kernel()->RequestInterface<IStorage>();
    m_pDemoPlayer = Kernel()->RequestInterface<IDemoPlayer>();
    m_pServerBrowser = Kernel()->RequestInterface<IServerBrowser>();
    m_pEditor = Kernel()->RequestInterface<IEditor>();
    m_pFavorites = Kernel()->RequestInterface<IFavorites>();
    m_pFriends = Kernel()->RequestInterface<IFriends>();
    m_pFoes = Client()->Foes();
#if defined(CONF_AUTOUPDATE)
    m_pUpdater = Kernel()->RequestInterface<IUpdater>();
#endif
    m_pHttp = Kernel()->RequestInterface<IHttp>();

    // make a list of all the systems, make sure to add them in the correct render order
    m_vpAll.insert(m_vpAll.end(), {&m_Skins,
                          &m_Skins7,
                          &m_CountryFlags,
                          &m_MapImages,
                          &m_Effects, // doesn't render anything, just updates effects
                          &m_Binds,
                          &m_Binds.m_SpecialBinds,
                          &m_Controls,
                          &m_Camera,
                          &m_Sounds,
                          &m_Voting,
                          &m_Particles, // doesn't render anything, just updates all the particles
                          &m_RaceDemo,
                          &m_MapSounds,
                          &m_Background, // render instead of m_MapLayersBackground when g_Config.m_ClOverlayEntities == 100
                          &m_MapLayersBackground, // first to render
                          &m_Particles.m_RenderTrail,
                          &m_Particles.m_RenderTrailExtra,
                          &m_Items,
                          &m_Ghost,
                          &m_Players,
                          &m_MapLayersForeground,
                          &m_Particles.m_RenderExplosions,
                          &m_NamePlates,
                          &m_Particles.m_RenderExtra,
                          &m_Particles.m_RenderGeneral,
                          &m_FreezeBars,
                          &m_DamageInd,
                          &m_Hud,
                          &m_Spectator,
                          &m_Emoticon,
                          &m_InfoMessages,
                          &m_Chat,
                          &m_Broadcast,
                          &m_DebugHud,
                          &m_TouchControls,
                          &m_Scoreboard,
                          &m_Statboard,
                          &m_Motd,
                          &m_Menus,
                          &m_Tooltips,
                          &CMenus::m_Binder,
                          &m_GameConsole,
                          &m_MenuBackground});

    // build the input stack
    m_vpInput.insert(m_vpInput.end(), {&CMenus::m_Binder, // this will take over all input when we want to bind a key
                      &m_Binds.m_SpecialBinds,
                      &m_GameConsole,
                      &m_Chat, // chat has higher prio, due to that you can quit it by pressing esc
                      &m_Motd, // for pressing esc to remove it
                      &m_Spectator,
                      &m_Emoticon,
                      &m_Menus,
                      &m_Controls,
                      &m_TouchControls,
                      &m_Binds});

    // add basic console commands
    Console()->Register("team", "i[team-id]", CFGFLAG_CLIENT, ConTeam, this, "Switch team");
    Console()->Register("kill", "", CFGFLAG_CLIENT, ConKill, this, "Kill yourself to restart");
    Console()->Register("ready_change", "", CFGFLAG_CLIENT, ConReadyChange7, this, "Change ready state (0.7 only)");

    // register tune zone command to allow the client prediction to load tunezones from the map
    Console()->Register("tune_zone", "i[zone] s[tuning] f[value]", CFGFLAG_GAME, ConTuneZone, this, "Tune in zone a variable to value");

    for(auto &pComponent : m_vpAll)
        pComponent->m_pClient = this;

    // let all the other components register their console commands
    for(auto &pComponent : m_vpAll)
        pComponent->OnConsoleInit();

    Console()->Chain("cl_languagefile", ConchainLanguageUpdate, this);

    Console()->Chain("player_name", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_clan", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_country", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_use_custom_color", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_color_body", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_color_feet", ConchainSpecialInfoupdate, this);
    Console()->Chain("player_skin", ConchainSpecialInfoupdate, this);

    Console()->Chain("player7_skin", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_body", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_marking", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_decoration", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_hands", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_feet", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_skin_eyes", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_body", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_marking", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_decoration", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_hands", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_feet", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_color_eyes", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_body", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_marking", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_decoration", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_hands", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_feet", ConchainSpecialInfoupdate, this);
    Console()->Chain("player7_use_custom_color_eyes", ConchainSpecialInfoupdate, this);

    Console()->Chain("dummy_name", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_clan", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_country", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_use_custom_color", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_color_body", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_color_feet", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy_skin", ConchainSpecialDummyInfoupdate, this);

    Console()->Chain("dummy7_skin", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_body", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_marking", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_decoration", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_hands", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_feet", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_skin_eyes", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_body", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_marking", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_decoration", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_hands", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_feet", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_color_eyes", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_body", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_marking", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_decoration", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_hands", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_feet", ConchainSpecialDummyInfoupdate, this);
    Console()->Chain("dummy7_use_custom_color_eyes", ConchainSpecialDummyInfoupdate, this);

    Console()->Chain("cl_skin_download_url", ConchainRefreshSkins, this);
    Console()->Chain("cl_skin_community_download_url", ConchainRefreshSkins, this);
    Console()->Chain("cl_download_skins", ConchainRefreshSkins, this);
    Console()->Chain("cl_download_community_skins", ConchainRefreshSkins, this);
    Console()->Chain("cl_vanilla_skins_only", ConchainRefreshSkins, this);

    Console()->Chain("cl_dummy", ConchainSpecialDummy, this);

    Console()->Chain("cl_menu_map", ConchainMenuMap, this);
}

static void GenerateTimeoutCode(char *pTimeoutCode)
{
    if(pTimeoutCode[0] == '\0' || str_comp(pTimeoutCode, "hGuEYnfxicsXGwFq") == 0)
    {
        for(unsigned int i = 0; i < 16; i++)
        {
            if(rand() % 2)
                pTimeoutCode[i] = (char)((rand() % ('z' - 'a' + 1)) + 'a');
            else
                pTimeoutCode[i] = (char)((rand() % ('Z' - 'A' + 1)) + 'A');
        }
    }
}

void CGameClient::InitializeLanguage()
{
    // set the language
    g_Localization.LoadIndexfile(Storage(), Console());
    if(g_Config.m_ClShowWelcome)
        g_Localization.SelectDefaultLanguage(Console(), g_Config.m_ClLanguagefile, sizeof(g_Config.m_ClLanguagefile));
    g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());
}

void CGameClient::OnInit()
{
    const int64_t OnInitStart = time_get();

    Client()->SetLoadingCallback([this](IClient::ELoadingCallbackDetail Detail) {
        const char *pTitle;
        if(Detail == IClient::LOADING_CALLBACK_DETAIL_DEMO || DemoPlayer()->IsPlaying())
        {
            pTitle = Localize("Preparing demo playback");
        }
        else
        {
            pTitle = Localize("Connected");
        }

        const char *pMessage;
        switch(Detail)
        {
        case IClient::LOADING_CALLBACK_DETAIL_MAP:
            pMessage = Localize("Loading map file from storage");
            break;
        case IClient::LOADING_CALLBACK_DETAIL_DEMO:
            pMessage = Localize("Loading demo file from storage");
            break;
        default:
            dbg_assert(false, "Invalid callback loading detail");
            dbg_break();
        }
        m_Menus.RenderLoading(pTitle, pMessage, 0);
    });

    m_pGraphics = Kernel()->RequestInterface<IGraphics>();

    // propagate pointers
    m_UI.Init(Kernel());
    m_RenderTools.Init(Graphics(), TextRender());

    if(GIT_SHORTREV_HASH)
    {
        str_format(m_aDDNetVersionStr, sizeof(m_aDDNetVersionStr), "%s %s (%s)", GAME_NAME, GAME_RELEASE_VERSION, GIT_SHORTREV_HASH);
    }
    else
    {
        str_format(m_aDDNetVersionStr, sizeof(m_aDDNetVersionStr), "%s %s", GAME_NAME, GAME_RELEASE_VERSION);
    }

    // TODO: this should be different
    // setup item sizes
    for(int i = 0; i < NUM_NETOBJTYPES; i++)
        Client()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
    // HACK: only set static size for items, which were available in the first 0.7 release
    // so new items don't break the snapshot delta
    static const int OLD_NUM_NETOBJTYPES = 23;
    for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
        Client()->SnapSetStaticsize7(i, m_NetObjHandler7.GetObjSize(i));

    if(!TextRender()->LoadFonts())
    {
        Client()->AddWarning(SWarning(Localize("Some fonts could not be loaded. Check the local console for details.")));
    }
    TextRender()->SetFontLanguageVariant(g_Config.m_ClLanguagefile);

    // update and swap after font loading, they are quite huge
    Client()->UpdateAndSwap();

    const char *pLoadingDDNetCaption = Localize("Loading DDNet Client");
    const char *pLoadingMessageComponents = Localize("Initializing components");
    const char *pLoadingMessageComponentsSpecial = Localize("Why are you slowmo replaying to read this?");
    char aLoadingMessage[256];

    // init all components
    int SkippedComps = 1;
    int CompCounter = 1;
    const int NumComponents = ComponentCount();
    for(int i = NumComponents - 1; i >= 0; --i)
    {
        m_vpAll[i]->OnInit();
        // try to render a frame after each component, also flushes GPU uploads
        if(m_Menus.IsInit())
        {
            str_format(aLoadingMessage, std::size(aLoadingMessage), "%s [%d/%d]", CompCounter == NumComponents ? pLoadingMessageComponentsSpecial : pLoadingMessageComponents, CompCounter, NumComponents);
            m_Menus.RenderLoading(pLoadingDDNetCaption, aLoadingMessage, SkippedComps);
            SkippedComps = 1;
        }
        else
        {
            ++SkippedComps;
        }
        ++CompCounter;
    }

    m_GameSkinLoaded = false;
    m_ParticlesSkinLoaded = false;
    m_EmoticonsSkinLoaded = false;
    m_HudSkinLoaded = false;

    // setup load amount, load textures
    const char *pLoadingMessageAssets = Localize("Initializing assets");
    for(int i = 0; i < g_pData->m_NumImages; i++)
    {
        if(i == IMAGE_GAME)
            LoadGameSkin(g_Config.m_ClAssetGame);
        else if(i == IMAGE_EMOTICONS)
            LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
        else if(i == IMAGE_PARTICLES)
            LoadParticlesSkin(g_Config.m_ClAssetParticles);
        else if(i == IMAGE_HUD)
            LoadHudSkin(g_Config.m_ClAssetHud);
        else if(i == IMAGE_EXTRAS)
            LoadExtrasSkin(g_Config.m_ClAssetExtras);
        else if(g_pData->m_aImages[i].m_pFilename[0] == '\0') // handle special null image without filename
            g_pData->m_aImages[i].m_Id = IGraphics::CTextureHandle();
        else
            g_pData->m_aImages[i].m_Id = Graphics()->LoadTexture(g_pData->m_aImages[i].m_pFilename, IStorage::TYPE_ALL);
        m_Menus.RenderLoading(pLoadingDDNetCaption, pLoadingMessageAssets, 1);
    }
    for(int i = 0; i < client_data7::g_pData->m_NumImages; i++)
    {
        if(client_data7::g_pData->m_aImages[i].m_pFilename[0] == '\0') // handle special null image without filename
            client_data7::g_pData->m_aImages[i].m_Id = IGraphics::CTextureHandle();
        else if(i == client_data7::IMAGE_DEADTEE)
            client_data7::g_pData->m_aImages[i].m_Id = Graphics()->LoadTexture(client_data7::g_pData->m_aImages[i].m_pFilename, IStorage::TYPE_ALL, 0);
        m_Menus.RenderLoading(pLoadingDDNetCaption, Localize("Initializing assets"), 1);
    }

    m_GameWorld.m_pCollision = Collision();
    m_GameWorld.m_pTuningList = m_aTuningList;
    OnReset();

    // Set free binds to DDRace binds if it's active
    m_Binds.SetDDRaceBinds(true);

    GenerateTimeoutCode(g_Config.m_ClTimeoutCode);
    GenerateTimeoutCode(g_Config.m_ClDummyTimeoutCode);

    // Aggressively try to grab window again since some Windows users report
    // window not being focused after starting client.
    Graphics()->SetWindowGrab(true);

    CChecksumData *pChecksum = Client()->ChecksumData();
    pChecksum->m_SizeofGameClient = sizeof(*this);
    pChecksum->m_NumComponents = m_vpAll.size();
    for(size_t i = 0; i < m_vpAll.size(); i++)
    {
        if(i >= std::size(pChecksum->m_aComponentsChecksum))
        {
            break;
        }
        int Size = m_vpAll[i]->Sizeof();
        pChecksum->m_aComponentsChecksum[i] = Size;
    }

    m_Menus.FinishLoading();
    log_trace("gameclient", "initialization finished after %.2fms", (time_get() - OnInitStart) * 1000.0f / (float)time_freq());
}

void CGameClient::OnUpdate()
{
    HandleLanguageChanged();

    CUIElementBase::Init(Ui()); // update static pointer because game and editor use separate UI

    // handle mouse movement
    float x = 0.0f, y = 0.0f;
    IInput::ECursorType CursorType = Input()->CursorRelative(&x, &y);
    if(CursorType != IInput::CURSOR_NONE)
    {
        for(auto &pComponent : m_vpInput)
        {
            if(pComponent->OnCursorMove(x, y, CursorType))
                break;
        }
    }

    // handle touch events
    const std::vector<IInput::CTouchFingerState> &vTouchFingerStates = Input()->TouchFingerStates();
    bool TouchHandled = false;
    for(auto &pComponent : m_vpInput)
    {
        if(TouchHandled)
        {
            // Also update inactive components so they can handle touch fingers being released.
            pComponent->OnTouchState({});
        }
        else if(pComponent->OnTouchState(vTouchFingerStates))
        {
            Input()->ClearTouchDeltas();
            TouchHandled = true;
        }
    }

    // handle key presses
    Input()->ConsumeEvents([&](const IInput::CEvent &Event) {
        for(auto &pComponent : m_vpInput)
        {
            // Events with flag `FLAG_RELEASE` must always be forwarded to all components so keys being
            // released can be handled in all components also after some components have been disabled.
            if(pComponent->OnInput(Event) && (Event.m_Flags & ~IInput::FLAG_RELEASE) != 0)
                break;
        }
    });

    if(g_Config.m_ClSubTickAiming && m_Binds.m_MouseOnAction)
    {
        m_Controls.m_aMousePosOnAction[g_Config.m_ClDummy] = m_Controls.m_aMousePos[g_Config.m_ClDummy];
        m_Binds.m_MouseOnAction = false;
    }
}

void CGameClient::OnDummySwap()
{
    if(g_Config.m_ClDummyResetOnSwitch)
    {
        int PlayerOrDummy = (g_Config.m_ClDummyResetOnSwitch == 2) ? g_Config.m_ClDummy : (!g_Config.m_ClDummy);
        m_Controls.ResetInput(PlayerOrDummy);
        m_Controls.m_aInputData[PlayerOrDummy].m_Hook = 0;
    }
    int tmp = m_DummyInput.m_Fire;
    m_DummyInput = m_Controls.m_aInputData[!g_Config.m_ClDummy];
    m_Controls.m_aInputData[g_Config.m_ClDummy].m_Fire = tmp;
    m_IsDummySwapping = 1;
}

int CGameClient::OnSnapInput(int *pData, bool Dummy, bool Force)
{
    if(!Dummy)
    {
        return m_Controls.SnapInput(pData);
    }

    if(!g_Config.m_ClDummyHammer)
    {
        if(m_DummyFire != 0)
        {
            m_DummyInput.m_Fire = (m_HammerInput.m_Fire + 1) & ~1;
            m_DummyFire = 0;
        }

        if(!Force && (!m_DummyInput.m_Direction && !m_DummyInput.m_Jump && !m_DummyInput.m_Hook))
        {
            return 0;
        }

        mem_copy(pData, &m_DummyInput, sizeof(m_DummyInput));
        return sizeof(m_DummyInput);
    }
    else
    {
        if(m_DummyFire % 25 != 0)
        {
            m_DummyFire++;
            return 0;
        }
        m_DummyFire++;

        m_HammerInput.m_Fire = (m_HammerInput.m_Fire + 1) | 1;
        m_HammerInput.m_WantedWeapon = WEAPON_HAMMER + 1;
        if(!g_Config.m_ClDummyRestoreWeapon)
        {
            m_DummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;
        }

        vec2 MainPos = m_LocalCharacterPos;
        vec2 DummyPos = m_aClients[m_aLocalIds[!g_Config.m_ClDummy]].m_Predicted.m_Pos;
        vec2 Dir = MainPos - DummyPos;
        m_HammerInput.m_TargetX = (int)(Dir.x);
        m_HammerInput.m_TargetY = (int)(Dir.y);

        mem_copy(pData, &m_HammerInput, sizeof(m_HammerInput));
        return sizeof(m_HammerInput);
    }
}

void CGameClient::OnConnected()
{
    const char *pConnectCaption = DemoPlayer()->IsPlaying() ? Localize("Preparing demo playback") : Localize("Connected");
    const char *pLoadMapContent = Localize("Initializing map logic");
    // render loading before skip is calculated
    m_Menus.RenderLoading(pConnectCaption, pLoadMapContent, 0);
    m_Layers.Init(Kernel()->RequestInterface<IMap>(), false);
    m_Collision.Init(Layers());
    m_GameWorld.m_Core.InitSwitchers(m_Collision.m_HighestSwitchNumber);
    m_RaceHelper.Init(this);

    // render loading before going through all components
    m_Menus.RenderLoading(pConnectCaption, pLoadMapContent, 0);
    for(auto &pComponent : m_vpAll)
    {
        pComponent->OnMapLoad();
        pComponent->OnReset();
    }

    Client()->SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_GETTING_READY);
    m_Menus.RenderLoading(pConnectCaption, Localize("Sending initial client info"), 0);

    // send the initial info
    SendInfo(true);
    // we should keep this in for now, because otherwise you can't spectate
    // people at start as the other info 64 packet is only sent after the first
    // snap
    Client()->Rcon("crashmeplx");

    ConfigManager()->ResetGameSettings();
    LoadMapSettings();

    if(Client()->State() != IClient::STATE_DEMOPLAYBACK && g_Config.m_ClAutoDemoOnConnect)
        Client()->DemoRecorder_HandleAutoStart();
}

void CGameClient::OnReset()
{
    InvalidateSnapshot();

    m_EditorMovementDelay = 5;

    m_PredictedTick = -1;
    std::fill(std::begin(m_aLastNewPredictedTick), std::end(m_aLastNewPredictedTick), -1);

    m_LastRoundStartTick = -1;
    m_LastRaceTick = -1;
    m_LastFlagCarrierRed = -4;
    m_LastFlagCarrierBlue = -4;

    std::fill(std::begin(m_aCheckInfo), std::end(m_aCheckInfo), -1);

    // m_aDDNetVersionStr is initialized once in OnInit

    std::fill(std::begin(m_aLastPos), std::end(m_aLastPos), vec2(0.0f, 0.0f));
    std::fill(std::begin(m_aLastActive), std::end(m_aLastActive), false);

    m_GameOver = false;
    m_GamePaused = false;
    m_PrevLocalId = -1;

    m_SuppressEvents = false;
    m_NewTick = false;
    m_NewPredictedTick = false;

    m_aFlagDropTick[TEAM_RED] = 0;
    m_aFlagDropTick[TEAM_BLUE] = 0;

    m_ServerMode = SERVERMODE_PURE;
    mem_zero(&m_GameInfo, sizeof(m_GameInfo));

    m_DemoSpecId = SPEC_FOLLOW;
    m_LocalCharacterPos = vec2(0.0f, 0.0f);

    m_PredictedPrevChar.Reset();
    m_PredictedChar.Reset();

    // m_Snap was cleared in InvalidateSnapshot

    std::fill(std::begin(m_aLocalTuneZone), std::end(m_aLocalTuneZone), -1);
    std::fill(std::begin(m_aReceivedTuning), std::end(m_aReceivedTuning), false);
    std::fill(std::begin(m_aExpectingTuningForZone), std::end(m_aExpectingTuningForZone), -1);
    std::fill(std::begin(m_aExpectingTuningSince), std::end(m_aExpectingTuningSince), 0);
    std::fill(std::begin(m_aTuning), std::end(m_aTuning), CTuningParams());

    for(auto &Client : m_aClients)
        Client.Reset();

    for(auto &Stats : m_aStats)
        Stats.Reset();

    m_NextChangeInfo = 0;
    std::fill(std::begin(m_aLocalIds), std::end(m_aLocalIds), -1);
    m_DummyInput = {};
    m_HammerInput = {};
    m_DummyFire = 0;
    m_ReceivedDDNetPlayer = false;

    m_Teams.Reset();
    m_GameWorld.Clear();
    m_GameWorld.m_WorldConfig.m_InfiniteAmmo = true;
    m_PredictedWorld.CopyWorld(&m_GameWorld);
    m_PrevPredictedWorld.CopyWorld(&m_PredictedWorld);

    m_vSnapEntities.clear();

    std::fill(std::begin(m_aDDRaceMsgSent), std::end(m_aDDRaceMsgSent), false);
    std::fill(std::begin(m_aShowOthers), std::end(m_aShowOthers), SHOW_OTHERS_NOT_SET);
    std::fill(std::begin(m_aLastUpdateTick), std::end(m_aLastUpdateTick), 0);

    m_PredictedDummyId = -1;
    m_IsDummySwapping = false;
    m_CharOrder.Reset();
    std::fill(std::begin(m_aSwitchStateTeam), std::end(m_aSwitchStateTeam), -1);

    // m_aTuningList is reset in LoadMapSettings

    m_LastShowDistanceZoom = 0.0f;
    m_LastZoom = 0.0f;
    m_LastScreenAspect = 0.0f;
    m_LastDeadzone = 0.0f;
    m_LastFollowFactor = 0.0f;
    m_LastDummyConnected = false;

    m_MultiViewPersonalZoom = 0.0f;
    m_MultiViewActivated = false;
    m_MultiView.m_IsInit = false;

    m_CursorInfo.m_CursorOwnerId = -1;
    m_CursorInfo.m_NumSamples = 0;

    for(auto &pComponent : m_vpAll)
        pComponent->OnReset();

    Editor()->ResetMentions();
    Editor()->ResetIngameMoved();

    Collision()->Unload();
    Layers()->Unload();
}

void CGameClient::UpdatePositions()
{
    // local character position
    if(g_Config.m_ClPredict && Client()->State() != IClient::STATE_DEMOPLAYBACK)
    {
        if(!AntiPingPlayers())
        {
            if(!m_Snap.m_pLocalCharacter || (m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
            {
                // don't use predicted
            }
            else
                m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
        }
        else
        {
            if(!(m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
            {
                if(m_Snap.m_pLocalCharacter)
                    m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
            }
            //      else
            //          m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
        }
    }
    else if(m_Snap.m_pLocalCharacter && m_Snap.m_pLocalPrevCharacter)
    {
        m_LocalCharacterPos = mix(
            vec2(m_Snap.m_pLocalPrevCharacter->m_X, m_Snap.m_pLocalPrevCharacter->m_Y),
            vec2(m_Snap.m_pLocalCharacter->m_X, m_Snap.m_pLocalCharacter->m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
    }

    // spectator position
    if(m_Snap.m_SpecInfo.m_Active)
    {
        if(m_MultiViewActivated)
        {
            HandleMultiView();
        }
        else if(Client()->State() == IClient::STATE_DEMOPLAYBACK && m_DemoSpecId != SPEC_FOLLOW && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
        {
            m_Snap.m_SpecInfo.m_Position = mix(
                vec2(m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Prev.m_X, m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Prev.m_Y),
                vec2(m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Cur.m_X, m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Cur.m_Y),
                Client()->IntraGameTick(g_Config.m_ClDummy));
            m_Snap.m_SpecInfo.m_UsePosition = true;
        }
        else if(m_Snap.m_pSpectatorInfo && ((Client()->State() == IClient::STATE_DEMOPLAYBACK && m_DemoSpecId == SPEC_FOLLOW) || (Client()->State() != IClient::STATE_DEMOPLAYBACK && m_Snap.m_SpecInfo.m_Spec[...]
        {
            if(m_Snap.m_pPrevSpectatorInfo && m_Snap.m_pPrevSpectatorInfo->m_SpectatorId == m_Snap.m_pSpectatorInfo->m_SpectatorId)
                m_Snap.m_SpecInfo.m_Position = mix(vec2(m_Snap.m_pPrevSpectatorInfo->m_X, m_Snap.m_pPrevSpectatorInfo->m_Y),
                    vec2(m_Snap.m_pSpectatorInfo->m_X, m_Snap.m_pSpectatorInfo->m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
            else
                m_Snap.m_SpecInfo.m_Position = vec2(m_Snap.m_pSpectatorInfo->m_X, m_Snap.m_pSpectatorInfo->m_Y);
            m_Snap.m_SpecInfo.m_UsePosition = true;
        }
    }

    if(!m_MultiViewActivated && m_MultiView.m_IsInit)
        ResetMultiView();

    UpdateRenderedCharacters();
}

void CGameClient::OnRender()
{
    // check if multi view got activated
    if(!m_MultiView.m_IsInit && m_MultiViewActivated)
    {
        int TeamId = 0;
        if(m_Snap.m_SpecInfo.m_SpectatorId >= 0)
            TeamId = m_Teams.Team(m_Snap.m_SpecInfo.m_SpectatorId);

        if(TeamId > MAX_CLIENTS || TeamId < 0)
            TeamId = 0;

        if(!InitMultiView(TeamId))
        {
            dbg_msg("MultiView", "No players found to spectate");
            ResetMultiView();
        }
    }

    // update the local character and spectate position
    UpdatePositions();

    // display warnings
    if(m_Menus.CanDisplayWarning())
    {
        std::optional<SWarning> Warning = Graphics()->CurrentWarning();
        if(!Warning.has_value())
        {
            Warning = Client()->CurrentWarning();
        }
        if(Warning.has_value())
        {
            const SWarning TheWarning = Warning.value();
            m_Menus.PopupWarning(TheWarning.m_aWarningTitle[0] == '\0' ? Localize("Warning") : TheWarning.m_aWarningTitle, TheWarning.m_aWarningMsg, Localize("Ok"), TheWarning.m_AutoHide ? 10s : 0s);
        }
    }

    // update camera data prior to CControls::OnRender to allow CControls::m_aTargetPos to compensate using camera data
    m_Camera.UpdateCamera();

    UpdateSpectatorCursor();

    // render all systems
    for(auto &pComponent : m_vpAll)
        pComponent->OnRender();

    // clear all events/input for this frame
    Input()->Clear();

    CLineInput::RenderCandidates();

    const bool WasNewTick = m_NewTick;

    // clear new tick flags
    m_NewTick = false;
    m_NewPredictedTick = false;

    if(g_Config.m_ClDummy && !Client()->DummyConnected())
        g_Config.m_ClDummy = 0;

    // resend player and dummy info if it was filtered by server
    if(Client()->State() == IClient::STATE_ONLINE && !m_Menus.IsActive() && WasNewTick)
    {
        if(m_aCheckInfo[0] == 0)
        {
            if(m_pClient->IsSixup())
            {
                if(!GotWantedSkin7(false))
                    SendSkinChange7(false);
                else
                    m_aCheckInfo[0] = -1;
            }
            else
            {
                if(
                    str_comp(m_aClients[m_aLocalIds[0]].m_aName, Client()->PlayerName()) ||
                    str_comp(m_aClients[m_aLocalIds[0]].m_aClan, g_Config.m_PlayerClan) ||
                    m_aClients[m_aLocalIds[0]].m_Country != g_Config.m_PlayerCountry ||
                    str_comp(m_aClients[m_aLocalIds[0]].m_aSkinName, g_Config.m_ClPlayerSkin) ||
                    m_aClients[m_aLocalIds[0]].m_UseCustomColor != g_Config.m_ClPlayerUseCustomColor ||
                    m_aClients[m_aLocalIds[0]].m_ColorBody != (int)g_Config.m_ClPlayerColorBody ||
                    m_aClients[m_aLocalIds[0]].m_ColorFeet != (int)g_Config.m_ClPlayerColorFeet)
                    SendInfo(false);
                else
                    m_aCheckInfo[0] = -1;
            }
        }

        if(m_aCheckInfo[0] > 0)
        {
            m_aCheckInfo[0] -= minimum(Client()->GameTick(0) - Client()->PrevGameTick(0), m_aCheckInfo[0]);
        }

        if(Client()->DummyConnected())
        {
            if(m_aCheckInfo[1] == 0)
            {
                if(m_pClient->IsSixup())
                {
                    if(!GotWantedSkin7(true))
                        SendSkinChange7(true);
                    else
                        m_aCheckInfo[1] = -1;
                }
                else
                {
                    if(
                        str_comp(m_aClients[m_aLocalIds[1]].m_aName, Client()->DummyName()) ||
                        str_comp(m_aClients[m_aLocalIds[1]].m_aClan, g_Config.m_ClDummyClan) ||
                        m_aClients[m_aLocalIds[1]].m_Country != g_Config.m_ClDummyCountry ||
                        str_comp(m_aClients[m_aLocalIds[1]].m_aSkinName, g_Config.m_ClDummySkin) ||
                        m_aClients[m_aLocalIds[1]].m_UseCustomColor != g_Config.m_ClDummyUseCustomColor ||
                        m_aClients[m_aLocalIds[1]].m_ColorBody != (int)g_Config.m_ClDummyColorBody ||
                        m_aClients[m_aLocalIds[1]].m_ColorFeet != (int)g_Config.m_ClDummyColorFeet)
                        SendDummyInfo(false);
                    else
                        m_aCheckInfo[1] = -1;
                }
            }

            if(m_aCheckInfo[1] > 0)
            {
                m_aCheckInfo[1] -= minimum(Client()->GameTick(1) - Client()->PrevGameTick(1), m_aCheckInfo[1]);
            }
        }
    }
}

// Добавление кода функции avoid freeze

#include <cmath>

// Структура для хранения позиции игрока
struct Position {
    float x, y;
};

// Глобальные переменные для функции Avoid Freeze
bool g_AvoidFreezeEnabled = false;

// Включение/выключение функции Avoid Freeze
void ToggleAvoidFreeze() {
    g_AvoidFreezeEnabled = !g_AvoidFreezeEnabled;
    if(g_AvoidFreezeEnabled) {
        dbg_msg("gameclient", "Avoid Freeze Enabled");
    } else {
        dbg_msg("gameclient", "Avoid Freeze Disabled");
    }
}

// Проверка расстояния до фриз-зоны
bool IsNearFreezeZone(const Position& playerPos) {
    // Реализуйте логику для проверки расстояния до фриз-зоны
    // Верните true, если игрок слишком близко к фриз-зоне, иначе false
    return false;
}

// Корректировка позиции игрока с помощью хуков
void CorrectPositionWithHooks(Position& playerPos) {
    // Реализуйте логику для корректировки позиции игрока с помощью хуков
    // Например, измените координаты игрока, чтобы он не попал во фриз
}

void CGameClient::OnRender() {
    // Ваш существующий код...

    // Обработка нажатия клавиши F
    if(Input()->KeyPress(KEY_F)) {
        ToggleAvoidFreeze();
    }

    // Проверка и корректировка позиции игрока для Avoid Freeze
    if(g_AvoidFreezeEnabled) {
        Position playerPos = GetPlayerPosition();
        if(IsNearFreezeZone(playerPos)) {
            CorrectPositionWithHooks(playerPos);
            SetPlayerPosition(playerPos);
        }
    }

    // Ваш существующий код...
}
