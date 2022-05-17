// blmapv5

#include "blmapv5_upper_blocker.h"

#include "../character.h"
#include <base/math_ddpp.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#define X (GetPos().x / 32)
#define Y (GetPos().y / 32)
#define RAW(pos) ((pos)*32)

CDummyBlmapV5UpperBlocker::CDummyBlmapV5UpperBlocker(class CCharacter *pChr, class CPlayer *pPlayer) :
	CDummyBase(pChr, pPlayer)
{
}

void CDummyBlmapV5UpperBlocker::OnTick()
{
	Jump(0);
	Fire(0);
	Hook(0);
	StopMoving();
	Aim(45, 45);
	int bot_x = GetPos().x + g_Config.m_SvDummyMapOffsetX * 32;

	if(GetPos().y > 38 * 32) //too low
	{
		Die();
	}

	if(bot_x > 16 * 32 && GetPos().y < 27 * 32) //lovley wayblock spot
	{
	}
	else //not at lovley wayblock spot
	{
		if(Server()->Tick() % 420 == 0)
		{
			if(m_pCharacter->m_FreezeTime && IsGrounded()) //stuck ?
			{
				Die();
			}
		}

		if(m_move_left)
		{
			Left();

			//failed?
			if(bot_x < 22 * 32)
			{
				m_move_left = false;
			}
		}
		else
		{
			Right();

			if(bot_x > 10 * 32 && bot_x < 17 * 32) //jump in tunnel
			{
				Jump();
				if(Server()->Tick() % 20 == 0)
				{
					SetWeapon(3);
				}
			}

			if(bot_x > 28 * 32 && IsGrounded())
			{
				Jump();
			}

			if(GetPos().y < 31 * 32 - 10) //dont touch the roof
			{
				Hook();
			}
			if(bot_x > 35 * 32 && GetVel().x == 0.000000f) //hit the rocketjump wall
			{
				Jump();
				if(GetVel().y < -1.1f && m_pCharacter->m_FreezeTime == 0)
				{
					Fire();
					m_move_left = true;
				}
			}
		}
	}
}
