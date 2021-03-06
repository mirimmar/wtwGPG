#pragma once

#include "stdafx.h"
#include "../common/Settings.h"
#include "GPGWrapper.h"

namespace wtwGPG 
{

/** Singleton */
class PluginController
{

	struct GPGData
	{
		GPGData() : encryptionOn(false)
		{
		}
		std::string keyId;
		bool encryptionOn;
	};
	typedef std::map<std::wstring, GPGData> ReciverIdToGPG;

public:

	static PluginController& getInstance()
	{
		static PluginController instance;
		return instance;
	}

	GPGWrapper& getGPG()
	{
		return *gpgwrapper;
	}

	int onLoad(DWORD callReason, WTWFUNCTIONS *fn);

	int onUnload();

	inline const WTWPLUGINFO* getPlugInfo()
	{
		static WTWPLUGINFO _plugInfo = {
			sizeof(WTWPLUGINFO),						// struct size
			L"wtwGPG",									// plugin name
			L"Szyfrowanie wiadomości za pomoca GPG",	// plugin description
			L"© 2015 Łukasz Palczewski",				// copyright
			L"Łukasz Palczewski",						// author
			L"",										// authors contact
			L"",										// authors webpage
			L"",										// url to xml with autoupdate data
			PLUGIN_API_VERSION,							// api version
			MAKE_QWORD(1, 0, 0, 1),						// plugin version
			WTW_CLASS_UTILITY,							// plugin class
			NULL,										// function called after "O wtyczce..." pressed
			L"{A368430B-C52E-4140-80D1-6040181C1457}",	// guid
			NULL,										// dependencies (list of guids)
			0,											// options
			0, 0, 0										// reserved
		};
		return &_plugInfo;
	}

	inline void setDllHINSTANCE(const HINSTANCE h) 
	{
		hInst = h;
	}

	inline HINSTANCE getDllHINSTANCE() const 
	{
		return hInst;
	}

	inline WTWFUNCTIONS* getWTWFUNCTIONS() const 
	{
		return wtw;
	}

	inline ReciverIdToGPG& getCiphered()
	{
		return ciphered;
	}

	inline wtwUtils::Settings* getSettings()
	{
		return sett;
	}

	inline HWND getChatWindowHook()
	{
		return chatWindowHook;
	}

	inline void setChatWindowHook(HWND h)
	{
		chatWindowHook = h;
	}

private:
	WTWFUNCTIONS*	wtw;
	HINSTANCE		hInst;

	ReciverIdToGPG ciphered;

	HANDLE			protoEventHook;
	HANDLE			msgProcHook;
	HANDLE			menuRebuildHook;
	HANDLE			wndCreateHook;

	HWND chatWindowHook;

	wtwUtils::Settings*	sett;

	GPGWrapper* gpgwrapper;

	PluginController() : wtw(NULL), hInst(NULL),
		protoEventHook(NULL), menuRebuildHook(NULL),
		msgProcHook(NULL), wndCreateHook(NULL),
		sett(NULL), chatWindowHook(NULL), gpgwrapper(NULL)
	{}
	PluginController(const PluginController&);

	static WTW_PTR onProtocolEvent(WTW_PARAM wParam, WTW_PARAM lParam, void *cbData);
	static WTW_PTR onWndCreated(WTW_PARAM wParam, WTW_PARAM lParam, void *cbData);
	static WTW_PTR onMessage(WTW_PARAM wParam, WTW_PARAM lParam, void *ptr);
	static WTW_PTR onMenuRebuild(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr);
	static WTW_PTR onCryptMenuClick(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr);
	static WTW_PTR onSetKeyMenuClick(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr);

	static void setSecuredButton(const wtwContactDef& cnt, bool secured, void* pWnd);

	void createHooks();
	void deleteHooks();
	void createMenu();
	void deleteMenu();

	std::string getPrivateKeyId(DWORD callReason);

	static ReciverIdToGPG::iterator getKeyFromSettings(const wchar_t* contactId);

}; // class PluginController

}; // namespace wtwGPG
