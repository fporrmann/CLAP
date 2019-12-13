#pragma once

#include <vector>
#include "../Utils.h"
#include "../xdmaAccess.h"
#include "RegisterInterface.h"

class IPCoreException : public std::exception
{
	public:
		explicit IPCoreException(const std::string& what) : m_what(what) {}

		virtual ~IPCoreException() throw() {}

		virtual const char* what() const throw()
		{
			return m_what.c_str();
		}

	private:
		std::string m_what;
};

enum DMAChannel
{
	MM2S,
	S2MM
};


class IPControlBase : public XDMAManaged
{
	DISABLE_COPY_ASSIGN_MOVE(IPControlBase)

	public:
		IPControlBase(class XDMABase* pXdma, const uint64_t& ctrlOffset) : 
			XDMAManaged(pXdma),
			m_ctrlOffset(ctrlOffset),
			m_registers()
		{}

		// Method used by the static update callback function to update
		// the given register
		template<typename T>
		void UpdateRegister(Register<T>* pReg, const uint64_t& offset, const Direction& dir)
		{
			if(dir == READ)
				pReg->Update(readRegister<T>(offset));
			else
				writeRegister<T>(offset, pReg->GetValue());
		}

		// Updates all registered registers
		void UpdateAllRegisters()
		{
			for(RegisterIntf* pReg : m_registers)
				pReg->Update(READ);
		}

		// Callback function, called by the register when the Update() method is called
		template<typename T>
		static void UpdateCallBack(Register<T>* pReg, const uint64_t& offset, const Direction& dir, void* pObj)
		{
			// Make sure that the given object pointer is in fact an IPControlBase object
			IPControlBase* pIPCtrl = static_cast<IPControlBase*>(pObj);
			if(!pIPCtrl) return;

			pIPCtrl->UpdateRegister(pReg, offset, dir);
		}

	protected:
		// Register a register to the list of known registers and
		// setup its update callback function
		template<typename T>
		void registerReg(Register<T>& reg, const uint64_t& offset = 0x0)
		{
			if(sizeof(T) > sizeof(uint64_t))
			{
				std::stringstream ss("");
				ss << CLASS_TAG("") << "Registers with a size > " << sizeof(uint64_t) << " byte are currently not supported";
				throw std::runtime_error(ss.str());
			}

			reg.SetupCallBackBasedUpdate(this, offset, UpdateCallBack<T>);
			m_registers.push_back(&reg);
		}

		template<typename T>
		T readRegister(const uint64_t& regOffset)
		{
			switch(sizeof(T))
			{
				case 8:
					return XDMA()->Read64(m_ctrlOffset + regOffset);
				case 4:
					return XDMA()->Read32(m_ctrlOffset + regOffset);
				case 2:
					return XDMA()->Read16(m_ctrlOffset + regOffset);
				case 1:
					return XDMA()->Read8(m_ctrlOffset + regOffset);
			}

			throw std::runtime_error("THIS SHOULD NOT HAPPEN - readRegister");
		}

		template<typename T>
		void writeRegister(const uint64_t& regOffset, const T& regData)
		{
			switch(sizeof(T))
			{
				case 8:
					XDMA()->Write64(m_ctrlOffset + regOffset, regData);
					return;
				case 4:
					XDMA()->Write32(m_ctrlOffset + regOffset, regData);
					return;
				case 2:
					XDMA()->Write16(m_ctrlOffset + regOffset, regData);
					return;
				case 1:
					XDMA()->Write8(m_ctrlOffset + regOffset, regData);
					return;
			}

			throw std::runtime_error("THIS SHOULD NOT HAPPEN - writeRegister");
		}

		uint32_t getDevNum()
		{
			return XDMA()->GetDevNum();
		}

	protected:
		uint64_t m_ctrlOffset;
		std::vector<RegisterIntf*> m_registers;
};
