#include "stdafx.h"

#include "PluginController.h"

#include <fstream>

#include "KeyDlgProc.h"
#include "resource.h"
#include "WtwMsg.h"
#include "..\common\StringUtils.h"

namespace wtwGPG 
{

	namespace menu
	{
		static const wchar_t CRYPTED[] = L"wtwGPG/crypted";
		static const wchar_t CRYPT[] = L"wtwGPG/crypt";
		static const wchar_t DONT_CRYPT[] = L"wtwGPG/dontCrypt";
		static const wchar_t SET_KEY[] = L"wtwGPG/setKey";
	};

	namespace text
	{
		static const wchar_t CRYPTED[] = L"GPG - Rozmowa szyfrowana";
		static const wchar_t CRYPT[] = L"GPG - Szyfruj";
		static const wchar_t DONT_CRYPT[] = L"GPG - Nie szyfruj";
		static const wchar_t SET_KEY[] = L"GPG - Ustaw klucz publiczny";
	};

	const wchar_t* MDL = L"GPG";
	const wchar_t* PRIVATE_KEY_SETTING_ID = L"wtwGPG/PrivateKeyID";

	int PluginController::onLoad(DWORD callReason, WTWFUNCTIONS *fn)
	{
		wtw = fn;
#ifdef _DEBUG
		_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
		createHooks();
		createMenu();

		sett = new wtwUtils::Settings(wtw, hInst);
		sett->read();

		gpgwrapper = new wtwGPG::GPGWrapper(getPrivateKeyId(callReason).c_str());

		return 0;
	}

	void PluginController::createHooks()
	{
		protoEventHook = wtw->evHook(WTW_ON_PROTOCOL_EVENT, onProtocolEvent, this);
		msgProcHook = wtw->evHook(WTW_EVENT_CHATWND_BEFORE_MSG_PROC, onMessage, this);
		menuRebuildHook = wtw->evHook(WTW_EVENT_MENU_REBUILD, onMenuRebuild, this);
		wndCreateHook = wtw->evHook(WTW_EVENT_ON_CHATWND_CREATE, onWndCreated, this);
	}

	void PluginController::createMenu()
	{
		wtwMenuItemDef menuDef;
		menuDef.menuID = WTW_MENU_ID_CONTACT;
		menuDef.callback = onCryptMenuClick;
		menuDef.itemId = menu::CRYPT;
		menuDef.menuCaption = text::CRYPT;
		menuDef.iconId = WTW_GRAPH_ID_ENCRYPTION;
		wtw->fnCall(WTW_MENU_ITEM_ADD, menuDef, NULL);

		menuDef.itemId = menu::DONT_CRYPT;
		menuDef.menuCaption = text::DONT_CRYPT;
		menuDef.iconId = NULL;
		wtw->fnCall(WTW_MENU_ITEM_ADD, menuDef, NULL);

		menuDef.callback = onSetKeyMenuClick;
		menuDef.itemId = menu::SET_KEY;
		menuDef.menuCaption = text::SET_KEY;
		menuDef.iconId = NULL;
		wtw->fnCall(WTW_MENU_ITEM_ADD, menuDef, NULL);
	}

	std::string PluginController::getPrivateKeyId(DWORD callReason)
	{
		std::string privateKey = sett->getStr(PRIVATE_KEY_SETTING_ID);

		if (callReason != WTW_CALL_REASON_MANUAL && callReason != WTW_CALL_REASON_AUTOADD)
			return privateKey;
		
		INT_PTR dlgRes = DialogBoxParam(
			hInst
			, MAKEINTRESOURCE(IDD_PRIVATE_KEY_DLG)
			, 0
			, KeyDlgProc
			, reinterpret_cast<LPARAM>(privateKey.c_str())
			);

		if (dlgRes == NULL || dlgRes == -1)
			return privateKey;
		
		char* key = reinterpret_cast<char*>(dlgRes);

		privateKey = key;
		sett->setStr(PRIVATE_KEY_SETTING_ID, strUtils::convertEnc(key).c_str());
		sett->write();

		free(key); // _strdup

		return privateKey;
	}

	PluginController::ReciverIdToGPG::iterator PluginController::getKeyFromSettings(const wchar_t*  contactId)
	{
		wchar_t settKey[512];
		swprintf_s(settKey, 511, L"wtwGPGKey/%s", contactId);

		PluginController& pc = PluginController::getInstance();
		wtwUtils::Settings* settings = pc.getSettings();
		settings->read();
		std::string key = settings->getStr(settKey);

		ReciverIdToGPG& ciphered = pc.getCiphered();
		if (key.size() <= 0)
			return ciphered.end();
		
		GPGData data;
		data.keyId = key;
		data.encryptionOn = true;
		return ciphered.insert(std::make_pair(contactId, data)).first;
	}

	int PluginController::onUnload()
	{
		deleteMenu();
		deleteHooks();

		wtw->fnCall(WTW_CCB_FUNCT_CLEAR, reinterpret_cast<WTW_PARAM>(hInst), 0);

		if(sett)
			delete sett;

		if (gpgwrapper)
			delete gpgwrapper;

		return 0;
	}

	void PluginController::deleteHooks()
	{
		if (protoEventHook)
			wtw->evUnhook(protoEventHook);

		if (msgProcHook)
			wtw->evUnhook(msgProcHook);

		if (menuRebuildHook)
			wtw->evUnhook(menuRebuildHook);

		if (wndCreateHook)
			wtw->evUnhook(wndCreateHook);
	}

	void PluginController::deleteMenu()
	{
		wtwMenuItemDef menuDef;
		menuDef.menuID = WTW_MENU_ID_CONTACT;
		menuDef.itemId = menu::CRYPT;
		wtw->fnCall(WTW_MENU_ITEM_DELETE, menuDef, NULL);

		menuDef.itemId = menu::DONT_CRYPT;
		wtw->fnCall(WTW_MENU_ITEM_DELETE, menuDef, NULL);

		menuDef.itemId = menu::SET_KEY;
		wtw->fnCall(WTW_MENU_ITEM_DELETE, menuDef, NULL);
	}

	WTW_PTR PluginController::onProtocolEvent(WTW_PARAM wParam, WTW_PARAM lParam, void *cbData)
	{
		wtwProtocolEvent *ev = reinterpret_cast<wtwProtocolEvent*>(wParam);

		if (ev->event != WTW_PEV_MESSAGE_RECV || ev->type != WTW_PEV_TYPE_BEFORE)
			return S_OK;

		wtwMessageDef *msg = (wtwMessageDef*)lParam;
		if(!msg) 
			return S_OK;

		if(!(msg->msgFlags & WTW_MESSAGE_FLAG_INCOMING)) // only incoming
			return S_OK;

		if(!(msg->msgFlags & WTW_MESSAGE_FLAG_CHAT_MSG)) // only messages
			return S_OK;

		wtwContactDef& cnt = msg->contactData;
		if(!cnt.id || !cnt.netClass)
			return S_OK;
									
		wchar_t	id[512] = {0}; // contact unique id
		swprintf_s(id, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);				

		PluginController& pc = PluginController::getInstance();
		ReciverIdToGPG& ciphered = pc.getCiphered();
		ReciverIdToGPG::iterator it = ciphered.find(id);

		if(it == ciphered.end())
		{
			it = getKeyFromSettings(id);

			if (it == ciphered.end())
				return S_OK;
		}

		if (!it->second.encryptionOn)
			return S_OK; // this message is not to be encrypted
			
		std::wstring message;
		GPGWrapperDecryptedData decrypted;

		try
		{
			decrypted = pc.getGPG().decrypt(strUtils::convertEnc(msg->msgMessage));
			message = strUtils::convertEnc(decrypted.data);
		}
		catch (const GPGWrapperError& e)
		{
			message = e.errorMessage;
			message += L"\nOrginalna wiadomość:\n";
			message += msg->msgMessage;
		}

		WTWFUNCTIONS* wtw = pc.getWTWFUNCTIONS();

		WtwMsg sendData(*msg);

		if (!decrypted.isSignValid)
		{
			WtwMsg notValid(sendData);
			notValid.get().msgFlags = WTW_MESSAGE_FLAG_CHAT_MSG | WTW_MESSAGE_FLAG_WARNING;
			notValid.setMessage(L"Uwaga. Nie poprawny podpis wiadomości.");
			wtw->fnCall(WTW_CHATWND_SHOW_MESSAGE, notValid.get(), NULL);
		}

		sendData.setMessage(message.c_str());
		wtw->fnCall(WTW_CHATWND_SHOW_MESSAGE, sendData.get(), NULL);
		return S_FALSE;
	}

	WTW_PTR PluginController::onMessage(WTW_PARAM wParam, WTW_PARAM lParam, void *ptr)
	{
		wtwBmpStruct *pBmp = reinterpret_cast<wtwBmpStruct*>(wParam);

		if (!pBmp || !pBmp->message.msgMessage) 
			return BMP_OK;

		if(!(pBmp->message.msgFlags & WTW_MESSAGE_FLAG_OUTGOING)) // only outgoing
			return BMP_OK;

		if(!(pBmp->message.msgFlags & WTW_MESSAGE_FLAG_CHAT_MSG)) // only messages
			return BMP_OK;

		if(pBmp->message.msgFlags & WTW_MESSAGE_FLAG_PICTURE) // no pictures
			return BMP_OK;

		if(pBmp->message.msgFlags & WTW_MESSAGE_FLAG_CONFERENCE) // no conferences
			return BMP_OK;

		wtwContactDef& cnt = pBmp->message.contactData;
		if(!cnt.netClass || !cnt.id)
			return BMP_OK;

		PluginController& pc = PluginController::getInstance();
		wchar_t	id[512] = {0}; // contact unique id
		swprintf_s(id, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);
		
		ReciverIdToGPG& ciphered = pc.getCiphered();
		ReciverIdToGPG::iterator it = ciphered.find(id);
		if(it == ciphered.end() || !it->second.encryptionOn) 
			return S_OK; // this message is not to be encrypted
		
		std::wstring msg(pBmp->message.msgMessage);
		
		msg = strUtils::convertEnc(pc.getGPG().encrypt(it->second.keyId, strUtils::convertEnc(msg)));

		wchar_t fnSendMsg[512];
		swprintf_s(fnSendMsg, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, WTW_PF_MESSAGE_SEND);

		WTWFUNCTIONS* wtw = pc.getWTWFUNCTIONS();

		WtwMsg encrypted(pBmp->message);
		encrypted.setMessage(msg.c_str());

		WTW_PTR ret = wtw->fnCall(fnSendMsg, encrypted.get(), 0);
		if (ret != S_OK)
			return BMP_OK; // forward

		wtw->fnCall(WTW_CHATWND_SHOW_MESSAGE, reinterpret_cast<WTW_PARAM>(&pBmp->message), NULL);
		return BMP_NO_PROCESS; // we have eaten it
	}

	WTW_PTR PluginController::onMenuRebuild(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr)
	{
		wtwMenuCallbackEvent* ev = reinterpret_cast<wtwMenuCallbackEvent*>(wParam);
		if(!ev || !ev->pInfo)
			return E_INVALIDARG;

		if(ev->pInfo->iContacts != 1)
			return 0;

		wtwContactDef& cnt = ev->pInfo->pContacts[0];
		if(!cnt.netClass || !cnt.id)
			return E_INVALIDARG;

		PluginController& pc = PluginController::getInstance();
		wtwPresenceDef pr;
		wchar_t fn[512] = {0};

		// get status
		swprintf_s(fn, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, WTW_PF_STATUS_GET);
		pc.getWTWFUNCTIONS()->fnCall(fn, pr, NULL);

		if(pr.curStatus != WTW_PRESENCE_OFFLINE) // add ony if connected to that network
		{ 
			// check if cnt is crypted or not
			swprintf_s(fn, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);

			ReciverIdToGPG& ciphered = pc.getCiphered();
			ReciverIdToGPG::iterator it = ciphered.find(fn);
			if(it == ciphered.end() || !it->second.encryptionOn)
				ev->slInt.add(ev->itemsToShow, menu::CRYPT);
			else 
				ev->slInt.add(ev->itemsToShow, menu::DONT_CRYPT);

			ev->slInt.add(ev->itemsToShow, menu::SET_KEY);
		}

		return 0;
	}

	WTW_PTR PluginController::onCryptMenuClick(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr)
	{		
		wtwMenuItemDef* menuItem = reinterpret_cast<wtwMenuItemDef*>(wParam);
		wtwMenuPopupInfo* menuPopupInfo = reinterpret_cast<wtwMenuPopupInfo*>(lParam);
		
		if(!menuItem || !menuPopupInfo || !menuPopupInfo->pContacts)
			return E_INVALIDARG;

		if(menuPopupInfo->iContacts != 1)
			return 0;

		wtwContactDef& cnt = menuPopupInfo->pContacts[0];
		if(!cnt.netClass || !cnt.id)
			return E_INVALIDARG;

		PluginController& pc = PluginController::getInstance();
		if(!pc.getSettings())
			return E_ABORT;

		wchar_t cntId[512] = {0};
		swprintf_s(cntId, 511, L"%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);

		ReciverIdToGPG& ciphered = pc.getCiphered();
		ReciverIdToGPG::iterator it = ciphered.find(cntId);

		if (it == ciphered.end())
		{
			MessageBox(pc.getChatWindowHook(), L"Nie przypisano klucza do kontaktu.", L"GPG", MB_OK | MB_ICONINFORMATION | MB_APPLMODAL);
		}
		else if (it->second.encryptionOn)
		{
			it->second.encryptionOn = false;
			pc.setSecuredButton(cnt, false, NULL);
		}
		else
		{
			it->second.encryptionOn = true;
			pc.setSecuredButton(cnt, true, NULL);
		}

		return 0;
	}

	WTW_PTR PluginController::onSetKeyMenuClick(WTW_PARAM wParam, WTW_PARAM lParam, void* ptr)
	{
		wtwMenuItemDef* menuItem = reinterpret_cast<wtwMenuItemDef*>(wParam);
		wtwMenuPopupInfo* menuPopupInfo = reinterpret_cast<wtwMenuPopupInfo*>(lParam);

		if (!menuItem || !menuPopupInfo || !menuPopupInfo->pContacts)
			return E_INVALIDARG;

		if (menuPopupInfo->iContacts != 1)
			return 0;

		wtwContactDef& cnt = menuPopupInfo->pContacts[0];
		if (!cnt.netClass || !cnt.id)
			return E_INVALIDARG;

		PluginController& pc = PluginController::getInstance();
		if (!pc.getSettings())
			return E_ABORT;

		wchar_t cntId[512] = { 0 };
		swprintf_s(cntId, 512, L"%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);

		std::string privateKey;

		ReciverIdToGPG& ciphered = pc.getCiphered();
		ReciverIdToGPG::iterator it = ciphered.find(cntId);
		if (it != ciphered.end())
			privateKey = it->second.keyId;

		HINSTANCE hInst = pc.getDllHINSTANCE();

		INT_PTR dlgRes = DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_PUBLIC_KEY_DLG), 0, KeyDlgProc, reinterpret_cast<LPARAM>(privateKey.c_str()));

		if (dlgRes == NULL || dlgRes == -1)
			return 0;
		
		char* key = reinterpret_cast<char*>(dlgRes);
		wchar_t settKey[512] = { 0 };
		swprintf_s(settKey, 511, L"wtwGPGKey/%s/%d/%s", cnt.netClass, cnt.netId, cnt.id);

		ciphered[cntId].keyId = key;
		pc.getSettings()->setStr(settKey, strUtils::convertEnc(key).c_str());
		pc.getSettings()->write();
		free(key); // _strdup
		return 0;
	}

	void PluginController::setSecuredButton(const wtwContactDef& cnt, bool secured, void* pWnd)
	{
		wtwCommandEntry	entry;		
		if(pWnd)
			entry.pWnd = pWnd;
		else
			entry.pContactData = const_cast<wtwContactDef*>(&cnt);
		entry.itemId = menu::CRYPTED;
		entry.hInstance = PluginController::getInstance().getDllHINSTANCE();

		WTWFUNCTIONS* wtw = PluginController::getInstance().getWTWFUNCTIONS();
		if(secured) 
		{
			entry.itemFlags = CCB_FLAG_CHANGECAPTION|CCB_FLAG_CHANGEICON|CCB_FLAG_CHANGETIP;
			entry.itemType = CCB_TYPE_STANDARD;
			entry.graphId = WTW_GRAPH_ID_ENCRYPTION;
			entry.toolTip = text::CRYPTED;
			wtw->fnCall(WTW_CCB_FUNCT_ADD, entry, 0);
		} 
		else 
		{
			if(wtw->fnCall(WTW_CCB_FUNCT_DEL, entry, 0) != S_OK)
				__LOG_F(wtw, WTW_LOG_LEVEL_ERROR, MDL, L"Could not remove button");
		}
	}

	WTW_PTR PluginController::onWndCreated(WTW_PARAM wParam, WTW_PARAM lParam, void *cbData)
	{
		wtwContactDef* cnt = reinterpret_cast<wtwContactDef*>(wParam);
		wtwChatWindowInfo* info = reinterpret_cast<wtwChatWindowInfo*>(lParam);

		if(!info || !cnt || !cnt->id || !cnt->netClass || !info->pWnd)
			return E_INVALIDARG;

		PluginController& pc = PluginController::getInstance();
		pc.setChatWindowHook(info->hWindow);

		if(info->iContacts != 1)
			return 0;

		if(!pc.getSettings())
			return E_ABORT;

		wchar_t	cntId[512] = {0}; // contact unique id
		swprintf_s(cntId, 511, L"%s/%d/%s", cnt->netClass, cnt->netId, cnt->id);

		ReciverIdToGPG& ciphered = pc.getCiphered();
		ReciverIdToGPG::iterator it = ciphered.find(cntId);
		if (it != ciphered.end())
		{
			if (it->second.encryptionOn)
				pc.setSecuredButton(*cnt, true, info->pWnd);

			return 0;
		}

		if(getKeyFromSettings(cntId) != ciphered.end())
			pc.setSecuredButton(*cnt, true, info->pWnd);

		return 0;
	}
};
