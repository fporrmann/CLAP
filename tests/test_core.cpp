#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "CLAP.hpp"
#include "IP_Cores/internal/WatchDog.hpp"
#include "internal/AlignmentAllocator.hpp"
#include "internal/Expected.hpp"
#include "internal/Logger.hpp"
#include "internal/Memory.hpp"
#include "internal/RegisterControl.hpp"
#include "internal/RegisterInterface.hpp"
#include "internal/Utils.hpp"

#include "support/DummyBackend.hpp"
#include "support/DummyConfig.hpp"

class EnvGuard
{
public:
	EnvGuard(const std::string& key, const std::string& value, const std::string& tempPath) :
		m_key(key),
		m_tempPath(tempPath)
	{
		const char* old = std::getenv(key.c_str());
		if (old)
		{
			m_hadOld   = true;
			m_oldValue = old;
		}

		setenv(key.c_str(), value.c_str(), 1);
	}

	~EnvGuard()
	{
		if (m_hadOld)
			setenv(m_key.c_str(), m_oldValue.c_str(), 1);
		else
			unsetenv(m_key.c_str());

		if (!m_tempPath.empty())
			std::remove(m_tempPath.c_str());
	}

private:
	std::string m_key;
	std::string m_oldValue;
	std::string m_tempPath;
	bool m_hadOld = false;
};

std::string WriteConfigFile(const std::string& content)
{
	static uint32_t counter = 0;
	const std::string path  = "/tmp/clap_dummy_backend_core_" + std::to_string(counter++) + ".json";
	std::ofstream file(path);
	file << content;
	file.close();
	return path;
}

EnvGuard SetBackendConfig(const std::string& content)
{
	const std::string path = WriteConfigFile(content);
	return EnvGuard("CLAP_DUMMY_BACKEND_CONFIG", path, path);
}

class TestRegisterBlock : public clap::internal::RegisterControlBase
{
public:
	explicit TestRegisterBlock(const clap::CLAPPtr& pClap, const uint64_t& ctrlOffset) :
		RegisterControlBase(pClap, ctrlOffset, "TestRegBlock"),
		m_reg("TestReg")
	{
		registerReg<uint32_t>(m_reg, 0x0, clap::PostRegisterReg::DoNothing);
	}

	void WriteValue(const uint32_t& value)
	{
		m_reg.SetBits(value);
	}

	uint32_t ReadValue()
	{
		return m_reg.ToUint32();
	}

private:
	clap::internal::Bit32Register m_reg;
};

class MinimalBackend : public clap::internal::CLAPBackend
{
public:
	MinimalBackend(const std::size_t& memorySize = 0x100) :
		m_memory(memorySize, 0)
	{
		m_backendName = "Minimal";
		m_nameRead    = "Minimal Read";
		m_nameWrite   = "Minimal Write";
		m_nameCtrl    = "Minimal Ctrl";
	}

	void Read(const uint64_t& addr, void* pData, const uint64_t& sizeInByte) override
	{
		if (addr + sizeInByte > m_memory.size())
			throw clap::CLAPException("Read out of bounds");

		std::memcpy(pData, &m_memory[static_cast<std::size_t>(addr)], static_cast<std::size_t>(sizeInByte));
	}

	void Write(const uint64_t& addr, const void* pData, const uint64_t& sizeInByte) override
	{
		if (addr + sizeInByte > m_memory.size())
			throw clap::CLAPException("Write out of bounds");

		std::memcpy(&m_memory[static_cast<std::size_t>(addr)], pData, static_cast<std::size_t>(sizeInByte));
	}

	clap::internal::UserInterruptPtr MakeUserInterrupt() const override
	{
		return std::make_unique<clap::test::DummyUserInterrupt>();
	}

private:
	std::vector<uint8_t> m_memory;
};

class DummyStatus : public clap::internal::HasInterrupt
{
public:
	void ClearInterrupts() override {}

	uint32_t GetInterrupts() override
	{
		return 0;
	}

	bool HasDoneIntr() const override
	{
		return false;
	}

	bool HasErrorIntr() const override
	{
		return false;
	}

	void Reset() override {}

	void ResetStates() override {}
};

class TestUserInterrupt : public clap::test::DummyUserInterrupt
{
public:
	void ForceProcessCallbacks(const bool& runCallbacks, const uint32_t& lastIntr)
	{
		processCallbacks(runCallbacks, lastIntr);
	}
};

template<typename T>
void BackendRoundTrip(const uint64_t& v32Addr = 0x100, const uint32_t& v32Data = 0xA5A5A5A5, const uint64_t& v8Addr = 0x200, const std::vector<uint8_t>& v8Data = { 0x1, 0x2, 0x3, 0x4, 0x5 })
{
	clap::CLAPPtr pClap = clap::CLAP::Create<T>(0, 0, true);

	const uint32_t value32 = v32Data;
	pClap->Write32(v32Addr, value32);
	const uint32_t read32 = pClap->Read32(v32Addr);
	REQUIRE(read32 == value32);

	clap::CLAPBuffer<uint8_t> writeBuf(v8Data.size(), 0);
	std::copy(v8Data.begin(), v8Data.end(), writeBuf.data());
	pClap->Write(v8Addr, writeBuf.data(), writeBuf.size());

	clap::CLAPBuffer<uint8_t> readBuf(writeBuf.size(), 0);
	pClap->Read(v8Addr, readBuf);
	REQUIRE(readBuf == writeBuf);
}

TEST_CASE("DummyPCIeBackendRoundTrip")
{
	BackendRoundTrip<clap::test::DummyPCIeBackend>();
}

TEST_CASE("DummyPetaLinuxBackendRoundTrip")
{
	BackendRoundTrip<clap::test::DummyPetaLinuxBackend>();
}

TEST_CASE("DummyBareMetalBackendRoundTrip")
{
	BackendRoundTrip<clap::test::DummyBareMetalBackend>();
}

TEST_CASE("CLAPMemoryReadWrite")
{
	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);

	pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, 0x1000, 0x200);
	clap::Memory mem = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x80);

	clap::CLAPBuffer<uint32_t> data = { 1, 2, 3, 4 };
	pClap->Write(mem, data, data.size() * sizeof(uint32_t));

	clap::CLAPBuffer<uint32_t> read(data.size(), 0);
	pClap->Read(mem, read, read.size() * sizeof(uint32_t));
	REQUIRE(read == data);
}

TEST_CASE("CLAPReadWrite16And64")
{
	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPCIeBackend>(0, 0, true);

	pClap->Write16(0x120, 0xDEAD);
	REQUIRE(pClap->Read16(0x120) == 0xDEAD);

	pClap->Write64(0x140, 0xDEADBEEFA5A5A5A5ULL);
	REQUIRE(pClap->Read64(0x140) == 0xDEADBEEFA5A5A5A5ULL);

	clap::CLAPBuffer<uint16_t> words = { 0x1, 0x2, 0x3, 0x4 };
	pClap->Write(0x180, words);
	clap::CLAPBuffer<uint16_t> readWords(words.size(), 0);
	pClap->Read(0x180, readWords);
	REQUIRE(readWords == words);
}

TEST_CASE("CLAPUIOStringPropertyRead")
{
	auto guard = SetBackendConfig(
		R"json({
	"memory_size": "0x10000",
	"uio": {
		"strings": [
			{"addr": "0x1200", "name": "compatible", "value": "testvalue"}
		]
	}
})json");

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPetaLinuxBackend>(0, 0, true);
	auto prop    = pClap->ReadUIOStringProperty(0x1200, "compatible");
	REQUIRE(prop);
	REQUIRE(prop.Value() == "testvalue");
}

TEST_CASE("MemoryManagerAllocFree")
{
	auto pManager     = std::make_shared<clap::internal::MemoryManager>(0x1000, 0x200);
	clap::Memory mem1 = pManager->AllocMemory<clap::Memory>(0x20);
	clap::Memory mem2 = pManager->AllocMemory<clap::Memory>(0x10);

	REQUIRE(mem1.GetBaseAddr() == 0x1000);
	REQUIRE(mem1.GetSize() == 0x20);
	REQUIRE(mem2.GetSize() == 0x10);
	REQUIRE(pManager->GetAvailableSpace() == 0x200 - 0x80);

	REQUIRE(pManager->FreeMemory(mem1));
	REQUIRE(!mem1.IsValid());
	pManager->Reset();
	REQUIRE(pManager->GetAvailableSpace() == 0x200);
}

TEST_CASE("RegisterControlReadWrite")
{
	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPCIeBackend>(0, 0, true);

	TestRegisterBlock block(pClap, 0x1000);
	block.WriteValue(0xA5A5A5A5);
	const uint32_t readValue = block.ReadValue();
	REQUIRE(readValue == 0xA5A5A5A5);
}

TEST_CASE("AlignmentAllocatorWorks")
{
	clap::CLAPBuffer<uint8_t> buffer(128, 0);
	uintptr_t addr = reinterpret_cast<uintptr_t>(buffer.data());
	REQUIRE((addr % clap::ALIGNMENT) == 0);
}

TEST_CASE("AlignmentAllocatorOperations")
{
	using Alloc = clap::internal::AlignmentAllocator<uint16_t, 4096>;
	Alloc alloc;

	auto ptr = alloc.allocate(4);
	alloc.construct(ptr, static_cast<uint16_t>(0x1234));
	alloc.destroy(ptr);
	alloc.deallocate(ptr, 4);

	REQUIRE(alloc.max_size() > 0);
}

TEST_CASE("ExpectedBehavior")
{
	clap::Expected<uint32_t> ok(42);
	REQUIRE(ok.HasValue());
	REQUIRE(ok.Value() == 42);

	clap::Expected<uint32_t> other = ok;
	REQUIRE(other.HasValue());
	REQUIRE(other.Value() == 42);

	clap::Expected<uint32_t> moved(std::move(other));
	REQUIRE(moved.HasValue());
	REQUIRE(moved.Value() == 42);

	clap::Expected<uint32_t> fail(clap::MakeUnexpected());
	REQUIRE(!fail.HasValue());
}

TEST_CASE("ExpectedStringBehavior")
{
	clap::Expected<std::string> ok(std::string("value"));
	REQUIRE(ok.HasValue());
	REQUIRE(static_cast<bool>(ok));
	REQUIRE(ok.Value() == "value");

	std::string released = ok.Release();
	REQUIRE(released == "value");
	REQUIRE(!ok.HasValue());

	clap::Expected<std::string> err(clap::MakeUnexpected());
	REQUIRE(!err.HasValue());
}

TEST_CASE("MemoryManagerCoalesceAndExceptions")
{
	auto pManager = std::make_shared<clap::internal::MemoryManager>(0x1000, 0x1000);
	std::vector<clap::MemoryUPtr> blocks;

	for (uint32_t i = 0; i < 6; i++)
		blocks.push_back(pManager->AllocMemory<clap::MemoryUPtr>(0x10));

	for (auto& block : blocks)
		REQUIRE(pManager->FreeMemory(*block));

	REQUIRE(pManager->GetAvailableSpace() == 0x1000);

	bool memThrown = false;

	try
	{
		pManager->AllocMemory<clap::Memory>(0);
	}
	catch (const clap::MemoryException& ex)
	{
		memThrown = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(memThrown);

	clap::Memory invalid;
	bool threw = false;

	try
	{
		(void)invalid.GetBaseAddr();
	}
	catch (const clap::MemoryException& ex)
	{
		threw = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(threw);

	clap::CLAPPtr pClap = clap::CLAP::Create<clap::test::DummyPCIeBackend>(0, 0, true);
	pClap->AddMemoryRegion(clap::CLAP::MemoryType::DDR, 0x2000, 0x40);

	clap::Memory mem = pClap->AllocMemory(clap::CLAP::MemoryType::DDR, 0x20);

	std::vector<uint8_t> data(0x30, 0xAA);
	threw = false;

	try
	{
		pClap->Write(mem, data.data(), data.size());
	}
	catch (const clap::CLAPException& ex)
	{
		threw = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(threw);
}

TEST_CASE("RegisterInterfaceHelpers")
{
	uint16_t field = 0;
	clap::internal::Register<uint16_t> reg16("Reg16");
	reg16.RegisterElement<uint16_t>(&field, "Field", 0, 3);
	reg16.Update(0x5);
	reg16.Print(clap::internal::RegisterIntf::RegUpdate::NoUpdate);

	bool flag = false;
	clap::internal::Register<uint32_t> reg32("Reg32");
	reg32.RegisterElement<bool>(&flag, "Flag", 0);
	reg32.Update(0x1);
	reg32.Print(clap::internal::RegisterIntf::RegUpdate::NoUpdate);

	clap::internal::Bit32Register bits("Bits");
	bits.SetBitAt(1, true);
	REQUIRE(bits.GetBitAt(1) == true);
	REQUIRE((bits.ToUint32() & 0x2) != 0);
}

TEST_CASE("UserInterruptBaseBehavior")
{
	TestUserInterrupt intr;
	bool callbackHit = false;
	intr.RegisterCallback([&callbackHit](uint32_t) { callbackHit = true; });
	intr.SetIPCoreFinishCallback([]() { return true; });

	REQUIRE(intr.CallIpCoreFinishCallback());

	bool threw = false;
	try
	{
		intr.HasErrorIntr();
	}
	catch (const clap::internal::UserInterruptException& ex)
	{
		threw = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(threw);

	DummyStatus status;
	intr.Init(0, 1, &status);
	intr.ForceProcessCallbacks(true, 0x1);
	REQUIRE(callbackHit);
	REQUIRE(intr.IsIpCoreFinished());
	intr.ResetStates();
}

TEST_CASE("WatchDogThrowsWithoutInputs")
{
	clap::internal::WatchDog watchDog("WatchDogTest", std::make_unique<TestUserInterrupt>());
	bool threw = false;

	try
	{
		watchDog.Start();
	}
	catch (const clap::internal::WatchDogException& ex)
	{
		threw = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(threw);
}

TEST_CASE("CLAPBackendDefaults")
{
	MinimalBackend backend;
	uint64_t data = 0;
	bool threw    = false;

	try
	{
		backend.ReadCtrl(0, data, 1);
	}
	catch (const clap::CLAPException& ex)
	{
		threw = true;
		REQUIRE(!std::string(ex.what()).empty());
	}

	REQUIRE(threw);

	auto prop = backend.ReadUIOProperty(0, "missing");
	REQUIRE(!prop.HasValue());

	auto strProp = backend.ReadUIOStringProperty(0, "missing");
	REQUIRE(!strProp.HasValue());

	auto vecProp = backend.ReadUIOPropertyVec(0, "missing");
	REQUIRE(!vecProp.HasValue());

	REQUIRE(!backend.CheckUIOPropertyExists(0, "missing"));
	REQUIRE(!backend.GetUIOID(0));

	REQUIRE(backend.GetName(clap::internal::CLAPBackend::TYPE::READ) == "Minimal Read");
	REQUIRE(backend.GetName(clap::internal::CLAPBackend::TYPE::WRITE) == "Minimal Write");
	REQUIRE(backend.GetName(clap::internal::CLAPBackend::TYPE::CONTROL) == "Minimal Ctrl");
}

TEST_CASE("UtilsHelpers")
{
	REQUIRE(clap::utils::Hex2Str(0xABCD) == "abcd");

	REQUIRE(!clap::utils::SizeWithSuffix(1024.0).empty());
	REQUIRE(clap::utils::SizeWithSuffix(1024.0) != clap::utils::SizeWithSuffix(2048.0));
	REQUIRE(clap::utils::SizeWithSuffix(1024.0) == "1.02 KB");
	REQUIRE(clap::utils::SizeWithSuffix(1024.0 * 1024.0) == "1.05 MB");
	REQUIRE(clap::utils::SizeWithSuffix(1024.0 * 1024.0 * 1024.0) == "1.07 GB");
	REQUIRE(clap::utils::SizeWithSuffix(1024.0 * 1024.0 * 1024.0 * 1024.0) == "1.10 TB");

	REQUIRE(!clap::utils::SpeedWithSuffix(2048.0).empty());
	REQUIRE(clap::utils::SpeedWithSuffix(2048.0) != clap::utils::SpeedWithSuffix(4096.0));
	REQUIRE(clap::utils::SpeedWithSuffix(2048.0) == "2.05 KB/s");
	REQUIRE(clap::utils::SpeedWithSuffix(2048.0 * 1024.0) == "2.10 MB/s");
	REQUIRE(clap::utils::SpeedWithSuffix(2048.0 * 1024.0 * 1024.0) == "2.15 GB/s");
	REQUIRE(clap::utils::SpeedWithSuffix(2048.0 * 1024.0 * 1024.0 * 1024.0) == "2.20 TB/s");

	auto parts = clap::utils::SplitString("a,b,c", ',');
	REQUIRE(parts.size() == 3);
	REQUIRE(parts[0] == "a");
	REQUIRE(parts[1] == "b");
	REQUIRE(parts[2] == "c");

	struct Foo
	{};
	Foo foo;
	REQUIRE(!clap::utils::ClassName(foo).empty());
	REQUIRE(clap::utils::ClassName(foo) == "Foo");
	REQUIRE(!clap::utils::GetCurrentTime().empty());
}

TEST_CASE("TimerBasic")
{
	clap::Timer timer;
	timer.Start();
	clap::utils::SleepMS(1);
	timer.Stop();
	REQUIRE(timer.GetElapsedTimeInMicroSec() >= 0);
}

TEST_CASE("LoggerToFile")
{
	const std::filesystem::path logPath = "/tmp/clap_test_log.txt";
	std::filesystem::remove(logPath);

	clap::logging::Log2File(logPath.string());
	CLAP_LOG_INFO << "test log" << std::endl;
	clap::logging::Log2File("");

	std::ifstream file(logPath);
	REQUIRE(file.is_open());

	std::filesystem::remove(logPath);
}
