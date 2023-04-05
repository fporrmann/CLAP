/* 
 *  File: RegisterInterface.h
 *  Copyright (c) 2021 Florian Porrmann
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

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../../internal/Logger.h"
#include "../../internal/Utils.h"

static const uint8_t SAME_AS_START_BIT = 0xFF;

// Required to store the template class RegElem which will be used with different template types in a vector
template<typename T>
class RegIntf
{
	DISABLE_COPY_ASSIGN_MOVE(RegIntf)

public:
	RegIntf(const std::string& name, const uint8_t& startBit, const uint8_t& endBit = SAME_AS_START_BIT) :
		m_name(name),
		m_startBit(startBit),
		m_endBit(endBit)
	{
		if (m_endBit == SAME_AS_START_BIT)
			m_endBit = startBit;

		uint32_t cnt = m_endBit - m_startBit + 1;

		// Calculate the base shift value, for boolean values it is 1, for all other
		// it is determined based on the number of bits, using the formular:
		// shiftValue = 2^CNT - 1
		// For a count value of 4 this would result in 15 (2^4 = 16; 16 - 1 = 15) or 0xF
		// which after being shifted returns the 4 bits starting at the given start bit position.
		if (cnt != 1)
			m_shiftVal = static_cast<uint32_t>(std::pow(2, cnt)) - 1;

		m_shiftVal <<= m_startBit;
	}

	virtual ~RegIntf() {}

	const std::string& GetName() const
	{
		return m_name;
	}

	const uint8_t& GetStartBit() const
	{
		return m_startBit;
	}

	const uint8_t& GetEndBit() const
	{
		return m_endBit;
	}

	const uint32_t& GetShiftValue() const
	{
		return m_shiftVal;
	}

	virtual void UpdateValue(const uint32_t& val)                       = 0;
	virtual T GetValue() const                                          = 0;
	virtual std::string ToString(const uint32_t& nameSpacing = 0) const = 0;

	static std::string CreateString(const uint8_t& startBit, const uint8_t& endBit, const std::string& name, const uint32_t& nameSpacing = 0)
	{
		std::stringstream ss("");
		ss << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(endBit);

		if (startBit == endBit)
			ss << "   ";
		else
			ss << "-" << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(startBit);

		ss << " - " << std::setfill(' ') << std::left << std::setw(nameSpacing) << name;

		return ss.str();
	}

protected:
	std::string m_name;
	uint8_t m_startBit;
	uint8_t m_endBit;
	uint32_t m_shiftVal = 1;
};

// Class holding information regarding one register element (n-Bit)
// T  - Type of the register element, e.g., bool, uint8_t, etc., depends on the elements bit width
// BT - Base type of the register, e.g., uint32_t, uint64_t, etc., depends on the registers bit width
// The base type is required to be able to easily return the elements value whoes bit width will always
// be <= to that of the base type
template<typename T, typename BT>
class RegElem : public RegIntf<BT>
{
	DISABLE_COPY_ASSIGN_MOVE(RegElem)

public:
	RegElem(T* pVar, const std::string& name, const uint8_t& startBit, const uint8_t& endBit = SAME_AS_START_BIT) :
		RegIntf<BT>(name, startBit, endBit),
		m_pValue(pVar)
	{
		if (m_pValue == nullptr)
		{
			std::stringstream ss("");
			ss << "ERROR: Trying to create RegElem (" << m_name << ": " << m_startBit << "-" << m_endBit << ") without a valid pointer";
			throw std::runtime_error(ss.str());
		}
	}

	std::string ToString(const uint32_t& nameSpacing = 0) const
	{
		checkDataPointer();
		std::string str = RegIntf<BT>::CreateString(m_startBit, m_endBit, m_name, nameSpacing);
		std::stringstream ss("");
		ss << " - 0x" << std::hex << std::uppercase << static_cast<uint32_t>(*m_pValue);
		str.append(ss.str());

		return str;
	}

	friend std::ostream& operator<<(std::ostream& stream, const RegElem& re)
	{
		stream << re.ToString();
		return stream;
	}

	void UpdateValue(const uint32_t& val)
	{
		checkDataPointer();
		*m_pValue = (val & m_shiftVal) >> m_startBit;
	}

	BT GetValue() const
	{
		checkDataPointer();
		return static_cast<BT>(*m_pValue);
	}

private:
	void checkDataPointer() const
	{
		if (m_pValue == nullptr)
		{
			std::stringstream ss("");
			ss << "ERROR: Trying to use RegElem (" << m_name << ": " << m_startBit << "-" << m_endBit << ") whoes pointer is invalid";
			throw std::runtime_error(ss.str());
		}
	}

private:
	T* m_pValue;
	using RegIntf<BT>::m_name;
	using RegIntf<BT>::m_startBit;
	using RegIntf<BT>::m_endBit;
	using RegIntf<BT>::m_shiftVal;
};

enum class Direction
{
	READ,
	WRITE
};

// Template less base class for a register to easily store
// registers in a vector and execute callback based operations
// such as Update
class RegisterIntf
{
	DISABLE_COPY_ASSIGN_MOVE(RegisterIntf)

public:
	RegisterIntf() {}

	virtual ~RegisterIntf() {}

	virtual void Update(const Direction& dir = Direction::READ) = 0;
};

template<typename T>
class Register : public RegisterIntf
{
	static const std::string RESERVED_STRING;
	const std::size_t RESERVED_STRING_LENGTH = RESERVED_STRING.length();

	using RegIntfShr = std::shared_ptr<class RegIntf<T>>;

	DISABLE_COPY_ASSIGN_MOVE(Register)

public:
	using UpdateCB = void(Register<T>*, const uint64_t&, const Direction&, void*);

public:
	Register(const std::string& name) :
		m_regElems(),
		m_registerBitSize(sizeof(T) * 8),
		m_name(name)
	{}

	// Register a new element, identified by name for the register,
	// the element starts at bit position startBit and ends at endBit
	// its value is stored in the provided pointer pVar of type T2
	// By linking the pointer to the element, changes made by the user
	// are automatically accessable by the element and changes made by the
	// element, e.g., through an update are automatically seen by the user
	template<typename T2>
	void RegisterElement(T2* pVar, const std::string& name, const uint8_t& startBit, const uint8_t& endBit = SAME_AS_START_BIT)
	{
		// Check if the target bit space exceeds the possible range
		if (startBit > m_registerBitSize || (endBit > m_registerBitSize && endBit != SAME_AS_START_BIT))
		{
			LOG_ERROR << CLASS_TAG("") << "ERROR: Trying to register element: \"" << name << "\" whose bit space (" << startBit << "-" << endBit
					  << ") exceeds the registers bit size (" << m_registerBitSize << ")" << std::endl;
			return;
		}

		// Create a new element with the given parameter
		std::shared_ptr<RegElem<T2, T>> pElem = std::make_shared<RegElem<T2, T>>(pVar, name, startBit, endBit);
		uint32_t shiftVal                     = pElem->GetShiftValue();

		// Check if the the entire or a part of the bit range have already been registered
		if ((m_regUsage & shiftVal) != 0)
		{
			LOG_ERROR << CLASS_TAG("") << "ERROR: Trying to register element: \"" << name << "\" whose bit space (" << startBit << "-" << endBit
					  << ") has already been registered, either entirely or partially by:" << std::endl;

			// Print the elements occupying the target bit space
			for (const RegIntfShr& pRElem : m_regElems)
			{
				if ((pRElem->GetShiftValue() & shiftVal) != 0)
					LOG_ERROR << pRElem->GetName() << " " << pRElem->GetStartBit() << "-" << pRElem->GetEndBit() << std::endl;
			}

			return;
		}

		// Add the element to the list of registered ele
		m_regElems.push_back(pElem);
		// Update the used register bits
		m_regUsage |= shiftVal;
	}

	// Set the variables required for callback based updating
	void SetupCallBackBasedUpdate(void* pObj, const uint64_t& offset, UpdateCB* cb)
	{
		m_pCallBackObject = pObj;
		m_offset          = offset;
		m_pUpdateCB       = cb;
	}

	// Triggers the callback based update process for the given direction
	void Update(const Direction& dir = Direction::READ)
	{
		if (m_pCallBackObject == nullptr) return;
		m_pUpdateCB(this, m_offset, dir, m_pCallBackObject);
	}

	// Update the all registered elements using the given value
	void Update(const uint32_t& val)
	{
		for (RegIntfShr pElem : m_regElems)
			pElem->UpdateValue(val);
	}

	// Get the value of the register
	T GetValue() const
	{
		T value = 0x0;

		for (const RegIntfShr& pRElem : m_regElems)
			value |= (pRElem->GetValue() << pRElem->GetStartBit());

		return value;
	}

	// Print the register in a register address map
	void Print(bool update = false)
	{
		// If the update flag is set, update the register before printing
		if (update) Update();

		// Search for the max name length
		const RegIntfShr maxElem = *std::max_element(m_regElems.begin(), m_regElems.end(), [](const RegIntfShr lhs, const RegIntfShr rhs) { return lhs->GetName().length() < rhs->GetName().length(); });

		std::size_t maxLength = maxElem->GetName().length();

		if (maxLength < RESERVED_STRING_LENGTH)
			maxLength = RESERVED_STRING_LENGTH;

		// Store the register address map strings in a map,
		// using the start bit position as the key this ensures
		// that the map will be in the right order
		std::map<uint32_t, std::string> map;

		bool reserved          = false;
		uint8_t reserverdStart = 0;
		for (uint32_t i = 0; i < 32; i++)
		{
			// Bit position has not been registerd
			if (((m_regUsage >> i) & 0x1) == 0)
			{
				// First bit position that has not been registered
				if (!reserved)
				{
					reserved       = true;
					reserverdStart = i;
				}
			}
			// Bit position has been registered,
			// check if a reserved block has been encounterd
			else
			{
				if (reserved)
				{
					// Create a string to the reserved block
					// It ends at i - 1 because the current i has already been registered
					map[reserverdStart] = RegIntf<T>::CreateString(reserverdStart, i - 1, RESERVED_STRING, maxLength);
					// Reset the reserved flag
					reserved = false;
				}
			}
		}

		// Add strings for all registered elements to the map
		for (const RegIntfShr pElem : m_regElems)
			map[pElem->GetStartBit()] = pElem->ToString(maxLength);

		// Build up the register address map header string
		std::stringstream header("");
		header << std::left << std::setfill(' ') << std::setw(5) << "Bits"
			   << " - " << std::setfill(' ') << std::setw(maxLength) << "Field Name"
			   << " - "
			   << "Value";

		// Print the registers name
		LOG_INFO << m_name << ":" << std::endl;
		// Print the header
		LOG_INFO << header.str() << std::endl;
		// Print the divider
		LOG_INFO << std::left << std::setfill('-') << std::setw(header.str().length()) << "-" << std::endl;

		// Print the actuall register address map
		for (const std::pair<uint32_t, std::string> p : ReverseIterate(map))
			LOG_INFO << p.second << std::endl;

		LOG_INFO << std::endl;
	}

private:
	std::vector<RegIntfShr> m_regElems;
	uint32_t m_registerBitSize;
	T m_regUsage = 0;
	std::string m_name;

	// Member used for callback based updating
	UpdateCB* m_pUpdateCB   = nullptr;
	uint64_t m_offset       = 0;
	void* m_pCallBackObject = nullptr;
};

template<typename T>
const std::string Register<T>::RESERVED_STRING = "Reserved";

class HasStatus
{
public:
	HasStatus() {}

	virtual ~HasStatus() {}

	virtual bool PollDone()
	{
		return false;
	}
};

class HasInterrupt
{
public:
	HasInterrupt() {}

	virtual ~HasInterrupt() {}

	virtual void ClearInterrupts() {}
	virtual uint32_t GetInterrupts()
	{
		return 0;
	}

	uint32_t GetLastInterrupt() const
	{
		return m_lastInterrupt;
	}

protected:
	uint32_t m_lastInterrupt = 0;
};
