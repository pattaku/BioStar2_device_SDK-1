#pragma once
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <sstream>
#include "BS_API.h"


template <typename T>
struct ArrayDeleter
{
	void operator() (T* ptr) { delete[] ptr; }
};

class Utils
{
public:
	template <typename T>
	static T getInput(std::string msg);
	static std::string getLine(std::string msg);

	static std::string getIPAddress(uint32_t ip);
	static std::string convertTimeUTC2String(uint32_t utcTime);
	static uint32_t convertTimeString2UTC(std::string time);
	static uint32_t getResourceSize(std::string file);
	static bool getResourceFromFile(std::string file, std::shared_ptr<uint8_t> buffer, uint32_t size = 0);
	static bool setResourceToFile(std::string file, std::shared_ptr<uint8_t> buffer, uint32_t size);

	static std::string replaceSlashToPeriod(const std::string& source);

	static std::vector<std::string> tokenizeString(const std::string& data, const char delimiter = ' ');
	template <typename T>
	static std::vector<T> tokenizeInteger(const std::string& data, const char delimiter = ' ');
	template <typename T>
	static T str2int(const std::string& data);

	static std::string getEventString(const BS2Event& event, int32_t timezone);

	static std::string getHexaString(const uint8_t* data, uint32_t size);
};

template <typename T>
inline T Utils::getInput(std::string msg)
{
	std::cout << "==> " << msg << " ";
	T value;
	std::cin >> value;

	return value;
}

template <typename T>
inline std::vector<T> Utils::tokenizeInteger(const std::string& data, const char delimiter)
{
	std::vector<T> result;
	std::string tokenized;
	std::stringstream ss(data);
	while (std::getline(ss, tokenized, delimiter))
	{
		T tok = str2int<T>(tokenized);
		result.push_back(tok);
	}

	return result;
}

template <typename T>
inline T Utils::str2int(const std::string& data)
{
	if (data.size() == 0)
		return (T)0;

	return static_cast<T>(atoi(data.c_str()));
}