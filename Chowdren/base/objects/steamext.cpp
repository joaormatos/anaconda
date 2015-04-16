#include "steamext.h"
#include <stdlib.h>
#include <iostream>
#include "fileio.h"
#include "path.h"

#ifdef CHOWDREN_IS_DESKTOP
#include <SDL.h>
#endif

// SteamGlobal

#ifdef CHOWDREN_ENABLE_STEAM
#include "steam/steam_api.h"
#include "steam/steamtypes.h"

class SteamGlobal
{
public:
    bool initialized;
    bool has_data;

    SteamGlobal();
    static void on_close();
    bool is_ready();
    void init();

    STEAM_CALLBACK(SteamGlobal, receive_callback, UserStatsReceived_t,
                   receive_callback_data);
};

static SteamGlobal global_steam_obj;

#ifdef CHOWDREN_IS_FP
#include "objects/steamfp/frontend.cpp"
#endif

SteamGlobal::SteamGlobal()
: initialized(false), has_data(false),
  receive_callback_data(this, &SteamGlobal::receive_callback)
{
}

void SteamGlobal::init()
{
    initialized = SteamAPI_Init();
    if (!initialized) {
        std::cout << "Could not initialize Steam API" << std::endl;
        return;
    }
    SteamUserStats()->RequestCurrentStats();

#ifdef CHOWDREN_STEAM_APPID
    ISteamApps * ownapp = SteamApps();
    if (!ownapp->BIsSubscribedApp(CHOWDREN_STEAM_APPID)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Steam error",
                                 "Please purchase the Steam version of the "
                                 "game if you want to play it on Steam.",
                                 NULL);
        exit(0);
    }
#endif

#ifdef CHOWDREN_IS_FP
    initialize_fp();
#endif
}

void SteamGlobal::on_close()
{
    SteamAPI_Shutdown();
}

bool SteamGlobal::is_ready()
{
    return has_data;
}

void SteamGlobal::receive_callback(UserStatsReceived_t * callback)
{
    if (SteamUtils()->GetAppID() != callback->m_nGameID)
        return;
    has_data = true;
}

#endif

// SteamObject

SteamObject::SteamObject(int x, int y, int type_id)
: FrameObject(x, y, type_id)
{
#ifdef CHOWDREN_ENABLE_STEAM
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    global_steam_obj.init();
#endif
}

bool SteamObject::is_ready()
{
#ifdef CHOWDREN_ENABLE_STEAM
    return global_steam_obj.is_ready();
#else
    return true;
#endif
}

void SteamObject::update()
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return;
    SteamAPI_RunCallbacks();
#endif
}

void SteamObject::unlock_achievement(const std::string & name)
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return;
    SteamUserStats()->SetAchievement(name.c_str());
    SteamUserStats()->StoreStats();
#endif
}

void SteamObject::clear_achievement(const std::string & name)
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return;
    SteamUserStats()->ClearAchievement(name.c_str());
    SteamUserStats()->StoreStats();
#endif
}

bool SteamObject::is_achievement_unlocked(const std::string & name)
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return false;
    bool achieved;
    SteamUserStats()->GetAchievement(name.c_str(), &achieved);
    return achieved;
#else
    return false;
#endif
}

void SteamObject::upload(const std::string & name)
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return;
    std::string filename = get_path_filename(name);
    const char * filename_c = filename.c_str();
    char * data;
    size_t size;
    if (!read_file(filename_c, &data, &size))
        return;
    SteamRemoteStorage()->FileWrite(filename_c, data, size);
#endif
}

void SteamObject::download(const std::string & name)
{
#ifdef CHOWDREN_ENABLE_STEAM
    if (!global_steam_obj.initialized)
        return;
    std::string filename = get_path_filename(name);
    const char * filename_c = filename.c_str();
    if (!SteamRemoteStorage()->FileExists(filename_c))
        return;

    int32 size = SteamRemoteStorage()->GetFileSize(filename_c);
    std::string value;
    value.resize(size);
    SteamRemoteStorage()->FileRead(filename_c, &value[0], size);
    FSFile fp(name.c_str(), "w");
    if (!fp.is_open())
        return;
    fp.write(&value[0], value.size());
    fp.close();
#endif
}

#if !defined(CHOWDREN_ENABLE_STEAM) && defined(CHOWDREN_IS_FP)
void SteamObject::find_board(int char_id, int stage_id)
{
}

void SteamObject::upload_crystal(int value)
{
}

void SteamObject::upload_time(int value)
{
}
#endif