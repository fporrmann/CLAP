#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "internal/CLAPBackend.hpp"
#include "internal/Exceptions.hpp"
#include "internal/Expected.hpp"
#include "internal/UserInterruptBase.hpp"

namespace clap
{
namespace test
{
class DummyUserInterrupt : public internal::UserInterruptBase
{
public:
	DummyUserInterrupt() = default;

	void Init([[maybe_unused]] const uint32_t& devNum, const uint32_t& interruptNum, internal::HasInterrupt* pReg = nullptr) override
	{
		m_isSet        = true;
		m_interruptNum = interruptNum;
		m_pReg         = pReg;
	}

	void Unset() override
	{
		m_isSet = false;
		m_pReg  = nullptr;
	}

	bool IsSet() const override
	{
		return m_isSet;
	}

	bool WaitForInterrupt([[maybe_unused]] const int32_t& timeout = WAIT_INFINITE, [[maybe_unused]] const bool& runCallbacks = true) override
	{
		return false;
	}

private:
	bool m_isSet = false;
};

class DummyBackendBase : public internal::CLAPBackend
{
	static constexpr std::size_t DEFAULT_MEMORY_SIZE = 0x10000;

	struct PropertyStore
	{
		std::unordered_map<std::string, uint64_t> scalars;
		std::unordered_map<std::string, std::string> strings;
		std::unordered_map<std::string, std::vector<uint64_t>> vectors;
	};

	struct RegisterHook
	{
		std::size_t width     = 4;
		uint64_t setOnWrite   = 0;
		uint64_t clearOnWrite = 0;
		uint64_t setOnRead    = 0;
		uint64_t clearOnRead  = 0;
	};

	struct Config
	{
		nlohmann::json doc;
	};

public:
	explicit DummyBackendBase(const std::string& backendName, const bool& uioSupported, [[maybe_unused]] const uint32_t& deviceNum = 0, [[maybe_unused]] const uint32_t& channelNum = 0) :
		m_uioSupported(uioSupported)
	{
		const Config cfg = loadConfig();
		m_memory.assign(getMemorySize(cfg.doc), 0);
		applyConfig(cfg);

		m_backendName = backendName;
		m_nameRead    = backendName + " Read";
		m_nameWrite   = backendName + " Write";
		m_nameCtrl    = backendName + " Ctrl";
		m_valid       = true;
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte) override
	{
		checkRange(addr, sizeInByte);
		std::lock_guard<std::mutex> lock(m_mutex);
		applyHooks(addr, sizeInByte, false);
		std::memcpy(pData, &m_memory[static_cast<std::size_t>(addr)], static_cast<std::size_t>(sizeInByte));
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte) override
	{
		checkRange(addr, sizeInByte);
		std::lock_guard<std::mutex> lock(m_mutex);
		std::memcpy(&m_memory[static_cast<std::size_t>(addr)], pData, static_cast<std::size_t>(sizeInByte));
		applyHooks(addr, sizeInByte, true);
		applyApCtrlAutoComplete(addr, sizeInByte);
	}

	void ReadCtrl(const uint64_t& addr, uint64_t& data, const std::size_t& byteCnt) override
	{
		if (byteCnt > sizeof(uint64_t))
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Control read size (" << std::dec << byteCnt << " byte) exceeds maximal allowed size (" << sizeof(uint64_t) << " byte)";
			throw CLAPException(ss.str());
		}

		uint64_t tmp = 0;
		Read(addr, &tmp, static_cast<uint64_t>(byteCnt));
		data = tmp;
	}

	internal::UserInterruptPtr MakeUserInterrupt() const override
	{
		return std::make_unique<DummyUserInterrupt>();
	}

	void SetUIOProperty(const uint64_t& addr, const std::string& name, const uint64_t& value)
	{
		if (!m_uioSupported)
			return;
		m_uioProperties[addr].scalars[name] = value;
	}

	void SetUIOStringProperty(const uint64_t& addr, const std::string& name, const std::string& value)
	{
		if (!m_uioSupported)
			return;
		m_uioProperties[addr].strings[name] = value;
	}

	void SetUIOPropertyVec(const uint64_t& addr, const std::string& name, const std::vector<uint64_t>& value)
	{
		if (!m_uioSupported)
			return;
		m_uioProperties[addr].vectors[name] = value;
	}

	void SetUIOID(const uint64_t& addr, const int32_t& id)
	{
		if (!m_uioSupported)
			return;
		m_uioIds[addr] = id;
	}

	Expected<uint64_t> ReadUIOProperty(const uint64_t& addr, const std::string& propName) const override
	{
		if (!m_uioSupported)
			return MakeUnexpected();

		auto it = m_uioProperties.find(addr);
		if (it == m_uioProperties.end())
			return MakeUnexpected();

		auto propIt = it->second.scalars.find(propName);
		if (propIt == it->second.scalars.end())
			return MakeUnexpected();

		return Expected<uint64_t>(static_cast<uint64_t>(propIt->second));
	}

	Expected<std::string> ReadUIOStringProperty(const uint64_t& addr, const std::string& propName) const override
	{
		if (!m_uioSupported)
			return MakeUnexpected();

		auto it = m_uioProperties.find(addr);
		if (it == m_uioProperties.end())
			return MakeUnexpected();

		auto propIt = it->second.strings.find(propName);
		if (propIt == it->second.strings.end())
			return MakeUnexpected();

		return Expected<std::string>(std::string(propIt->second));
	}

	Expected<std::vector<uint64_t>> ReadUIOPropertyVec(const uint64_t& addr, const std::string& propName) const override
	{
		if (!m_uioSupported)
			return MakeUnexpected();

		auto it = m_uioProperties.find(addr);
		if (it == m_uioProperties.end())
			return MakeUnexpected();

		auto propIt = it->second.vectors.find(propName);
		if (propIt == it->second.vectors.end())
			return MakeUnexpected();

		return Expected<std::vector<uint64_t>>(std::vector<uint64_t>(propIt->second));
	}

	bool CheckUIOPropertyExists(const uint64_t& addr, const std::string& propName) const override
	{
		if (!m_uioSupported)
			return false;

		auto it = m_uioProperties.find(addr);
		if (it == m_uioProperties.end())
			return false;

		return (it->second.scalars.count(propName) > 0) || (it->second.strings.count(propName) > 0) || (it->second.vectors.count(propName) > 0);
	}

	Expected<int32_t> GetUIOID(const uint64_t& addr) const override
	{
		if (!m_uioSupported)
			return MakeUnexpected();

		auto it = m_uioIds.find(addr);
		if (it == m_uioIds.end())
			return MakeUnexpected();

		return Expected<int32_t>(static_cast<int32_t>(it->second));
	}

	void EnableApCtrlAutoComplete(const uint64_t& addr)
	{
		m_apCtrlAutoComplete.insert(addr);
	}

	void AddRegisterHook(const uint64_t& addr, const std::size_t& width, const uint64_t& setOnWrite, const uint64_t& clearOnWrite, const uint64_t& setOnRead, const uint64_t& clearOnRead)
	{
		RegisterHook hook;
		hook.width            = width;
		hook.setOnWrite       = setOnWrite;
		hook.clearOnWrite     = clearOnWrite;
		hook.setOnRead        = setOnRead;
		hook.clearOnRead      = clearOnRead;
		m_registerHooks[addr] = hook;
	}

	void AddAutoClearOnWrite(const uint64_t& addr, const uint64_t& clearMask, const std::size_t& width = 4)
	{
		AddRegisterHook(addr, width, 0, clearMask, 0, 0);
	}

	void AddAutoClearOnRead(const uint64_t& addr, const uint64_t& clearMask, const std::size_t& width = 4)
	{
		AddRegisterHook(addr, width, 0, 0, 0, clearMask);
	}

	void SetRegisterValue(const uint64_t& addr, const uint64_t& value, const std::size_t& width = 4)
	{
		checkRange(addr, static_cast<uint64_t>(width));
		std::lock_guard<std::mutex> lock(m_mutex);
		writeValue(addr, value, width);
	}

	uint64_t GetRegisterValue(const uint64_t& addr, const std::size_t& width = 4) const
	{
		if (addr + width > m_memory.size())
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Address range out of bounds: addr=0x" << std::hex << addr << ", width=0x" << std::hex << width << ", memory size=0x" << std::hex << m_memory.size() << std::endl;
			throw CLAPException(ss.str());
		}

		return readValue(addr, width);
	}

	void SetMemoryByte(const uint64_t& addr, const uint8_t& value)
	{
		checkRange(addr, 1);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_memory[static_cast<std::size_t>(addr)] = value;
	}

	uint8_t GetMemoryByte(const uint64_t& addr) const
	{
		if (addr >= static_cast<uint64_t>(m_memory.size()))
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Address out of bounds: addr=0x" << std::hex << addr << ", memory size=0x" << std::hex << m_memory.size() << std::endl;
			throw CLAPException(ss.str());
		}

		return m_memory[static_cast<std::size_t>(addr)];
	}

private:
	static uint64_t parseUInt64(const nlohmann::json& value)
	{
		if (value.is_string())
			return static_cast<uint64_t>(std::stoull(value.get<std::string>(), nullptr, 0));

		if (value.is_number_unsigned())
			return value.get<uint64_t>();

		if (value.is_number_integer())
			return static_cast<uint64_t>(value.get<int64_t>());

		return 0;
	}

	static std::string parseString(const nlohmann::json& value)
	{
		if (value.is_string())
			return value.get<std::string>();

		if (value.is_number_integer())
			return std::to_string(value.get<int64_t>());

		if (value.is_number_unsigned())
			return std::to_string(value.get<uint64_t>());

		return "";
	}

	static uint64_t getUint64(const nlohmann::json& obj, const char* key)
	{
		if (!obj.contains(key))
			return 0;

		return parseUInt64(obj.at(key));
	}

	static std::size_t getMemorySize(const nlohmann::json& doc)
	{
		if (doc.is_null() || !doc.is_object() || !doc.contains("memory_size"))
			return DEFAULT_MEMORY_SIZE;

		return static_cast<std::size_t>(parseUInt64(doc.at("memory_size")));
	}

	static std::string getString(const nlohmann::json& obj, const char* key)
	{
		if (!obj.contains(key))
			return "";

		return parseString(obj.at(key));
	}

	static Config loadConfig()
	{
		Config cfg;
		const char* envPath = std::getenv("CLAP_DUMMY_BACKEND_CONFIG");
		if (!envPath || std::string(envPath).empty())
			return cfg;

		std::ifstream file(envPath);
		if (!file.is_open())
			return cfg;

		try
		{
			file >> cfg.doc;
		}
		catch (...)
		{
			return cfg;
		}

		return cfg;
	}

	void applyConfig(const Config& cfg)
	{
		if (cfg.doc.is_null())
			return;

		if (m_uioSupported && cfg.doc.contains("uio"))
		{
			const auto& uio = cfg.doc["uio"];
			for (const auto& item : uio.value("scalars", nlohmann::json::array()))
				SetUIOProperty(getUint64(item, "addr"), getString(item, "name"), getUint64(item, "value"));

			for (const auto& item : uio.value("strings", nlohmann::json::array()))
				SetUIOStringProperty(getUint64(item, "addr"), getString(item, "name"), getString(item, "value"));

			for (const auto& item : uio.value("vectors", nlohmann::json::array()))
			{
				std::vector<uint64_t> values;
				for (const auto& value : item.value("values", nlohmann::json::array()))
					values.push_back(parseUInt64(value));

				SetUIOPropertyVec(getUint64(item, "addr"), getString(item, "name"), values);
			}

			for (const auto& item : uio.value("ids", nlohmann::json::array()))
				SetUIOID(getUint64(item, "addr"), static_cast<int32_t>(getUint64(item, "id")));
		}

		if (cfg.doc.contains("reg"))
		{
			const auto& reg = cfg.doc["reg"];
			for (const auto& item : reg.value("values", nlohmann::json::array()))
				SetRegisterValue(getUint64(item, "addr"), getUint64(item, "value"), static_cast<std::size_t>(getUint64(item, "width")));

			for (const auto& item : reg.value("hooks", nlohmann::json::array()))
				AddRegisterHook(getUint64(item, "addr"), static_cast<std::size_t>(getUint64(item, "width")),
								getUint64(item, "set_on_write"), getUint64(item, "clear_on_write"),
								getUint64(item, "set_on_read"), getUint64(item, "clear_on_read"));
		}

		for (const auto& value : cfg.doc.value("apctrl_autocomplete", nlohmann::json::array()))
			EnableApCtrlAutoComplete(parseUInt64(value));

		for (const auto& item : cfg.doc.value("mem_bytes", nlohmann::json::array()))
			SetMemoryByte(getUint64(item, "addr"), static_cast<uint8_t>(getUint64(item, "value")));
	}

	void checkRange(const uint64_t& addr, const uint64_t& sizeInByte) const
	{
		const uint64_t memSize = static_cast<uint64_t>(m_memory.size());

		if (sizeInByte == 0)
			return;

		if (addr >= memSize || addr + sizeInByte > memSize)
		{
			std::stringstream ss;
			ss << CLASS_TAG_AUTO << "Address range out of bounds: addr=0x" << std::hex << addr << ", size=0x" << std::hex << sizeInByte << ", memory size=0x" << std::hex << memSize << std::endl;
			throw CLAPException(ss.str());
		}
	}

	void applyHooks(const uint64_t& addr, const uint64_t& sizeInByte, const bool& isWrite)
	{
		auto it = m_registerHooks.find(addr);

		if (it == m_registerHooks.end())
			return;

		if (sizeInByte != it->second.width)
			return;

		uint64_t value = readValue(addr, it->second.width);
		if (isWrite)
		{
			value |= it->second.setOnWrite;
			value &= ~it->second.clearOnWrite;
		}
		else
		{
			value |= it->second.setOnRead;
			value &= ~it->second.clearOnRead;
		}

		writeValue(addr, value, it->second.width);
	}

	uint64_t readValue(const uint64_t& addr, const std::size_t& width) const
	{
		uint64_t value = 0;
		std::memcpy(&value, &m_memory[static_cast<std::size_t>(addr)], width);
		return value;
	}

	void writeValue(const uint64_t& addr, const uint64_t& value, const std::size_t& width)
	{
		std::memcpy(&m_memory[static_cast<std::size_t>(addr)], &value, width);
	}

	void applyApCtrlAutoComplete(const uint64_t& addr, const uint64_t& sizeInByte)
	{
		if (sizeInByte == 0)
			return;

		if (m_apCtrlAutoComplete.count(addr) == 0)
			return;

		uint8_t* pReg = &m_memory[static_cast<std::size_t>(addr)];
		if ((*pReg & 0x1) == 0)
			return;

		const uint8_t autoRestart = (*pReg & 0x80);

		*pReg = static_cast<uint8_t>(autoRestart | 0x0E);
	}

private:
	std::vector<uint8_t> m_memory;
	std::mutex m_mutex;
	std::unordered_map<uint64_t, PropertyStore> m_uioProperties;
	std::unordered_map<uint64_t, int32_t> m_uioIds;
	std::unordered_set<uint64_t> m_apCtrlAutoComplete;
	std::unordered_map<uint64_t, RegisterHook> m_registerHooks;
	bool m_uioSupported = false;
};

class DummyPCIeBackend : public DummyBackendBase
{
public:
	explicit DummyPCIeBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
		DummyBackendBase("Dummy PCIe", false, deviceNum, channelNum)
	{
	}
};

class DummyPetaLinuxBackend : public DummyBackendBase
{
public:
	explicit DummyPetaLinuxBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
		DummyBackendBase("Dummy PetaLinux", true, deviceNum, channelNum)
	{
	}
};

class DummyBareMetalBackend : public DummyBackendBase
{
public:
	explicit DummyBareMetalBackend(const uint32_t& deviceNum = 0, const uint32_t& channelNum = 0) :
		DummyBackendBase("Dummy BareMetal", false, deviceNum, channelNum)
	{
	}
};
} // namespace test
} // namespace clap
