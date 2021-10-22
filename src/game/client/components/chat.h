/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H
#include <engine/shared/ringbuffer.h>
#include <game/client/component.h>
#include <game/client/lineinput.h>

class CChat : public CComponent
{
	CLineInput m_Input;

	enum
	{
		MAX_LINES = 25,
	};

	struct CLine
	{
		int64 m_Time;
		float m_YOffset[2];
		int m_ClientID;
		int m_Team;
		int m_NameColor;
		char m_aName[64];
		char m_aText[512];
		bool m_Friend;
		bool m_Highlighted;

		int m_TextContainerIndex;
		float m_TextYOffset;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;

	// chat
	enum
	{
		MODE_NONE=0,
		MODE_ALL,
		MODE_TEAM,

		CHAT_SERVER=0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_InputUpdate;
	int m_ChatStringOffset;
	int m_OldChatStringLength;
	int m_CompletionChosen;
	char m_aCompletionBuffer[256];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;

	struct CCommand
	{
		const char *pName;
		const char *pParams;

		bool operator <(const CCommand &Other) const { return str_comp(pName, Other.pName) < 0; }
		bool operator <=(const CCommand &Other) const { return str_comp(pName, Other.pName) <= 0; }
		bool operator ==(const CCommand &Other) const { return str_comp(pName, Other.pName) == 0; }
	};

	sorted_array<CCommand> m_Commands;
	bool m_ReverseTAB;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	TStaticRingBuffer<CHistoryEntry, 64*1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64 m_LastChatSend;
	int64 m_aLastSoundPlayed[CHAT_NUM];

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);

public:
	CChat();

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientID, int Team, const char *pLine);
	void EnableMode(int Team);
	void Say(int Team, const char *pLine);
	void SayChat(const char *pLine);
	void RegisterCommand(const char *pName, const char *pParams, int flags, const char *pHelp);
	void Echo(const char *pString);

	virtual void OnWindowResize();
	virtual void OnReset();
	virtual void OnConsoleInit();
	virtual void OnStateChange(int NewState, int OldState);
	virtual void OnRender();
	virtual void OnPrepareLines();
	virtual void OnRelease();
	virtual void OnMessage(int MsgType, void *pRawMsg);
	virtual bool OnInput(IInput::CEvent Event);
};
#endif
