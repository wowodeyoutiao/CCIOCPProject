/**************************************************************************************
@author: 陈昌
@content: Dispatch对客户端连接的监听socket管理
**************************************************************************************/
#include "stdafx.h"
#include "CClientServerSocket.h"
#include "CDBServerSocket.h"

using namespace CC_UTILS;

const std::string IPCONFIG_FILE = "ipaddress.txt";        // IP配置文件
const int MAX_CONNECT_TIMEOUT = 30 * 1000;                // 最长的连接时间
const int DELAY_DISCONNECT_TIME = 3000;                   // 延时断开时间
const std::string War_Warning = "  本游戏区是自由对战模式，遵守游戏规则，可获得畅快游戏体验。</br>  龙界争霸现已开启<font color=\"0xFFDD0000\">防沉迷系统</font>，详细情况请关注游戏官网信息。";

CClientServerSocket* pG_GateSocket;

/************************Start Of CDGClient********************************************************/
CDGClient::CDGClient() :m_uiLastConnectTick(GetTickCount()), m_uiForceCloseTick(0), m_usSelectMaskServerID(0), m_iEncodeIdx(0),
						m_iSelectRealServerID(0), m_bIsGMIP(false), m_iClientType(0), m_ucNetType(0)
{
	SendDebugString("CDGClient 创建");
}

CDGClient::~CDGClient()
{
	SendDebugString("CDGClient 销毁");
}

unsigned short CDGClient::GetSelectMaskServerID()
{
	return m_usSelectMaskServerID;
}

int CDGClient::GetSelectRealServerID()
{
	return m_iSelectRealServerID;
}

bool CDGClient::GetIsGMIP()
{
	return m_bIsGMIP;
}

void CDGClient::SetGMIP(const bool bFlag)
{
	m_bIsGMIP = bFlag;
}

int CDGClient::GetEncodeIdx()
{
	return m_iEncodeIdx;
}

int CDGClient::GetClientType()
{
	return m_iClientType;
}

unsigned char CDGClient::GetNetType()
{
	return m_ucNetType;
}

void CDGClient::ForceClose()
{
	m_uiForceCloseTick = GetTickCount();
}

void CDGClient::Execute(unsigned int uiTick)
{
	CClientConnector::Execute(uiTick);
	if (((uiTick > m_uiLastConnectTick) && (uiTick - m_uiLastConnectTick > MAX_CONNECT_TIMEOUT)) ||
		((m_uiForceCloseTick > 0) && (uiTick > m_uiForceCloseTick) && (uiTick - m_uiForceCloseTick > DELAY_DISCONNECT_TIME)))
	{
		Close();
	}

	if (uiTick < m_uiLastConnectTick)
		m_uiLastConnectTick = uiTick;
}

void CDGClient::ProcessReceiveMsg(char* pHeader, char* pData, int iDataLen)
{
	switch (((PClientSocketHead)pHeader)->usIdent)
	{
	case CM_PING:
		SendToClientPeer(CM_PING, nullptr, 0);
		break;
	case CM_SELECT_SERVER_OLD:
		OpenWindow(cwMessageBox, 0, "客户端版本不正确，请先更新！");
		ForceClose();
		break;
	case CM_SELECT_SERVER:
		CMSelectServer(pData, iDataLen);
		break;
	case CM_CLOSE_WINDOW:
		CMCloseWindow(pData, iDataLen);
		break;
	case CM_QUIT:
		ForceClose();
		break;
	default:
		Log("收到未知客户端协议，IP=" + GetRemoteAddress() + " Ident=" + to_string(((PClientSocketHead)pHeader)->usIdent), lmtWarning);
		break;
	}
}

void CDGClient::SocketRead(const char* pBuf, int iCount)
{
	if (m_uiForceCloseTick > 0)
		return;
	if (iCount >= sizeof(TClientSocketHead))
	{
		PClientSocketHead pHead = (PClientSocketHead)pBuf;
		if (pHead->usPackageLen < sizeof(TClientSocketHead))
			return;
		if (CS_SEGMENTATION_CLIENTSIGN == pHead->uiSign)
			m_iClientType = 1;
		else if (CS_SEGMENTATION_CLIENTSIGN + 1 == pHead->uiSign)
			m_iClientType = 2;
		else
			return;

		char* pData = (char*)pHead + sizeof(TClientSocketHead);
		int iDataLen = pHead->usPackageLen - sizeof(TClientSocketHead);
		ProcessReceiveMsg((char*)pHead, pData, iDataLen);
	}
}

void CDGClient::OpenWindow(TClientWindowType wtype, int iParam, const std::string& msg)
{
	int iStrLen = msg.length();
	int iDataLen = sizeof(TClientWindowRec)+iStrLen + 1;
	char* pData = (char*)malloc(iDataLen);
	try
	{
		memset(pData, 0, iDataLen);
		((PClientWindowRec)pData)->WinType = wtype;
		((PClientWindowRec)pData)->Param = iParam;
		if (iStrLen > 0)
			memcpy(pData + sizeof(TClientWindowRec), msg.c_str(), iStrLen + 1);
		SendToClientPeer(SCM_OPEN_WINDOW, pData, iDataLen);
		free(pData);
	}
	catch (...)
	{
		free(pData);
	}
}

void CDGClient::CMSelectServer(char* pBuf, unsigned short usBufLen)
{
	m_ucNetType = 0;
	if (sizeof(int) == usBufLen)
	{
		m_usSelectMaskServerID = *((int*)pBuf);
	}
	else if (sizeof(TCMSelectServer) == usBufLen)
	{
		m_usSelectMaskServerID = ((PCMSelectServer)pBuf)->iMaskServerID;
		m_ucNetType = pG_GateSocket->GetNetType(((PCMSelectServer)pBuf)->iMaskServerID);
	}
	else
	{
		OpenWindow(cwMessageBox, 0, "选择服务器失败");
		Log("选服务器失败: ServerID = " + to_string(m_usSelectMaskServerID));
		return;
	}

	m_iSelectRealServerID = pG_DBSocket->SelectServer(this);
	if (m_iSelectRealServerID > 0)
	{
		OpenWindow(cwWarRule, 0, War_Warning);
		int iMinCount = 100000;
		TServerAddress address;
		memset(&address, 0, sizeof(TServerAddress));
		for (int i = 0; i < MAX_RESSERVER_COUNT; i++)
		{
			if (0 == G_ResServerInfos[i].Addr.iPort)
				break;

			if (iMinCount > G_ResServerInfos[i].iConnectCount)
			{
				iMinCount = G_ResServerInfos[i].iConnectCount;
				address = G_ResServerInfos[i].Addr;
			}
		}
		if (address.iPort > 0)
			SendToClientPeer(SCM_RESSERVER_INFO, &address, sizeof(TServerAddress));
	}
}

void CDGClient::CMCloseWindow(char* pBuf, unsigned short usBufLen)
{
	if (usBufLen >= sizeof(TClientWindowRec))
	{
		if (cwWarRule == ((PClientWindowRec)pBuf)->WinType)
			pG_DBSocket->SendSelectServer(this);
	}
}

void CDGClient::SendToClientPeer(unsigned short usIdent, void* pData, unsigned short usDataLen)
{
	unsigned short usBufLen = sizeof(TClientSocketHead)+usDataLen;
	char* pBuf = (char*)malloc(usBufLen);
	try
	{
		((PClientSocketHead)pBuf)->uiSign = CS_SEGMENTATION_CLIENTSIGN;
		((PClientSocketHead)pBuf)->usPackageLen = usBufLen;
		((PClientSocketHead)pBuf)->usIdent = usIdent;
		((PClientSocketHead)pBuf)->uiIdx = 0;
		if (usDataLen > 0)
		{
			memcpy(pBuf + sizeof(TClientSocketHead), pData, usDataLen);
		}		
		SendBuf(pBuf, usBufLen);
		free(pBuf);
	}
	catch (...)
	{
		free(pBuf);
	}
}

/************************End Of CDGClient***********************************************************/


/************************Start Of CClientServerSocket************************************************/
CClientServerSocket::CClientServerSocket() :m_uiLastCheckTick(0), m_iIPConfigFileAge(0), m_DefaultRule(itUnKnow), m_sWarWarning("")				 
{
	SendDebugString("CClientServerSocket 创建");
	m_OnCreateClient = std::bind(&CClientServerSocket::OnCreateClientSocket, this, std::placeholders::_1);
	m_OnClientError = std::bind(&CClientServerSocket::OnSocketError, this, std::placeholders::_1, std::placeholders::_2);
	m_OnConnect = std::bind(&CClientServerSocket::OnClientConnect, this, std::placeholders::_1);
}

CClientServerSocket::~CClientServerSocket()
{
	Clear();
	SendDebugString("CClientServerSocket 销毁");
}

void CClientServerSocket::LoadConfig(CWgtIniFile* pIniFileParser)
{
	if (pIniFileParser != nullptr)
	{
		int iPort = pIniFileParser->getInteger("Setup", "GatePort", DEFAULT_DispatchGate_CLIENT_PORT);
		std::string sTemp;
		std::string sKeyName("Type_");
		for (int i = 0; i < MAX_NET_TYPE_CONFIG; i++)
		{
			sTemp = pIniFileParser->getString("NetType", sKeyName + to_string(i), "");
			if ("" == sTemp)
				break;
			m_NetTypes[i] = inet_addr(sTemp.c_str());
		}

		if (!IsActive())
		{
			m_sLocalIP = "0.0.0.0";
			m_iListenPort = iPort;
			Log("接受客户端连接, Port = " + to_string(iPort), lmtMessage);
			Open();
		}
	}
}

bool CClientServerSocket::IsMasterIP(std::string &sIP)
{
	bool retFlag = false;
	std::string sTempIP(sIP);
	sTempIP.append(".");	

	PIpRuleNode pNode;
	std::list<PIpRuleNode>::iterator vIter;
	std::lock_guard<std::mutex> guard(m_IPRuleLockCS);
	for (vIter = m_IPRuleList.begin(); vIter != m_IPRuleList.end(); ++vIter)
	{
		pNode = (PIpRuleNode)*vIter;
		if (pNode->ipType != itMaster)
			continue;
		if (sTempIP.find(pNode->sMatchIP) == 0)
		{
			retFlag = true;
			break;
		}
	}
	return retFlag;
}

void CClientServerSocket::SMSelectServer(int iSocketHandle, char* pBuf, unsigned short usBufLen)
{
	if (usBufLen = sizeof(TNextGateInfo))
	{
		std::lock_guard<std::mutex> guard(m_LockCS);
		CDGClient* pClient = (CDGClient*)ValueOf(iSocketHandle);
		if (pClient != nullptr)
		{
			if ((0 == ((PNextGateInfo)pBuf)->iGateAddr) || (0 == ((PNextGateInfo)pBuf)->iGatePort))
			{
				pClient->OpenWindow(cwMessageBox, 0, "目前服务器处于维护中！！");
				pClient->ForceClose();
			}
			else
			{
				pClient->SendToClientPeer(SCM_SELECT_SERVER, pBuf, usBufLen);
			}
		}
	}
	else
	{
		Log("SM_SELECT_SERVER Response: invalid length=", lmtWarning);
	}
}

TIpType CClientServerSocket::GetDefaultRule()
{
	return m_DefaultRule;
}

void CClientServerSocket::DoActive()
{
	CIOCPServerSocketManager::DoActive();
	CheckIpConfig(GetTickCount());
}

void CClientServerSocket::CheckIpConfig(unsigned int uiTick)
{
	if ((0 == m_uiLastCheckTick) || (uiTick - m_uiLastCheckTick >= 20 * 1000))
	{
		m_uiLastCheckTick = uiTick;
		std::string sIPConfigFileName(G_CurrentExeDir + "ipaddress.txt");
		int iAge = GetFileAge(sIPConfigFileName);

		if ((iAge != -1) && (iAge != m_iIPConfigFileAge))
		{
			if (m_iIPConfigFileAge > 0)
				Log("Reload IPConfig File...", lmtMessage);

			m_iIPConfigFileAge = iAge;
			LoadIpConfigFile(sIPConfigFileName);
		}
	}
}

void CClientServerSocket::LoadIpConfigFile(const std::string& sFileName)
{
	m_DefaultRule = itAllow;
	Clear();
	ifstream configFile;
	std::string sTemp;
	std::string sKey;
	std::string sValue;
	int iPos;
	configFile.open(sFileName);
	while (!configFile.eof())
	{
		configFile >> sTemp;
		iPos = sTemp.find('=');
		if (iPos != string::npos)
		{
			sKey = sTemp.substr(0, iPos - 1);
			sValue = sTemp.substr(iPos + 1, sTemp.length());
			if (0 == sKey.compare("WarWarning"))
			{
				m_sWarWarning = sValue;
			}
			else if (0 == sKey.compare("GameMaster"))
			{
				AddIpRuleNode(sValue, itMaster);
			}
			else if (0 == sKey.compare("Deny"))
			{				
				if (0 == sValue.compare("All"))
					m_DefaultRule = itDeny;
				else
					AddIpRuleNode(sValue, itDeny);
			}
			else if (0 == sKey.compare("Allow"))
			{
				AddIpRuleNode(sValue, itAllow);
			}
		}
	}
	configFile.close();
}

void CClientServerSocket::Clear()
{
	PIpRuleNode pNode;
	std::list<PIpRuleNode>::iterator vIter;
	
	std::lock_guard<std::mutex> guard(m_IPRuleLockCS);
	for (vIter = m_IPRuleList.begin(); vIter != m_IPRuleList.end(); ++vIter)
	{
		pNode = (PIpRuleNode)*vIter;
		delete(pNode);
	}
	m_IPRuleList.clear();	
}

void CClientServerSocket::AddIpRuleNode(const std::string& sIP, TIpType ipType)
{
	std::string sTempIP;
	int iPos = sIP.find('*');
	if (iPos != string::npos)
		sTempIP = sIP.substr(0, iPos-1);
	else
		sTempIP = sIP;	

	if ("" == sTempIP)
		return;
	if (sTempIP.at(sTempIP.length()-1) != '.')
		sTempIP = sTempIP + '.';
	
	std::lock_guard<std::mutex> guard(m_IPRuleLockCS);
	PIpRuleNode pNode = new TIpRuleNode;
	pNode->ipType = ipType;
	pNode->sMatchIP = sIP;
	m_IPRuleList.push_back(pNode);
}

/*
返回：
0 : 未知
1 : 默认(电信)
2 : 电信
3 : 网通
4 : 移动
5 : 教育网
*/
unsigned char CClientServerSocket::GetNetType(int nAddr)
{
	unsigned char result = 0;
	for (int i = 0; i < MAX_NET_TYPE_CONFIG; i++)
	{
		if (0 == m_NetTypes[i])
			break;
		if (nAddr == m_NetTypes[i])
		{
			result = i;
			break;
		}
	}
	return result;
}

bool CClientServerSocket::CheckConnectIP(const std::string& sIP)
{
	bool retFlag = (m_DefaultRule != itDeny);
	std::string sTempIP(sIP);
	sTempIP.append(".");

	PIpRuleNode pNode;
	std::list<PIpRuleNode>::iterator vIter;
	std::lock_guard<std::mutex> guard(m_IPRuleLockCS);
	for (vIter = m_IPRuleList.begin(); vIter != m_IPRuleList.end(); ++vIter)
	{
		pNode = (PIpRuleNode)*vIter;
		if (sTempIP.find(pNode->sMatchIP) == 0)
		{
			switch (pNode->ipType)
			{
			case itDeny:
				retFlag = false;
				break;
			case itAllow:
			case itMaster:
				retFlag = true;
				break;
			default:
				retFlag = false;
				break;
			}
		}
	}

	if (!retFlag)
	{
		Log(sIP + " 连接被禁止!", lmtWarning);
	}
	return retFlag;
}

CClientConnector* CClientServerSocket::OnCreateClientSocket(const std::string& sIP)
{
	SendDebugString("OnCreateClientSocket");
	return new CDGClient;
}

void CClientServerSocket::OnSocketError(void* Sender, int& iErrorCode)
{
	if (iErrorCode != 10054)
	{
		std::string sInfo("Server Socket Error, Code = ");
		sInfo.append(to_string(iErrorCode));
		Log(sInfo, lmtError);
	}
	iErrorCode = 0;
}

void CClientServerSocket::OnClientConnect(void* Sender)
{
	CDGClient* client = (CDGClient*)Sender;
	if (!CheckConnectIP(client->GetRemoteAddress()))
	{
		client->OpenWindow(cwMessageBox, 0, "您目前无法进入");
		client->ForceClose();
	}
	else
	{
		client->SetGMIP(pG_GateSocket->IsMasterIP(client->GetRemoteAddress()));
	}
}

/************************End Of CClientServerSocket****************************************************/

