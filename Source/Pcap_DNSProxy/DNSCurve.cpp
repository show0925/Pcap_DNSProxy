﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2016 Chengr28
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "DNSCurve.h"

/* DNSCurve/DNSCrypt Protocol version 2

Client -> Server:
*  8 bytes: Magic query bytes
* 32 bytes: The client's DNSCurve public key (crypto_box_PUBLICKEYBYTES)
* 12 bytes: A client-selected nonce for this packet (crypto_box_NONCEBYTES / 2)
* 16 bytes: Poly1305 MAC (crypto_box_ZEROBYTES - crypto_box_BOXZEROBYTES)
* Variable encryption data ...

Server -> Client:
*  8 bytes: The string r6fnvWJ8 (DNSCRYPT_MAGIC_RESPONSE)
* 12 bytes: The client's nonce (crypto_box_NONCEBYTES / 2)
* 12 bytes: A server-selected nonce extension (crypto_box_NONCEBYTES / 2)
* 16 bytes: Poly1305 MAC (crypto_box_ZEROBYTES - crypto_box_BOXZEROBYTES)
* Variable encryption data ...

Using TCP protocol:
* 2 bytes: DNSCurve/DNSCrypt data payload length
* Variable DNSCurve/DNSCrypt data ...

*/

#if defined(ENABLE_LIBSODIUM)
//DNSCurve check padding data length
ssize_t DNSCurvePaddingData(
	const bool SetPadding, 
	uint8_t *Buffer, 
	const ssize_t Length)
{
//Set padding data sign.
	if (SetPadding)
	{
		Buffer[Length] = (uint8_t)DNSCRYPT_PADDING_SIGN_STRING;
	}
//Check padding data sign.
	else if (Length > (ssize_t)DNS_PACKET_MINSIZE)
	{
		ssize_t Index = 0;

	//Check padding data sign(0x80).
		for (Index = Length - 1U;Index > (ssize_t)DNS_PACKET_MINSIZE;--Index)
		{
			if (Buffer[Index] == DNSCRYPT_PADDING_SIGN)
				return Index;
		}

	//Check no null sign.
		for (Index = Length - 1U;Index > (ssize_t)DNS_PACKET_MINSIZE;--Index)
		{
			if (Buffer[Index] > 0)
				return Index;
		}
	}

	return 0;
}

//DNSCurve verify keypair
bool DNSCurveVerifyKeypair(
	const uint8_t *PublicKey, 
	const uint8_t *SecretKey)
{
//Initialization
	uint8_t Test_PublicKey[crypto_box_PUBLICKEYBYTES] = {0}, Validation[crypto_box_PUBLICKEYBYTES + crypto_box_SECRETKEYBYTES + crypto_box_ZEROBYTES] = {0};
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> Test_SecretKey(crypto_box_PUBLICKEYBYTES);

//Keypair, Nonce and validation data
	if (crypto_box_keypair(
		Test_PublicKey, 
		Test_SecretKey.Buffer) != 0)
			return false;
	else 
		memcpy_s(Validation + crypto_box_ZEROBYTES, crypto_box_PUBLICKEYBYTES + crypto_box_SECRETKEYBYTES, PublicKey, crypto_box_PUBLICKEYBYTES);
	uint8_t Nonce[crypto_box_NONCEBYTES] = {0};
	for (size_t Index = 0;Index < crypto_box_NONCEBYTES;++Index) //DNSCurve Test Nonce, 0x00 - 0x23(ASCII)
		*(Nonce + Index) = (uint8_t)Index;

//Verify keys
	if (crypto_box(
			Validation, 
			Validation, 
			crypto_box_PUBLICKEYBYTES + crypto_box_ZEROBYTES, 
			Nonce, 
			Test_PublicKey, 
			SecretKey) != 0 || 
		crypto_box_open(
			Validation, 
			Validation, 
			crypto_box_PUBLICKEYBYTES + crypto_box_ZEROBYTES, 
			Nonce, 
			PublicKey, 
			Test_SecretKey.Buffer) != 0)
				return false;

	return true;
}

//DNSCurve select socket data of DNS target
size_t DNSCurveSelectTargetSocket(
	SOCKET_DATA *TargetSocketData, 
	DNSCURVE_SERVER_DATA **PacketTarget, 
	bool **IsAlternate, 
	size_t **AlternateTimeoutTimes, 
	const uint16_t Protocol)
{
//Socket initialization
	size_t ServerType = 0;
	uint16_t SocketType = 0;
	if (Protocol == IPPROTO_TCP)
		SocketType = SOCK_STREAM;
	else if (Protocol == IPPROTO_UDP)
		SocketType = SOCK_DGRAM;
	sodium_memzero(TargetSocketData, sizeof(SOCKET_DATA));

//IPv6
	if (DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH && GlobalRunningStatus.GatewayAvailable_IPv6) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //IPv6
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0))) //Non-IPv4
	{
		if (Protocol == IPPROTO_TCP)
		{
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
			*AlternateTimeoutTimes = &AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
		}
		else if (Protocol == IPPROTO_UDP)
		{
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
			*AlternateTimeoutTimes = &AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		}
		else {
			return 0;
		}

	//Encryption mode check
		if (DNSCurveParameter.IsEncryption)
		{
			if ((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN))
					**IsAlternate = true;
			if (**IsAlternate && 
				((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
					**IsAlternate = false;
		}

		if (**IsAlternate && DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.Storage.ss_family > 0)
		{
			((PSOCKADDR_IN6)&TargetSocketData->SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&TargetSocketData->SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_port;
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
			ServerType = DNSCURVE_ALTERNATE_IPV6;
		}
		else { //Main
		//Encryption mode check
			if (DNSCurveParameter.IsEncryption && 
				((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
					return 0;

			((PSOCKADDR_IN6)&TargetSocketData->SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&TargetSocketData->SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_port;
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv6;
			ServerType = DNSCURVE_MAIN_IPV6;
		}

		TargetSocketData->AddrLen = sizeof(sockaddr_in6);
		TargetSocketData->SockAddr.ss_family = AF_INET6;
		TargetSocketData->Socket = socket(AF_INET6, SocketType, Protocol);
		if (!SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
			(Protocol == IPPROTO_TCP && !SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
			!SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr))
		{
			SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			TargetSocketData->Socket = 0;

			return 0;
		}
		else {
			return ServerType;
		}
	}
//IPv4
	else if (DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH && GlobalRunningStatus.GatewayAvailable_IPv4) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //IPv4
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0))) //Non-IPv6
	{
		if (Protocol == IPPROTO_TCP)
		{
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV4];
			*AlternateTimeoutTimes = &AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_TCP_IPV4];
		}
		else if (Protocol == IPPROTO_UDP)
		{
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
			*AlternateTimeoutTimes = &AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
		}
		else {
			return false;
		}

	//Encryption mode check
		if (DNSCurveParameter.IsEncryption)
		{
			if ((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN))
					**IsAlternate = true;
			if (**IsAlternate && 
				((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
					**IsAlternate = false;
		}

		if (**IsAlternate && DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.Storage.ss_family > 0)
		{
			((PSOCKADDR_IN)&TargetSocketData->SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&TargetSocketData->SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_port;
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
			ServerType = DNSCURVE_ALTERNATE_IPV4;
		}
		else { //Main
		//Encryption mode check
			if (DNSCurveParameter.IsEncryption && 
				((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
				(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
				CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
					return 0;

			((PSOCKADDR_IN)&TargetSocketData->SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&TargetSocketData->SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_port;
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv4;
			ServerType = DNSCURVE_MAIN_IPV4;
		}

		TargetSocketData->AddrLen = sizeof(sockaddr_in);
		TargetSocketData->SockAddr.ss_family = AF_INET;
		TargetSocketData->Socket = socket(AF_INET, SocketType, Protocol);
		if (!SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
			(Protocol == IPPROTO_TCP && !SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
			!SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
			!SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))
		{
			SocketSetting(TargetSocketData->Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			TargetSocketData->Socket = 0;

			return 0;
		}
		else {
			return ServerType;
		}
	}

	return 0;
}

//DNSCurve select socket data of DNS target(Multiple threading)
bool DNSCurveSelectTargetSocketMultiple(
	bool &IsIPv6, 
	bool **IsAlternate, 
	const uint16_t Protocol)
{
	IsIPv6 = false;

//IPv6
	if (DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH && GlobalRunningStatus.GatewayAvailable_IPv6) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //IPv6
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0))) //Non-IPv4
	{
		IsIPv6 = true;
		if (Protocol == IPPROTO_TCP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
		else if (Protocol == IPPROTO_UDP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		else 
			return false;
	}
//IPv4
	else if (DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH && GlobalRunningStatus.GatewayAvailable_IPv4) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //IPv4
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0))) //Non-IPv6
	{
		IsIPv6 = false;
		if (Protocol == IPPROTO_TCP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV4];
		else if (Protocol == IPPROTO_UDP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
		else 
			return false;
	}
	else {
		return false;
	}

	return true;
}

//DNSCurve set packet target
bool DNSCurvePacketTargetSetting(
	const size_t ServerType, 
	DNSCURVE_SERVER_DATA **PacketTarget)
{
	switch (ServerType)
	{
		case DNSCURVE_ALTERNATE_IPV6:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
		}break;
		case DNSCURVE_MAIN_IPV6:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv6;
		}break;
		case DNSCURVE_ALTERNATE_IPV4:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
		}break;
		case DNSCURVE_MAIN_IPV4:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv4;
		}break;
		default:
		{
			return false;
		}
	}

	return true;
}

//DNSCurve set Precomputation Key between client and server
bool DNSCurvePrecomputationKeySetting(
	uint8_t *PrecomputationKey, 
	uint8_t *Client_PublicKey, 
	const uint8_t *ServerFingerprint)
{
//Server fingerprint check
	if (CheckEmptyBuffer(ServerFingerprint, crypto_box_PUBLICKEYBYTES))
	{
		return false;
	}
	else {
		sodium_memzero(PrecomputationKey, crypto_box_BEFORENMBYTES);
		sodium_memzero(Client_PublicKey, crypto_box_PUBLICKEYBYTES);
	}

//Make a client ephemeral key pair.
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> Client_SecretKey(crypto_box_SECRETKEYBYTES);
	if (crypto_box_keypair(Client_PublicKey, Client_SecretKey.Buffer) != 0)
		return false;

//Make a precomputation key.
	if (crypto_box_beforenm(
			PrecomputationKey, 
			ServerFingerprint, 
			Client_SecretKey.Buffer) != 0)
				return false;

	return true;
}

//DNSCurve packet precomputation
void DNSCurveSocketPrecomputation(
	const uint16_t Protocol, 
	const uint8_t *OriginalSend, 
	const size_t SendSize, 
	const size_t RecvSize, 
	uint8_t **PrecomputationKey, 
	uint8_t **Alternate_PrecomputationKey, 
	DNSCURVE_SERVER_DATA **PacketTarget, 
	std::vector<SOCKET_DATA> &SocketDataList, 
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> &SocketSelectingList, 
	std::shared_ptr<uint8_t> &SendBuffer, 
	size_t &DataLength, 
	std::shared_ptr<uint8_t> &Alternate_SendBuffer, 
	size_t &Alternate_DataLength)
{
//Selecting check
	bool *IsAlternate = nullptr;
	auto IsIPv6 = false;
	if (!DNSCurveSelectTargetSocketMultiple(IsIPv6, &IsAlternate, Protocol))
		return;

//Initialization
	SOCKET_DATA SocketDataTemp;
	DNSCURVE_SOCKET_SELECTING_DATA SocketSelectingDataTemp;
	memset(&SocketDataTemp, 0, sizeof(SocketDataTemp));
	memset(&SocketSelectingDataTemp, 0, sizeof(SocketSelectingDataTemp));
	std::vector<SOCKET_DATA> Alternate_SocketDataList;
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> Alternate_SocketSelectingList;
	uint8_t Client_PublicKey_PTR[crypto_box_PUBLICKEYBYTES] = {0};
	auto Client_PublicKey = Client_PublicKey_PTR;
	size_t Index = 0, LoopLimits = 0;
	uint16_t InnerProtocol = 0;
	if (Protocol == IPPROTO_TCP)
		InnerProtocol = SOCK_STREAM;
	else if (Protocol == IPPROTO_UDP)
		InnerProtocol = SOCK_DGRAM;
	else 
		return;

//Main
	if (!*IsAlternate)
	{
	//Set target.
		if (IsIPv6)
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv6;
		else //IPv4
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv4;

	//Encryption mode check
		if (DNSCurveParameter.IsEncryption && 
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer((*PacketTarget)->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
				goto SkipMain;

	//Socket initialization
		if (Protocol == IPPROTO_TCP)
			LoopLimits = Parameter.MultipleRequestTimes;
		else if (Protocol == IPPROTO_UDP)
			LoopLimits = 1U;
		else 
			goto SkipMain;
		for (Index = 0;Index < LoopLimits;++Index)
		{
			SocketDataTemp.SockAddr = (*PacketTarget)->AddressData.Storage;
			if (IsIPv6)
				SocketDataTemp.Socket = socket(AF_INET6, InnerProtocol, Protocol);
			else //IPv4
				SocketDataTemp.Socket = socket(AF_INET, InnerProtocol, Protocol);

		//Socket attribute settings
			if (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
				(InnerProtocol == IPPROTO_TCP && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
				(IsIPv6 && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
				(!IsIPv6 && (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();

				goto SkipMain;
			}

		//IPv6
			if (IsIPv6)
			{
				SocketDataTemp.AddrLen = sizeof(sockaddr_in6);
				SocketSelectingDataTemp.ServerType = DNSCURVE_MAIN_IPV6;
			}
		//IPv4
			else {
				SocketDataTemp.AddrLen = sizeof(sockaddr_in);
				SocketSelectingDataTemp.ServerType = DNSCURVE_MAIN_IPV4;
			}

			SocketDataList.push_back(SocketDataTemp);
			SocketSelectingList.push_back(SocketSelectingDataTemp);
			sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));
		}

	//Make Precomputation Key between client and server.
		if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
		{
			if (!DNSCurvePrecomputationKeySetting(*PrecomputationKey, Client_PublicKey, (*PacketTarget)->ServerFingerprint))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();

				goto SkipMain;
			}
		}
		else {
			Client_PublicKey = DNSCurveParameter.Client_PublicKey;
			*PrecomputationKey = (*PacketTarget)->PrecomputationKey;
		}

	//Make encryption or normal packet of Main server.
		if (DNSCurveParameter.IsEncryption || Protocol == IPPROTO_TCP)
		{
			std::shared_ptr<uint8_t> SendBufferTemp(new uint8_t[RecvSize]());
			sodium_memzero(SendBufferTemp.get(), RecvSize);
			SendBuffer.swap(SendBufferTemp);
			DataLength = DNSCurvePacketEncryption(Protocol, (*PacketTarget)->SendMagicNumber, Client_PublicKey, *PrecomputationKey, OriginalSend, SendSize, SendBuffer.get(), RecvSize);
			if (DataLength < DNS_PACKET_MINSIZE)
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;

				goto SkipMain;
			}
		}
	}

//Jump here to skip Main process
SkipMain:
	sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));

//Set target.
	if (IsIPv6)
		*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
	else //IPv4
		*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;

//Alternate
	if ((*PacketTarget)->AddressData.Storage.ss_family > 0 && (*IsAlternate || Parameter.AlternateMultipleRequest))
	{
	//Encryption mode check
		if (DNSCurveParameter.IsEncryption && 
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer((*PacketTarget)->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			for (auto &SocketDataIter:SocketDataList)
				SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			SocketDataList.clear();
			SocketSelectingList.clear();
			DataLength = 0;

			return;
		}

	//Socket initialization
		if (Protocol == IPPROTO_TCP)
		{
			LoopLimits = Parameter.MultipleRequestTimes;
		}
		else if (Protocol == IPPROTO_UDP)
		{
			LoopLimits = 1U;
		}
		else {
			for (auto &SocketDataIter:SocketDataList)
				SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			SocketDataList.clear();
			SocketSelectingList.clear();
			DataLength = 0;

			return;
		}
		for (Index = 0;Index < LoopLimits;++Index)
		{
			SocketDataTemp.SockAddr = (*PacketTarget)->AddressData.Storage;
			if (IsIPv6)
				SocketDataTemp.Socket = socket(AF_INET6, InnerProtocol, Protocol);
			else //IPv4
				SocketDataTemp.Socket = socket(AF_INET, InnerProtocol, Protocol);

		//Socket attribute settings
			if (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
				(Protocol == IPPROTO_TCP && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
				(IsIPv6 && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
				(!IsIPv6 && (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();

				return;
			}

		//IPv6
			if (IsIPv6)
			{
				SocketDataTemp.AddrLen = sizeof(sockaddr_in6);
				SocketSelectingDataTemp.ServerType = DNSCURVE_ALTERNATE_IPV6;
			}
		//IPv4
			else {
				SocketDataTemp.AddrLen = sizeof(sockaddr_in);
				SocketSelectingDataTemp.ServerType = DNSCURVE_ALTERNATE_IPV4;
			}

			Alternate_SocketDataList.push_back(SocketDataTemp);
			Alternate_SocketSelectingList.push_back(SocketSelectingDataTemp);
			sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));
		}

	//Make Precomputation Key between client and server.
		if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
		{
			if (!DNSCurvePrecomputationKeySetting(*Alternate_PrecomputationKey, Client_PublicKey, (*PacketTarget)->ServerFingerprint))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();

				return;
			}
		}
		else {
			Client_PublicKey = DNSCurveParameter.Client_PublicKey;
			*Alternate_PrecomputationKey = (*PacketTarget)->PrecomputationKey;
		}

	//Make encryption or normal packet of Alternate server.
		if (DNSCurveParameter.IsEncryption)
		{
			std::shared_ptr<uint8_t> SendBufferTemp(new uint8_t[RecvSize]());
			sodium_memzero(SendBufferTemp.get(), RecvSize);
			Alternate_SendBuffer.swap(SendBufferTemp);
			SendBufferTemp.reset();
			Alternate_DataLength = DNSCurvePacketEncryption(Protocol, (*PacketTarget)->SendMagicNumber, Client_PublicKey, *Alternate_PrecomputationKey, OriginalSend, SendSize, Alternate_SendBuffer.get(), RecvSize);
			if (Alternate_DataLength < DNS_PACKET_MINSIZE)
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();
				Alternate_DataLength = 0;

				return;
			}
		}

	//Add to global list.
		if (!Alternate_SocketDataList.empty() && !Alternate_SocketSelectingList.empty())
		{
			for (auto &SocketDataIter:Alternate_SocketDataList)
				SocketDataList.push_back(SocketDataIter);
			for (auto &SocketSelectingIter:Alternate_SocketSelectingList)
				SocketSelectingList.push_back(SocketSelectingIter);
		}
	}

	return;
}

//DNSCurve packet encryption
size_t DNSCurvePacketEncryption(
	const uint16_t Protocol, 
	const uint8_t *SendMagicNumber, 
	const uint8_t *Client_PublicKey, 
	const uint8_t *PrecomputationKey, 
	const uint8_t *OriginalSend, 
	const size_t Length, 
	uint8_t *SendBuffer, 
	const size_t SendSize)
{
//Encryption mode
	if (DNSCurveParameter.IsEncryption)
	{
		uint8_t Nonce[crypto_box_NONCEBYTES] = {0};

	//Make nonce.
		*(uint32_t *)Nonce = randombytes_random();
		*(uint32_t *)(Nonce + sizeof(uint32_t)) = randombytes_random();
		*(uint32_t *)(Nonce + sizeof(uint32_t) * 2U) = randombytes_random();
		sodium_memzero(Nonce + crypto_box_HALF_NONCEBYTES, crypto_box_HALF_NONCEBYTES);

	//Make a crypto box.
		std::shared_ptr<uint8_t> Buffer;
		if (Protocol == IPPROTO_TCP)
		{
			std::shared_ptr<uint8_t> BufferTemp(new uint8_t[DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN]());
			sodium_memzero(BufferTemp.get(), DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN);
			Buffer.swap(BufferTemp);
		}
		else if (Protocol == IPPROTO_UDP)
		{
			std::shared_ptr<uint8_t> BufferTemp(new uint8_t[DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN]());
			sodium_memzero(BufferTemp.get(), DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN);
			Buffer.swap(BufferTemp);
		}
		else {
			return EXIT_FAILURE;
		}

		memcpy_s(Buffer.get() + crypto_box_ZEROBYTES, DNSCurveParameter.DNSCurvePayloadSize - crypto_box_ZEROBYTES, OriginalSend, Length);
		DNSCurvePaddingData(true, Buffer.get(), crypto_box_ZEROBYTES + Length);

	//Encrypt data.
		if (Protocol == IPPROTO_TCP)
		{
			if (crypto_box_afternm(
					SendBuffer + DNSCRYPT_BUFFER_RESERVE_TCP_LEN, 
					Buffer.get(), 
					DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_TCP_LEN, 
					Nonce, 
					PrecomputationKey) != 0)
						return EXIT_FAILURE;
		}
		else if (Protocol == IPPROTO_UDP)
		{
			if (crypto_box_afternm(
					SendBuffer + DNSCRYPT_BUFFER_RESERVE_LEN, 
					Buffer.get(), 
					DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN, 
					Nonce, 
					PrecomputationKey) != 0)
						return EXIT_FAILURE;
		}
		else {
			return EXIT_FAILURE;
		}

	//Make DNSCurve encryption packet.
		Buffer.reset();
		if (Protocol == IPPROTO_TCP)
		{
			memcpy_s(SendBuffer + sizeof(uint16_t), SendSize - sizeof(uint16_t), SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
			memcpy_s(SendBuffer + sizeof(uint16_t) + DNSCURVE_MAGIC_QUERY_LEN, SendSize - sizeof(uint16_t) - DNSCURVE_MAGIC_QUERY_LEN, Client_PublicKey, crypto_box_PUBLICKEYBYTES);
			memcpy_s(SendBuffer + sizeof(uint16_t) + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_PUBLICKEYBYTES, SendSize - sizeof(uint16_t) - DNSCURVE_MAGIC_QUERY_LEN - crypto_box_PUBLICKEYBYTES, Nonce, crypto_box_HALF_NONCEBYTES);

		//Add length of request packet(It must be written in header when transport with TCP protocol).
			*(uint16_t *)SendBuffer = htons((uint16_t)(DNSCurveParameter.DNSCurvePayloadSize - sizeof(uint16_t)));
		}
		else if (Protocol == IPPROTO_UDP)
		{
			memcpy_s(SendBuffer, SendSize, SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
			memcpy_s(SendBuffer + DNSCURVE_MAGIC_QUERY_LEN, SendSize - DNSCURVE_MAGIC_QUERY_LEN, Client_PublicKey, crypto_box_PUBLICKEYBYTES);
			memcpy_s(SendBuffer + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_PUBLICKEYBYTES, SendSize - DNSCURVE_MAGIC_QUERY_LEN - crypto_box_PUBLICKEYBYTES, Nonce, crypto_box_HALF_NONCEBYTES);
		}
		else {
			return EXIT_FAILURE;
		}

		return DNSCurveParameter.DNSCurvePayloadSize;
	}
//Normal mode
	else {
		memcpy_s(SendBuffer, SendSize, OriginalSend, Length);
		if (Protocol == IPPROTO_TCP)
			return AddLengthDataToHeader(SendBuffer, Length, SendSize); //Add length of request packet(It must be written in header when transport with TCP protocol).
		else if (Protocol == IPPROTO_UDP)
			return Length;
	}

	return EXIT_FAILURE;
}

//DNSCurve packet decryption
ssize_t DNSCurvePacketDecryption(
	const uint8_t *ReceiveMagicNumber, 
	const uint8_t *PrecomputationKey, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize, 
	const ssize_t Length)
{
//Initialization
	ssize_t DataLength = Length;

//Encryption mode
	if (DNSCurveParameter.IsEncryption)
	{
	//Receive Magic number check
		sodium_memzero(OriginalRecv + Length, RecvSize - Length);
		if (sodium_memcmp(OriginalRecv, ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) != 0)
			return EXIT_FAILURE;

	//Nonce initialization
		uint8_t WholeNonce[crypto_box_NONCEBYTES] = {0};
		memcpy_s(WholeNonce, crypto_box_NONCEBYTES, OriginalRecv + DNSCURVE_MAGIC_QUERY_LEN, crypto_box_NONCEBYTES);

	//Open crypto box.
		sodium_memzero(OriginalRecv, DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES);
		memmove_s(OriginalRecv + crypto_box_BOXZEROBYTES, RecvSize - crypto_box_BOXZEROBYTES, OriginalRecv + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES, Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES));
		if (crypto_box_open_afternm(
			(unsigned char *)OriginalRecv, 
			(unsigned char *)OriginalRecv, 
			Length + crypto_box_BOXZEROBYTES - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES), 
			WholeNonce, 
			PrecomputationKey) != 0)
				return EXIT_FAILURE;
		memmove_s(OriginalRecv, RecvSize, OriginalRecv + crypto_box_ZEROBYTES, Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES));
		sodium_memzero(OriginalRecv + Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES), RecvSize - (Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES)));

	//Check padding data and responses check.
		DataLength = DNSCurvePaddingData(false, OriginalRecv, Length);
		if (DataLength < (ssize_t)DNS_PACKET_MINSIZE)
			return EXIT_FAILURE;
	}

//Responses check
	DataLength = CheckResponseData(
		REQUEST_PROCESS_DNSCURVE, 
		OriginalRecv, 
		DataLength, 
		RecvSize, 
		nullptr);
	if (DataLength < (ssize_t)DNS_PACKET_MINSIZE)
		return EXIT_FAILURE;

	return DataLength;
}

//DNSCurve non-blocking mode selecting
ssize_t DNSCurveSocketSelecting(
	const uint16_t Protocol, 
	std::vector<SOCKET_DATA> &SocketDataList, 
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> &SocketSelectingList, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize, 
	ssize_t *ErrorCode)
{
	size_t Index = 0;
	if (ErrorCode != nullptr)
		*ErrorCode = 0;

//TCP or UDP connecting
	ssize_t RecvLen = 0;
	for (Index = 0;Index < SocketDataList.size();++Index)
	{
		RecvLen = SocketConnecting(Protocol, SocketDataList.at(Index).Socket, (PSOCKADDR)&SocketDataList.at(Index).SockAddr, SocketDataList.at(Index).AddrLen, SocketSelectingList.at(Index).SendBuffer, SocketSelectingList.at(Index).SendSize);
		if (RecvLen == EXIT_FAILURE)
		{
			SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			SocketDataList.at(Index).Socket = 0;
		}
		else if (Protocol == IPPROTO_TCP && Parameter.TCP_FastOpen && RecvLen >= (ssize_t)DNS_PACKET_MINSIZE)
		{
			SocketSelectingList.at(Index).IsPacketSend = true;
			++Index;
		}
	}

//Socket check(Part 1)
	for (auto SocketDataIter = SocketDataList.begin();SocketDataIter != SocketDataList.end();++SocketDataIter)
	{
		if (SocketSetting(SocketDataIter->Socket, SOCKET_SETTING_INVALID_CHECK, false, nullptr))
			break;
		else if (SocketDataIter + 1U == SocketDataList.end())
			return EXIT_FAILURE;
	}

//Initialization
	fd_set ReadFDS, WriteFDS;
	timeval Timeout;
	memset(&Timeout, 0, sizeof(Timeout));
	memset(&ReadFDS, 0, sizeof(ReadFDS));
	memset(&WriteFDS, 0, sizeof(WriteFDS));
	ssize_t SelectResult = 0;
	size_t LastReceiveIndex = 0;
	SYSTEM_SOCKET MaxSocket = 0;
	auto IsAllSocketClosed = false;

//Socket attribute setting(Timeout)
#if defined(PLATFORM_WIN)
	Timeout.tv_sec = DNSCurveParameter.DNSCurve_SocketTimeout_Reliable / SECOND_TO_MILLISECOND;
	Timeout.tv_usec = DNSCurveParameter.DNSCurve_SocketTimeout_Reliable % SECOND_TO_MILLISECOND * MICROSECOND_TO_MILLISECOND;
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	Timeout.tv_sec = DNSCurveParameter.DNSCurve_SocketTimeout_Reliable.tv_sec;
	Timeout.tv_usec = DNSCurveParameter.DNSCurve_SocketTimeout_Reliable.tv_usec;
#endif

//Selecting process
	for (;;)
	{
	//Socket check(Part 2)
		for (auto SocketDataIter = SocketDataList.begin();SocketDataIter != SocketDataList.end();++SocketDataIter)
		{
			if (SocketSetting(SocketDataIter->Socket, SOCKET_SETTING_INVALID_CHECK, false, nullptr))
				break;
			else if (SocketDataIter + 1U == SocketDataList.end())
				IsAllSocketClosed = true;
		}

	//Buffer list check(Part 1)
		if (IsAllSocketClosed || Parameter.ReceiveWaiting == 0 || SocketDataList.size() == 1U)
		{
		//Scan all result.
			RecvLen = DNSCurveSelectingResult(Protocol, SocketDataList, SocketSelectingList, OriginalRecv, RecvSize);
			if (RecvLen >= (ssize_t)DNS_PACKET_MINSIZE)
				return RecvLen;
		//All socket cloesed.
			else if (IsAllSocketClosed)
				return EXIT_FAILURE;
		}

	//Reset parameters.
		FD_ZERO(&ReadFDS);
		FD_ZERO(&WriteFDS);
		MaxSocket = 0;

	//Socket check and non-blocking process
		for (Index = 0;Index < SocketDataList.size();++Index)
		{
		//Non-blocking process
			if (SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_INVALID_CHECK, false, nullptr))
			{
			//Select structure initialization
				if (SocketDataList.at(Index).Socket > MaxSocket)
					MaxSocket = SocketDataList.at(Index).Socket;

			//Receive and send process
				FD_SET(SocketDataList.at(Index).Socket, &ReadFDS);
				if (!SocketSelectingList.at(Index).IsPacketSend)
					FD_SET(SocketDataList.at(Index).Socket, &WriteFDS);
			}
			else if (MaxSocket == 0 && Index + 1U == SocketDataList.size())
			{
				return EXIT_FAILURE;
			}
		}

	//Wait for system calling.
	#if defined(PLATFORM_WIN)
		SelectResult = select(0, &ReadFDS, &WriteFDS, nullptr, &Timeout);
	#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
		SelectResult = select(MaxSocket + 1U, &ReadFDS, &WriteFDS, nullptr, &Timeout);
	#endif
		if (SelectResult > 0)
		{
			for (Index = 0;Index < SocketDataList.size();++Index)
			{
			//Receive process
				if (FD_ISSET(SocketDataList.at(Index).Socket, &ReadFDS))
				{
				//Buffer initialization
					if (!SocketSelectingList.at(Index).RecvBuffer)
					{
						std::shared_ptr<uint8_t> RecvBufferTemp(new uint8_t[RecvSize]());
						sodium_memzero(RecvBufferTemp.get(), RecvSize);
						SocketSelectingList.at(Index).RecvBuffer.swap(RecvBufferTemp);
					}

				//Receive from selecting.
					RecvLen = recv(SocketDataList.at(Index).Socket, (char *)SocketSelectingList.at(Index).RecvBuffer.get() + SocketSelectingList.at(Index).Length, (int)(RecvSize - SocketSelectingList.at(Index).Length), 0);

				//Connection closed or SOCKET_ERROR
					if (RecvLen <= 0)
					{
						SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
						SocketDataList.at(Index).Socket = 0;
						SocketSelectingList.at(Index).RecvBuffer.reset();
						SocketSelectingList.at(Index).Length = 0;
						continue;
					}
					else if (Protocol == IPPROTO_UDP && RecvLen > (ssize_t)DNS_PACKET_MINSIZE && SocketSelectingList.at(Index).Length > 0)
					{
						sodium_memzero(SocketSelectingList.at(Index).RecvBuffer.get(), SocketSelectingList.at(Index).Length);
						memmove_s(SocketSelectingList.at(Index).RecvBuffer.get(), RecvSize, SocketSelectingList.at(Index).RecvBuffer.get() + SocketSelectingList.at(Index).Length, RecvLen);
						SocketSelectingList.at(Index).Length = 0;
					}

				//Mark whole packet length and last packet.
					SocketSelectingList.at(Index).Length += RecvLen;
					LastReceiveIndex = Index;
				}

			//Send process
				if (FD_ISSET(SocketDataList.at(Index).Socket, &WriteFDS) && !SocketSelectingList.at(Index).IsPacketSend)
				{
					if (send(SocketDataList.at(Index).Socket, (const char *)SocketSelectingList.at(Index).SendBuffer, (int)SocketSelectingList.at(Index).SendSize, 0) == SOCKET_ERROR)
					{
						ssize_t InnerErrorCode = WSAGetLastError();
						SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
						SocketDataList.at(Index).Socket = 0;

					#if defined(PLATFORM_WIN)
						if (InnerErrorCode == WSAEWOULDBLOCK)
					#elif defined(PLATFORM_LINUX)
						if (InnerErrorCode == EAGAIN || InnerErrorCode == EINPROGRESS)
					#elif defined(PLATFORM_MACX)
						if (InnerErrorCode == EWOULDBLOCK || InnerErrorCode == EAGAIN || InnerErrorCode == EINPROGRESS)
					#endif
						{
							SocketSelectingList.at(Index).RecvBuffer.reset();
							SocketSelectingList.at(Index).Length = 0;
						}
					}
					else {
						SocketSelectingList.at(Index).IsPacketSend = true;
					}
				}
			}
		}
	//Timeout
		else if (SelectResult == 0)
		{
			uint8_t *PrecomputationKeyTemp = nullptr, *ReceiveMagicNumberTemp = nullptr;
			Index = 0;

		//Swap to last receive packet when Receive Waiting is ON.
			MaxSocket = SocketDataList.at(LastReceiveIndex).Socket;
			SocketDataList.at(LastReceiveIndex).Socket = SocketDataList.at(Index).Socket;
			SocketDataList.at(Index).Socket = MaxSocket;
			PrecomputationKeyTemp = SocketSelectingList.at(Index).PrecomputationKey;
			SocketSelectingList.at(Index).PrecomputationKey = SocketSelectingList.at(LastReceiveIndex).PrecomputationKey;
			SocketSelectingList.at(LastReceiveIndex).PrecomputationKey = PrecomputationKeyTemp;
			ReceiveMagicNumberTemp = SocketSelectingList.at(Index).ReceiveMagicNumber;
			SocketSelectingList.at(Index).ReceiveMagicNumber = SocketSelectingList.at(LastReceiveIndex).ReceiveMagicNumber;
			SocketSelectingList.at(LastReceiveIndex).ReceiveMagicNumber = ReceiveMagicNumberTemp;
			SocketSelectingList.at(LastReceiveIndex).RecvBuffer.swap(SocketSelectingList.at(Index).RecvBuffer);
			RecvLen = SocketSelectingList.at(LastReceiveIndex).Length;
			SocketSelectingList.at(LastReceiveIndex).Length = SocketSelectingList.at(Index).Length;
			SocketSelectingList.at(Index).Length = RecvLen;

		//Buffer list check(Part 2)
			RecvLen = DNSCurveSelectingResult(Protocol, SocketDataList, SocketSelectingList, OriginalRecv, RecvSize);
			if (RecvLen >= (ssize_t)DNS_PACKET_MINSIZE)
				return RecvLen;

			if (ErrorCode != nullptr)
				*ErrorCode = WSAETIMEDOUT;
			break;
		}
	//SOCKET_ERROR
		else {
			if (ErrorCode != nullptr)
				*ErrorCode = WSAGetLastError();
			break;
		}
	}

//Close all sockets.
	for (auto &SocketDataIter:SocketDataList)
	{
		SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		SocketDataIter.Socket = 0;
	}

	return EXIT_FAILURE;
}

//DNSCurve Socket selecting result
ssize_t DNSCurveSelectingResult(
	const uint16_t Protocol, 
	std::vector<SOCKET_DATA> &SocketDataList, 
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> &SocketSelectingList, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize)
{
	ssize_t RecvLen = 0;

//Scan all result.
	for (size_t Index = 0;Index < SocketDataList.size();++Index)
	{
		if (SocketSelectingList.at(Index).RecvBuffer && SocketSelectingList.at(Index).Length >= DNS_PACKET_MINSIZE)
		{
		//TCP header length check
			if (Protocol == IPPROTO_TCP)
			{
				RecvLen = ntohs(((uint16_t *)SocketSelectingList.at(Index).RecvBuffer.get())[0]);
				if (RecvLen > (ssize_t)SocketSelectingList.at(Index).Length)
				{
					SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
					SocketDataList.at(Index).Socket = 0;
					SocketSelectingList.at(Index).RecvBuffer.reset();
					SocketSelectingList.at(Index).Length = 0;
					continue;
				}
				else {
					memmove_s(SocketSelectingList.at(Index).RecvBuffer.get(), RecvSize, SocketSelectingList.at(Index).RecvBuffer.get() + sizeof(uint16_t), RecvLen);
					sodium_memzero(SocketSelectingList.at(Index).RecvBuffer.get() + RecvLen, (ssize_t)RecvSize - RecvLen);
				}
			}
		//UDP length
			else if (Protocol == IPPROTO_UDP)
			{
				RecvLen = SocketSelectingList.at(Index).Length;
			}
			else {
				SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.at(Index).Socket = 0;
				SocketSelectingList.at(Index).RecvBuffer.reset();
				SocketSelectingList.at(Index).Length = 0;
				continue;
			}

		//Receive from buffer list and decrypt or get packet data.
			RecvLen = DNSCurvePacketDecryption(SocketSelectingList.at(Index).ReceiveMagicNumber, SocketSelectingList.at(Index).PrecomputationKey, SocketSelectingList.at(Index).RecvBuffer.get(), RecvSize, RecvLen);
			if (RecvLen < (ssize_t)DNS_PACKET_MINSIZE)
			{
				SocketSetting(SocketDataList.at(Index).Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.at(Index).Socket = 0;
				SocketSelectingList.at(Index).RecvBuffer.reset();
				SocketSelectingList.at(Index).Length = 0;
				continue;
			}
			else {
				sodium_memzero(OriginalRecv, RecvSize);
				memcpy_s(OriginalRecv, RecvSize, SocketSelectingList.at(Index).RecvBuffer.get(), RecvLen);
			}

		//Mark sockets to global list.
			std::unique_lock<std::mutex> SocketMarkingMutex(SocketMarkingLock);
			for (auto &SocketDataIter:SocketDataList)
			{
				if (SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_INVALID_CHECK, false, nullptr))
				{
					SOCKET_MARKING_DATA SocketMarkingDataTemp;
					SocketMarkingDataTemp.first = SocketDataIter.Socket;
					if (Protocol == IPPROTO_TCP)
					{
					#if defined(PLATFORM_WIN_XP)
						SocketMarkingDataTemp.second = GetTickCount() + DNSCurveParameter.DNSCurve_SocketTimeout_Reliable;
					#elif defined(PLATFORM_WIN)
						SocketMarkingDataTemp.second = GetTickCount64() + DNSCurveParameter.DNSCurve_SocketTimeout_Reliable;
					#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
						SocketMarkingDataTemp.second = IncreaseMillisecondTime(GetCurrentSystemTime(), DNSCurveParameter.DNSCurve_SocketTimeout_Reliable);
					#endif
					}
					else if (Protocol == IPPROTO_UDP)
					{
					#if defined(PLATFORM_WIN_XP)
						SocketMarkingDataTemp.second = GetTickCount() + DNSCurveParameter.DNSCurve_SocketTimeout_Unreliable;
					#elif defined(PLATFORM_WIN)
						SocketMarkingDataTemp.second = GetTickCount64() + DNSCurveParameter.DNSCurve_SocketTimeout_Unreliable;
					#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
						SocketMarkingDataTemp.second = IncreaseMillisecondTime(GetCurrentSystemTime(), DNSCurveParameter.DNSCurve_SocketTimeout_Unreliable);
					#endif
					}
					else {
						continue;
					}

					SocketMarkingList.push_back(SocketMarkingDataTemp);
					SocketDataIter.Socket = 0;
				}
			}
			SocketMarkingMutex.unlock();

		//Mark DNS cache.
			if (Parameter.CacheType > 0)
				MarkDomainCache(OriginalRecv, RecvLen);

			return RecvLen;
		}
	}

	return EXIT_FAILURE;
}

//DNSCurve initialization
void DNSCurveInit(
	void)
{
//Libsodium ramdom bytes initialization
	randombytes_set_implementation(&randombytes_salsa20_implementation);
	randombytes_stir();

//DNSCurve signature request TCP Mode
	if (DNSCurveParameter.DNSCurveProtocol_Transport == REQUEST_MODE_TCP)
	{
	//Main(IPv6)
		if (DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family > 0 && 
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //Auto select and IPv6
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0)) && //Non-IPv4
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			std::thread DNSCurveTCPSignatureRequestThread(std::bind(DNSCurveTCPSignatureRequest, AF_INET6, false));
			DNSCurveTCPSignatureRequestThread.detach();
		}

	//Main(IPv4)
		if (DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family > 0 && 
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //Auto select and IPv4
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0)) && //Non-IPv6
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			std::thread DNSCurveTCPSignatureRequestThread(std::bind(DNSCurveTCPSignatureRequest, AF_INET, false));
			DNSCurveTCPSignatureRequestThread.detach();
		}

	//Alternate(IPv6)
		if (DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.Storage.ss_family > 0 && 
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //Auto select and IPv6
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0)) && //Non-IPv4
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			std::thread DNSCurveTCPSignatureRequestThread(std::bind(DNSCurveTCPSignatureRequest, AF_INET6, true));
			DNSCurveTCPSignatureRequestThread.detach();
		}

	//Alternate(IPv4)
		if (DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.Storage.ss_family > 0 && 
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //Auto select and IPv4
			(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0)) && //Non-IPv6
			((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			std::thread DNSCurveTCPSignatureRequestThread(std::bind(DNSCurveTCPSignatureRequest, AF_INET, true));
			DNSCurveTCPSignatureRequestThread.detach();
		}
	}

//DNSCurve signature request UDP Mode
//Main(IPv6)
	if (DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family > 0 && 
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //Auto select and IPv6
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0)) && //Non-IPv4
		((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
		(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
		CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
	{
		std::thread DNSCurveUDPSignatureRequestThread(std::bind(DNSCurveUDPSignatureRequest, AF_INET6, false));
		DNSCurveUDPSignatureRequestThread.detach();
	}

//Main(IPv4)
	if (DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family > 0 && 
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //Auto select and IPv4
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0)) && //Non-IPv6
		((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
		(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
		CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
	{
		std::thread DNSCurveUDPSignatureRequestThread(std::bind(DNSCurveUDPSignatureRequest, AF_INET, false));
		DNSCurveUDPSignatureRequestThread.detach();
	}

//Alternate(IPv6)
	if (DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.Storage.ss_family > 0 && 
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //Auto select and IPv6
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.Storage.ss_family == 0)) && //Non-IPv4
		((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
		(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
		CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
	{
		std::thread DNSCurveUDPSignatureRequestThread(std::bind(DNSCurveUDPSignatureRequest, AF_INET6, true));
		DNSCurveUDPSignatureRequestThread.detach();
	}

//Alternate(IPv4)
	if (DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.Storage.ss_family > 0 && 
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_NETWORK_BOTH || DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //Auto select and IPv4
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.Storage.ss_family == 0)) && //Non-IPv6
		((!DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
		(DNSCurveParameter.ClientEphemeralKey && CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
		CheckEmptyBuffer(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
	{
		std::thread DNSCurveUDPSignatureRequestThread(std::bind(DNSCurveUDPSignatureRequest, AF_INET, true));
		DNSCurveUDPSignatureRequestThread.detach();
	}

	return;
}

//Send TCP request to get Signature Data of servers
bool DNSCurveTCPSignatureRequest(
	const uint16_t Protocol, 
	const bool IsAlternate)
{
//Initialization(Part 1)
	std::shared_ptr<uint8_t> SendBuffer(new uint8_t[PACKET_MAXSIZE]()), RecvBuffer(new uint8_t[LARGE_PACKET_MAXSIZE]());
	sodium_memzero(SendBuffer.get(), PACKET_MAXSIZE);
	sodium_memzero(RecvBuffer.get(), LARGE_PACKET_MAXSIZE);
	std::vector<SOCKET_DATA> TCPSocketDataList(1U);
	sodium_memzero(&TCPSocketDataList.front(), sizeof(TCPSocketDataList.front()));

//Make packet data(Part 1).
	size_t DataLength = sizeof(dns_tcp_hdr);
	auto DNS_TCP_Header = (pdns_tcp_hdr)SendBuffer.get();
#if defined(ENABLE_PCAP)
	DNS_TCP_Header->ID = Parameter.DomainTest_ID;
#else
	DNS_TCP_Header->ID = htons(U16_NUM_ONE);
#endif
	DNS_TCP_Header->Flags = htons(DNS_STANDARD);
	DNS_TCP_Header->Question = htons(U16_NUM_ONE);
	if (Protocol == AF_INET6)
	{
		if (IsAlternate)
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.ProviderName, SendBuffer.get() + DataLength);
		else 
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ProviderName, SendBuffer.get() + DataLength);
	}
	else if (Protocol == AF_INET)
	{
		if (IsAlternate)
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.ProviderName, SendBuffer.get() + DataLength);
		else 
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ProviderName, SendBuffer.get() + DataLength);
	}
	else {
		return false;
	}
	((pdns_qry)(SendBuffer.get() + DataLength))->Type = htons(DNS_RECORD_TXT);
	((pdns_qry)(SendBuffer.get() + DataLength))->Classes = htons(DNS_CLASS_IN);
	DataLength += sizeof(dns_qry);

//EDNS Label
	DataLength = AddEDNSLabelToAdditionalRR(SendBuffer.get() + sizeof(uint16_t), DataLength - sizeof(uint16_t), PACKET_MAXSIZE, nullptr);
	DataLength += sizeof(uint16_t);

//Add length of request packet(It must be written in header when transport with TCP protocol).
	DNS_TCP_Header->Length = htons((uint16_t)(DataLength - sizeof(uint16_t)));

//Socket initialization(Part 1)
	size_t ServerType = 0;
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	if (Protocol == AF_INET6)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN6)&TCPSocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&TCPSocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
			ServerType = DNSCURVE_ALTERNATE_IPV6;
		}
		else { //Main
			((PSOCKADDR_IN6)&TCPSocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&TCPSocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv6;
			ServerType = DNSCURVE_MAIN_IPV6;
		}

		TCPSocketDataList.front().AddrLen = sizeof(sockaddr_in6);
		TCPSocketDataList.front().SockAddr.ss_family = AF_INET6;
	}
	else if (Protocol == AF_INET)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN)&TCPSocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&TCPSocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
			ServerType = DNSCURVE_ALTERNATE_IPV4;
		}
		else { //Main
			((PSOCKADDR_IN)&TCPSocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&TCPSocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv4;
			ServerType = DNSCURVE_MAIN_IPV4;
		}

		TCPSocketDataList.front().AddrLen = sizeof(sockaddr_in);
		TCPSocketDataList.front().SockAddr.ss_family = AF_INET;
	}
	else {
		return false;
	}

//Initialization(Part 2)
	size_t SleepTime_SignatureRequest = 0, SpeedTime_SignatureRequest = DNSCurveParameter.KeyRecheckTime;
	std::wstring Message;
	ssize_t RecvLen = 0;

//Send request.
	for (;;)
	{
	//Sleep time controller
		if (SleepTime_SignatureRequest > 0)
		{
			if (SpeedTime_SignatureRequest != DNSCurveParameter.KeyRecheckTime)
			{
				SpeedTime_SignatureRequest = DNSCurveParameter.KeyRecheckTime;
			}
			else if (SleepTime_SignatureRequest < SpeedTime_SignatureRequest)
			{
				SleepTime_SignatureRequest += Parameter.FileRefreshTime;

				Sleep(Parameter.FileRefreshTime);
				continue;
			}

			SleepTime_SignatureRequest = 0;
		}

	//Socket initialization(Part 2)
		if (Protocol == AF_INET6)
			TCPSocketDataList.front().Socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
		else if (Protocol == AF_INET)
			TCPSocketDataList.front().Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		else 
			goto JumpToRestart;
		if (!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
			!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr) || 
			!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
			(Protocol == AF_INET6 && !SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
			(Protocol == AF_INET && (!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
			!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
				goto JumpToRestart;

	//Socket selecting
		RecvLen = SocketSelecting(REQUEST_PROCESS_DNSCURVE, IPPROTO_TCP, TCPSocketDataList, SendBuffer.get(), DataLength, RecvBuffer.get(), LARGE_PACKET_MAXSIZE, nullptr);
		if (RecvLen < (ssize_t)DNS_PACKET_MINSIZE)
		{
			goto JumpToRestart;
		}
		else {
		//Check Signature.
			if (PacketTarget == nullptr || !DNSCruveGetSignatureData(RecvBuffer.get() + DNS_PACKET_RR_LOCATE(RecvBuffer.get()), ServerType) || 
				CheckEmptyBuffer(PacketTarget->ServerFingerprint, crypto_box_PUBLICKEYBYTES) || 
				CheckEmptyBuffer(PacketTarget->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN))
					goto JumpToRestart;
		}

	//Wait for sending again.
		SleepTime_SignatureRequest += Parameter.FileRefreshTime;
		continue;

	//Jump here to restart.
	JumpToRestart:
		SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		TCPSocketDataList.front().Socket = 0;

	//Print error log.
		DNSCurvePrintLog(ServerType, Message);
		if (!Message.empty())
		{
			Message.append(L"TCP get signature data error");
			PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
		}

	//Send request again.
		sodium_memzero(RecvBuffer.get(), LARGE_PACKET_MAXSIZE);
		if (!Parameter.AlternateMultipleRequest)
		{
			if (ServerType == DNSCURVE_MAIN_IPV6)
				++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
			else if (ServerType == DNSCURVE_MAIN_IPV4)
				++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_TCP_IPV4];
		}

		Sleep(SENDING_INTERVAL_TIME);
	}

	SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
	PrintError(LOG_LEVEL_2, LOG_ERROR_SYSTEM, L"DNSCurve TCP Signature Request module Monitor terminated", 0, nullptr, 0);
	return true;
}

//Send UDP request to get Signature Data of servers
bool DNSCurveUDPSignatureRequest(
	const uint16_t Protocol, 
	const bool IsAlternate)
{
//Initialization(Part 1)
	std::shared_ptr<uint8_t> SendBuffer(new uint8_t[PACKET_MAXSIZE]()), RecvBuffer(new uint8_t[PACKET_MAXSIZE]());
	sodium_memzero(SendBuffer.get(), PACKET_MAXSIZE);
	sodium_memzero(RecvBuffer.get(), PACKET_MAXSIZE);
	std::vector<SOCKET_DATA> UDPSocketDataList(1U);
	sodium_memzero(&UDPSocketDataList.front(), sizeof(UDPSocketDataList.front()));

//Make packet data(Part 1).
	size_t DataLength = sizeof(dns_hdr);
	auto DNS_Header = (pdns_hdr)SendBuffer.get();
#if defined(ENABLE_PCAP)
	DNS_Header->ID = Parameter.DomainTest_ID;
#else
	DNS_Header->ID = htons(U16_NUM_ONE);
#endif
	DNS_Header->Flags = htons(DNS_STANDARD);
	DNS_Header->Question = htons(U16_NUM_ONE);
	if (Protocol == AF_INET6)
	{
		if (IsAlternate)
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.ProviderName, SendBuffer.get() + DataLength);
		else 
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_IPv6.ProviderName, SendBuffer.get() + DataLength);
	}
	else if (Protocol == AF_INET)
	{
		if (IsAlternate)
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.ProviderName, SendBuffer.get() + DataLength);
		else 
			DataLength += CharToDNSQuery(DNSCurveParameter.DNSCurve_Target_Server_IPv4.ProviderName, SendBuffer.get() + DataLength);
	}
	else {
		return false;
	}
	((pdns_qry)(SendBuffer.get() + DataLength))->Type = htons(DNS_RECORD_TXT);
	((pdns_qry)(SendBuffer.get() + DataLength))->Classes = htons(DNS_CLASS_IN);
	DataLength += sizeof(dns_qry);

//EDNS Label
	DataLength = AddEDNSLabelToAdditionalRR(SendBuffer.get(), DataLength, PACKET_MAXSIZE, nullptr);

//Socket initialization(Part 1)
	size_t ServerType = 0;
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	if (Protocol == AF_INET6)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN6)&UDPSocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&UDPSocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
			ServerType = DNSCURVE_ALTERNATE_IPV6;
		}
		else { //Main
			((PSOCKADDR_IN6)&UDPSocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&UDPSocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv6;
			ServerType = DNSCURVE_MAIN_IPV6;
		}

		UDPSocketDataList.front().AddrLen = sizeof(sockaddr_in6);
		UDPSocketDataList.front().SockAddr.ss_family = AF_INET6;
	}
	else if (Protocol == AF_INET)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN)&UDPSocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&UDPSocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
			ServerType = DNSCURVE_ALTERNATE_IPV4;
		}
		else { //Main
			((PSOCKADDR_IN)&UDPSocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&UDPSocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_IPv4;
			ServerType = DNSCURVE_MAIN_IPV4;
		}

		UDPSocketDataList.front().AddrLen = sizeof(sockaddr_in);
		UDPSocketDataList.front().SockAddr.ss_family = AF_INET;
	}
	else {
		return false;
	}

//Initialization(Part 2)
	size_t SleepTime_SignatureRequest = 0, SpeedTime_SignatureRequest = DNSCurveParameter.KeyRecheckTime;
	std::wstring Message;
	ssize_t RecvLen = 0;

//Send request.
	for (;;)
	{
	//Sleep time controller
		if (SleepTime_SignatureRequest > 0)
		{
			if (SpeedTime_SignatureRequest != DNSCurveParameter.KeyRecheckTime)
			{
				SpeedTime_SignatureRequest = DNSCurveParameter.KeyRecheckTime;
			}
			else if (SleepTime_SignatureRequest < SpeedTime_SignatureRequest)
			{
				SleepTime_SignatureRequest += Parameter.FileRefreshTime;

				Sleep(Parameter.FileRefreshTime);
				continue;
			}

			SleepTime_SignatureRequest = 0;
		}

	//Socket initialization(Part 2)
		if (Protocol == AF_INET6)
			UDPSocketDataList.front().Socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
		else if (Protocol == AF_INET)
			UDPSocketDataList.front().Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		else 
			goto JumpToRestart;
		if (!SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
			!SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
			(Protocol == AF_INET6 && !SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
			(Protocol == AF_INET && (!SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
			!SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
				goto JumpToRestart;

	//Socket selecting
		RecvLen = SocketSelecting(REQUEST_PROCESS_DNSCURVE, IPPROTO_UDP, UDPSocketDataList, SendBuffer.get(), DataLength, RecvBuffer.get(), PACKET_MAXSIZE, nullptr);
		if (RecvLen < (ssize_t)DNS_PACKET_MINSIZE)
		{
			goto JumpToRestart;
		}
		else {
		//Check Signature.
			if (PacketTarget == nullptr || !DNSCruveGetSignatureData(RecvBuffer.get() + DNS_PACKET_RR_LOCATE(RecvBuffer.get()), ServerType) || 
				CheckEmptyBuffer(PacketTarget->ServerFingerprint, crypto_box_PUBLICKEYBYTES) || 
				CheckEmptyBuffer(PacketTarget->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN))
					goto JumpToRestart;
		}

	//Wait for sending again.
		SleepTime_SignatureRequest += Parameter.FileRefreshTime;
		continue;

	//Jump here to restart.
	JumpToRestart:
		SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		UDPSocketDataList.front().Socket = 0;

	//Print error log.
		DNSCurvePrintLog(ServerType, Message);
		if (!Message.empty())
		{
			Message.append(L"UDP get signature data error");
			PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
		}

	//Send request again.
		sodium_memzero(RecvBuffer.get(), PACKET_MAXSIZE);
		if (!Parameter.AlternateMultipleRequest)
		{
			if (ServerType == DNSCURVE_MAIN_IPV6)
				++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
			else if (ServerType == DNSCURVE_MAIN_IPV4)
				++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
		}

		Sleep(SENDING_INTERVAL_TIME);
	}

	SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
	PrintError(LOG_LEVEL_2, LOG_ERROR_SYSTEM, L"DNSCurve UDP Signature Request module Monitor terminated", 0, nullptr, 0);
	return true;
}

//Get Signature Data of server from packets
bool DNSCruveGetSignatureData(
	const uint8_t *Buffer, 
	const size_t ServerType)
{
	if (((pdns_record_txt)Buffer)->Name == htons(DNS_POINTER_QUERY) && 
		((pdns_record_txt)Buffer)->Length == htons(((pdns_record_txt)Buffer)->TXT_Length + 1U) && ((pdns_record_txt)Buffer)->TXT_Length == DNSCRYPT_RECORD_TXT_LEN)
	{
		if (sodium_memcmp(&((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->CertMagicNumber, DNSCRYPT_CERT_MAGIC, sizeof(uint16_t)) == 0 && 
			((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->MajorVersion == htons(DNSCURVE_VERSION_MAJOR) && 
			((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->MinorVersion == DNSCURVE_VERSION_MINOR)
		{
			unsigned long long SignatureLength = 0;

		//Get Send Magic Number, Server Fingerprint and Precomputation Key.
			PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
			if (!DNSCurvePacketTargetSetting(ServerType, &PacketTarget))
				return false;

		//Check Signature.
			std::shared_ptr<uint8_t> DeBuffer(new uint8_t[PACKET_MAXSIZE]());
			memset(DeBuffer.get(), 0, PACKET_MAXSIZE);
			if (PacketTarget == nullptr || 
				crypto_sign_open(
					(unsigned char *)DeBuffer.get(), 
					&SignatureLength, 
					(unsigned char *)(Buffer + sizeof(dns_record_txt) + sizeof(dnscurve_txt_hdr)), ((pdns_record_txt)Buffer)->TXT_Length - sizeof(dnscurve_txt_hdr), 
					PacketTarget->ServerPublicKey) != 0)
			{
				std::wstring Message;
				DNSCurvePrintLog(ServerType, Message);
				if (!Message.empty())
				{
					Message.append(L"Fingerprint signature validation error");
					PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
				}

				return false;
			}

		//Signature available time check
			auto TimeValues = time(nullptr);
			if (PacketTarget->ServerFingerprint != nullptr && 
				TimeValues >= (time_t)ntohl(((pdnscurve_txt_signature)DeBuffer.get())->CertTime_Begin) && 
				TimeValues <= (time_t)ntohl(((pdnscurve_txt_signature)DeBuffer.get())->CertTime_End))
			{
				memcpy_s(PacketTarget->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN, ((pdnscurve_txt_signature)DeBuffer.get())->MagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
				memcpy_s(PacketTarget->ServerFingerprint, crypto_box_PUBLICKEYBYTES, ((pdnscurve_txt_signature)DeBuffer.get())->PublicKey, crypto_box_PUBLICKEYBYTES);
				if (!DNSCurveParameter.ClientEphemeralKey)
				{
					if (crypto_box_beforenm(
							PacketTarget->PrecomputationKey, 
							PacketTarget->ServerFingerprint, 
							DNSCurveParameter.Client_SecretKey) != 0)
					{
						std::wstring Message;
						DNSCurvePrintLog(ServerType, Message);
						if (!Message.empty())
						{
							Message.append(L"Key calculating error");
							PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
						}

						return false;
					}
				}

				return true;
			}
			else {
				std::wstring Message;
				DNSCurvePrintLog(ServerType, Message);
				if (!Message.empty())
				{
					Message.append(L"Fingerprint signature validation error");
					PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
				}
			}
		}
	}

	return false;
}

//Transmission of DNSCurve TCP protocol
size_t DNSCurveTCPRequest(
	const uint8_t *OriginalSend, 
	const size_t SendSize, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize)
{
//Initialization
	std::vector<SOCKET_DATA> TCPSocketDataList(1U);
	sodium_memzero(&TCPSocketDataList.front(), sizeof(TCPSocketDataList.front()));
	DNSCURVE_SOCKET_SELECTING_DATA TCPSocketSelectingData;
	memset(&TCPSocketSelectingData, 0, sizeof(TCPSocketSelectingData));
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	bool *IsAlternate = nullptr;
	size_t *AlternateTimeoutTimes = nullptr;
	sodium_memzero(OriginalRecv, RecvSize);
	auto SendBuffer = OriginalRecv;

//Socket initialization
	TCPSocketSelectingData.ServerType = DNSCurveSelectTargetSocket(&TCPSocketDataList.front(), &PacketTarget, &IsAlternate, &AlternateTimeoutTimes, IPPROTO_TCP);
	if (TCPSocketSelectingData.ServerType == 0)
	{
		SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		PrintError(LOG_LEVEL_2, LOG_ERROR_NETWORK, L"DNSCurve TCP socket initialization error", 0, nullptr, 0);

		return EXIT_FAILURE;
	}

//Make Precomputation Key between client and server.
	uint8_t *Client_PublicKey = nullptr, *PrecomputationKey = nullptr;
	std::shared_ptr<uint8_t> Client_PublicKey_PTR;
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR;
	if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
	{
		std::shared_ptr<uint8_t> Client_PublicKey_PTR_Temp(new uint8_t[crypto_box_PUBLICKEYBYTES]());
		Client_PublicKey_PTR.swap(Client_PublicKey_PTR_Temp);

		DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR_Temp(crypto_box_BEFORENMBYTES);
		PrecomputationKeyPTR_Temp.Swap(PrecomputationKeyPTR);
		Client_PublicKey = Client_PublicKey_PTR.get();
		PrecomputationKey = PrecomputationKeyPTR.Buffer;
		if (!DNSCurvePrecomputationKeySetting(PrecomputationKey, Client_PublicKey, PacketTarget->ServerFingerprint))
		{
			SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			return EXIT_FAILURE;
		}
	}
	else {
		PrecomputationKey = PacketTarget->PrecomputationKey, Client_PublicKey = DNSCurveParameter.Client_PublicKey;
	}

//Socket attribute setting(Non-blocking mode)
	if (!SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr))
	{
		SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		return EXIT_FAILURE;
	}

//Make encryption or normal packet.
	ssize_t RecvLen = DNSCurvePacketEncryption(IPPROTO_TCP, PacketTarget->SendMagicNumber, Client_PublicKey, PrecomputationKey, OriginalSend, SendSize, SendBuffer, RecvSize);
	if (RecvLen < (ssize_t)DNS_PACKET_MINSIZE)
	{
		SocketSetting(TCPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		return EXIT_FAILURE;
	}

//Socket selecting structure initialization
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> TCPSocketSelectingList;
	if (DNSCurveParameter.IsEncryption) //Encryption mode
	{
		TCPSocketSelectingData.PrecomputationKey = PrecomputationKey;
		TCPSocketSelectingData.ReceiveMagicNumber = PacketTarget->ReceiveMagicNumber;
	}

	TCPSocketSelectingData.SendBuffer = SendBuffer;
	TCPSocketSelectingData.SendSize = RecvLen;
	TCPSocketSelectingData.Length = 0;
	TCPSocketSelectingData.IsPacketSend = false;
	TCPSocketSelectingList.push_back(TCPSocketSelectingData);

//Socket selecting
	ssize_t ErrorCode = 0;
	RecvLen = DNSCurveSocketSelecting(IPPROTO_TCP, TCPSocketDataList, TCPSocketSelectingList, OriginalRecv, RecvSize, &ErrorCode);
	if (ErrorCode == WSAETIMEDOUT && !Parameter.AlternateMultipleRequest) //Mark timeout.
	{
		if (TCPSocketDataList.front().AddrLen == sizeof(sockaddr_in6))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
		else if (TCPSocketDataList.front().AddrLen == sizeof(sockaddr_in))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
	}

	return RecvLen;
}

//Transmission of DNSCurve TCP protocol(Multiple threading)
size_t DNSCurveTCPRequestMultiple(
	const uint8_t *OriginalSend, 
	const size_t SendSize, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize)
{
//Key initialization
	uint8_t *PrecomputationKey = nullptr, *Alternate_PrecomputationKey = nullptr;
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR, Alternate_PrecomputationKeyPTR;
	if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
	{
		DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR_Temp(crypto_box_BEFORENMBYTES), Alternate_PrecomputationKeyPTR_Temp(crypto_box_BEFORENMBYTES);

	//Main
		PrecomputationKeyPTR_Temp.Swap(PrecomputationKeyPTR);
		PrecomputationKey = PrecomputationKeyPTR.Buffer;

	//Alternate
		Alternate_PrecomputationKeyPTR_Temp.Swap(Alternate_PrecomputationKeyPTR);
		Alternate_PrecomputationKey = Alternate_PrecomputationKeyPTR.Buffer;
	}

//Initialization(Part 1)
	std::vector<SOCKET_DATA> TCPSocketDataList;
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> TCPSocketSelectingList;
	std::shared_ptr<uint8_t> SendBuffer, Alternate_SendBuffer;
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	size_t DataLength = 0, Alternate_DataLength = 0;
	sodium_memzero(OriginalRecv, RecvSize);

//Socket precomputation
	DNSCurveSocketPrecomputation(IPPROTO_TCP, OriginalSend, SendSize, RecvSize, &PrecomputationKey, &Alternate_PrecomputationKey, &PacketTarget, 
		TCPSocketDataList, TCPSocketSelectingList, SendBuffer, DataLength, Alternate_SendBuffer, Alternate_DataLength);
	if (TCPSocketDataList.empty() || TCPSocketDataList.size() != TCPSocketSelectingList.size())
		return EXIT_FAILURE;

//Socket selecting structure initialization
	for (size_t Index = 0;Index < TCPSocketDataList.size();++Index)
	{
	//Encryption mode
		if (DNSCurveParameter.IsEncryption)
		{
			DNSCurvePacketTargetSetting(TCPSocketSelectingList.at(Index).ServerType, &PacketTarget);
			TCPSocketSelectingList.at(Index).ReceiveMagicNumber = PacketTarget->ReceiveMagicNumber;

		//Alternate
			if (TCPSocketSelectingList.at(Index).ServerType == DNSCURVE_ALTERNATE_IPV6 || TCPSocketSelectingList.at(Index).ServerType == DNSCURVE_ALTERNATE_IPV4)
			{
				TCPSocketSelectingList.at(Index).PrecomputationKey = Alternate_PrecomputationKey;
				TCPSocketSelectingList.at(Index).SendBuffer = Alternate_SendBuffer.get();
				TCPSocketSelectingList.at(Index).SendSize = Alternate_DataLength;
			}
		//Main
			else {
				TCPSocketSelectingList.at(Index).PrecomputationKey = PrecomputationKey;
				TCPSocketSelectingList.at(Index).SendBuffer = SendBuffer.get();
				TCPSocketSelectingList.at(Index).SendSize = DataLength;
			}
		}
	//Normal mode
		else {
			TCPSocketSelectingList.at(Index).SendBuffer = SendBuffer.get();
			TCPSocketSelectingList.at(Index).SendSize = DataLength;
		}

		TCPSocketSelectingList.at(Index).Length = 0;
		TCPSocketSelectingList.at(Index).IsPacketSend = false;
	}

//Socket selecting
	ssize_t ErrorCode = 0, RecvLen = DNSCurveSocketSelecting(IPPROTO_TCP, TCPSocketDataList, TCPSocketSelectingList, OriginalRecv, RecvSize, &ErrorCode);
	if (ErrorCode == WSAETIMEDOUT && !Parameter.AlternateMultipleRequest) //Mark timeout.
	{
		if (TCPSocketDataList.front().AddrLen == sizeof(sockaddr_in6))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		else if (TCPSocketDataList.front().AddrLen == sizeof(sockaddr_in))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
	}

	return RecvLen;
}

//Transmission of DNSCurve UDP protocol
size_t DNSCurveUDPRequest(
	const uint8_t *OriginalSend, 
	const size_t SendSize, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize)
{
//Initialization
	std::vector<SOCKET_DATA> UDPSocketDataList(1U);
	sodium_memzero(&UDPSocketDataList.front(), sizeof(UDPSocketDataList.front()));
	DNSCURVE_SOCKET_SELECTING_DATA UDPSocketSelectingData;
	memset(&UDPSocketSelectingData, 0, sizeof(UDPSocketSelectingData));
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	bool *IsAlternate = nullptr;
	size_t *AlternateTimeoutTimes = nullptr;
	sodium_memzero(OriginalRecv, RecvSize);
	auto SendBuffer = OriginalRecv;

//Socket initialization
	UDPSocketSelectingData.ServerType = DNSCurveSelectTargetSocket(&UDPSocketDataList.front(), &PacketTarget, &IsAlternate, &AlternateTimeoutTimes, IPPROTO_UDP);
	if (UDPSocketSelectingData.ServerType == 0)
	{
		SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		PrintError(LOG_LEVEL_2, LOG_ERROR_NETWORK, L"DNSCurve UDP socket initialization error", 0, nullptr, 0);

		return EXIT_FAILURE;
	}

//Make Precomputation Key between client and server.
	uint8_t *Client_PublicKey = nullptr, *PrecomputationKey = nullptr;
	std::shared_ptr<uint8_t> Client_PublicKey_PTR, PrecomputationKeyPTR;
	if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
	{
		std::shared_ptr<uint8_t> Client_PublicKey_PTR_Temp(new uint8_t[crypto_box_PUBLICKEYBYTES]()), PrecomputationKeyPTR_Temp(new uint8_t[crypto_box_BEFORENMBYTES]());
		Client_PublicKey_PTR.swap(Client_PublicKey_PTR_Temp);
		PrecomputationKeyPTR.swap(PrecomputationKeyPTR_Temp);
		Client_PublicKey = Client_PublicKey_PTR.get();
		PrecomputationKey = PrecomputationKeyPTR.get();
		if (!DNSCurvePrecomputationKeySetting(PrecomputationKey, Client_PublicKey, PacketTarget->ServerFingerprint))
		{
			SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			return EXIT_FAILURE;
		}
	}
	else {
		PrecomputationKey = PacketTarget->PrecomputationKey, Client_PublicKey = DNSCurveParameter.Client_PublicKey;
	}

//Socket attribute setting(Timeout) and UDP connecting
	if (!SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_TIMEOUT, true, &DNSCurveParameter.DNSCurve_SocketTimeout_Unreliable) || 
		SocketConnecting(IPPROTO_UDP, UDPSocketDataList.front().Socket, (PSOCKADDR)&UDPSocketDataList.front().SockAddr, UDPSocketDataList.front().AddrLen, nullptr, 0) == EXIT_FAILURE)
	{
		SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		return EXIT_FAILURE;
	}

//Make encryption or normal packet.
	ssize_t RecvLen = DNSCurvePacketEncryption(IPPROTO_UDP, PacketTarget->SendMagicNumber, Client_PublicKey, PrecomputationKey, OriginalSend, SendSize, SendBuffer, RecvSize);
	if (RecvLen < (ssize_t)DNS_PACKET_MINSIZE)
	{
		SocketSetting(UDPSocketDataList.front().Socket, SOCKET_SETTING_CLOSE, false, nullptr);
		return EXIT_FAILURE;
	}

//Socket selecting structure initialization
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> UDPSocketSelectingList;
	if (DNSCurveParameter.IsEncryption) //Encryption mode
	{
		UDPSocketSelectingData.PrecomputationKey = PrecomputationKey;
		UDPSocketSelectingData.ReceiveMagicNumber = PacketTarget->ReceiveMagicNumber;
		UDPSocketSelectingData.SendBuffer = SendBuffer;
		UDPSocketSelectingData.SendSize = RecvLen;
	}
	else { //Normal mode
		UDPSocketSelectingData.SendBuffer = (uint8_t *)OriginalSend;
		UDPSocketSelectingData.SendSize = SendSize;
	}

	UDPSocketSelectingData.Length = 0;
	UDPSocketSelectingData.IsPacketSend = false;
	UDPSocketSelectingList.push_back(UDPSocketSelectingData);

//Socket selecting
	ssize_t ErrorCode = 0;
	RecvLen = DNSCurveSocketSelecting(IPPROTO_UDP, UDPSocketDataList, UDPSocketSelectingList, OriginalRecv, RecvSize, &ErrorCode);
	if (ErrorCode == WSAETIMEDOUT && !Parameter.AlternateMultipleRequest) //Mark timeout.
	{
		if (UDPSocketDataList.front().AddrLen == sizeof(sockaddr_in6))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		else if (UDPSocketDataList.front().AddrLen == sizeof(sockaddr_in))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
	}

	return RecvLen;
}

//Transmission of DNSCurve UDP protocol(Multiple threading)
size_t DNSCurveUDPRequestMultiple(
	const uint8_t *OriginalSend, 
	const size_t SendSize, 
	uint8_t *OriginalRecv, 
	const size_t RecvSize)
{
//Key initialization
	uint8_t *PrecomputationKey = nullptr, *Alternate_PrecomputationKey = nullptr;
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR, Alternate_PrecomputationKeyPTR;
	if (DNSCurveParameter.IsEncryption && DNSCurveParameter.ClientEphemeralKey)
	{
		DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> PrecomputationKeyPTR_Temp(crypto_box_BEFORENMBYTES), Alternate_PrecomputationKeyPTR_Temp(crypto_box_BEFORENMBYTES);

	//Main
		PrecomputationKeyPTR_Temp.Swap(PrecomputationKeyPTR);
		PrecomputationKey = PrecomputationKeyPTR.Buffer;

	//Alternate
		Alternate_PrecomputationKeyPTR_Temp.Swap(Alternate_PrecomputationKeyPTR);
		Alternate_PrecomputationKey = Alternate_PrecomputationKeyPTR.Buffer;
	}

//Initialization(Part 1)
	std::vector<SOCKET_DATA> UDPSocketDataList;
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> UDPSocketSelectingList;
	std::shared_ptr<uint8_t> SendBuffer, Alternate_SendBuffer;
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	size_t DataLength = 0, Alternate_DataLength = 0;
	sodium_memzero(OriginalRecv, RecvSize);

//Socket precomputation
	DNSCurveSocketPrecomputation(IPPROTO_UDP, OriginalSend, SendSize, RecvSize, &PrecomputationKey, &Alternate_PrecomputationKey, &PacketTarget, 
		UDPSocketDataList, UDPSocketSelectingList, SendBuffer, DataLength, Alternate_SendBuffer, Alternate_DataLength);
	if (UDPSocketDataList.empty() || UDPSocketDataList.size() != UDPSocketSelectingList.size())
		return EXIT_FAILURE;

//Socket selecting structure initialization
	for (size_t Index = 0;Index < UDPSocketDataList.size();++Index)
	{
	//Encryption mode
		if (DNSCurveParameter.IsEncryption)
		{
			DNSCurvePacketTargetSetting(UDPSocketSelectingList.at(Index).ServerType, &PacketTarget);
			UDPSocketSelectingList.at(Index).ReceiveMagicNumber = PacketTarget->ReceiveMagicNumber;

		//Alternate
			if (UDPSocketSelectingList.at(Index).ServerType == DNSCURVE_ALTERNATE_IPV6 || UDPSocketSelectingList.at(Index).ServerType == DNSCURVE_ALTERNATE_IPV4)
			{
				UDPSocketSelectingList.at(Index).PrecomputationKey = Alternate_PrecomputationKey;
				UDPSocketSelectingList.at(Index).SendBuffer = Alternate_SendBuffer.get();
				UDPSocketSelectingList.at(Index).SendSize = Alternate_DataLength;
			}
		//Main
			else {
				UDPSocketSelectingList.at(Index).PrecomputationKey = PrecomputationKey;
				UDPSocketSelectingList.at(Index).SendBuffer = SendBuffer.get();
				UDPSocketSelectingList.at(Index).SendSize = DataLength;
			}
		}
	//Normal mode
		else {
			UDPSocketSelectingList.at(Index).SendBuffer = (uint8_t *)OriginalSend;
			UDPSocketSelectingList.at(Index).SendSize = SendSize;
		}

		UDPSocketSelectingList.at(Index).Length = 0;
		UDPSocketSelectingList.at(Index).IsPacketSend = false;
	}

//Socket selecting
	ssize_t ErrorCode = 0, RecvLen = DNSCurveSocketSelecting(IPPROTO_UDP, UDPSocketDataList, UDPSocketSelectingList, OriginalRecv, RecvSize, &ErrorCode);
	if (ErrorCode == WSAETIMEDOUT && !Parameter.AlternateMultipleRequest) //Mark timeout.
	{
		if (UDPSocketDataList.front().AddrLen == sizeof(sockaddr_in6))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		else if (UDPSocketDataList.front().AddrLen == sizeof(sockaddr_in))
			++AlternateSwapList.TimeoutTimes[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
	}

	return RecvLen;
}
#endif
