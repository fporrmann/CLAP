#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace dummyConfig
{
class EnvGuard
{
public:
	EnvGuard(const std::string& key, const std::string& value, const std::filesystem::path& tempPath) :
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
			std::filesystem::remove(m_tempPath);
	}

private:
	std::string m_key;
	std::string m_oldValue;
	std::filesystem::path m_tempPath;
	bool m_hadOld = false;
};

inline std::string WriteConfigFile(const std::string& content)
{
	static int counter     = 0;
	const std::string path = "/tmp/clap_dummy_backend_" + std::to_string(counter++) + ".json";
	std::ofstream file(path);
	file << content;
	file.close();
	return path;
}

inline EnvGuard SetBackendConfig(const std::string& content)
{
	const std::string path = WriteConfigFile(content);
	return EnvGuard("CLAP_DUMMY_BACKEND_CONFIG", path, path);
}

} // namespace dummyConfig