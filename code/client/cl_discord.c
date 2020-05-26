#include "client.h"

#define DISCORD_UPDATE_MSEC 20000
#define NUM_OFFICIAL_MAPS 34
#define MAX_DISCORD_BUFFER_LENGTH 256
#define APPLICATION_ID "692466407480361001"

cvar_t *discordEnabled;
cvar_t *discordAutoJoinAccept;
DiscordRichPresence discordPresence;

qboolean discordstatus = qfalse;

static const char *GAMETYPES[] = {"UNK", "LMS", "FFA", "TDM", "TS", "FTL", "CNH", "CTF", "BM", "JUMP", "FT", "GUN", NULL};

static const char *OFFICIAL_MAPS[NUM_OFFICIAL_MAPS] = {
    "ut4_abbey",
    "ut4_algiers",
    "ut4_austria",
    "ut4_bohemia",
    "ut4_casa",
    "ut4_cascade",
    "ut4_docks",
    "ut4_dressingroom",
    "ut4_eagle",
    "ut4_elgin",
    "ut4_firingrange",
    "ut4_ghosttown",
    "ut4_herring",
    "ut4_killroom",
    "ut4_kingdom",
    "ut4_kingpin",
    "ut4_mandolin",
    "ut4_mykonos_a17",
    "ut4_oildepot",
    "ut4_paris",
    "ut4_prague",
    "ut4_prominence",
    "ut4_raiders",
    "ut4_ramelle",
    "ut4_ricochet",
    "ut4_riyadh",
    "ut4_sanc",
    "ut4_suburbs",
    "ut4_subway",
    "ut4_swim",
    "ut4_thingley",
    "ut4_tombs",
    "ut4_turnpike",
    "ut4_uptown"
};


static qboolean is_official_map(char *s) {
    int i;
    for (i = 0; i < NUM_OFFICIAL_MAPS; i++) {
        if (!Q_stricmp(OFFICIAL_MAPS[i], s)) {
            return qtrue;
        }
    }

    return qfalse;
}


static void HandleDiscordReady(const DiscordUser *request) {
    Com_Printf("Discord ready\n");
}

static void HandleDiscordError(int errcode, const char *message) {
  Com_Printf("Discord error: %s\n", message);
}


static void HandleDiscordDisconnected(int errcode, const char *message) {
    Com_Printf("Discord Disconnect\n");
}

static void HandleDiscordJoinGame(const char *secret) {
  // SECURE ME ?
    Com_Printf("Discord Trying to join\n");
    Cbuf_ExecuteText( EXEC_APPEND, va( "connect %s\n", secret ) );
}

static void HandleDiscordSpectateGame(const char *secret) {
    Com_Printf("Discord Want to spec\n");
}

static void HandleDiscordJoinRequest(const DiscordUser* request) {
    int autojoin;

    Com_Printf("Discord Join Request\n");
    Com_Printf("User %s asked to join party\n", request->username);

    autojoin = discordAutoJoinAccept->integer;
    Com_Printf("status: %d", autojoin);
    if (autojoin < 0 || autojoin > 2) autojoin = 0; // ignore if autojoin is another value than 0 1 2
    Discord_Respond(request->userId, autojoin);
}

void CL_InitDiscordHandlers() {
    DiscordEventHandlers handlers;

    memset(&handlers, 0, sizeof(handlers));

    handlers.ready = HandleDiscordReady;
    handlers.errored = HandleDiscordError;
    handlers.disconnected = HandleDiscordDisconnected;
    handlers.joinGame = HandleDiscordJoinGame;
    handlers.spectateGame = HandleDiscordSpectateGame;
    handlers.joinRequest = HandleDiscordJoinRequest;


    discordstatus = qtrue;

    Discord_Initialize(APPLICATION_ID, &handlers, 1, NULL);

}

void CL_InitDiscord(){
    // enabled by default
    discordEnabled = Cvar_Get("cl_discordEnabled", "1", CVAR_ARCHIVE);
    // say yes to join requests by default
    discordAutoJoinAccept = Cvar_Get("cl_discordAutoJoinAccept", "1", CVAR_ARCHIVE);

    if (!discordEnabled->integer) {
      return ;
    }

    CL_InitDiscordHandlers();
    CL_InitDiscordPresence();
 }

void CL_InitDiscordPresence() {
    memset(&discordPresence, 0, sizeof(discordPresence));
}

void CL_ShutdownDiscord() {
  Com_Printf("Shutdown Discord\n");

  discordstatus = qfalse;

  Discord_ClearPresence();
  Discord_Shutdown();
}


void CL_RunDiscord(void) {
    static int accumulated_time = 0;

    if (!discordEnabled->integer) {
      // we switch from enabled to disabeld, remove it
      if (discordstatus == qtrue) {
	CL_ShutdownDiscord();
      }
      return ;
    }

    // discord is enabled, but status is false

    if (!discordstatus) {
      CL_InitDiscordHandlers();
    }

#ifdef DISCORD_DISABLE_IO_THREAD
    Discord_UpdateConnection();
#endif
    Discord_RunCallbacks();
/*
    accumulated_time += cls.frametime;
    if (accumulated_time >= DISCORD_UPDATE_MSEC) {
        CL_UpdatePresence();
        accumulated_time = 0;
    }
    */
}


void CL_UpdatePresence()
{
    char buffer_state[MAX_DISCORD_BUFFER_LENGTH];
    char buffer_details[MAX_DISCORD_BUFFER_LENGTH];
    char buffer_partyId[MAX_DISCORD_BUFFER_LENGTH];
    char buffer_joinSecret[MAX_DISCORD_BUFFER_LENGTH];

    memset(&buffer_state, 0, MAX_DISCORD_BUFFER_LENGTH);
    memset(&buffer_details, 0, MAX_DISCORD_BUFFER_LENGTH);
    memset(&buffer_partyId, 0, MAX_DISCORD_BUFFER_LENGTH);
    memset(&buffer_joinSecret, 0, MAX_DISCORD_BUFFER_LENGTH);

    if (!discordstatus) {
      Com_Printf("Discord is disabled, use cl_discordEnabled 1 to activate it\n");
      return;
    }

    if (cls.state == CA_ACTIVE) {
      char *serverInfo, *info, *mapname;
      int currentPlayers, maxPlayers, gameType;
      int timeStart, timeEnd, timeCurrent, now, i;

      serverInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
      info = Info_ValueForKey(serverInfo, "sv_hostname");
      Com_sprintf(buffer_state, sizeof(buffer_state), "On %s", info);
      Q_CleanStr(buffer_state);
      discordPresence.state = buffer_state;

      info = Info_ValueForKey(serverInfo, "sv_maxclients");
      maxPlayers = atoi(info);
      currentPlayers = 0;
      for (i = 0; i < MAX_CLIENTS; i++) {
	    if (cl.gameState.stringOffsets[CS_PLAYERS + i]) {
	        currentPlayers++;
	    }
      }

      discordPresence.partySize = currentPlayers;
      discordPresence.partyMax = maxPlayers;

      info = Info_ValueForKey(serverInfo, "g_gametype");
      gameType = atoi(info);

      mapname = Info_ValueForKey(serverInfo, "mapname");
      Com_sprintf(buffer_details, sizeof(buffer_details), "Playing %s on %s", GAMETYPES[gameType], mapname);
      discordPresence.details = buffer_details;


      // FIXME: if no timeEnd, the presence is fucked
      info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_LEVEL_START_TIME];
      timeStart = atoi(info) / 1000;
      timeCurrent = cl.serverTime / 1000;
      info = Info_ValueForKey(serverInfo, "timelimit");
      timeEnd = atoi(info) * 60;
      now = (unsigned)time(NULL);
      discordPresence.startTimestamp =  now - timeCurrent;

      if (timeStart) {
	discordPresence.startTimestamp += timeStart;

	if (timeEnd) {
	  discordPresence.endTimestamp = discordPresence.startTimestamp + timeEnd;
	}
      }

      if (strlen(cls.servername)) {
          Com_sprintf(buffer_joinSecret, sizeof(buffer_partyId), "%s", cls.servername);
          discordPresence.joinSecret = buffer_joinSecret;
          Com_sprintf(buffer_partyId, sizeof(buffer_partyId), "party-%s", cls.servername);
          discordPresence.partyId = buffer_partyId;
      }



    } else {
      discordPresence.state = "Menu title";
      memset(&discordPresence.joinSecret, 0, sizeof(discordPresence.joinSecret));
      memset(&discordPresence.partyId, 0, sizeof(discordPresence.partyId));
      memset(&discordPresence.partySize, 0, sizeof(discordPresence.partySize));
      memset(&discordPresence.partyMax, 0, sizeof(discordPresence.partyMax));
      discordPresence.details = NULL;
    }

    if (clc.demoplaying) {
      discordPresence.state = "Watching Demo";
      discordPresence.details = NULL;
      discordPresence.startTimestamp = 0;
      discordPresence.endTimestamp = 0;
      discordPresence.partyId = NULL;
      discordPresence.partySize = 0;
      discordPresence.partyMax = 0;
    }
    if (strlen(clc.mapname) == 0 || !is_official_map(clc.mapname)) {
      discordPresence.largeImageKey = "mbebd";
    } else {
      discordPresence.largeImageKey = clc.mapname;
      discordPresence.smallImageKey = "mbebd";
    }

    if (strlen(clc.mapname) > 0) {
      discordPresence.largeImageText = clc.mapname;
    }



    Discord_UpdatePresence(&discordPresence);

    /**/
    Com_Printf("\n\n----------------------------\n");
    Com_Printf("DISCORD: presence updated\n");
    Com_Printf("DISCORD: state - %s\n", discordPresence.state);
    Com_Printf("DISCORD: details - %s\n", discordPresence.details);
    Com_Printf("DISCORD: startTimestamp - %ld\n", discordPresence.startTimestamp);
    Com_Printf("DISCORD: endTimestamp - %ld\n", discordPresence.endTimestamp);
    Com_Printf("DISCORD: largeImageKey - %s\n", discordPresence.largeImageKey);
    Com_Printf("DISCORD: largeImageText - %s\n", discordPresence.largeImageText);
    Com_Printf("DISCORD: smallImageKey - %s\n", discordPresence.smallImageKey);
    Com_Printf("DISCORD: smallImageText - %s\n", discordPresence.smallImageText);
    Com_Printf("DISCORD: partyId - %s\n", discordPresence.partyId);
    Com_Printf("DISCORD: party - (%d / %d)\n", discordPresence.partySize, discordPresence.partyMax);
    Com_Printf("DISCORD: matchSecret - %s\n", discordPresence.matchSecret);
    Com_Printf("DISCORD: joinSecret - %s\n", discordPresence.joinSecret);
    Com_Printf("DISCORD: spectateSecret - %s\n", discordPresence.spectateSecret);
    Com_Printf("DISCORD: instance - %d\n", discordPresence.instance);
    Com_Printf("----------------------------\n\n");
    /**/
}

