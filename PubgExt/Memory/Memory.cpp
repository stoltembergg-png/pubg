#include "pch.h"
#include "Memory.h"

#include <thread>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <vector>
#include <Globals.h>
Memory::Memory()
{
	LOG("loading driver...\n");
}

Memory::~Memory()
{
	driver.Cleanup();
	PROCESS_INITIALIZED = false;
}

bool Memory::Init(std::string process_name, bool memMap, bool debug)
{
	if (!driver.Initialize())
	{
		LOG("[!] Initialization failed! Is the driver loaded?\n");
		return false;
	}
	LOG("[+] Driver initialized successfully.\n");
	std::wstring wProcessName(process_name.begin(), process_name.end());
	this->current_process.PID = driver.GetProcessId(wProcessName.c_str());

	if (!this->current_process.PID)
	{
		LOG("[!] Could not get PID from [%s]!\n", process_name.c_str());
		return false;
	}
	LOG("[+] Found PID: %i\n", this->current_process.PID);
    driver.SetCurrentPid(this->current_process.PID);

	this->current_process.process_name = process_name;
	this->current_process.base_address = driver.GetModuleBase(this->current_process.PID, wProcessName.c_str());
	LOG("[+] Found Base Address: 0x%p\n", this->current_process.base_address);
	if (!this->current_process.base_address)
	{
		LOG("[!] Could not get base address!\n");
		return false;
	}
	LOG("[+] Base Address: 0x%p\n", this->current_process.base_address);
	driver.SetBaseAddress(this->current_process.base_address);
	this->current_process.base_size = 0x5000000;

	LOG("Process information of %s\n", process_name.c_str());
	LOG("PID: %i\n", this->current_process.PID);
	LOG("Base Address: 0x%p\n", this->current_process.base_address);

	PROCESS_INITIALIZED = TRUE;

	return true;
}

DWORD Memory::GetPidFromName(std::string process_name)
{
	std::wstring wProcessName(process_name.begin(), process_name.end());
	return driver.GetProcessId(wProcessName.c_str());
}

std::vector<int> Memory::GetPidListFromName(std::string name)
{
	std::vector<int> list;
	list.push_back(GetPidFromName(name));
	return list;
}

std::vector<std::string> Memory::GetModuleList(std::string process_name)
{
	return std::vector<std::string>(); 
}

size_t Memory::GetBaseAddress(std::string module_name)
{
	std::wstring str(module_name.begin(), module_name.end());
	if (!Modules.contains(str))
	{
		size_t base = driver.GetModuleBase(this->current_process.PID, str.c_str());
		if (!base)
		{
			LOG("[!] Couldn't find Base Address for %s\n", module_name.c_str());
			return 0;
		}

		LOG("[+] Found Base Address for %s at 0x%p\n", module_name.c_str(), base);
		Modules[str] = base;
		return base;
	}
	return Modules[str];
}

int Memory::ReturnPid()
{
	return this->current_process.PID;
}

size_t Memory::GetBaseSize(std::string module_name)
{
	return 0x500000;
}

uintptr_t Memory::GetExportTableAddress(std::string import, std::string process, std::string module)
{
	return 0;
}

uintptr_t Memory::GetImportTableAddress(std::string import, std::string process, std::string module)
{
	return 0;
}

bool Memory::FixCr3()
{
	uintptr_t data_anchor = this->current_process.base_address + SDK.Decrypt;
	LOG("[*] Trying Data Anchor to find isolated CR3 at 0x%p...\n", (void*)data_anchor);
	uintptr_t cr3 = driver.FindRealCr3(this->current_process.PID, this->current_process.base_address, data_anchor);

	if (!cr3) {
		LOG("[-] Data Anchor CR3 failed. Falling back to Base CR3...\n");
		cr3 = driver.FindRealCr3(this->current_process.PID, this->current_process.base_address, this->current_process.base_address);
	}

	if (!cr3) {
		cr3 = driver.GetProcessCr3(this->current_process.PID);
		if (cr3) {
			char test_buffer[2] = { 0 };
			if (!driver.ReadMemory(this->current_process.PID, this->current_process.base_address, test_buffer, 2)) {
				LOG("[-] GetProcessCr3 returned an isolated/invalid CR3 context. Verification failed.\n");
				cr3 = 0;
			}
			else {
				LOG("[+] Verified CR3 via GetProcessCr3: %p\n", cr3);
			}
		}
	}
	if (!cr3) {
		LOG("[-] Failed to find any valid CR3 context\n");
		return false;
	}
	
	LOG("[+] Patched DTB / CR3 Resolved: %p\n", cr3);
	return true;
}

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	return false;
}

uint64_t Memory::FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID)
{
	return 0;
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size) const
{
	return false; 
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size, int pid) const
{
	return false;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, const char* debugName) const
{
	return const_cast<Memory*>(this)->driver.ReadMemory(this->current_process.PID, address, buffer, size, debugName);
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid, const char* debugName) const
{
	return const_cast<Memory*>(this)->driver.ReadMemory(pid, address, buffer, size, debugName);
}

bool Memory::BatchRead(DriverInterfaceV3::BatchReadEntry* entries, size_t count) const
{
	return const_cast<Memory*>(this)->driver.BatchReadMemory(this->current_process.PID, entries, count);
}


