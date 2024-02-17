#define _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_DEPRECATE

#include "test.hpp"

#include "writer_string.hpp"

#include <math.h>
#include <float.h>
#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <vector>

bool test_string_equal(const pugi::char_t* lhs, const pugi::char_t* rhs)
{
	return (!lhs || !rhs) ? lhs == rhs :
	#ifdef PUGIXML_WCHAR_MODE
		wcscmp(lhs, rhs) == 0;
	#else
		strcmp(lhs, rhs) == 0;
	#endif
}

bool test_node(const pugi::xml_node& node, const pugi::char_t* contents, const pugi::char_t* indent, unsigned int flags)
{
	xml_writer_string writer;

	node.print(writer, indent, flags, get_native_encoding());

	return writer.as_string() == contents;
}

bool test_double_nan(double value)
{
#if defined(_MSC_VER) || defined(__BORLANDC__)
	return _isnan(value) != 0;
#else
	return value != value;
#endif
}

bool is_little_endian()
{
	unsigned int ui = 1;
	return *reinterpret_cast<char*>(&ui) == 1;
}

pugi::xml_encoding get_native_encoding()
{
#ifdef PUGIXML_WCHAR_MODE
	return pugi::encoding_wchar;
#else
	return pugi::encoding_utf8;
#endif
}
