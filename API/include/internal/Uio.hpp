/*
 *  File: Uio.hpp
 *  Copyright (c) 2023 Florian Porrmann
 *
 *  MIT License
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <vector>

#include "Exceptions.hpp"
#include "FileOps.hpp"
#include "Logger.hpp"

#ifndef MINORBITS
#define MINORBITS 20
#endif

#ifndef UIO_MAX_DEVICES
#define UIO_MAX_DEVICES (1U << MINORBITS)
#endif

#ifndef UIO_MAX_MAPS
#define UIO_MAX_MAPS 5
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#endif

// TODO:
//  - Handle multiple maps
//    This will require proper offsetting -- according to https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html#mmap-device-memory

namespace clap
{
namespace internal
{

template<typename T>
class UioDev
{
	static inline const std::string UIO_SYS_PATH_MAP_BASE = "/maps/map";
	static inline const std::string UIO_DEV_PATH          = "/dev/uio";
	static inline const std::string UIO_OF_NODE_PATH      = "/device/of_node/";
	static inline const std::string MAP_NAME_FILE         = "/name";
	static inline const std::string MAP_ADDR_FILE         = "/addr";
	static inline const std::string MAP_SIZE_FILE         = "/size";
	static inline const std::string MAP_OFFSET_FILE       = "/offset";

	template<typename U>
	class UioMap
	{
	public:
		UioMap(const uint32_t& id, const U& addr, const U& size, const U& offset, const std::string& name, const std::string& path) :
			m_id(id),
			m_addr(addr),
			m_size(size),
			m_offset(offset),
			m_name(name),
			m_path(path)
		{}

		uint32_t GetId() const
		{
			return m_id;
		}
		const U& GetAddr() const
		{
			return m_addr;
		}
		const U& GetSize() const
		{
			return m_size;
		}
		const U& GetOffset() const
		{
			return m_offset;
		}
		std::string GetName() const
		{
			return m_name;
		}
		std::string GetPath() const
		{
			return m_path;
		}

		bool AddrInRange(const U& addr) const
		{
			return ((addr >= m_addr) && (addr < (m_addr + m_size)));
		}

	private:
		uint32_t m_id = 0;
		U m_addr      = 0;
		U m_size      = 0;
		U m_offset    = 0;

		std::string m_name = "";
		std::string m_path = "";
	};

	class MMDev
	{
		DISABLE_COPY_ASSIGN_MOVE(MMDev)

	public:
		MMDev(const std::string& path, const std::size_t& size) :
			m_path(path),
			m_size(size)
		{
			m_fd = OpenDevice(m_path, O_RDWR);
			if (m_fd == INVALID_HANDLE)
			{
				LOG_ERROR << CLASS_TAG("MMDev") << "Could not open the device \"" << m_path << "\"" << std::endl;
				m_valid = false;
				return;
			}

			m_pPtr = mmap(0, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
			if (m_pPtr == MAP_FAILED)
			{
				LOG_ERROR << CLASS_TAG("MMDev") << "Could not map the device \"" << m_path << "\"" << std::endl;
				m_valid = false;
				return;
			}

			m_valid = true;
		}

		void* GetPtr() const
		{
			return m_pPtr;
		}

		~MMDev()
		{
			if (m_pPtr != nullptr)
				munmap(m_pPtr, m_size);

			CloseDevice(m_fd);
		}

		operator bool() const
		{
			return m_valid;
		}

	private:
		DeviceHandle m_fd = INVALID_HANDLE;
		void* m_pPtr      = nullptr;
		std::string m_path;
		std::size_t m_size;
		bool m_valid = false;
	};

	using MMDevPtr = std::shared_ptr<MMDev>;

	using UioMaps = std::vector<UioMap<T>>;

public:
	UioDev(const std::string& name, const std::string& path, const uint32_t& id) :
		m_name(name),
		m_path(path),
		m_devTreePropPath(path + UIO_OF_NODE_PATH),
		m_id(id)
	{
		if (m_id == static_cast<uint32_t>(-1) || m_name.empty() || m_path.empty())
		{
			m_valid = false;
			return;
		}

		initMaps();

		if (m_maps.empty())
		{
			m_valid = false;
			return;
		}

		openDevice();
		m_valid = true;
	}

	~UioDev()
	{
	}

	operator bool() const
	{
		return m_valid;
	}

	T Read(const T& addr, void* pData, const T& sizeInByte) const
	{
		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("UioDev") << "Device \"" << m_name << "\" is not valid" << std::endl;
			throw UIOException(ss.str());
		}

		// TODO: Check here all maps and also properly map the address to the correct map
		if (!m_maps[0].AddrInRange(addr))
		{
			std::stringstream ss;
			ss << CLASS_TAG("UioDev") << "Address \"" << addr << "\" is not in range of device \"" << m_name << "\"" << std::endl;
			throw UIOException(ss.str());
		}

		T addrBase = m_maps[0].GetAddr();
		T count    = 0;
		T offset   = addr - addrBase;

		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);

		while (count < sizeInByte)
		{
			T bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(pByteData + count, reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(m_mmDev->GetPtr()) + offset + count), bytes);

			count += bytes;
		}

		return count;
	}

	T Write(const T& addr, const void* pData, const T& sizeInByte) const
	{
		if (!m_valid)
		{
			std::stringstream ss;
			ss << CLASS_TAG("UioDev") << "Device \"" << m_name << "\" is not valid" << std::endl;
			throw UIOException(ss.str());
		}

		if (!m_maps[0].AddrInRange(addr))
		{
			std::stringstream ss;
			ss << CLASS_TAG("UioDev") << "Address \"" << addr << "\" is not in range of device \"" << m_name << "\"" << std::endl;
			throw UIOException(ss.str());
		}

		T addrBase = m_maps[0].GetAddr();
		T count    = 0;
		T offset   = addr - addrBase;

		const uint8_t* pByteData = reinterpret_cast<const uint8_t*>(pData);

		while (count < sizeInByte)
		{
			T bytes = sizeInByte - count;

			if (bytes > RW_MAX_SIZE)
				bytes = RW_MAX_SIZE;

			std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(m_mmDev->GetPtr()) + offset + count), pByteData + count, bytes);

			count += bytes;
		}

		return count;
	}

	T ReadHexStringProperty(const std::string& name) const
	{
		std::ifstream file(m_devTreePropPath + name, std::ios::binary);
		if (!file.is_open()) return -1;

		T value;
		file >> std::hex >> value;
		file.close();

		return value;
	}

	template<typename U>
	U ReadBinaryProperty(const std::string& property) const
	{
		std::vector<uint8_t> propValues = readProperty(property);
		U propValue = 0;

		if (propValues.empty()) return propValue;

		// Copy the bytes from the property vector to the result vector
		std::copy(propValues.begin(), propValues.end(), reinterpret_cast<uint8_t*>(&propValue));

		// Convert the property value from big endian to little endian
		propValue = ntohl(propValue);

		return propValue;
	}

	template<typename U>
	std::vector<U> ReadBinaryPropertyVec(const std::string& property) const
	{
		std::vector<uint8_t> propValues = readProperty(property);
		std::vector<U> resValues;

		if (propValues.empty()) return resValues;

		// Resize the result vector to the number of elements
		resValues.resize(DIV_ROUND_UP(propValues.size(), sizeof(U)));

		// Copy the bytes from the property vector to the result vector
		std::copy(propValues.begin(), propValues.end(), reinterpret_cast<uint8_t*>(resValues.data()));

		// Convert each property value from big endian to little endian
		for (U& propValue : propValues)
			propValue = ntohl(propValue);

		return propValues;
	}

	const std::string& GetName() const
	{
		return m_name;
	}

	const std::string& GetPath() const
	{
		return m_path;
	}

	const std::string& GetDevTreePropPath() const
	{
		return m_devTreePropPath;
	}

	const uint32_t& GetId() const
	{
		return m_id;
	}

	const UioMaps& GetMaps() const
	{
		return m_maps;
	}

	bool HasAddr(const T& addr) const
	{
		for (const auto& map : m_maps)
			if (map.AddrInRange(addr)) return true;
		return false;
	}

	friend std::ostream& operator<<(std::ostream& os, const UioDev& uioDev)
	{
		os << "UIO Device: " << uioDev.m_name << std::endl;
		os << "Path: " << uioDev.m_path << std::endl;
		os << "Device Tree Property Path: " << uioDev.m_devTreePropPath << std::endl;
		os << "ID: " << uioDev.m_id << std::endl;
		os << "Maps:" << std::endl;

		for (const auto& map : uioDev.m_maps)
		{
			os << "\tID: " << map.GetId() << std::endl;
			os << "\tAddress: 0x" << std::hex << map.GetAddr() << std::endl;
			os << "\tSize: 0x" << std::hex << map.GetSize() << std::endl;
			os << "\tOffset: 0x" << std::hex << map.GetOffset() << std::dec << std::endl;
			os << "\tName: " << map.GetName() << std::endl;
			os << "\tPath: " << map.GetPath() << std::endl;
		}

		return os;
	}

private:
	void initMaps()
	{
		for (uint32_t map = 0; map < UIO_MAX_MAPS; map++)
		{
			std::string basePath = m_path + UIO_SYS_PATH_MAP_BASE + std::to_string(map);

			std::ifstream file(basePath + MAP_NAME_FILE);
			if (!file.is_open()) break;

			std::string name;

			std::getline(file, name);
			file.close();

			T size   = readHexFromFile(basePath + MAP_SIZE_FILE);
			T addr   = readHexFromFile(basePath + MAP_ADDR_FILE);
			T offset = readHexFromFile(basePath + MAP_OFFSET_FILE);

			m_maps.push_back({ map, addr, size, offset, name, basePath });
		}
	}

	void openDevice()
	{
		const std::string path = UIO_DEV_PATH + std::to_string(m_id);

		m_mmDev = std::make_shared<MMDev>(path, m_maps.front().GetSize());
	}

	std::vector<uint8_t> readProperty(const std::string& property) const
	{
		const std::string propertyPath = m_devTreePropPath + property;
		std::ifstream propFile(propertyPath, std::ios::binary);
		std::vector<uint8_t> propValue;

		// Check if the file is open
		if (!propFile.is_open())
		{
			LOG_ERROR << CLASS_TAG("") << "Could not open the property file \"" << propertyPath << "\"" << std::endl;
			return propValue;
		}

		// Read the size of the file
		propFile.seekg(0, std::ios::end);
		std::streampos propFileSize = propFile.tellg();
		propFile.seekg(0, std::ios::beg);

		// Resize the vector to the size of the file
		propValue.resize(propFileSize);

		// Read the bytes from the file
		propFile.read(reinterpret_cast<char*>(propValue.data()), propFileSize);

		// Close the file
		propFile.close();

		return propValue;
	}

	static T readHexFromFile(const std::string& filename)
	{
		std::ifstream file(filename);
		if (!file.is_open()) return -1;

		T value;
		file >> std::hex >> value;
		file.close();

		return value;
	}

	static std::size_t getFileSize(const std::string& filename)
	{
		std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
		return static_cast<std::size_t>(in.tellg());
	}

private:
	std::string m_name;
	std::string m_path;
	std::string m_devTreePropPath;
	uint32_t m_id;
	UioMaps m_maps   = {};
	bool m_valid     = false;
	MMDevPtr m_mmDev = nullptr;
};

template<typename T>
using UioDevs = std::vector<UioDev<T>>;

// TODO:
//    - Store valid address ranges for each device
//    - Add read / write methods that check the address ranges and use the correct device
//

template<typename T>
class UioManager
{
	static inline const std::string UIO_SYS_PATH_BASE = "/sys/class/uio/uio";
	static inline const std::string UIO_NAME_FILE     = "/name";

public:
	UioManager() {}

	~UioManager() {}

	bool Init()
	{
		for (uint32_t uio = 0; uio < UIO_MAX_DEVICES; uio++)
		{
			std::string basePath = UIO_SYS_PATH_BASE + std::to_string(uio);

			std::ifstream file(basePath + UIO_NAME_FILE);
			if (!file.is_open()) break;

			std::string name;

			std::getline(file, name);
			file.close();

			m_uioDevs.push_back(UioDev<T>(name, basePath, uio));
		}

		m_initialized = !m_uioDevs.empty();

		return m_initialized;
	}

	operator bool() const
	{
		return m_initialized;
	}

	bool IsInitialized() const
	{
		return m_initialized;
	}

	const UioDev<T>& FindUioDevByName(const std::string& name) const
	{
		for (const UioDev<T>& uioDev : m_uioDevs)
		{
			if (uioDev.GetName() == name)
				return uioDev;
		}

		return m_invalidUioDev;
	}

	const UioDev<T>& FindUioDevById(const uint32_t& id) const
	{
		for (const UioDev<T>& uioDev : m_uioDevs)
		{
			if (uioDev.GetId() == id)
				return uioDev;
		}

		return m_invalidUioDev;
	}

	const UioDev<T>& FindUioDevByAddr(const uint32_t& addr) const
	{
		for (const UioDev<T>& uioDev : m_uioDevs)
		{
			if (uioDev.HasAddr(addr))
				return uioDev;
		}

		return m_invalidUioDev;
	}

	friend std::ostream& operator<<(std::ostream& os, const UioManager& uioManager)
	{
		for (const UioDev<T>& uioDev : uioManager.m_uioDevs)
		{
			os << uioDev << std::endl;
			std::cout << "----------------------------------------" << std::endl;
		}

		return os;
	}

private:
private:
	bool m_initialized        = false;
	UioDevs<T> m_uioDevs      = {};
	UioDev<T> m_invalidUioDev = UioDev<T>("", "", -1);
};

} // namespace internal
} // namespace clap
