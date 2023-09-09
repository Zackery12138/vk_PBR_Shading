#include "error.hpp"

#include <cstdarg>

namespace labutils
{
	Error::Error( char const* aFmt, ... )
	{
		va_list args;
		va_start( args, aFmt );

		char buff[1024]{};
		vsnprintf( buff, 1023, aFmt, args );

		va_end( args );

		mMsg = buff;
	}

	char const* Error::what() const noexcept
	{
		return mMsg.c_str();
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
