/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "version_ddpp.h"

#ifndef GAME_VERSION_H
#define GAME_VERSION_H
#include "generated/nethash.cpp"
#define GAME_VERSION "0.6.3, 10.8"
#define GAME_NETVERSION "0.6 626fce9a778df4d4"
// static const char GAME_RELEASE_VERSION[8] = "10.8";
#define CLIENT_VERSIONNR 10080
#endif

#ifndef GAME_RELEASE_VERSION
#define GAME_RELEASE_VERSION = "10.8"; // TODO: remove
#endif
