﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2015 Chengr28
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


#include "Base.h"

extern CONFIGURATION_TABLE Parameter;

//Check empty buffer
bool __fastcall CheckEmptyBuffer(const void *Buffer, const size_t Length)
{
//Null pointer
	if (Buffer == nullptr)
		return false;

//Scan all data.
	for (size_t Index = 0;Index < Length;++Index)
	{
		if (((uint8_t *)Buffer)[Index] != 0)
			return false;
	}

	return true;
}

//Convert host values to network byte order with 16 bits(Force)
uint16_t __fastcall hton16_Force(const uint16_t Value)
{
	uint8_t *Result = (uint8_t *)&Value;
	return (uint16_t)(Result[0] << 8U | Result[1U]);
}

/* Redirect to hton16_Force.
//Convert network byte order to host values with 16 bits(Force)
uint16_t __fastcall ntoh16_Force(const uint16_t Value)
{
	uint8_t *Result = (uint8_t *)&Value;
	return (uint16_t)(Result[0] << 8U | Result[1U]);
}
*/

//Convert host values to network byte order with 32 bits(Force)
uint32_t __fastcall hton32_Force(const uint32_t Value)
{
	uint8_t *Result = (uint8_t *)&Value;
	return (uint32_t)(Result[0] << 24U | Result[1U] << 16U | Result[2U] << 8U | Result[3U]);
}

/* Redirect to hton32_Force.
//Convert network byte order to host values with 32 bits(Force)
uint32_t __fastcall ntoh32_Force(const uint32_t Value)
{
	uint8_t *Result = (uint8_t *)&Value;
	return (uint32_t)(Result[0] << 24U | Result[1U] << 16U | Result[2U] << 8U | Result[3U]);
}
*/

//Convert host values to network byte order with 64 bits
uint64_t __fastcall hton64(const uint64_t Value)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return (((uint64_t)htonl((int32_t)((Value << 32U) >> 32U))) << 32U) | (uint32_t)htonl((int32_t)(Value >> 32U));
#else //BIG_ENDIAN
	return Value;
#endif
}

//Convert network byte order to host values with 64 bits
uint64_t __fastcall ntoh64(const uint64_t Value)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return (((uint64_t)ntohl((int32_t)((Value << 32U) >> 32U))) << 32U) | (uint32_t)ntohl((int32_t)(Value >> 32U));
#else //BIG_ENDIAN
	return Value;
#endif
}

//Convert multiple bytes to wide char string
bool __fastcall MBSToWCSString(std::wstring &Target, const char *Buffer)
{
//Check buffer.
	if (CheckEmptyBuffer(Buffer, strnlen_s(Buffer, LARGE_PACKET_MAXSIZE)))
		return false;

//Convert string.
	std::shared_ptr<wchar_t> TargetPTR(new wchar_t[strnlen_s(Buffer, LARGE_PACKET_MAXSIZE) + 1U]());
	wmemset(TargetPTR.get(), 0, strnlen_s(Buffer, LARGE_PACKET_MAXSIZE) + 1U);
#if defined(PLATFORM_WIN)
	if (MultiByteToWideChar(CP_ACP, 0, Buffer, MBSTOWCS_NULLTERMINATE, TargetPTR.get(), (int)(strnlen_s(Buffer, LARGE_PACKET_MAXSIZE) + 1U)) == 0)
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	if (mbstowcs(TargetPTR.get(), Buffer, strnlen(Buffer, LARGE_PACKET_MAXSIZE) + 1U) == RETURN_ERROR)
#endif
		return false;
	else 
		Target = TargetPTR.get();

	return true;
}

//Convert lowercase/uppercase words to uppercase/lowercase words(Character version)
void __fastcall CaseConvert(const bool IsLowerToUpper, PSTR Buffer, const size_t Length)
{
	for (size_t Index = 0;Index < Length;++Index)
	{
	//Lowercase to uppercase
		if (IsLowerToUpper)
			Buffer[Index] = (char)toupper(Buffer[Index]);
	//Uppercase to lowercase
		else 
			Buffer[Index] = (char)tolower(Buffer[Index]);
	}

	return;
}

//Convert lowercase/uppercase words to uppercase/lowercase words(String version)
void __fastcall CaseConvert(const bool IsLowerToUpper, std::string &Buffer)
{
	for (auto &StringIter:Buffer)
	{
	//Lowercase to uppercase
		if (IsLowerToUpper)
			StringIter = (char)toupper(StringIter);
	//Uppercase to lowercase
		else 
			StringIter = (char)tolower(StringIter);
	}

	return;
}

#if (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
//Linux and Mac OS X compatible with GetTickCount64
uint64_t GetTickCount64(void)
{
	std::shared_ptr<timeval> CurrentTime(new timeval());
	memset(CurrentTime.get(), 0, sizeof(timeval));
	gettimeofday(CurrentTime.get(), nullptr);
	return (uint64_t)CurrentTime->tv_sec * SECOND_TO_MILLISECOND + (uint64_t)CurrentTime->tv_usec / MICROSECOND_TO_MILLISECOND;
}

//Windows XP with SP3 support
#elif (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
//Verify version of system(Greater than Windows Vista)
BOOL WINAPI IsGreaterThanVista(void)
{
	std::shared_ptr<OSVERSIONINFOEXW> OSVI(new OSVERSIONINFOEXW());
	memset(OSVI.get(), 0, sizeof(OSVERSIONINFOEXW));
	DWORDLONG dwlConditionMask = 0;

//Initialization
	ZeroMemory(OSVI.get(), sizeof(OSVERSIONINFOEXW));
	OSVI->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
	OSVI->dwMajorVersion = 6U; //Greater than Windows Vista.
	OSVI->dwMinorVersion = 0;

//System Major version > dwMajorVersion
	VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER);
	if (VerifyVersionInfoW(OSVI.get(), VER_MAJORVERSION, dwlConditionMask))
		return TRUE;

//Sytem Major version = dwMajorVersion and Minor version > dwMinorVersion
	VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_EQUAL);
	VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER);
	return VerifyVersionInfoW(OSVI.get(), VER_MAJORVERSION|VER_MINORVERSION, dwlConditionMask);
}

//Try to load library to get pointers of functions
BOOL WINAPI GetFunctionPointer(const size_t FunctionType)
{
//GetTickCount64() function
	if (FunctionType == FUNCTION_GETTICKCOUNT64)
	{
		Parameter.GetTickCount64_DLL = LoadLibraryW(L"Kernel32.dll");
		if (Parameter.GetTickCount64_DLL != nullptr)
		{
			Parameter.GetTickCount64_PTR = (GetTickCount64Function)GetProcAddress(Parameter.GetTickCount64_DLL, "GetTickCount64");
			if (Parameter.GetTickCount64_PTR == nullptr)
			{
				FreeLibrary(Parameter.GetTickCount64_DLL);
				return FALSE;
			}
		}
	}
//inet_ntop() function
	else if (FunctionType == FUNCTION_INET_NTOP)
	{
		Parameter.Inet_Ntop_DLL = LoadLibraryW(L"ws2_32.dll");
		if (Parameter.Inet_Ntop_DLL != nullptr)
		{
			Parameter.Inet_Ntop_PTR = (Inet_Ntop_Function)GetProcAddress(Parameter.Inet_Ntop_DLL, "inet_ntop");
			if (Parameter.Inet_Ntop_PTR == nullptr)
			{
				FreeLibrary(Parameter.Inet_Ntop_DLL);
				return FALSE;
			}
		}
	}

	return TRUE;
}
#endif