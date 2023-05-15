#include <array>

#include "fdr_common.hpp"

CommandResult_t exec(const char *cmd)
{
	int exitcode = 0;
	std::array<char, ONE_MB> buffer{};
	std::string result;

	FILE *pipe = popen(cmd, "r");
	if (pipe == nullptr)
	{
		throw std::runtime_error("popen() failed!");
	}
	try
	{
		std::size_t bytesread;
		while ((bytesread = std::fread(buffer.data(), sizeof(buffer.at(0)), sizeof(buffer), pipe)) != 0)
		{
			result += std::string(buffer.data(), bytesread);
		}
	}
	catch (...)
	{
		pclose(pipe);
		throw;
	}
	exitcode = WEXITSTATUS(pclose(pipe));

	return CommandResult_t{result, exitcode};
}