#pragma once

#include <string>
#include <iomanip>
#include <sstream>

template<typename T>
std::string to_hex(T t) {
	std::stringstream ss;
	ss << "0x"
	   << std::setfill('0')
	   << std::setw(sizeof(T)*2)
	   << std::hex
	   << int(t);
	return ss.str();
}
