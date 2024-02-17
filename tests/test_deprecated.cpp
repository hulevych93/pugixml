#define PUGIXML_DEPRECATED // Suppress deprecated declarations to avoid warnings

#include "test.hpp"

using namespace pugi;

TEST(document_deprecated_load)
{
	xml_document doc;
	CHECK(doc.load(STR("<node/>")));
	CHECK_NODE(doc, STR("<node/>"));
}

