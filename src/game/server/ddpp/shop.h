// ddnet++ 2021

#ifndef GAME_SERVER_DDPP_SHOP_H
#define GAME_SERVER_DDPP_SHOP_H

#include <engine/shared/config.h>

#include <vector>

class CShopItem
{
protected:
	int m_Price;
	int m_NeededLevel;

	/*
        m_Active

        Shop items can be deactivated in rcon
        then they wont be listed in the shop
    */
	bool m_Active;

	/*
        m_aTitle

        fancy formatted title shown in the motd
        shop menu
    */
	char m_aTitle[64];

	/*
        m_aName

        Short name shown the compressed shop
        and used for /buy commands
    */
	char m_aName[64];

	/*
        m_aDescription

        Description shown in the motd shop menu
    */
	char m_aDescription[1024];

	/*
        m_aOwnUntil

        String holding the time as human readable string
        for how long the shop item can be used.

        values:

            dead
            disconnect
            forever
    */
	char m_aOwnUntil[64];

	/*
        m_aPriceStr

        Holding the m_Price as string.
        Make it more readable by splitting every 3 digits.
    */
	char m_aPriceStr[64];

	/*
        m_aNeededLevelStr

        Holding the m_NeededLevel as string.

        Can also hold a police level as value in the format of
        Police[level]
    */
	char m_aNeededLevelStr[64];

public:
	CShopItem(
		const char *pName,
		const char *pPrice,
		int Level,
		const char *pDescription,
		const char *pOwnedUntil);

	int Price();
	const char *PriceStr() { return m_aPriceStr; };
	int NeededLevel() { return m_NeededLevel; }
	virtual const char *NeededLevelStr(int ClientID);
	const char *Name() { return m_aName; }
	const char *Title();
	const char *Description() { return m_aDescription; }
	const char *OwnUntil() { return m_aOwnUntil; }

	bool IsActive() { return m_Active; }
	void Deactivate() { m_Active = false; }
	void Activate() { m_Active = true; }
	virtual bool CanBuy(int ClientID);
};

class CShopItemRoomKey : public CShopItem
{
public:
	using CShopItem::CShopItem;

	int Price();
};

class CShopItemTaser : public CShopItem
{
	char m_aNeededPoliceRank[64];

public:
	using CShopItem::CShopItem;

	int NeededPoliceRank(int ClientID);
	virtual const char *NeededLevelStr(int ClientID) override;
	bool CanBuy(int ClientID);
};

class CGameContext;

class CShop
{
	CGameContext *m_pGameContext;

public:
	CShop(class CGameContext *pGameContext);

	std::vector<CShopItem *> m_vItems;

	void OnInit();
	void ShowShopMotdCompressed(int ClientID);
};

#endif