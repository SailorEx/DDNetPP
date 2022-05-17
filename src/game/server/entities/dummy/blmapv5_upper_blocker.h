#ifndef GAME_SERVER_ENTITIES_DUMMY_BLMAPV5_UPPER_BLOCKER_H
#define GAME_SERVER_ENTITIES_DUMMY_BLMAPV5_UPPER_BLOCKER_H

#include "dummybase.h"

class CDummyBlmapV5UpperBlocker : public CDummyBase
{
public:
	CDummyBlmapV5UpperBlocker(class CCharacter *pChr, class CPlayer *pPlayer);
	virtual void OnTick();
	virtual ~CDummyBlmapV5UpperBlocker(){};

private:
	bool m_move_left;
};

#endif