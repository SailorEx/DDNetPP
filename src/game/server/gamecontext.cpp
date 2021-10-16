/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/tl/sorted_array.h>

#include <new>
#include <base/math.h>
#include <base/ddpp_logs.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/shared/datafile.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/entities/flag.h>
/*#include "gamemodes/dm.h"
#include "gamemodes/tdm.h"
#include "gamemodes/ctf.h"
#include "gamemodes/mod.h"*/

#include "../../black_hole.h" //testy by ChillerDragon random back_hole.h file i recoved from random russian guy giving no information what it is
#include <stdio.h>
#include <string.h>
#include <engine/server/server.h> // ddpp
#include "gamemodes/DDRace.h"
#include "score.h"
#include "score/file_score.h"
#include <time.h>
#if defined(CONF_SQL)
#include "score/sql_score.h"
#endif

//ChillerDragon (ddpp)
#include <game/server/teams.h>
#include <fstream>

enum
{
	RESET,
	NO_RESET
};


void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LastMapVote = 0;
	//m_LockTeams = 0;

	m_Database = new CSql();
	m_pLetters = new CLetters(this);

	if(Resetting==NO_RESET)
	{
		m_pVoteOptionHeap = new CHeap();
		m_pScore = 0;
		m_NumMutes = 0;
	}
	m_ChatResponseTargetID = -1;
	m_aDeleteTempfile[0] = 0;
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;

	//m_Database->~CSql();
	//delete m_Database;

	if(m_pScore)
		delete m_pScore;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount, int64_t Mask)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd), Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos, int64_t Mask)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, int64_t Mask)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
/*
	if (!NoDamage)
	{
	*/
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Strength;
			if (Owner == -1 || !m_apPlayers[Owner] || !m_apPlayers[Owner]->m_TuneZone)
				Strength = Tuning()->m_ExplosionStrength;
			else
				Strength = TuningList()[m_apPlayers[Owner]->m_TuneZone].m_ExplosionStrength;

			float Dmg = Strength * l;
			if((int)Dmg)
				if((GetPlayerChar(Owner) ? !(GetPlayerChar(Owner)->m_Hit&CCharacter::DISABLE_HIT_GRENADE) : g_Config.m_SvHit || NoDamage) || Owner == apEnts[i]->GetPlayer()->GetCID())
				{
					if(Owner != -1 && apEnts[i]->IsAlive() && !apEnts[i]->CanCollide(Owner)) continue;
					if(Owner == -1 && ActivatedTeam != -1 && apEnts[i]->IsAlive() && apEnts[i]->Team() != ActivatedTeam) continue;
					apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon);
					if(GetPlayerChar(Owner) ? GetPlayerChar(Owner)->m_Hit&CCharacter::DISABLE_HIT_GRENADE : !g_Config.m_SvHit || NoDamage) break;
				}
		}
	//}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos, int64_t Mask)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn), Mask);
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
		//SendChatTarget(SpamProtectionClientID, "Spauuuuuuuuuuuuuuuuun!");
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID, int64_t Mask)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int64_t Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}

void CGameContext::CallVote(int ClientID, const char *aDesc, const char *aCmd, const char *pReason, const char *aChatmsg, bool IsDDPPVetoVote)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	m_IsDDPPVetoVote = IsDDPPVetoVote; // Veto votes only pass if nobody voted agianst it (vote yes doesnt count at all so if nobody votes yes or no the vote will pass)

	int64 Now = Server()->Tick();
	if (ClientID == -1) //Server vote
	{
		SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
		StartVote(aDesc, aCmd, pReason);
		m_VoteCreator = ClientID;
		m_LastVoteCallAll = Now;
	}
	else
	{
		CPlayer *pPlayer = m_apPlayers[ClientID];
		if (!pPlayer)
			return;

		SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
		StartVote(aDesc, aCmd, pReason);
		pPlayer->m_Vote = 1;
		pPlayer->m_VotePos = m_VotePos = 1;
		m_VoteCreator = ClientID;
		pPlayer->m_LastVoteCall = Now;
		m_LastVoteCallAll = Now;
	}
}

void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	if(g_Config.m_SvDemoChat)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	else
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, To);
}

void CGameContext::SendChatTeam(int Team, const char *pText)
{
	for(int i = 0; i<MAX_CLIENTS; i++)
		if(((CGameControllerDDRace*)m_pController)->m_Teams.m_Core.Team(i) == Team)
			SendChatTarget(i, pText);
}

void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText, int SpamProtectionClientID, int ToClientID)
{
	if(SpamProtectionClientID >= 0 && SpamProtectionClientID < MAX_CLIENTS)
	{
		if(ProcessSpamProtection(SpamProtectionClientID))
		{
			SendChatTarget(SpamProtectionClientID, "Stop spamming!");
			//SendChatTarget(SpamProtectionClientID, pText);
			return;
		}
	}

	char aBuf[256], aText[256];
	str_copy(aText, pText, sizeof(aText));
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), aText);
	else if(ChatterClientID == -2)
	{
		str_format(aBuf, sizeof(aBuf), "### %s", aText);
		str_copy(aText, aBuf, sizeof(aText));
		ChatterClientID = -1;
	}
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", aText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = aText;

		// pack one for the recording only
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] != 0) {
				if(!m_apPlayers[i]->m_DND)
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
		}
	}
	else if (Team == CHAT_TO_ONE_CLIENT)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = aText;

		// pack one for the recording only
		if (g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, -1);

		// send to the clients
		if (!m_apPlayers[ToClientID]->m_DND)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ToClientID);
	}
	else
	{
		CTeamsCore * Teams = &((CGameControllerDDRace*)m_pController)->m_Teams.m_Core;
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = aText;

		// pack one for the recording only
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] != 0) {
				if(Team == CHAT_SPEC) {
					if(m_apPlayers[i]->GetTeam() == CHAT_SPEC) {
						Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
					}
				} else {
					if(Teams->Team(i) == Team && m_apPlayers[i]->GetTeam() != CHAT_SPEC) {
						Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
					}
				}
			}
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	if (m_apPlayers[ClientID]->m_SpookyGhostActive)
		Msg.m_Emoticon = 7; // ghost emote only
	else
		Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID, int importance, bool supermod)
{
	if (ClientID == -1) //classical rcon broadcast
	{
		CNetMsg_Sv_Broadcast Msg;
		Msg.m_pMessage = pText; //default broadcast
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

		//set important broadcast for all
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
			{
				m_apPlayers[i]->m_LastBroadcastImportance = 1;
				m_apPlayers[i]->m_LastBroadcast = Server()->Tick();
				//dbg_msg("broadcast","[%s] importance is %d", Server()->ClientName(i), m_apPlayers[i]->m_LastBroadcastImportance);
			}
		}
	}
	else //non rcon broadcast
	{
		if (!m_apPlayers[ClientID]) //ddpp added by ChillerDragon because we handel player vriables here and idk why we should send it to non exsisting players anyways
		{
			//dbg_msg("cBug", "returned id=%d", ClientID);
			return;
		}

		if (m_apPlayers[ClientID]->m_LastBroadcastImportance) //only care if last broadcast was important
		{
			if (m_apPlayers[ClientID]->m_LastBroadcast > Server()->Tick() - Server()->TickSpeed() * 6) //dont overwrite broadcasts send 6 seconds ago
			{
				if (importance == 0)
				{
					//SendChat(-1, CGameContext::CHAT_ALL, "broadcast got ignored");
					return;
				}
				else if (importance == 1 && supermod && m_apPlayers[ClientID]->m_LastBroadcastImportance == 2) //supermoderators can't overwrite broadcaste with lvl 2 importance
				{
					//SendChat(-1, CGameContext::CHAT_ALL, "broadcast got ignored");
					return;
				}
			}
		}

		//dbg_msg("cBug", "curr_imp[%d] last_imp[%d]     curr_t[%d] last_t[%d]", importance, m_apPlayers[ClientID]->m_LastBroadcastImportance, Server()->Tick(), m_apPlayers[ClientID]->m_LastBroadcast);

		CNetMsg_Sv_Broadcast Msg;
		//if (supermod)
		//{
		//	if (m_iBroadcastDelay) { return; } //only send supermod broadcasts if no other broadcast recencly was sent
		//									   //char aText[256];																//supermod broadcast with advertisement attached
		//									   //str_format(aText, sizeof(aText), "%s\n[%s]", pText, aBroadcastMSG);			//supermod broadcast with advertisement attached
		//									   //Msg.m_pMessage = aText;														//supermod broadcast with advertisement attached

		//	Msg.m_pMessage = pText; //default broadcast (comment this out if you want to use the adveertisement string)
		//	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
		//}
		//else
		//{
			Msg.m_pMessage = pText; //default broadcast
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

			m_iBroadcastDelay = Server()->TickSpeed() * 4; //set 4 second delay after normal broadcasts before supermods can send a new one
		//}

		m_apPlayers[ClientID]->m_LastBroadcast = Server()->Tick();
		m_apPlayers[ClientID]->m_LastBroadcastImportance = importance;
	}
}

void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	m_VoteEnforcer = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq() * g_Config.m_SvVoteTime;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	if (Total > VANILLA_MAX_CLIENTS && m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion <= VERSION_DDRACE)
	{
		Yes = float(Yes) * VANILLA_MAX_CLIENTS / float(Total);
		No = float(No) * VANILLA_MAX_CLIENTS / float(Total);
		Total = VANILLA_MAX_CLIENTS;
	}

	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID, int Zone)
{
	if (ClientID == -1)
	{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if (m_apPlayers[i])
				{
					if(m_apPlayers[i]->GetCharacter())
					{
						if (m_apPlayers[i]->GetCharacter()->m_TuneZone == Zone)
							SendTuningParams(i, Zone);
					}
					else if (m_apPlayers[i]->m_TuneZone == Zone)
					{
						SendTuningParams(i, Zone);
					}
				}
			}
			return;
	}

	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = 0;
	if (Zone == 0)
		pParams = (int *)&m_Tuning;
	else
		pParams = (int *)&(m_TuningList[Zone]);

	unsigned int last = sizeof(m_Tuning)/sizeof(int);
	if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion < VERSION_DDNET_EXTRATUNES)
		last = 33;
	else if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion < VERSION_DDNET_HOOKDURATION_TUNE)
		last = 37;
	else if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion < VERSION_DDNET_FIREDELAY_TUNE)
		last = 38;

	for(unsigned i = 0; i < last; i++)
		{
			if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				if((i==31) // collision
				&& (m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_SOLO
				 || m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOCOLL))
				{
					Msg.AddInt(0);
				}
				else if((i==32) // hooking
				&& (m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_SOLO
				 || m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOHOOK))
				{
					Msg.AddInt(0);
				}
				else if((i==3) // ground jump impulse
				&& m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOJUMP)
				{
					Msg.AddInt(0);
				}
				else if((i==33) // jetpack
				&& !(m_apPlayers[ClientID]->GetCharacter()->NeededFaketuning() & FAKETUNE_JETPACK))
				{
					Msg.AddInt(0);
				}
				else if((i==12) // gravity for 420 trolling
				&& m_apPlayers[ClientID]->m_TROLL420)
				{
					Msg.AddInt(-1000000);
				}
				else
				{
					Msg.AddInt(pParams[i]);
				}
			}
			else
				Msg.AddInt(pParams[i]); // if everything is normal just send true tunings
		}
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}
/*
void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;

	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_apPlayers[i]->SetTeam(m_apPlayers[i]->GetTeam()^1, false);
	}

	(void)m_pController->CheckTeamBalance();
}
*/
void CGameContext::OnTick()
{

	// check tuning
	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning[0] = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	// process sql queries
	m_Database->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			// send vote options
			ProgressVoteOptions(i);

			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			bool Veto = false, VetoStop = false;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					//if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
					if(!m_apPlayers[i] ||
							(g_Config.m_SvSpectatorVotes == 0 &&
									m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS) ||
									aVoteChecked[i])	// don't count in votes by spectators if the admin doesn't want it
						continue;

					if((m_VoteKick || m_VoteSpec) && ((!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)))
						continue;

					if (m_VoteCreator != -1) // Ignore Server Votes
					{
						if (GetPlayerChar(m_VoteCreator) && GetPlayerChar(i) &&
							GetPlayerChar(m_VoteCreator)->Team() != GetPlayerChar(i)->Team())
							continue;
					}

					if(m_apPlayers[i]->m_Afk && i != m_VoteCreator)
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;

					// veto right for players with much progress and who're not afk
					if(!m_VoteKick && !m_VoteSpec && !m_apPlayers[i]->m_Afk &&
						m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS &&
						m_apPlayers[i]->GetCharacter() &&
						m_apPlayers[i]->GetCharacter()->m_DDRaceState == DDRACE_STARTED &&
						g_Config.m_SvVoteVetoTime &&
						(Server()->Tick() - m_apPlayers[i]->GetCharacter()->m_StartTime) / (Server()->TickSpeed() * 60) > g_Config.m_SvVoteVetoTime)
					{
						if(ActVote == 0)
							Veto = true;
						else if(ActVote < 0)
							VetoStop = true;
					}
				}

				if(g_Config.m_SvVoteMaxTotal && Total > g_Config.m_SvVoteMaxTotal &&
						(m_VoteKick || m_VoteSpec))
					Total = g_Config.m_SvVoteMaxTotal;

				if((Yes > Total / (100.0 / g_Config.m_SvVoteYesPercentage)) && !Veto && !m_IsDDPPVetoVote)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= Total - Total / (100.0 / g_Config.m_SvVoteYesPercentage))
					m_VoteEnforce = VOTE_ENFORCE_NO;

				if(VetoStop)
					m_VoteEnforce = VOTE_ENFORCE_NO;
				else if (m_IsDDPPVetoVote && No)
					m_VoteEnforce = VOTE_ENFORCE_NO;

				m_VoteWillPass = Yes > (Yes + No) / (100.0 / g_Config.m_SvVoteYesPercentage);
			}

			if(time_get() > m_VoteCloseTime && !g_Config.m_SvVoteMajority)
				m_VoteEnforce = (m_VoteWillPass && !Veto && !m_IsDDPPVetoVote) ? VOTE_ENFORCE_YES : VOTE_ENFORCE_NO;
			if (time_get() > m_VoteCloseTime && m_IsDDPPVetoVote && !No) // pass vote even if nobody votes yes
				m_VoteEnforce = VOTE_ENFORCE_YES;

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				if (m_IsDDPPVetoVote)
				{
					SendChat(-1, CGameContext::CHAT_ALL, "Vote passed because nobody used veto (Veto Vote)");
				}
				else
				{
					SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");
				}

				if (m_VoteCreator != -1) // Ignore server votes
				{
					if (m_apPlayers[m_VoteCreator])
						m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
				}
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_YES_ADMIN)
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf),"Vote passed enforced by server moderator");
				Console()->ExecuteLine(m_aVoteCommand, m_VoteEnforcer);
				SendChat(-1, CGameContext::CHAT_ALL, aBuf);
				EndVote();
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO_ADMIN)
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf),"Vote failed enforced by server moderator");
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			//else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || (time_get() > m_VoteCloseTime && g_Config.m_SvVoteMajority))
			{
				EndVote();
				if(VetoStop || (m_VoteWillPass && Veto))
					SendChat(-1, CGameContext::CHAT_ALL, "Vote failed because of veto. Find an empty server instead");
				else if (m_IsDDPPVetoVote)
					SendChat(-1, CGameContext::CHAT_ALL, "Vote failed because someone voted agianst it. (Veto Vote)");
				else
					SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				for(int i = 0; i < MAX_CLIENTS; ++i)
					if(Server()->ClientIngame(i))
						SendVoteStatus(i, Total, Yes, No);
			}
		}
	}
	for(int i = 0; i < m_NumMutes; i++)
	{
		if(m_aMutes[i].m_Expire <= Server()->Tick())
		{
			m_NumMutes--;
			m_aMutes[i] = m_aMutes[m_NumMutes];
		}
	}

	if(Server()->Tick() % (g_Config.m_SvAnnouncementInterval * Server()->TickSpeed() * 60) == 0)
	{
		char *Line = ((CServer *) Server())->GetAnnouncementLine(g_Config.m_SvAnnouncementFileName);
		if(Line)
			SendChat(-1, CGameContext::CHAT_ALL, Line);
	}

	if(Collision()->m_NumSwitchers > 0)
		for (int i = 0; i < Collision()->m_NumSwitchers+1; ++i)
		{
			for (int j = 0; j < MAX_CLIENTS; ++j)
			{
				if(Collision()->m_pSwitchers[i].m_EndTick[j] <= Server()->Tick() && Collision()->m_pSwitchers[i].m_Type[j] == TILE_SWITCHTIMEDOPEN)
				{
					Collision()->m_pSwitchers[i].m_Status[j] = false;
					Collision()->m_pSwitchers[i].m_EndTick[j] = 0;
					Collision()->m_pSwitchers[i].m_Type[j] = TILE_SWITCHCLOSE;
				}
				else if(Collision()->m_pSwitchers[i].m_EndTick[j] <= Server()->Tick() && Collision()->m_pSwitchers[i].m_Type[j] == TILE_SWITCHTIMEDCLOSE)
				{
					Collision()->m_pSwitchers[i].m_Status[j] = true;
					Collision()->m_pSwitchers[i].m_EndTick[j] = 0;
					Collision()->m_pSwitchers[i].m_Type[j] = TILE_SWITCHOPEN;
				}
			}
		}


	if (m_CreateShopBot && (Server()->Tick() % 50 == 0))
	{
		CreateNewDummy(99);//shop bot
		m_CreateShopBot = false;
	}

	DDPP_Tick();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

struct CVoteOptionServer *CGameContext::GetVoteOption(int Index)
{
	CVoteOptionServer *pCurrent;
	for (pCurrent = m_pVoteOptionFirst;
			Index > 0 && pCurrent;
			Index--, pCurrent = pCurrent->m_pNext);

	if (Index > 0)
		return 0;
	return pCurrent;
}

void CGameContext::ProgressVoteOptions(int ClientID)
{
	CPlayer *pPl = m_apPlayers[ClientID];

	if (pPl->m_SendVoteIndex == -1)
		return; // we didn't start sending options yet

	if (pPl->m_SendVoteIndex > m_NumVoteOptions)
		return; // shouldn't happen / fail silently

	int VotesLeft = m_NumVoteOptions - pPl->m_SendVoteIndex;
	int NumVotesToSend = min(g_Config.m_SvSendVotesPerTick, VotesLeft);

	if (!VotesLeft)
	{
		// player has up to date vote option list
		return;
	}

	// build vote option list msg
	int CurIndex = 0;

	CNetMsg_Sv_VoteOptionListAdd OptionMsg;
	OptionMsg.m_pDescription0 = "";
	OptionMsg.m_pDescription1 = "";
	OptionMsg.m_pDescription2 = "";
	OptionMsg.m_pDescription3 = "";
	OptionMsg.m_pDescription4 = "";
	OptionMsg.m_pDescription5 = "";
	OptionMsg.m_pDescription6 = "";
	OptionMsg.m_pDescription7 = "";
	OptionMsg.m_pDescription8 = "";
	OptionMsg.m_pDescription9 = "";
	OptionMsg.m_pDescription10 = "";
	OptionMsg.m_pDescription11 = "";
	OptionMsg.m_pDescription12 = "";
	OptionMsg.m_pDescription13 = "";
	OptionMsg.m_pDescription14 = "";

	// get current vote option by index
	CVoteOptionServer *pCurrent = GetVoteOption(pPl->m_SendVoteIndex);

	while(CurIndex < NumVotesToSend && pCurrent != NULL)
	{
		switch(CurIndex)
		{
			case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
			case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
			case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
			case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
			case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
			case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
			case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
			case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
			case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
			case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
			case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
			case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
			case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
			case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
			case 14: OptionMsg.m_pDescription14 = pCurrent->m_aDescription; break;
		}

		CurIndex++;
		pCurrent = pCurrent->m_pNext;
	}

	// send msg
	OptionMsg.m_NumOptions = NumVotesToSend;
	Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);

	pPl->m_SendVoteIndex += NumVotesToSend;
}

void CGameContext::OnClientEnter(int ClientID, bool silent)
{

	if (IsDDPPgametype("survival"))
	{
		SetPlayerSurvival(ClientID, 1);
	}
	else if (IsDDPPgametype("vanilla"))
	{
		if (m_apPlayers[ClientID])
		{
			m_apPlayers[ClientID]->m_IsVanillaDmg = true;
			m_apPlayers[ClientID]->m_IsVanillaWeapons = true;
			m_apPlayers[ClientID]->m_IsVanillaCompetetive = true;
		}
	}
	else if (IsDDPPgametype("fng"))
	{
		if (m_apPlayers[ClientID])
		{
			m_apPlayers[ClientID]->m_IsInstaMode_idm = true;
			m_apPlayers[ClientID]->m_IsInstaMode_fng = true;
		}
	}

	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->Respawn();
	// init the player
	Score()->PlayerData(ClientID)->Reset();
	m_apPlayers[ClientID]->m_Score = -9999;

	// Can't set score here as LoadScore() is threaded, run it in
	// LoadScoreThreaded() instead
	Score()->LoadScore(ClientID);
	CheckIpJailed(ClientID);

	m_apPlayers[ClientID]->m_Score = (Score()->PlayerData(ClientID)->m_BestTime) ? Score()->PlayerData(ClientID)->m_BestTime : -9999;

	if (g_Config.m_SvDDPPscore == 0)
	{
		m_apPlayers[ClientID]->m_Score = 0;
	}

	if(((CServer *) Server())->m_aPrevStates[ClientID] < CServer::CClient::STATE_INGAME)
	{
		char aBuf[512];
		if (!silent)
		{
			if (ShowJoinMessage(ClientID))
			{
				str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()));
				SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s (message hidden)", Server()->ClientName(ClientID), m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()));
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			}
		}
		if (g_Config.m_SvInstagibMode)
		{
			SendChatTarget(ClientID, "DDNet++ Instagib Mod (" DDNETPP_VERSION ") based on DDNet 9.0.2");
		}
		else
		{
			char aWelcome[128];
			str_format(aWelcome, sizeof(aWelcome), "DDNet++ %s Mod (%s) based on DDNet 9.0.2", g_Config.m_SvDDPPgametype, DDNETPP_VERSION);
			SendChatTarget(ClientID, aWelcome);
		}

		if(g_Config.m_SvWelcome[0]!=0)
			SendChatTarget(ClientID,g_Config.m_SvWelcome);
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());

		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		if (g_Config.m_SvShowOthersDefault)
		{
			if (g_Config.m_SvShowOthers)
				SendChatTarget(ClientID, "You can see other players. To disable this use the DDNet client and type /showothers .");

			m_apPlayers[ClientID]->m_ShowOthers = true;
		}
	}
	m_VoteUpdate = true;

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	m_apPlayers[ClientID]->m_Authed = ((CServer*)Server())->m_aClients[ClientID].m_Authed;
}

void CGameContext::OnClientConnected(int ClientID)
{
	m_ClientLeftServer[ClientID] = false;

	// Check which team the player should be on (copyed all the stuff cuz const int mukked)
	//if (g_Config.m_SvInstagibMode == 2 || g_Config.m_SvInstagibMode == 4) //grenade zCatch and rifle zCatch
	if (m_insta_survival_gamestate) //running survival game
	{
		const int StartTeam = TEAM_SPECTATORS;

		if (!m_apPlayers[ClientID])
			m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
		else
		{
			delete m_apPlayers[ClientID];
			m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
			//	//m_apPlayers[ClientID]->Reset();
			//	//((CServer*)Server())->m_aClients[ClientID].Reset();
			//	((CServer*)Server())->m_aClients[ClientID].m_State = 4;
		}
		//players[client_id].init(client_id);
		//players[client_id].client_id = client_id;

		//(void)m_pController->CheckTeamBalance();
	}
	else
	{
		const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(ClientID);

		if (!m_apPlayers[ClientID])
			m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
		else
		{
			delete m_apPlayers[ClientID];
			m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
			//	//m_apPlayers[ClientID]->Reset();
			//	//((CServer*)Server())->m_aClients[ClientID].Reset();
			//	((CServer*)Server())->m_aClients[ClientID].m_State = 4;
		}
		//players[client_id].init(client_id);
		//players[client_id].client_id = client_id;

		//(void)m_pController->CheckTeamBalance();
	}


#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

	// send motd
	CNetMsg_Sv_Motd Msg;
	char aBuf[128]; 
	char aBroad[2048];
	bool IsSupporterOnline = false;
	str_format(aBroad, sizeof(aBroad), "%s\n[ONLINE SUPPORTER]:\n", g_Config.m_SvMotd);

	//lass mal durch alle spieler iterieren und schauen ob n mod online is
	for (int i = 0; i < MAX_CLIENTS; i++) //iteriert durch alle 64 client ids
	{
		if (m_apPlayers[i] && m_apPlayers[i]->m_IsSupporter) //schaut ob der spieler existiert und supporter is
		{
			str_format(aBuf, sizeof(aBuf), "• '%s'\n", Server()->ClientName(i));
			str_append(aBroad, aBuf, sizeof(aBroad));
			IsSupporterOnline = true;
		}
	}

	if (IsSupporterOnline) // so wenn ein mod online ist schicken wir die modifizierte message of the day mit dem namen des sup 
	{
		Msg.m_pMessage = aBroad;
	}
	else //sonst schicken wir die normale 
	{
		Msg.m_pMessage = g_Config.m_SvMotd; //hier wird der string aus der config variable in die message geklatscht du meinst das was man in der autoexec eingibt? yes oder ngame mit sv_modt yy also lass das mal modifizieren davo
	}
	
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason, bool silent)
{
	m_ClientLeftServer[ClientID] = true;
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(pReason, silent);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	//(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
	}

	// update conversation targets
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_LastWhisperTo == ClientID)
			m_apPlayers[i]->m_LastWhisperTo = -1;
	}
}

int CGameContext::GetNextClientID()
{
	int ClientID = -1;
	for (int i = 0; i < g_Config.m_SvMaxClients; i++)
	{
		if (m_apPlayers[i])
			continue;

		ClientID = i;
		break;
	}

	return ClientID;
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		//char aBuf[256];
		//str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
		//Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
			int Team = pMsg->m_Team;

			// trim right and set maximum length to 256 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(str_utf8_isspace(Code))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 256)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 32 characters per second)
			if(Length == 0 || (pMsg->m_pMessage[0]!='/' && (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed()*((31+Length)/32) > Server()->Tick())))
				return;

			//pPlayer->m_LastChat = Server()->Tick();

			int GameTeam = ((CGameControllerDDRace*)m_pController)->m_Teams.m_Core.Team(pPlayer->GetCID());
			if(Team)
				Team = ((pPlayer->GetTeam() == -1) ? CHAT_SPEC : GameTeam);
			else
				Team = CHAT_ALL;

			pPlayer->m_PlayerHumanLevelState++;

			GlobalChat(ClientID, pMsg->m_pMessage);

			//############
			//CHAT COMMANDS
			//############
			if(pMsg->m_pMessage[0]=='/')
			{
				// todo: adde mal deine ganzen cmds hier in das system von ddnet ddracechat.cpp
				// geb mal ein cmd /join spec   && /join fight (player)
				if(IsDDPPChatCommand(ClientID, pPlayer, pMsg->m_pMessage + 1))
				{
					// pass
				}
				else if (str_comp_nocase_num(pMsg->m_pMessage + 1, "w ", 2) == 0)
				{
					char pWhisperMsg[256];
					str_copy(pWhisperMsg, pMsg->m_pMessage + 3, 256);
					Whisper(pPlayer->GetCID(), pWhisperMsg);
				}
				else if (str_comp_nocase_num(pMsg->m_pMessage + 1, "whisper ", 8) == 0)
				{
					char pWhisperMsg[256];
					str_copy(pWhisperMsg, pMsg->m_pMessage + 9, 256);
					Whisper(pPlayer->GetCID(), pWhisperMsg);
				}
				else if (str_comp_nocase_num(pMsg->m_pMessage + 1, "c ", 2) == 0)
				{
					char pWhisperMsg[256];
					str_copy(pWhisperMsg, pMsg->m_pMessage + 3, 256);
					Converse(pPlayer->GetCID(), pWhisperMsg);
				}
				else if (str_comp_nocase_num(pMsg->m_pMessage + 1, "converse ", 9) == 0)
				{
					char pWhisperMsg[256];
					str_copy(pWhisperMsg, pMsg->m_pMessage + 10, 256);
					Converse(pPlayer->GetCID(), pWhisperMsg);
				}
				else
				{
					if(g_Config.m_SvSpamprotection && str_comp_nocase_num(pMsg->m_pMessage+1, "timeout ", 8) != 0
						&& pPlayer->m_LastCommands[0] && pPlayer->m_LastCommands[0]+Server()->TickSpeed() > Server()->Tick()
						&& pPlayer->m_LastCommands[1] && pPlayer->m_LastCommands[1]+Server()->TickSpeed() > Server()->Tick()
						&& pPlayer->m_LastCommands[2] && pPlayer->m_LastCommands[2]+Server()->TickSpeed() > Server()->Tick()
						&& pPlayer->m_LastCommands[3] && pPlayer->m_LastCommands[3]+Server()->TickSpeed() > Server()->Tick()
					)
						return;

					int64 Now = Server()->Tick();
					pPlayer->m_LastCommands[pPlayer->m_LastCommandPos] = Now;
					pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

					m_ChatResponseTargetID = ClientID;
					Server()->RestrictRconOutput(ClientID);
					Console()->SetFlagMask(CFGFLAG_CHAT);

					if (pPlayer->m_Authed)
						Console()->SetAccessLevel(pPlayer->m_Authed == CServer::AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : pPlayer->m_Authed == CServer::AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : IConsole::ACCESS_LEVEL_HELPER);
					else
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
					Console()->SetPrintOutputLevel(m_ChatPrintCBIndex, 0);

					Console()->ExecuteLine(pMsg->m_pMessage + 1, ClientID);
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "%d used %s", ClientID, pMsg->m_pMessage);
					Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "chat-command", aBuf);

					Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
					Console()->SetFlagMask(CFGFLAG_SERVER);
					m_ChatResponseTargetID = -1;
					Server()->RestrictRconOutput(-1);
				}
			}
			else
			{
				if (pPlayer->m_PlayerHumanLevel < g_Config.m_SvChatHumanLevel)
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "your '/human_level' is too low %d/%d to use the chat.", m_apPlayers[ClientID]->m_PlayerHumanLevel, g_Config.m_SvChatHumanLevel);
					SendChatTarget(ClientID, aBuf);
				}
				else if (m_apPlayers[ClientID] && !m_apPlayers[ClientID]->m_Authed && AdminChatPing(pMsg->m_pMessage))
				{
					if (g_Config.m_SvMinAdminPing > 256)
						SendChatTarget(ClientID, "you are not allowed to ping admins in chat.");
					else
						SendChatTarget(ClientID, "your message is too short to bother an admin with that.");
				}
				else
				{
					if (!pPlayer->m_ShowName)
					{
						str_copy(pPlayer->m_ChatText, pMsg->m_pMessage, sizeof(pPlayer->m_ChatText));
						pPlayer->m_ChatTeam = Team;
						pPlayer->FixForNoName(1);
					}
					else
						SendChat(ClientID, Team, pMsg->m_pMessage, ClientID); //hier stehe ich eig SendChatFUNKTION
				}
			}
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			int64 Now = Server()->Tick();
			pPlayer->m_LastVoteTry = Now;
			//if(pPlayer->GetTeam() == TEAM_SPECTATORS)
			if(g_Config.m_SvSpectatorVotes == 0 && pPlayer->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
				return;
			}

			if(m_VoteCloseTime)
			{
				SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
				return;
			}

			int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*g_Config.m_SvVoteDelay - Now;
			if(pPlayer->m_LastVoteCall && Timeleft > 0)
			{
				char aChatmsg[512] = {0};
				str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote.", (Timeleft/Server()->TickSpeed())+1);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}

			Timeleft = m_LastVoteCallAll + Server()->TickSpeed() * g_Config.m_SvVoteDelayAll - Now;

			if (Timeleft > 0)
			{
				char aChatmsg[512] = { 0 };
				str_format(aChatmsg, sizeof(aChatmsg), "there is a %d seconds delay between votes.", (Timeleft / Server()->TickSpeed()) + 1);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}

			char aChatmsg[512] = {0};
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						if(!Console()->LineIsValid(pOption->m_aCommand))
						{
							SendChatTarget(ClientID, "Invalid option");
							return;
						}
						if(!m_apPlayers[ClientID]->m_Authed && (strncmp(pOption->m_aCommand, "sv_map ", 7) == 0 || strncmp(pOption->m_aCommand, "change_map ", 11) == 0 || strncmp(pOption->m_aCommand, "random_map", 10) == 0 || strncmp(pOption->m_aCommand, "random_unfinished_map", 21) == 0) && time_get() < m_LastMapVote + (time_freq() * g_Config.m_SvVoteMapTimeDelay))
						{
							char chatmsg[512] = {0};
							str_format(chatmsg, sizeof(chatmsg), "There's a %d second delay between map-votes, please wait %d seconds.", g_Config.m_SvVoteMapTimeDelay,((m_LastMapVote+(g_Config.m_SvVoteMapTimeDelay * time_freq()))/time_freq())-(time_get()/time_freq()));
							SendChatTarget(ClientID, chatmsg);

							return;
						}

						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
									pOption->m_aDescription, pReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);

						if((strncmp(pOption->m_aCommand, "random_map", 10) == 0 || strncmp(pOption->m_aCommand, "random_unfinished_map", 21) == 0) && str_length(pReason) == 1 && pReason[0] >= '1' && pReason[0] <= '5')
						{
							int stars = pReason[0] - '0';
							str_format(aCmd, sizeof(aCmd), "%s %d", pOption->m_aCommand, stars);
						}
						else
						{
							str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						}

						m_LastMapVote = time_get();
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
				{
					if (pPlayer->m_Authed != CServer::AUTHED_ADMIN)  // allow admins to call any vote they want
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
					else
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s'", Server()->ClientName(ClientID), pMsg->m_Value);
						str_format(aDesc, sizeof(aDesc), "%s", pMsg->m_Value);
						str_format(aCmd, sizeof(aCmd), "%s", pMsg->m_Value);
					}
				}

				m_LastMapVote = time_get();
				m_VoteKick = false;
				m_VoteSpec = false;
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!m_apPlayers[ClientID]->m_Authed && time_get() < m_apPlayers[ClientID]->m_Last_KickVote + (time_freq() * 5))
					return;
				else if(!m_apPlayers[ClientID]->m_Authed && time_get() < m_apPlayers[ClientID]->m_Last_KickVote + (time_freq() * g_Config.m_SvVoteKickTimeDelay))
				{
					char chatmsg[512] = {0};
					str_format(chatmsg, sizeof(chatmsg), "There's a %d second wait time between kick votes for each player please wait %d second(s)",
					g_Config.m_SvVoteKickTimeDelay,
					((m_apPlayers[ClientID]->m_Last_KickVote + (m_apPlayers[ClientID]->m_Last_KickVote*time_freq()))/time_freq())-(time_get()/time_freq())
					);
					SendChatTarget(ClientID, chatmsg);
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}
				//else if(!g_Config.m_SvVoteKick)
				else if(!g_Config.m_SvVoteKick && !pPlayer->m_Authed) // allow admins to call kick votes even if they are forbidden
				{
					SendChatTarget(ClientID, "Server does not allow voting to kick players");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}

				if(g_Config.m_SvVoteKickMin)
				{
					int PlayerNum = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
						if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
							++PlayerNum;

					if(PlayerNum < g_Config.m_SvVoteKickMin)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players on the server", g_Config.m_SvVoteKickMin);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
				}

				int KickID = str_toint(pMsg->m_Value);

				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget(ClientID, "Invalid client id to kick");
					return;
				}
				if(KickID == ClientID)
				{
					SendChatTarget(ClientID, "You can't kick yourself");
					return;
				}
				if (!Server()->ReverseTranslate(KickID, ClientID))
				{
					return;
				}
				//if(Server()->IsAuthed(KickID))
				if((m_apPlayers[KickID]->m_Authed != CServer::AUTHED_HONEY) && // always allow kicking honeypot users
					(m_apPlayers[KickID]->m_Authed > 0 && m_apPlayers[KickID]->m_Authed >= pPlayer->m_Authed))
				{
					SendChatTarget(ClientID, "You can't kick moderators");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					char aBufKick[128];
					str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
					SendChatTarget(KickID, aBufKick);
					return;
				}

				// Don't allow kicking if a player has no character
				if(!GetPlayerChar(ClientID) || !GetPlayerChar(KickID) || GetDDRaceTeam(ClientID) != GetDDRaceTeam(KickID))
				{
					SendChatTarget(ClientID, "You can kick only your team member");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}
				if (m_apPlayers[KickID]->m_IsDummy)
				{
					SendChatTarget(ClientID, "You can't kick dummies");
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
				str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
				if (!g_Config.m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
				}
				m_apPlayers[ClientID]->m_Last_KickVote = time_get();
				m_VoteKick = true;
				m_VoteSpec = false;
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!g_Config.m_SvVoteSpectate)
				{
					SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
					return;
				}

				int SpectateID = str_toint(pMsg->m_Value);

				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget(ClientID, "Invalid client id to move");
					return;
				}
				if(SpectateID == ClientID)
				{
					SendChatTarget(ClientID, "You can't move yourself");
					return;
				}
				if (!Server()->ReverseTranslate(SpectateID, ClientID))
				{
					return;
				}

				if(!GetPlayerChar(ClientID) || !GetPlayerChar(SpectateID) || GetDDRaceTeam(ClientID) != GetDDRaceTeam(SpectateID))
				{
					SendChatTarget(ClientID, "You can only move your team member to specators");
					return;
				}

				if(g_Config.m_SvPauseable && g_Config.m_SvVotePause)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to pause '%s' for %d seconds (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), g_Config.m_SvVotePauseTime, pReason);
					str_format(aDesc, sizeof(aDesc), "Pause '%s' (%ds)", Server()->ClientName(SpectateID), g_Config.m_SvVotePauseTime);
					str_format(aCmd, sizeof(aCmd), "force_pause %d %d", SpectateID, g_Config.m_SvVotePauseTime);
				}
				else
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
					str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
				}
				m_VoteKick = false;
				m_VoteSpec = true;
			}

			if(aCmd[0] && str_comp(aCmd,"info"))
				CallVote(ClientID, aDesc, aCmd, pReason, aChatmsg);
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
			CCharacter *pChr = pPlayer->GetCharacter();

			if (pMsg->m_Vote == 1) //vote yes (f3)
			{
				//SendChatTarget(ClientID, "you pressed f3");

				if (pChr)
				{
					if (pChr->m_InShop)
					{
						if (pChr->m_PurchaseState == 1)
						{
							pChr->ConfirmPurchase();
						}
						else if (pChr->m_PurchaseState == 2)
						{
							pChr->PurchaseEnd(false);
						}
					}
					else
					{
						if (pChr)
						{
							IGameController* ControllerDDrace = pPlayer->GetCharacter()->GameServer()->m_pController;
							if (((CGameControllerDDRace*)ControllerDDrace)->m_apFlags[0])
							{
								if (((CGameControllerDDRace*)ControllerDDrace)->m_apFlags[0]->m_pCarryingCharacter == pChr) {
									((CGameControllerDDRace*)ControllerDDrace)->DropFlag(0, pChr->GetAimDir()); //red
									//SendChatTarget(ClientID, "you dropped red flag");
								}
							}
							if (((CGameControllerDDRace*)ControllerDDrace)->m_apFlags[1])
							{
								if (((CGameControllerDDRace*)ControllerDDrace)->m_apFlags[1]->m_pCarryingCharacter == pChr) {
									((CGameControllerDDRace*)ControllerDDrace)->DropFlag(1, pChr->GetAimDir()); //blue
									//SendChatTarget(ClientID, "you dropped blue flag");
								}
							}
						}
					}
				}
			}
			else if (pMsg->m_Vote == -1) //vote no (f4)
			{
				//SendChatTarget(ClientID, "you pressed f4");

				if (pChr)
				{
					if (pChr->m_InShop)
					{
						if (pChr->m_PurchaseState == 2)
						{
							pChr->PurchaseEnd(true);
						}
						else if (pChr->m_ShopWindowPage == -1)
						{
							pChr->StartShop();
						}
					}
					else
					{
						if (g_Config.m_SvAllowDroppingWeapons)
						{
							pChr->DropWeapon(pChr->GetActiveWeapon()); // drop the weapon youre holding.
						}
					}
				}
			}

			if(!m_VoteCloseTime)
				return;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			int64 Now = Server()->Tick();

			pPlayer->m_LastVoteTry = Now;

			if(!pMsg->m_Vote)
				return;

			pPlayer->m_Vote = pMsg->m_Vote;
			pPlayer->m_VotePos = ++m_VotePos;
			m_VoteUpdate = true;
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			//if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
			if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam + Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay > Server()->Tick()))
				return;

			/*if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				SendBroadcast("Teams are locked", ClientID);
				return;
			}*/

			if (IsMinigame(ClientID))
			{
				SendChatTarget(ClientID, "[MINIGAMES] You can't change team while playing minigames or being in jail.");
				return;
			}

			if (m_apPlayers[ClientID]->m_SpawnBlocks > 3)
			{
				SendChatTarget(ClientID, "[SPAWNBLOCK] You can't change team because you spawnblock too much. Try agian later.");
				return;
			}

			if (m_apPlayers[ClientID]->m_IsBlockWaving)
			{
				SendChatTarget(ClientID, "[BlockWave] you can't change team while block waving. Try '/blockwave leave'");
				return;
			}

			//zCatch survival LMS ChillerDragon Instagib grenade rifle
			if (g_Config.m_SvInstagibMode == 2 || g_Config.m_SvInstagibMode == 4) //gLMS iLMS
			{
				SendChatTarget(ClientID, "You can't join running survival games. Wait until the round ends.");
				return;
			}

			if (pPlayer->m_GangsterBagMoney)
			{
				SendChatTarget(ClientID, "Make sure to empty your gangsterbag before disconnecting/spectating or you will lose it.");
				SendChatTarget(ClientID, "or clear it yourself with '/gangsterbag clear'");
				return;
			}

			//Kill Protection
			CCharacter* pChr = pPlayer->GetCharacter();
			if(pChr)
			{
				int CurrTime = (Server()->Tick() - pChr->m_StartTime) / Server()->TickSpeed();
				if(g_Config.m_SvKillProtection != 0 && CurrTime >= (60 * g_Config.m_SvKillProtection) && pChr->m_DDRaceState == DDRACE_STARTED)
				{
					SendChatTarget(ClientID, "Kill Protection enabled. If you really want to join the spectators, first type /kill");
					return;
				}
			}

			if(pPlayer->m_TeamChangeTick > Server()->Tick())
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
				SendBroadcast(aBuf, ClientID);
				return;
			}

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				//if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))

				if(pPlayer->m_Paused)
					SendChatTarget(ClientID,"Use /pause first then you can kill");
				else
				{
					//pPlayer->m_LastSetTeam = Server()->Tick();
					if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->SetTeam(pMsg->m_Team);
					//(void)m_pController->CheckTeamBalance();
					pPlayer->m_TeamChangeTick = Server()->Tick();
				}
				//else
					//SendBroadcast("Teams must be balanced, please join other team", ClientID);
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
				SendBroadcast(aBuf, ClientID);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_ISDDNET)
		{
			int Version = pUnpacker->GetInt();

			if (pUnpacker->Error())
			{
				if (pPlayer->m_ClientVersion < VERSION_DDRACE)
					pPlayer->m_ClientVersion = VERSION_DDRACE;
			}
			else if(pPlayer->m_ClientVersion < Version)
				pPlayer->m_ClientVersion = Version;

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%d using Custom Client %d", ClientID, pPlayer->m_ClientVersion);
			dbg_msg("DDNet", aBuf);

			if (Version >= 11043 && Version < 11073)
				m_apPlayers[ClientID]->m_ScoreFixForDDNet = true;

			//first update his teams state
			((CGameControllerDDRace*)m_pController)->m_Teams.SendTeamsState(ClientID);

			//second give him records
			SendRecord(ClientID);

			//third give him others current time for table score
			if(g_Config.m_SvHideScore) return;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_apPlayers[i] && Score()->PlayerData(i)->m_CurrentTime > 0)
				{
					CNetMsg_Sv_PlayerTime Msg;
					Msg.m_Time = Score()->PlayerData(i)->m_CurrentTime * 100;
					Msg.m_ClientID = i;
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
					//also send its time to others

				}
			}
			//also send its time to others
			if(Score()->PlayerData(ClientID)->m_CurrentTime > 0)
			{
				//TODO: make function for this fucking steps
				CNetMsg_Sv_PlayerTime Msg;
				Msg.m_Time = Score()->PlayerData(ClientID)->m_CurrentTime * 100;
				Msg.m_ClientID = ClientID;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
			}

			//and give him correct tunings
			if (Version >= VERSION_DDNET_EXTRATUNES)
				SendTuningParams(ClientID, pPlayer->m_TuneZone);


			//tell older clients than 11.4.3 to update to at least 11.4.3 to get zoom and eye wheel support for ddnet++
			if (Version < 11043 && g_Config.m_SvClientSuggestionOld[0] != '\0')
				SendBroadcast(g_Config.m_SvClientSuggestionSupported, ClientID);

			// block servers dont need those messages
			/*//tell old clients to update
			if (Version < VERSION_DDNET_UPDATER_FIXED && g_Config.m_SvClientSuggestionOld[0] != '\0')
				SendBroadcast(g_Config.m_SvClientSuggestionOld, ClientID);
			//tell known bot clients that they're botting and we know it
			if (((Version >= 15 && Version < 100) || Version == 502) && g_Config.m_SvClientSuggestionBot[0] != '\0')
				SendBroadcast(g_Config.m_SvClientSuggestionBot, ClientID);*/
		}
		else if (MsgID == NETMSGTYPE_CL_SHOWOTHERS)
		{
			if(g_Config.m_SvShowOthers && !g_Config.m_SvShowOthersDefault)
			{
				CNetMsg_Cl_ShowOthers *pMsg = (CNetMsg_Cl_ShowOthers *)pRawMsg;
				pPlayer->m_ShowOthers = (bool)pMsg->m_Show;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(pMsg->m_SpectatorID != SPEC_FREEVIEW)
				if (!Server()->ReverseTranslate(pMsg->m_SpectatorID, ClientID))
					return;

			if((g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()/4 > Server()->Tick()))
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
				SendChatTarget(ClientID, "Invalid spectator id used");
			else
				pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if (!pPlayer->m_SpookyGhostActive)
			{
				if (g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo + Server()->TickSpeed()*g_Config.m_SvInfoChangeDelay > Server()->Tick())
					return;

				CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
				pPlayer->m_LastChangeInfo = Server()->Tick();

				// set infos
				char aOldName[MAX_NAME_LENGTH];
				str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
				Server()->SetClientName(ClientID, pMsg->m_pName);
				if (str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
				{
					int mute = NameChangeMuteCheck(ClientID);
					char aChatText[256];
					if (mute > 0)
					{
						str_format(aChatText, sizeof aChatText, "[MUTE] %d seconds delay for name change message.", mute);
						SendChatTarget(ClientID, aChatText);
					}
					else
					{
						str_format(aChatText, sizeof(aChatText), "'%s' changed name to '%s'", aOldName, Server()->ClientName(ClientID));
						SendChat(-1, CGameContext::CHAT_ALL, aChatText);
					}

					// reload scores

					Score()->PlayerData(ClientID)->Reset();
					Score()->LoadScore(ClientID);
					Score()->PlayerData(ClientID)->m_CurrentTime = Score()->PlayerData(ClientID)->m_BestTime;
					if (g_Config.m_SvInstagibMode || g_Config.m_SvDDPPscore == 0)
					{
						m_apPlayers[ClientID]->m_Score = 0;
					}
					else
					{
						m_apPlayers[ClientID]->m_Score = (Score()->PlayerData(ClientID)->m_BestTime) ? Score()->PlayerData(ClientID)->m_BestTime : -9999;
					}
				}
				Server()->SetClientClan(ClientID, pMsg->m_pClan);
				Server()->SetClientCountry(ClientID, pMsg->m_Country);
				str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
				pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
				pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
				pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
				//m_pController->OnPlayerInfoChange(pPlayer);

				if (pPlayer->GetCharacter())
				{
					pPlayer->GetCharacter()->SaveRealInfos();
				}
			}
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*g_Config.m_SvEmoticonDelay > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
			CCharacter* pChr = pPlayer->GetCharacter();
			if(pChr && g_Config.m_SvEmotionalTees && pPlayer->m_EyeEmote)
			{
				switch(pMsg->m_Emoticon)
				{
				case EMOTICON_EXCLAMATION:
				case EMOTICON_GHOST:
				case EMOTICON_QUESTION:
				case EMOTICON_WTF:
						pChr->SetEmoteType(EMOTE_SURPRISE);
						break;
				case EMOTICON_DOTDOT:
				case EMOTICON_DROP:
				case EMOTICON_ZZZ:
						pChr->SetEmoteType(EMOTE_BLINK);
						break;
				case EMOTICON_EYES:
				case EMOTICON_HEARTS:
				case EMOTICON_MUSIC:
						pChr->SetEmoteType(EMOTE_HAPPY);
						break;
				case EMOTICON_OOP:
				case EMOTICON_SORRY:
				case EMOTICON_SUSHI:
						pChr->SetEmoteType(EMOTE_PAIN);
						break;
				case EMOTICON_DEVILTEE:
				case EMOTICON_SPLATTEE:
				case EMOTICON_ZOMG:
						pChr->SetEmoteType(EMOTE_ANGRY);
						break;
					default:
						pChr->SetEmoteType(EMOTE_NORMAL);
						break;
				}
				if (pPlayer->m_SpookyGhostActive)
				{
					pChr->SetEmoteType(EMOTE_SURPRISE);
				}
				pChr->SetEmoteStop(Server()->Tick() + 2 * Server()->TickSpeed());
			}
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if (m_InstaGrenadeRoundEndTickTicker && m_apPlayers[ClientID]->m_IsInstaArena_gdm)
			{
				return; //yy evil silent return
			}
			if (m_InstaRifleRoundEndTickTicker && m_apPlayers[ClientID]->m_IsInstaArena_idm)
			{
				return; //yy evil silent return
			}


			if (m_apPlayers[ClientID]->m_IsBlockTourning)
			{
				if (Server()->TickSpeed() * 5 > m_BlockTournaLobbyTick)
				{
					//silent return selfkill in last 5 secs of lobby tick to prevent the char being dead on tourna start
					return;
				}
			}

			if (m_apPlayers[ClientID]->m_IsBlockWaving && !pPlayer->m_IsBlockWaveWaiting)
			{
				SendChatTarget(ClientID, "[BlockWave] you can't selfkill while block waving. try '/blockwave leave'.");
				return;
			}

			if (m_apPlayers[ClientID]->m_SpawnBlocks > 3 && g_Config.m_SvSpawnBlockProtection == 2)
			{
				SendChatTarget(ClientID, "[SPAWNBLOCK] You can't selfkill because you spawnblock too much. Try agian later.");
				return;
			}

			if (!g_Config.m_SvAllowBombSelfkill && GetPlayerChar(ClientID) && GetPlayerChar(ClientID)->m_IsBombing)
			{
				SendChatTarget(ClientID, "[BOMB] selfkill protection activated. Try '/bomb leave' to leave and get the money back. All other ways of leaving the game are leading to lose your money.");
				return;
			}

			if (m_apPlayers[ClientID]->m_IsSurvivaling)
			{
				if (g_Config.m_SvSurvivalKillProtection == 2) //full on
				{
					SendChatTarget(ClientID, "[SURVIVAL] kill protection. '/survival leave' first to kill.");
					return;
				}
				else if (g_Config.m_SvSurvivalKillProtection == 1 && m_apPlayers[ClientID]->m_IsSurvivalLobby == false) //allowed in lobby
				{
					SendChatTarget(ClientID, "[SURVIVAL] kill protection. '/survival leave' first to kill.");
					return;
				}
				//else == off
			}

			if(m_VoteCloseTime && m_VoteCreator == ClientID && GetDDRaceTeam(ClientID) && (m_VoteKick || m_VoteSpec))
			{
				SendChatTarget(ClientID, "You are running a vote please try again after the vote is done!");
				return;
			}
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*g_Config.m_SvKillDelay > Server()->Tick())
				return;
			if(pPlayer->m_Paused)
				return;

			CCharacter* pChr = pPlayer->GetCharacter();
			if(!pChr)
				return;

			//Kill Protection
			int CurrTime = (Server()->Tick() - pChr->m_StartTime) / Server()->TickSpeed();
			if(g_Config.m_SvKillProtection != 0 && CurrTime >= (60 * g_Config.m_SvKillProtection) && pChr->m_DDRaceState == DDRACE_STARTED)
			{
				SendChatTarget(ClientID, "Kill Protection enabled. If you really want to kill, type /kill");
				return;
			}

			if (m_apPlayers[ClientID]->m_IsInstaArena_fng && pChr->m_FreezeTime)
			{
				SendChatTarget(ClientID, "[INSTA] You can't suicide in fng games while being frozen.");
				return;
			}

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
			pPlayer->Respawn();
		}
	}
	if (MsgID == NETMSGTYPE_CL_STARTINFO)
	{
		if(pPlayer->m_IsReady)
			return;

		CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set start infos
		Server()->SetClientName(ClientID, pMsg->m_pName);
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);
		str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
		pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
		pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
		pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
		//m_pController->OnPlayerInfoChange(pPlayer);

		// send clear vote options
		CNetMsg_Sv_VoteClearOptions ClearMsg;
		Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

		// begin sending vote options
		pPlayer->m_SendVoteIndex = 0;

		// send tuning parameters to client
		SendTuningParams(ClientID, pPlayer->m_TuneZone);

		// client is ready to enter
		if (!pPlayer->m_IsReady)
		{
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	/*CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");*/
	pSelf->ResetTuning();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConTuneZone(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int List = pResult->GetInteger(0);
	const char *pParamName = pResult->GetString(1);
	float NewValue = pResult->GetFloat(2);

	if (List >= 0 && List < NUM_TUNINGZONES)
	{
		if(pSelf->TuningList()[List].Set(pParamName, NewValue))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s in zone %d changed to %.2f", pParamName, List, NewValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
			pSelf->SendTuningParams(-1, List);
		}
		else
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
	}
}

void CGameContext::ConTuneDumpZone(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int List = pResult->GetInteger(0);
	char aBuf[256];
	if (List >= 0 && List < NUM_TUNINGZONES)
	{
		for(int i = 0; i < pSelf->TuningList()[List].Num(); i++)
		{
			float v;
			pSelf->TuningList()[List].Get(i, &v);
			str_format(aBuf, sizeof(aBuf), "zone %d: %s %.2f", List, pSelf->TuningList()[List].m_apNames[i], v);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		}
	}
}

void CGameContext::ConTuneResetZone(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNINGZONES)
		{
			pSelf->TuningList()[List] = TuningParams;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Tunezone %d resetted", List);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
			pSelf->SendTuningParams(-1, List);
		}
	}
	else
	{
		for (int i = 0; i < NUM_TUNINGZONES; i++)
		{
			*(pSelf->TuningList()+i) = TuningParams;
			pSelf->SendTuningParams(-1, i);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "All Tunezones resetted");
	}
}

void CGameContext::ConTuneSetZoneMsgEnter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNINGZONES)
		{
			str_format(pSelf->m_ZoneEnterMsg[List], sizeof(pSelf->m_ZoneEnterMsg[List]), pResult->GetString(1));
		}
	}
}

void CGameContext::ConTuneSetZoneMsgLeave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNINGZONES)
		{
			str_format(pSelf->m_ZoneLeaveMsg[List], sizeof(pSelf->m_ZoneLeaveMsg[List]), pResult->GetString(1));
		}
	}
}

void CGameContext::ConSwitchOpen(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Switch = pResult->GetInteger(0);

	if (pSelf->Collision()->m_NumSwitchers > 0 && Switch >= 0 && Switch < pSelf->Collision()->m_NumSwitchers+1)
	{
		pSelf->Collision()->m_pSwitchers[Switch].m_Initial = false;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "switch %d opened by default", Switch);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	/*if(pSelf->m_pController->IsGameOver())
		return;*/

	pSelf->m_World.m_Paused ^= 1;
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRandomMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int stars = 0;
	if (pResult->NumArguments())
		stars = pResult->GetInteger(0);

	if (pSelf->m_VoteCreator == -1)
	{
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "[DDNet++] error server can't vote random map.");
		return;
	}

	pSelf->m_pScore->RandomMap(pSelf->m_VoteCreator, stars);
}

void CGameContext::ConRandomUnfinishedMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int stars = 0;
	if (pResult->NumArguments())
		stars = pResult->GetInteger(0);

	if (pSelf->m_VoteCreator == -1)
	{
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "[DDNet++] error server can't vote random map.");
		return;
	}

	pSelf->m_pScore->RandomUnfinishedMap(pSelf->m_VoteCreator, stars);
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aBuf[1024];
	str_copy(aBuf, pResult->GetString(0), sizeof(aBuf));

	int i, j;
	for(i = 0, j = 0; aBuf[i]; i++, j++)
	{
		if(aBuf[i] == '\\' && aBuf[i+1] == 'n')
		{
			aBuf[j] = '\n';
			i++;
		}
		else if (i != j)
		{
			aBuf[j] = aBuf[i];
		}
	}
	aBuf[j] = '\0';

	pSelf->SendBroadcast(aBuf, -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	if(Team == TEAM_SPECTATORS)
		pSelf->m_apPlayers[ClientID]->m_Paused = CPlayer::PAUSED_NONE;
	// (void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

	// (void)pSelf->m_pController->CheckTeamBalance();
}
/*
void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;

	int CounterRed = 0;
	int CounterBlue = 0;
	int PlayerTeam = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			++PlayerTeam;
	PlayerTeam = (PlayerTeam+1)/2;

	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			if(CounterRed == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
			else if(CounterBlue == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
			else
			{
				if(rand() % 2)
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
					++CounterBlue;
				}
				else
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
					++CounterRed;
				}
			}
		}
	}

	// (void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
}
*/
void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// start reloading vote option list
	// clear vote options
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);

	// reset sending of vote options
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->m_SendVoteIndex = 0;
	}

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "moderator forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;

	// reset sending of vote options
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->m_SendVoteIndex = 0;
	}
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES_ADMIN;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO_ADMIN;
	pSelf->m_VoteEnforcer = pResult->m_ClientID;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moderator forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	m_ChatPrintCBIndex = Console()->RegisterPrintCallback(0, SendChatResponse, this);

	Console()->Register("tune", "s[tuning] i[value]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");
	Console()->Register("tune_zone", "i[zone] s[tuning] i[value]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneZone, this, "Tune in zone a variable to value");
	Console()->Register("tune_zone_dump", "i[zone]", CFGFLAG_SERVER, ConTuneDumpZone, this, "Dump zone tuning in zone x");
	Console()->Register("tune_zone_reset", "?i[zone]", CFGFLAG_SERVER, ConTuneResetZone, this, "reset zone tuning in zone x or in all zones");
	Console()->Register("tune_zone_enter", "i[zone] s[message]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneSetZoneMsgEnter, this, "which message to display on zone enter; use 0 for normal area");
	Console()->Register("tune_zone_leave", "i[zone] s[message]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneSetZoneMsgLeave, this, "which message to display on zone leave; use 0 for normal area");
	Console()->Register("switch_open", "i['0'|'1']", CFGFLAG_SERVER|CFGFLAG_GAME, ConSwitchOpen, this, "Whether a switch is open by default (otherwise closed)");
	Console()->Register("pause_game", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r[map]", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("random_map", "?i[stars]", CFGFLAG_SERVER, ConRandomMap, this, "Random map");
	Console()->Register("random_unfinished_map", "?i[stars]", CFGFLAG_SERVER, ConRandomUnfinishedMap, this, "Random unfinished map");
	Console()->Register("restart", "?i[seconds]", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r[message]", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r[message]", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "i[id] i[team-id] ?i[delay in minutes]", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i[team-id]", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	//Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	//Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	//Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");

	Console()->Register("add_vote", "s[name] r[command]", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s[name]", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "s[name] s[command] ?r[reason]", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

#define CONSOLE_COMMAND(name, params, flags, callback, userdata, help) m_pConsole->Register(name, params, flags, callback, userdata, help);
#include "game/ddracecommands.h"
#define CHAT_COMMAND(name, params, flags, callback, userdata, help, ddpp_al) m_pConsole->Register(name, params, flags, callback, userdata, help, ddpp_al);
#include "ddracechat.h"
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{

	// ChillerDragon konst constructor
	m_Database->CreateDatabase();
	LoadSinglePlayer();
	m_MapsavePlayers = 0;
	m_MapsaveLoadedPlayers = 0;
	//Friends_counter = 0;
	m_vDropLimit.resize(2);
	m_BalanceID1 = -1;
	m_BalanceID2 = -1;
	m_survivalgamestate = 0;
	m_survival_game_countdown = 0;
	m_BlockWaveGameState = 0;
	m_insta_survival_gamestate = 0;
	m_CucumberShareValue = 10;
	m_BombTick = g_Config.m_SvBombTicks;
	m_BombStartCountDown = g_Config.m_SvBombStartDelay;
	m_WrongRconAttempts = 0;
	str_copy(m_aAllowedCharSet, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:+@-_", sizeof(m_aAllowedCharSet));
	str_copy(m_aLastSurvivalWinnerName, "", sizeof(m_aLastSurvivalWinnerName));

	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	DeleteTempfile();

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// Reset Tunezones
	CTuningParams TuningParams;
	for (int i = 0; i < NUM_TUNINGZONES; i++)
	{
		//-- start comment for m_IsVanillaWeapons --
		//TuningList()[i] = TuningParams;
		//TuningList()[i].Set("gun_curvature", 0.0f);
		//TuningList()[i].Set("gun_speed", 1400.0f);
		//TuningList()[i].Set("shotgun_curvature", 0.0f);
		//TuningList()[i].Set("shotgun_speed", 500.0f);
		//TuningList()[i].Set("shotgun_speeddiff", 0.0f);
		//-- end comment for m_IsVanillaWeapons --

		//-- start add code for m_IsVanillaWeapons --
		TuningList()[i] = TuningParams;
		TuningList()[i].Set("gun_curvature", 0.0f);
		TuningList()[i].Set("gun_speed", 1400.0f);
		Tuning()->Set("shotgun_speed", 2750.00f);
		Tuning()->Set("shotgun_speeddiff", 0.80f);
		Tuning()->Set("shotgun_curvature", 7.00f);
		Tuning()->Set("shotgun_lifetime", 0.20f);
		//-- end add code for m_IsVanillaWeapons --
	}

	for (int i = 0; i < NUM_TUNINGZONES; i++) // decided to send no text on changing Tunezones for now
	{
		str_format(m_ZoneEnterMsg[i], sizeof(m_ZoneEnterMsg[i]), "", i);
		str_format(m_ZoneLeaveMsg[i], sizeof(m_ZoneLeaveMsg[i]), "", i);
	}

	// Reset Tuning
	if(g_Config.m_SvTuneReset)
	{
		ResetTuning();
	}
	else
	{
		//-- start2 comment for m_IsVanillaWeapons --
		//Tuning()->Set("gun_speed", 1400.0f);
		//Tuning()->Set("gun_curvature", 0.0f);
		//Tuning()->Set("shotgun_speed", 500.0f);
		//Tuning()->Set("shotgun_speeddiff", 0.0f);
		//Tuning()->Set("shotgun_curvature", 0.0f);
		//-- end2 comment for m_IsVanillaWeapons --

		//-- start2 add code for m_IsVanillaWeapons --
		Tuning()->Set("gun_speed", 1400.0f);
		Tuning()->Set("gun_curvature", 0.0f);
		//Shotgun tuning by chiller
		Tuning()->Set("shotgun_speed", 2750.00f);
		Tuning()->Set("shotgun_speeddiff", 0.80f);
		Tuning()->Set("shotgun_curvature", 7.00f);
		Tuning()->Set("shotgun_lifetime", 0.20f);
		//-- end2 add code for m_IsVanillaWeapons --
	}

	if(g_Config.m_SvDDRaceTuneReset)
	{
		g_Config.m_SvHit = 1;
		g_Config.m_SvEndlessDrag = 0;
		g_Config.m_SvOldLaser = 0;
		g_Config.m_SvOldTeleportHook = 0;
		g_Config.m_SvOldTeleportWeapons = 0;
		g_Config.m_SvTeleportHoldHook = 0;
		g_Config.m_SvTeam = 1;
		g_Config.m_SvShowOthersDefault = 0;

		if(Collision()->m_NumSwitchers > 0)
			for (int i = 0; i < Collision()->m_NumSwitchers+1; ++i)
				Collision()->m_pSwitchers[i].m_Initial = true;
	}

	Console()->ExecuteFile(g_Config.m_SvResetFile);

	LoadMapPlayerData();
	LoadMapSettings();

/*	// select gametype
	if(str_comp(g_Config.m_SvGametype, "mod") == 0)
		m_pController = new CGameControllerMOD(this);
	else if(str_comp(g_Config.m_SvGametype, "ctf") == 0)
		m_pController = new CGameControllerCTF(this);
	else if(str_comp(g_Config.m_SvGametype, "tdm") == 0)
		m_pController = new CGameControllerTDM(this);
	else
		m_pController = new CGameControllerDM(this);*/
	m_pController = new CGameControllerDDRace(this);
	((CGameControllerDDRace*)m_pController)->m_Teams.Reset();

	if(g_Config.m_SvSoloServer)
	{
		g_Config.m_SvTeam = 3;
		g_Config.m_SvShowOthersDefault = 1;

		Tuning()->Set("player_collision", 0);
		Tuning()->Set("player_hooking", 0);

		for (int i = 0; i < NUM_TUNINGZONES; i++)
		{
			TuningList()[i].Set("player_collision", 0);
			TuningList()[i].Set("player_hooking", 0);
		}
	}

	// delete old score object
	if(m_pScore)
		delete m_pScore;

	// create score object (add sql later)
#if defined(CONF_SQL)
	if(g_Config.m_SvUseSQL)
		m_pScore = new CSqlScore(this);
	else
#endif
	m_pScore = new CFileScore(this);
	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);




	/*
	num_spawn_points[0] = 0;
	num_spawn_points[1] = 0;
	num_spawn_points[2] = 0;
	*/

	CTile *pFront = 0;
	CSwitchTile *pSwitch = 0;
	if(m_Layers.FrontLayer())
		pFront = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(m_Layers.FrontLayer()->m_Front);
	if(m_Layers.SwitchLayer())
		pSwitch = (CSwitchTile *)Kernel()->RequestInterface<IMap>()->GetData(m_Layers.SwitchLayer()->m_Switch);

	int ShopTiles = 0;

	// by fokkonaut from F-DDrace
	Collision()->m_vTiles.clear();
	Collision()->m_vTiles.resize(NUM_INDICES);

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;
			Collision()->m_vTiles[Index].push_back(vec2(x*32.0f+16.0f, y*32.0f+16.0f));
			if(Index == TILE_OLDLASER)
			{
				g_Config.m_SvOldLaser = 1;
				dbg_msg("Game Layer", "Found Old Laser Tile");
			}
			else if(Index == TILE_NPC)
			{
				m_Tuning.Set("player_collision", 0);
				dbg_msg("Game Layer", "Found No Collision Tile");
			}
			else if(Index == TILE_EHOOK)
			{
				g_Config.m_SvEndlessDrag = 1;
				dbg_msg("Game Layer", "Found No Unlimited hook time Tile");
			}
			else if(Index == TILE_NOHIT)
			{
				g_Config.m_SvHit = 0;
				dbg_msg("Game Layer", "Found No Weapons Hitting others Tile");
			}
			else if(Index == TILE_NPH)
			{
				m_Tuning.Set("player_hooking", 0);
				dbg_msg("Game Layer", "Found No Player Hooking Tile");
			}
			else if (Index == TILE_SHOP_SPAWN)
			{
				m_ShopBotTileExists = true;
				dbg_msg("Game Layer", "Found Shop Spawn Tile");
			}
			else if (Index == TILE_SHOP)
			{
				m_ShopBotTileExists = true;
				ShopTiles++;
			}

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				//m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
				((CGameControllerDDRace*)m_pController)->OnEntity(Index-ENTITY_OFFSET, Pos);
				m_pController->OnEntity(Index - ENTITY_OFFSET, Pos, LAYER_GAME, pTiles[y * pTileMap->m_Width + x].m_Flags);
			}

			if(pFront)
			{
				Index = pFront[y * pTileMap->m_Width + x].m_Index;
				Collision()->m_vTiles[Index].push_back(vec2(x*32.0f+16.0f, y*32.0f+16.0f));
				if(Index == TILE_OLDLASER)
				{
					g_Config.m_SvOldLaser = 1;
					dbg_msg("Front Layer", "Found Old Laser Tile");
				}
				else if(Index == TILE_NPC)
				{
					m_Tuning.Set("player_collision", 0);
					dbg_msg("Front Layer", "Found No Collision Tile");
				}
				else if(Index == TILE_EHOOK)
				{
					g_Config.m_SvEndlessDrag = 1;
					dbg_msg("Front Layer", "Found No Unlimited hook time Tile");
				}
				else if(Index == TILE_NOHIT)
				{
					g_Config.m_SvHit = 0;
					dbg_msg("Front Layer", "Found No Weapons Hitting others Tile");
				}
				else if(Index == TILE_NPH)
				{
					m_Tuning.Set("player_hooking", 0);
					dbg_msg("Front Layer", "Found No Player Hooking Tile");
				}
				else if(Index == TILE_JAIL)
				{
					CJail Jail;
					Jail.m_Center = vec2(x,y);
					dbg_msg("game layer", "got Jail tile at (%.2f|%.2f)", Jail.m_Center.x, Jail.m_Center.y);
					m_Jail.push_back(Jail);
				}
				else if(Index == TILE_JAILRELEASE) 
				{
					CJailrelease Jailrelease;
					Jailrelease.m_Center = vec2(x,y);
					dbg_msg("game layer", "got Jailrelease tile at (%.2f|%.2f)", Jailrelease.m_Center.x, Jailrelease.m_Center.y);
					m_Jailrelease.push_back(Jailrelease);
				}
				else if (Index == TILE_BALANCE_BATTLE_1)
				{
					CBalanceBattleTile1 Balancebattle;
					Balancebattle.m_Center = vec2(x,y);
					dbg_msg("game layer", "got balancebattle1 tile at (%.2f|%.2f)", Balancebattle.m_Center.x, Balancebattle.m_Center.y);
					m_BalanceBattleTile1.push_back(Balancebattle);
				}
				else if (Index == TILE_BALANCE_BATTLE_2)
				{
					CBalanceBattleTile2 Balancebattle;
					Balancebattle.m_Center = vec2(x, y);
					dbg_msg("game layer", "got balancebattle2 tile at (%.2f|%.2f)", Balancebattle.m_Center.x, Balancebattle.m_Center.y);
					m_BalanceBattleTile2.push_back(Balancebattle);
				}
				else if (Index == TILE_SURVIVAL_LOBBY)
				{
					CSurvivalLobbyTile Survivallobby;
					Survivallobby.m_Center = vec2(x, y);
					dbg_msg("game layer", "got survival lobby tile at (%.2f|%.2f)", Survivallobby.m_Center.x, Survivallobby.m_Center.y);
					m_SurvivalLobby.push_back(Survivallobby);
				}
				else if (Index == TILE_SURVIVAL_SPAWN)
				{
					CSurvivalSpawnTile Survivalspawn;
					Survivalspawn.m_Center = vec2(x, y);
					dbg_msg("game layer", "got survival spawn tile at (%.2f|%.2f)", Survivalspawn.m_Center.x, Survivalspawn.m_Center.y);
					m_SurvivalSpawn.push_back(Survivalspawn);
				}
				else if (Index == TILE_SURVIVAL_DEATHMATCH)
				{
					CSurvivalDeathmatchTile Survivaldeathmatch;
					Survivaldeathmatch.m_Center = vec2(x, y);
					dbg_msg("game layer", "got survival deathmatch tile at (%.2f|%.2f)", Survivaldeathmatch.m_Center.x, Survivaldeathmatch.m_Center.y);
					m_SurvivalDeathmatch.push_back(Survivaldeathmatch);
				}
				else if (Index == TILE_BLOCKWAVE_BOT)
				{
					CBlockWaveBotTile BlockWaveBot;
					BlockWaveBot.m_Center = vec2(x, y);
					dbg_msg("game layer", "got blockwave bot spawn tile at (%.2f|%.2f)", BlockWaveBot.m_Center.x, BlockWaveBot.m_Center.y);
					m_BlockWaveBot.push_back(BlockWaveBot);
				}
				else if (Index == TILE_BLOCKWAVE_HUMAN)
				{
					CBlockWaveHumanTile BlockWaveHuman;
					BlockWaveHuman.m_Center = vec2(x, y);
					dbg_msg("game layer", "got blockwave Human spawn tile at (%.2f|%.2f)", BlockWaveHuman.m_Center.x, BlockWaveHuman.m_Center.y);
					m_BlockWaveHuman.push_back(BlockWaveHuman);
				}
				else if (Index == TILE_FNG_SCORE)
				{
					CFngScore FngScore;
					FngScore.m_Center = vec2(x, y);
					dbg_msg("game layer", "got fng score tile at (%.2f|%.2f)", FngScore.m_Center.x, FngScore.m_Center.y);
					m_FngScore.push_back(FngScore);
				}
				else if (Index == TILE_BLOCK_TOURNA_SPAWN)
				{
					CBlockTournaSpawn BlockTournaSpawn;
					BlockTournaSpawn.m_Center = vec2(x, y);
					dbg_msg("game layer", "got fng score tile at (%.2f|%.2f)", BlockTournaSpawn.m_Center.x, BlockTournaSpawn.m_Center.y);
					m_BlockTournaSpawn.push_back(BlockTournaSpawn);
				}
				else if (Index == TILE_PVP_ARENA_SPAWN)
				{
					CPVPArenaSpawn PVPArenaSpawn;
					PVPArenaSpawn.m_Center = vec2(x, y);
					dbg_msg("game layer", "got pvp arena spawn tile at (%.2f|%.2f)", PVPArenaSpawn.m_Center.x, PVPArenaSpawn.m_Center.y);
					m_PVPArenaSpawn.push_back(PVPArenaSpawn);
				}
				else if (Index == TILE_VANILLA_MODE)
				{
					CVanillaMode VanillaMode;
					VanillaMode.m_Center = vec2(x, y);
					dbg_msg("game layer", "got vanilla mode tile at (%.2f|%.2f)", VanillaMode.m_Center.x, VanillaMode.m_Center.y);
					m_VanillaMode.push_back(VanillaMode);
				}
				else if (Index == TILE_DDRACE_MODE)
				{
					CDDraceMode DDraceMode;
					DDraceMode.m_Center = vec2(x, y);
					dbg_msg("game layer", "got ddrace mode tile at (%.2f|%.2f)", DDraceMode.m_Center.x, DDraceMode.m_Center.y);
					m_DDraceMode.push_back(DDraceMode);
				}
				else if (Index == TILE_BOTSPAWN_1)
				{
					CBotSpawn1 BotSpawn1;
					BotSpawn1.m_Center = vec2(x, y);
					dbg_msg("game layer", "got botspawn1 tile at (%.2f|%.2f)", BotSpawn1.m_Center.x, BotSpawn1.m_Center.y);
					m_BotSpawn1.push_back(BotSpawn1);
				}
				else if (Index == TILE_BOTSPAWN_2)
				{
					CBotSpawn2 BotSpawn2;
					BotSpawn2.m_Center = vec2(x, y);
					dbg_msg("game layer", "got botspawn2 tile at (%.2f|%.2f)", BotSpawn2.m_Center.x, BotSpawn2.m_Center.y);
					m_BotSpawn2.push_back(BotSpawn2);
				}
				else if (Index == TILE_BOTSPAWN_3)
				{
					CBotSpawn3 BotSpawn3;
					BotSpawn3.m_Center = vec2(x, y);
					dbg_msg("game layer", "got botspawn3 tile at (%.2f|%.2f)", BotSpawn3.m_Center.x, BotSpawn3.m_Center.y);
					m_BotSpawn3.push_back(BotSpawn3);
				}
				else if (Index == TILE_BOTSPAWN_4)
				{
					CBotSpawn4 BotSpawn4;
					BotSpawn4.m_Center = vec2(x, y);
					dbg_msg("game layer", "got botspawn4 tile at (%.2f|%.2f)", BotSpawn4.m_Center.x, BotSpawn4.m_Center.y);
					m_BotSpawn4.push_back(BotSpawn4);
				}
				else if (Index == TILE_NO_HAMMER)
				{
					CNoHammer NoHammer;
					NoHammer.m_Center = vec2(x, y);
					dbg_msg("game layer", "got no hammer tile at (%.2f|%.2f)", NoHammer.m_Center.x, NoHammer.m_Center.y);
					m_NoHammer.push_back(NoHammer);
				}
				else if (Index == TILE_BLOCK_DM_A1)
				{
					CBlockDMA1 BlockDMA1;
					BlockDMA1.m_Center = vec2(x, y);
					dbg_msg("game layer", "got block deathmatch(1) tile at (%.2f|%.2f)", BlockDMA1.m_Center.x, BlockDMA1.m_Center.y);
					m_BlockDMA1.push_back(BlockDMA1);
				}
				else if (Index == TILE_BLOCK_DM_A2)
				{
					CBlockDMA2 BlockDMA2;
					BlockDMA2.m_Center = vec2(x, y);
					dbg_msg("game layer", "got block deathmatch(2) tile at (%.2f|%.2f)", BlockDMA2.m_Center.x, BlockDMA2.m_Center.y);
					m_BlockDMA2.push_back(BlockDMA2);
				}
				if(Index >= ENTITY_OFFSET)
				{
					vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
					m_pController->OnEntity(Index-ENTITY_OFFSET, Pos, LAYER_FRONT, pFront[y*pTileMap->m_Width+x].m_Flags);
				}
			}
			if(pSwitch)
			{
				Index = pSwitch[y*pTileMap->m_Width + x].m_Type;
				// TODO: Add off by default door here
				// if (Index == TILE_DOOR_OFF)
				if(Index >= ENTITY_OFFSET)
				{
					vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
					m_pController->OnEntity(Index-ENTITY_OFFSET, Pos, LAYER_SWITCH, pSwitch[y*pTileMap->m_Width+x].m_Flags, pSwitch[y*pTileMap->m_Width+x].m_Number);
				}
			}
		}
	}
	dbg_msg("Game Layer", "Found Shop Tiles (%d)", ShopTiles);


	//game.world.insert_entity(game.Controller);


	//ChillerDragon
	//dummy_init
	if (g_Config.m_SvBasicDummys)
	{
		CreateBasicDummys();
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::DeleteTempfile()
{
	if(m_aDeleteTempfile[0] != 0)
	{
		IStorage *pStorage = Kernel()->RequestInterface<IStorage>();
		pStorage->RemoveFile(m_aDeleteTempfile, IStorage::TYPE_SAVE);
		m_aDeleteTempfile[0] = 0;
	}
}

void CGameContext::OnMapChange(char *pNewMapName, int MapNameSize)
{
	IStorage *pStorage = Kernel()->RequestInterface<IStorage>();

	char aConfig[128];
	char aTemp[128];
	str_format(aConfig, sizeof(aConfig), "maps/%s.cfg", g_Config.m_SvMap);
	str_format(aTemp, sizeof(aTemp), "%s.temp.%d", pNewMapName, pid());

	IOHANDLE File = pStorage->OpenFile(aConfig, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		// No map-specific config, just return.
		return;
	}
	CLineReader LineReader;
	LineReader.Init(File);

	array<char *> aLines;
	char *pLine;
	int TotalLength = 0;
	while((pLine = LineReader.Get()))
	{
		int Length = str_length(pLine) + 1;
		char *pCopy = (char *)mem_alloc(Length, 1);
		mem_copy(pCopy, pLine, Length);
		aLines.add(pCopy);
		TotalLength += Length;
	}

	char *pSettings = (char *)mem_alloc(TotalLength, 1);
	int Offset = 0;
	for(int i = 0; i < aLines.size(); i++)
	{
		int Length = str_length(aLines[i]) + 1;
		mem_copy(pSettings + Offset, aLines[i], Length);
		Offset += Length;
		mem_free(aLines[i]);
	}

	CDataFileReader Reader;
	Reader.Open(pStorage, pNewMapName, IStorage::TYPE_ALL);

	CDataFileWriter Writer;
	Writer.Init();

	int SettingsIndex = Reader.NumData();
	bool FoundInfo = false;
	for(int i = 0; i < Reader.NumItems(); i++)
	{
		int TypeID;
		int ItemID;
		int *pData = (int *)Reader.GetItem(i, &TypeID, &ItemID);
		// GetItemSize returns item size including header, remove that.
		int Size = Reader.GetItemSize(i) - sizeof(int) * 2;
		CMapItemInfoSettings MapInfo;
		if(TypeID == MAPITEMTYPE_INFO && ItemID == 0)
		{
			FoundInfo = true;
			CMapItemInfoSettings *pInfo = (CMapItemInfoSettings *)pData;
			if(Size >= (int)sizeof(CMapItemInfoSettings))
			{
				if(pInfo->m_Settings > -1)
				{
					SettingsIndex = pInfo->m_Settings;
					char *pMapSettings = (char *)Reader.GetData(SettingsIndex);
					int DataSize = Reader.GetUncompressedDataSize(SettingsIndex);
					if(DataSize == TotalLength && mem_comp(pSettings, pMapSettings, DataSize) == 0)
					{
						// Configs coincide, no need to update map.
						return;
					}
					Reader.UnloadData(pInfo->m_Settings);
				}
				else
				{
					MapInfo = *pInfo;
					MapInfo.m_Settings = SettingsIndex;
					pData = (int *)&MapInfo;
					Size = sizeof(MapInfo);
				}
			}
			else
			{
				*(CMapItemInfo *)&MapInfo = *(CMapItemInfo *)pInfo;
				MapInfo.m_Settings = SettingsIndex;
				pData = (int *)&MapInfo;
				Size = sizeof(MapInfo);
			}
		}
		Writer.AddItem(TypeID, ItemID, Size, pData);
	}

	if(!FoundInfo)
	{
		CMapItemInfoSettings Info;
		Info.m_Version = 1;
		Info.m_Author = -1;
		Info.m_MapVersion = -1;
		Info.m_Credits = -1;
		Info.m_License = -1;
		Info.m_Settings = SettingsIndex;
		Writer.AddItem(MAPITEMTYPE_INFO, 0, sizeof(Info), &Info);
	}

	for(int i = 0; i < Reader.NumData() || i == SettingsIndex; i++)
	{
		if(i == SettingsIndex)
		{
			Writer.AddData(TotalLength, pSettings);
			continue;
		}
		unsigned char *pData = (unsigned char *)Reader.GetData(i);
		int Size = Reader.GetUncompressedDataSize(i);
		Writer.AddData(Size, pData);
		Reader.UnloadData(i);
	}

	dbg_msg("mapchange", "imported settings");
	Reader.Close();
	Writer.OpenFile(pStorage, aTemp);
	Writer.Finish();

	str_copy(pNewMapName, aTemp, MapNameSize);
	str_copy(m_aDeleteTempfile, aTemp, sizeof(m_aDeleteTempfile));
}

void CGameContext::OnShutdown()
{
	DeleteTempfile();
	Console()->ResetServerGameSettings();
	Layers()->Dest();
	Collision()->Dest();
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::LoadMapSettings()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	int Start, Num;
	pMap->GetType(MAPITEMTYPE_INFO, &Start, &Num);
	for(int i = Start; i < Start + Num; i++)
	{
		int ItemID;
		CMapItemInfoSettings *pItem = (CMapItemInfoSettings *)pMap->GetItem(i, 0, &ItemID);
		int ItemSize = pMap->GetItemSize(i) - 8;
		if(!pItem || ItemID != 0)
			continue;

		if(ItemSize < (int)sizeof(CMapItemInfoSettings))
			break;
		if(!(pItem->m_Settings > -1))
			break;

		int Size = pMap->GetUncompressedDataSize(pItem->m_Settings);
		char *pSettings = (char *)pMap->GetData(pItem->m_Settings);
		char *pNext = pSettings;
		while(pNext < pSettings + Size)
		{
			int StrSize = str_length(pNext) + 1;
			Console()->ExecuteLine(pNext, IConsole::CLIENT_ID_GAME);
			pNext += StrSize;
		}
		pMap->UnloadData(pItem->m_Settings);
		break;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map.cfg", g_Config.m_SvMap);
	Console()->ExecuteFile(aBuf, IConsole::CLIENT_ID_NO_GAME);
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}

	if(ClientID > -1)
		m_apPlayers[ClientID]->FakeSnap(ClientID);

}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::SendChatResponseAll(const char *pLine, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	static volatile int ReentryGuard = 0;
	const char *pLineOrig = pLine;

	if(ReentryGuard)
		return;
	ReentryGuard++;

	if(*pLine == '[')
	do
		pLine++;
	while((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0);//remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChat(-1, CHAT_ALL, pLine);

	ReentryGuard--;
}

void CGameContext::SendChatResponse(const char *pLine, void *pUser, bool Highlighted)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	int ClientID = pSelf->m_ChatResponseTargetID;

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	const char *pLineOrig = pLine;

	static volatile int ReentryGuard = 0;

	if(ReentryGuard)
		return;
	ReentryGuard++;

	if(*pLine == '[')
	do
		pLine++;
	while((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0); // remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChatTarget(ClientID, pLine);

	ReentryGuard--;
}

bool CGameContext::PlayerCollision()
{
	float Temp;
	m_Tuning.Get("player_collision", &Temp);
	return Temp != 0.0;
}

bool CGameContext::PlayerHooking()
{
	float Temp;
	m_Tuning.Get("player_hooking", &Temp);
	return Temp != 0.0;
}

float CGameContext::PlayerJetpack()
{
	float Temp;
	m_Tuning.Get("player_jetpack", &Temp);
	return Temp;
}

void CGameContext::OnSetAuthed(int ClientID, int Level)
{
	CServer* pServ = (CServer*)Server();
	if(m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->m_Authed = Level;
		if (Level == CServer::AUTHED_HONEY)
			return;
		char aBuf[512], aIP[NETADDR_MAXSTRSIZE];
		pServ->GetClientAddr(ClientID, aIP, sizeof(aIP));
		str_format(aBuf, sizeof(aBuf), "ban %s %d Banned by vote", aIP, g_Config.m_SvVoteKickBantime);
		if(!str_comp_nocase(m_aVoteCommand, aBuf) && Level > 0)
		{
			m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO_ADMIN;
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "CGameContext", "Aborted vote by admin login.");
		}
		time_t rawtime;
		struct tm* timeinfo;
		char timestr [80];

		time( &rawtime );
		timeinfo = localtime( &rawtime );

		strftime(timestr,sizeof(timestr),"%F %H:%M:%S",timeinfo);
		char aAccID[32];
		aAccID[0] = '\0';
		if (m_apPlayers[ClientID]->IsLoggedIn())
			str_format(aAccID, sizeof(aAccID), "accID=%d ", m_apPlayers[ClientID]->GetAccID());
		str_format(aBuf, sizeof(aBuf), "[%s] level=%d %sip=%s name=%s", timestr, Level, aAccID, aIP, pServ->ClientName(ClientID));
		ddpp_log(DDPP_LOG_AUTH_RCON, aBuf);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "AuthInfo", aBuf); // presist in normal logs to scan logs for illegal authing
		ShowAdminWelcome(ClientID);
		m_WrongRconAttempts = 0;
	}
}

void CGameContext::SendRecord(int ClientID)
{
	CNetMsg_Sv_Record RecordsMsg;
	RecordsMsg.m_PlayerTimeBest = Score()->PlayerData(ClientID)->m_BestTime * 100.0f;
	RecordsMsg.m_ServerTimeBest = m_pController->m_CurrentRecord * 100.0f; //TODO: finish this
	Server()->SendPackMsg(&RecordsMsg, MSGFLAG_VITAL, ClientID);
}

int CGameContext::ProcessSpamProtection(int ClientID)
{
	if(!m_apPlayers[ClientID])
		return 0;
	if(g_Config.m_SvSpamprotection && m_apPlayers[ClientID]->m_LastChat
		&& m_apPlayers[ClientID]->m_LastChat + Server()->TickSpeed() * g_Config.m_SvChatDelay > Server()->Tick())
		return 1;
	else
		m_apPlayers[ClientID]->m_LastChat = Server()->Tick();
	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);
	int Muted = 0;

	for(int i = 0; i < m_NumMutes && !Muted; i++)
	{
		if(!net_addr_comp(&Addr, &m_aMutes[i].m_Addr))
			Muted = (m_aMutes[i].m_Expire - Server()->Tick()) / Server()->TickSpeed();
	}

	if (Muted > 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof aBuf, "You are not permitted to talk for the next %d seconds.", Muted);
		SendChatTarget(ClientID, aBuf);
		return 1;
	}

	if ((m_apPlayers[ClientID]->m_ChatScore += g_Config.m_SvChatPenalty) > g_Config.m_SvChatThreshold)
	{
		Mute(0, &Addr, g_Config.m_SvSpamMuteDuration, Server()->ClientName(ClientID));
		m_apPlayers[ClientID]->m_ChatScore = 0;
		return 1;
	}

	return 0;
}

int CGameContext::GetDDRaceTeam(int ClientID)
{
	CGameControllerDDRace* pController = (CGameControllerDDRace*)m_pController;
	return pController->m_Teams.m_Core.Team(ClientID);
}

void CGameContext::ResetTuning()
{
	//-- start comment for m_IsVanillaWeapons --
	//CTuningParams TuningParams;
	//m_Tuning = TuningParams;
	//Tuning()->Set("gun_speed", 1400);
	//Tuning()->Set("gun_curvature", 0);
	//Tuning()->Set("shotgun_speed", 500);
	//Tuning()->Set("shotgun_speeddiff", 0);
	//Tuning()->Set("shotgun_curvature", 0);
	//SendTuningParams(-1);
	//-- end comment for m_IsVanillaWeapons --

	//-- start add code for m_IsVanillaWeapons --
	//CTuningParams TuningParams;
	//m_Tuning = TuningParams;
	//Tuning()->Set("gun_speed", 1400);
	//Tuning()->Set("gun_curvature", 0);
	//Tuning()->Set("shotgun_speed", 2750.00);
	//Tuning()->Set("shotgun_speeddiff", 0.80);
	//Tuning()->Set("shotgun_curvature", 7.00);
	//Tuning()->Set("shotgun_lifetime", 0.14);
	//SendTuningParams(-1);

	//test value copied from vanilla src (New test from 29.05.2017) looks pretty ok
	CTuningParams TuningParams;
	m_Tuning = TuningParams;
	Tuning()->Set("gun_speed", 1400);
	Tuning()->Set("gun_curvature", 0);
	Tuning()->Set("shotgun_speed", 2750.00f);
	Tuning()->Set("shotgun_speeddiff", 0.80f);
	Tuning()->Set("shotgun_curvature", 1.25f);
	Tuning()->Set("shotgun_lifetime", 0.20f);
	SendTuningParams(-1);
	//-- end add code for m_IsVanillaWeapons --
}

bool CheckClientID2(int ClientID)
{
	dbg_assert(ClientID >= 0 || ClientID < MAX_CLIENTS,
			"The Client ID is wrong");
	if (ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return true;
}

void CGameContext::Whisper(int ClientID, char *pStr)
{
	char *pName;
	char *pMessage;
	int Error = 0;

	if(ProcessSpamProtection(ClientID))
		return;

	pStr = str_skip_whitespaces(pStr);

	int Victim;

	// add token
	if(*pStr == '"')
	{
		pStr++;

		pName = pStr; // we might have to process escape data
		while(1)
		{
			if(pStr[0] == '"')
				break;
			else if(pStr[0] == '\\')
			{
				if(pStr[1] == '\\')
					pStr++; // skip due to escape
				else if(pStr[1] == '"')
					pStr++; // skip due to escape
			}
			else if(pStr[0] == 0)
				Error = 1;

			pStr++;
		}

		// write null termination
		*pStr = 0;
		pStr++;

		for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
			if (str_comp(pName, Server()->ClientName(Victim)) == 0)
				break;

	}
	else
	{
		pName = pStr;
		while(1)
		{
			if(pStr[0] == 0)
			{
				Error = 1;
				break;
			}
			if(pStr[0] == ' ')
			{
				pStr[0] = 0;
				for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
					if (str_comp(pName, Server()->ClientName(Victim)) == 0)
						break;

				pStr[0] = ' ';

				if (Victim < MAX_CLIENTS)
					break;
			}
			pStr++;
		}
	}

	if(pStr[0] != ' ')
	{
		Error = 1;
	}

	*pStr = 0;
	pStr++;

	pMessage = pStr;

	char aBuf[256];

	if (Error)
	{
		str_format(aBuf, sizeof(aBuf), "Invalid whisper");
		SendChatTarget(ClientID, aBuf);
		return;
	}

	if (Victim >= MAX_CLIENTS || !CheckClientID2(Victim))
	{
		str_format(aBuf, sizeof(aBuf), "No player with name \"%s\" found", pName);
		SendChatTarget(ClientID, aBuf);
		return;
	}

	WhisperID(ClientID, Victim, pMessage);
}

void CGameContext::WhisperID(int ClientID, int VictimID, char *pMessage)
{

	if (!CheckClientID2(ClientID))
		return;

	if (!CheckClientID2(VictimID))
		return;

	if (m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_LastWhisperTo = VictimID;

	char aBuf[256];

	if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = CHAT_WHISPER_SEND;
		Msg.m_ClientID = VictimID;
		Msg.m_pMessage = pMessage;
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
		else
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[→ %s] %s", Server()->ClientName(VictimID), pMessage);
		SendChatTarget(ClientID, aBuf);
	}

	if (m_apPlayers[VictimID] && m_apPlayers[VictimID]->m_ClientVersion >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg2;
		Msg2.m_Team = CHAT_WHISPER_RECV;
		Msg2.m_ClientID = ClientID;
		Msg2.m_pMessage = pMessage;
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg2, MSGFLAG_VITAL, VictimID);
		else
			Server()->SendPackMsg(&Msg2, MSGFLAG_VITAL|MSGFLAG_NORECORD, VictimID);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[← %s] %s", Server()->ClientName(ClientID), pMessage);
		SendChatTarget(VictimID, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "['%s' -> '%s'] %s", Server()->ClientName(ClientID), Server()->ClientName(VictimID), pMessage);
	dbg_msg("whisper", "%s", aBuf);
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_apPlayers[i] && i != VictimID && i != ClientID)
		{
			if (Server()->IsAuthed(i) && m_apPlayers[i]->m_Authed == CServer::AUTHED_ADMIN)
			{
				SendChatTarget(i, aBuf);
			}
		}
	}
}

void CGameContext::Converse(int ClientID, char *pStr)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer)
		return;

	if(ProcessSpamProtection(ClientID))
		return;

	if (pPlayer->m_LastWhisperTo < 0)
		SendChatTarget(ClientID, "You do not have an ongoing conversation. Whisper to someone to start one");
	else
	{
		WhisperID(ClientID, pPlayer->m_LastWhisperTo, pStr);
	}
}

void CGameContext::List(int ClientID, const char* filter)
{
	int total = 0;
	char buf[256];
	int bufcnt = 0;
	if (filter[0])
		str_format(buf, sizeof(buf), "Listing players with \"%s\" in name:", filter);
	else
		str_format(buf, sizeof(buf), "Listing all players:", filter);
	SendChatTarget(ClientID, buf);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			total++;
			const char* name = Server()->ClientName(i);
			if (str_find_nocase(name, filter) == NULL)
				continue;
			if (bufcnt + str_length(name) + 4 > 256)
			{
				SendChatTarget(ClientID, buf);
				bufcnt = 0;
			}
			if (bufcnt != 0)
			{
				str_format(&buf[bufcnt], sizeof(buf) - bufcnt, ", %s", name);
				bufcnt += 2 + str_length(name);
			}
			else
			{
				str_format(&buf[bufcnt], sizeof(buf) - bufcnt, "%s", name);
				bufcnt += str_length(name);
			}
		}
	}
	if (bufcnt != 0)
		SendChatTarget(ClientID, buf);
	str_format(buf, sizeof(buf), "%d players online", total);
	SendChatTarget(ClientID, buf);
}
