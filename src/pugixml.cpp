/**
 * pugixml parser - version 1.14
 * --------------------------------------------------------
 * Copyright (C) 2006-2024, by Arseny Kapoulkine (arseny.kapoulkine@gmail.com)
 * Report bugs and download new versions at https://pugixml.org/
 *
 * This library is distributed under the MIT License. See notice at the end
 * of this file.
 *
 * This work is based on the pugxml parser, which is:
 * Copyright (C) 2003, by Kristen Wegner (kristen@tima.net)
 */

#ifndef SOURCE_PUGIXML_CPP
#define SOURCE_PUGIXML_CPP

#include "pugixml.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#ifdef PUGIXML_WCHAR_MODE
#	include <wchar.h>
#endif

#ifndef PUGIXML_NO_XPATH
#	include <math.h>
#	include <float.h>
#endif

#ifndef PUGIXML_NO_STL
#	include <istream>
#	include <ostream>
#	include <string>
#endif

// For placement new
#include <new>

// For load_file
#if defined(__linux__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

#ifdef _MSC_VER
#	pragma warning(push)
#	pragma warning(disable: 4127) // conditional expression is constant
#	pragma warning(disable: 4324) // structure was padded due to __declspec(align())
#	pragma warning(disable: 4702) // unreachable code
#	pragma warning(disable: 4996) // this function or variable may be unsafe
#endif

#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant" // NULL as null pointer constant
#endif

#if defined(_MSC_VER) && defined(__c2__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wdeprecated" // this function or variable may be unsafe
#endif

#ifdef __INTEL_COMPILER
#	pragma warning(disable: 177) // function was declared but never referenced
#	pragma warning(disable: 279) // controlling expression is constant
#	pragma warning(disable: 1478 1786) // function was declared "deprecated"
#	pragma warning(disable: 1684) // conversion from pointer to same-sized integral type
#endif

#if defined(__BORLANDC__) && defined(PUGIXML_HEADER_ONLY)
#	pragma warn -8080 // symbol is declared but never used; disabling this inside push/pop bracket does not make the warning go away
#endif

#ifdef __BORLANDC__
#	pragma option push
#	pragma warn -8008 // condition is always false
#	pragma warn -8066 // unreachable code
#endif

#ifdef __SNC__
// Using diag_push/diag_pop does not disable the warnings inside templates due to a compiler bug
#	pragma diag_suppress=178 // function was declared but never referenced
#	pragma diag_suppress=237 // controlling expression is constant
#endif

#ifdef __TI_COMPILER_VERSION__
#	pragma diag_suppress 179 // function was declared but never referenced
#endif

// Inlining controls
#if defined(_MSC_VER) && _MSC_VER >= 1300
#	define PUGI_IMPL_NO_INLINE __declspec(noinline)
#elif defined(__GNUC__)
#	define PUGI_IMPL_NO_INLINE __attribute__((noinline))
#else
#	define PUGI_IMPL_NO_INLINE
#endif

// Branch weight controls
#if defined(__GNUC__) && !defined(__c2__)
#	define PUGI_IMPL_UNLIKELY(cond) __builtin_expect(cond, 0)
#else
#	define PUGI_IMPL_UNLIKELY(cond) (cond)
#endif

// Simple static assertion
#define PUGI_IMPL_STATIC_ASSERT(cond) { static const char condition_failed[(cond) ? 1 : -1] = {0}; (void)condition_failed[0]; }

// Digital Mars C++ bug workaround for passing char loaded from memory via stack
#ifdef __DMC__
#	define PUGI_IMPL_DMC_VOLATILE volatile
#else
#	define PUGI_IMPL_DMC_VOLATILE
#endif

// Integer sanitizer workaround; we only apply this for clang since gcc8 has no_sanitize but not unsigned-integer-overflow and produces "attribute directive ignored" warnings
#if defined(__clang__) && defined(__has_attribute)
#	if __has_attribute(no_sanitize)
#		define PUGI_IMPL_UNSIGNED_OVERFLOW __attribute__((no_sanitize("unsigned-integer-overflow")))
#	else
#		define PUGI_IMPL_UNSIGNED_OVERFLOW
#	endif
#else
#	define PUGI_IMPL_UNSIGNED_OVERFLOW
#endif

// Borland C++ bug workaround for not defining ::memcpy depending on header include order (can't always use std::memcpy because some compilers don't have it at all)
#if defined(__BORLANDC__) && !defined(__MEM_H_USING_LIST)
using std::memcpy;
using std::memmove;
using std::memset;
#endif

// Old versions of GCC do not define ::malloc and ::free depending on header include order
#if defined(__GNUC__) && (__GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4))
using std::malloc;
using std::free;
#endif

// Some MinGW/GCC versions have headers that erroneously omit LLONG_MIN/LLONG_MAX/ULLONG_MAX definitions from limits.h in some configurations
#if defined(PUGIXML_HAS_LONG_LONG) && defined(__GNUC__) && !defined(LLONG_MAX) && !defined(LLONG_MIN) && !defined(ULLONG_MAX)
#	define LLONG_MIN (-LLONG_MAX - 1LL)
#	define LLONG_MAX __LONG_LONG_MAX__
#	define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#endif

// In some environments MSVC is a compiler but the CRT lacks certain MSVC-specific features
#if defined(_MSC_VER) && !defined(__S3E__) && !defined(_WIN32_WCE)
#	define PUGI_IMPL_MSVC_CRT_VERSION _MSC_VER
#elif defined(_WIN32_WCE)
#	define PUGI_IMPL_MSVC_CRT_VERSION 1310 // MSVC7.1
#endif

// Not all platforms have snprintf; we define a wrapper that uses snprintf if possible. This only works with buffers with a known size.
#if __cplusplus >= 201103
#	define PUGI_IMPL_SNPRINTF(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)
#elif defined(PUGI_IMPL_MSVC_CRT_VERSION) && PUGI_IMPL_MSVC_CRT_VERSION >= 1400
#	define PUGI_IMPL_SNPRINTF(buf, ...) _snprintf_s(buf, _countof(buf), _TRUNCATE, __VA_ARGS__)
#elif defined(__APPLE__) && __clang_major__ >= 14 // Xcode 14 marks sprintf as deprecated while still using C++98 by default
#	define PUGI_IMPL_SNPRINTF(buf, fmt, arg1, arg2) snprintf(buf, sizeof(buf), fmt, arg1, arg2)
#else
#	define PUGI_IMPL_SNPRINTF sprintf
#endif

// We put implementation details into an anonymous namespace in source mode, but have to keep it in non-anonymous namespace in header-only mode to prevent binary bloat.
#ifdef PUGIXML_HEADER_ONLY
#	define PUGI_IMPL_NS_BEGIN namespace pugi { namespace impl {
#	define PUGI_IMPL_NS_END } }
#	define PUGI_IMPL_FN inline
#	define PUGI_IMPL_FN_NO_INLINE inline
#else
#	if defined(_MSC_VER) && _MSC_VER < 1300 // MSVC6 seems to have an amusing bug with anonymous namespaces inside namespaces
#		define PUGI_IMPL_NS_BEGIN namespace pugi { namespace impl {
#		define PUGI_IMPL_NS_END } }
#	else
#		define PUGI_IMPL_NS_BEGIN namespace pugi { namespace impl { namespace {
#		define PUGI_IMPL_NS_END } } }
#	endif
#	define PUGI_IMPL_FN
#	define PUGI_IMPL_FN_NO_INLINE PUGI_IMPL_NO_INLINE
#endif

// uintptr_t
#if (defined(_MSC_VER) && _MSC_VER < 1600) || (defined(__BORLANDC__) && __BORLANDC__ < 0x561)
namespace pugi
{
#	ifndef _UINTPTR_T_DEFINED
	typedef size_t uintptr_t;
#	endif

	typedef unsigned __int8 uint8_t;
	typedef unsigned __int16 uint16_t;
	typedef unsigned __int32 uint32_t;
}
#else
#	include <stdint.h>
#endif

// Memory allocation
PUGI_IMPL_NS_BEGIN
	PUGI_IMPL_FN void* default_allocate(size_t size)
	{
		return malloc(size);
	}

	PUGI_IMPL_FN void default_deallocate(void* ptr)
	{
		free(ptr);
	}

	template <typename T>
	struct xml_memory_management_function_storage
	{
		static allocation_function allocate;
		static deallocation_function deallocate;
	};

	// Global allocation functions are stored in class statics so that in header mode linker deduplicates them
	// Without a template<> we'll get multiple definitions of the same static
	template <typename T> allocation_function xml_memory_management_function_storage<T>::allocate = default_allocate;
	template <typename T> deallocation_function xml_memory_management_function_storage<T>::deallocate = default_deallocate;

	typedef xml_memory_management_function_storage<int> xml_memory;
PUGI_IMPL_NS_END

// String utilities
PUGI_IMPL_NS_BEGIN
	// Get string length
	PUGI_IMPL_FN size_t strlength(const char_t* s)
	{
		assert(s);

	#ifdef PUGIXML_WCHAR_MODE
		return wcslen(s);
	#else
		return strlen(s);
	#endif
	}

	// Compare two strings
	PUGI_IMPL_FN bool strequal(const char_t* src, const char_t* dst)
	{
		assert(src && dst);

	#ifdef PUGIXML_WCHAR_MODE
		return wcscmp(src, dst) == 0;
	#else
		return strcmp(src, dst) == 0;
	#endif
	}

	// Compare lhs with [rhs_begin, rhs_end)
	PUGI_IMPL_FN bool strequalrange(const char_t* lhs, const char_t* rhs, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
			if (lhs[i] != rhs[i])
				return false;

		return lhs[count] == 0;
	}

	// Get length of wide string, even if CRT lacks wide character support
	PUGI_IMPL_FN size_t strlength_wide(const wchar_t* s)
	{
		assert(s);

	#ifdef PUGIXML_WCHAR_MODE
		return wcslen(s);
	#else
		const wchar_t* end = s;
		while (*end) end++;
		return static_cast<size_t>(end - s);
	#endif
	}
PUGI_IMPL_NS_END

// auto_ptr-like object for exception recovery
PUGI_IMPL_NS_BEGIN
	template <typename T> struct auto_deleter
	{
		typedef void (*D)(T*);

		T* data;
		D deleter;

		auto_deleter(T* data_, D deleter_): data(data_), deleter(deleter_)
		{
		}

		~auto_deleter()
		{
			if (data) deleter(data);
		}

		T* release()
		{
			T* result = data;
			data = NULL;
			return result;
		}
	};
PUGI_IMPL_NS_END

#ifdef PUGIXML_COMPACT
PUGI_IMPL_NS_BEGIN
	class compact_hash_table
	{
	public:
		compact_hash_table(): _items(NULL), _capacity(0), _count(0)
		{
		}

		void clear()
		{
			if (_items)
			{
				xml_memory::deallocate(_items);
				_items = NULL;
				_capacity = 0;
				_count = 0;
			}
		}

		void* find(const void* key)
		{
			if (_capacity == 0) return NULL;

			item_t* item = get_item(key);
			assert(item);
			assert(item->key == key || (item->key == NULL && item->value == NULL));

			return item->value;
		}

		void insert(const void* key, void* value)
		{
			assert(_capacity != 0 && _count < _capacity - _capacity / 4);

			item_t* item = get_item(key);
			assert(item);

			if (item->key == NULL)
			{
				_count++;
				item->key = key;
			}

			item->value = value;
		}

		bool reserve(size_t extra = 16)
		{
			if (_count + extra >= _capacity - _capacity / 4)
				return rehash(_count + extra);

			return true;
		}

	private:
		struct item_t
		{
			const void* key;
			void* value;
		};

		item_t* _items;
		size_t _capacity;

		size_t _count;

		bool rehash(size_t count);

		item_t* get_item(const void* key)
		{
			assert(key);
			assert(_capacity > 0);

			size_t hashmod = _capacity - 1;
			size_t bucket = hash(key) & hashmod;

			for (size_t probe = 0; probe <= hashmod; ++probe)
			{
				item_t& probe_item = _items[bucket];

				if (probe_item.key == key || probe_item.key == NULL)
					return &probe_item;

				// hash collision, quadratic probing
				bucket = (bucket + probe + 1) & hashmod;
			}

			assert(false && "Hash table is full"); // unreachable
			return NULL;
		}

		static PUGI_IMPL_UNSIGNED_OVERFLOW unsigned int hash(const void* key)
		{
			unsigned int h = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(key) & 0xffffffff);

			// MurmurHash3 32-bit finalizer
			h ^= h >> 16;
			h *= 0x85ebca6bu;
			h ^= h >> 13;
			h *= 0xc2b2ae35u;
			h ^= h >> 16;

			return h;
		}
	};

	PUGI_IMPL_FN_NO_INLINE bool compact_hash_table::rehash(size_t count)
	{
		size_t capacity = 32;
		while (count >= capacity - capacity / 4)
			capacity *= 2;

		compact_hash_table rt;
		rt._capacity = capacity;
		rt._items = static_cast<item_t*>(xml_memory::allocate(sizeof(item_t) * capacity));

		if (!rt._items)
			return false;

		memset(rt._items, 0, sizeof(item_t) * capacity);

		for (size_t i = 0; i < _capacity; ++i)
			if (_items[i].key)
				rt.insert(_items[i].key, _items[i].value);

		if (_items)
			xml_memory::deallocate(_items);

		_capacity = capacity;
		_items = rt._items;

		assert(_count == rt._count);

		return true;
	}

PUGI_IMPL_NS_END
#endif

PUGI_IMPL_NS_BEGIN
#ifdef PUGIXML_COMPACT
	static const uintptr_t xml_memory_block_alignment = 4;
#else
	static const uintptr_t xml_memory_block_alignment = sizeof(void*);
#endif

	// extra metadata bits
	static const uintptr_t xml_memory_page_contents_shared_mask = 64;
	static const uintptr_t xml_memory_page_name_allocated_mask = 32;
	static const uintptr_t xml_memory_page_value_allocated_mask = 16;
	static const uintptr_t xml_memory_page_type_mask = 15;

	// combined masks for string uniqueness
	static const uintptr_t xml_memory_page_name_allocated_or_shared_mask = xml_memory_page_name_allocated_mask | xml_memory_page_contents_shared_mask;
	static const uintptr_t xml_memory_page_value_allocated_or_shared_mask = xml_memory_page_value_allocated_mask | xml_memory_page_contents_shared_mask;

#ifdef PUGIXML_COMPACT
	#define PUGI_IMPL_GETHEADER_IMPL(object, page, flags) // unused
	#define PUGI_IMPL_GETPAGE_IMPL(header) (header).get_page()
#else
	#define PUGI_IMPL_GETHEADER_IMPL(object, page, flags) (((reinterpret_cast<char*>(object) - reinterpret_cast<char*>(page)) << 8) | (flags))
	// this macro casts pointers through void* to avoid 'cast increases required alignment of target type' warnings
	#define PUGI_IMPL_GETPAGE_IMPL(header) static_cast<impl::xml_memory_page*>(const_cast<void*>(static_cast<const void*>(reinterpret_cast<const char*>(&header) - (header >> 8))))
#endif

	#define PUGI_IMPL_GETPAGE(n) PUGI_IMPL_GETPAGE_IMPL((n)->header)
	#define PUGI_IMPL_NODETYPE(n) static_cast<xml_node_type>((n)->header & impl::xml_memory_page_type_mask)

	struct xml_allocator;

	struct xml_memory_page
	{
		static xml_memory_page* construct(void* memory)
		{
			xml_memory_page* result = static_cast<xml_memory_page*>(memory);

			result->allocator = NULL;
			result->prev = NULL;
			result->next = NULL;
			result->busy_size = 0;
			result->freed_size = 0;

		#ifdef PUGIXML_COMPACT
			result->compact_string_base = NULL;
			result->compact_shared_parent = NULL;
			result->compact_page_marker = NULL;
		#endif

			return result;
		}

		xml_allocator* allocator;

		xml_memory_page* prev;
		xml_memory_page* next;

		size_t busy_size;
		size_t freed_size;

	#ifdef PUGIXML_COMPACT
		char_t* compact_string_base;
		void* compact_shared_parent;
		uint32_t* compact_page_marker;
	#endif
	};

	static const size_t xml_memory_page_size =
	#ifdef PUGIXML_MEMORY_PAGE_SIZE
		(PUGIXML_MEMORY_PAGE_SIZE)
	#else
		32768
	#endif
		- sizeof(xml_memory_page);

	struct xml_memory_string_header
	{
		uint16_t page_offset; // offset from page->data
		uint16_t full_size; // 0 if string occupies whole page
	};

	struct xml_allocator
	{
		xml_allocator(xml_memory_page* root): _root(root), _busy_size(root->busy_size)
		{
		#ifdef PUGIXML_COMPACT
			_hash = NULL;
		#endif
		}

		xml_memory_page* allocate_page(size_t data_size)
		{
			size_t size = sizeof(xml_memory_page) + data_size;

			// allocate block with some alignment, leaving memory for worst-case padding
			void* memory = xml_memory::allocate(size);
			if (!memory) return NULL;

			// prepare page structure
			xml_memory_page* page = xml_memory_page::construct(memory);
			assert(page);

			assert(this == _root->allocator);
			page->allocator = this;

			return page;
		}

		static void deallocate_page(xml_memory_page* page)
		{
			xml_memory::deallocate(page);
		}

		void* allocate_memory_oob(size_t size, xml_memory_page*& out_page);

		void* allocate_memory(size_t size, xml_memory_page*& out_page)
		{
			if (PUGI_IMPL_UNLIKELY(_busy_size + size > xml_memory_page_size))
				return allocate_memory_oob(size, out_page);

			void* buf = reinterpret_cast<char*>(_root) + sizeof(xml_memory_page) + _busy_size;

			_busy_size += size;

			out_page = _root;

			return buf;
		}

	#ifdef PUGIXML_COMPACT
		void* allocate_object(size_t size, xml_memory_page*& out_page)
		{
			void* result = allocate_memory(size + sizeof(uint32_t), out_page);
			if (!result) return NULL;

			// adjust for marker
			ptrdiff_t offset = static_cast<char*>(result) - reinterpret_cast<char*>(out_page->compact_page_marker);

			if (PUGI_IMPL_UNLIKELY(static_cast<uintptr_t>(offset) >= 256 * xml_memory_block_alignment))
			{
				// insert new marker
				uint32_t* marker = static_cast<uint32_t*>(result);

				*marker = static_cast<uint32_t>(reinterpret_cast<char*>(marker) - reinterpret_cast<char*>(out_page));
				out_page->compact_page_marker = marker;

				// since we don't reuse the page space until we reallocate it, we can just pretend that we freed the marker block
				// this will make sure deallocate_memory correctly tracks the size
				out_page->freed_size += sizeof(uint32_t);

				return marker + 1;
			}
			else
			{
				// roll back uint32_t part
				_busy_size -= sizeof(uint32_t);

				return result;
			}
		}
	#else
		void* allocate_object(size_t size, xml_memory_page*& out_page)
		{
			return allocate_memory(size, out_page);
		}
	#endif

		void deallocate_memory(void* ptr, size_t size, xml_memory_page* page)
		{
			if (page == _root) page->busy_size = _busy_size;

			assert(ptr >= reinterpret_cast<char*>(page) + sizeof(xml_memory_page) && ptr < reinterpret_cast<char*>(page) + sizeof(xml_memory_page) + page->busy_size);
			(void)!ptr;

			page->freed_size += size;
			assert(page->freed_size <= page->busy_size);

			if (page->freed_size == page->busy_size)
			{
				if (page->next == NULL)
				{
					assert(_root == page);

					// top page freed, just reset sizes
					page->busy_size = 0;
					page->freed_size = 0;

				#ifdef PUGIXML_COMPACT
					// reset compact state to maximize efficiency
					page->compact_string_base = NULL;
					page->compact_shared_parent = NULL;
					page->compact_page_marker = NULL;
				#endif

					_busy_size = 0;
				}
				else
				{
					assert(_root != page);
					assert(page->prev);

					// remove from the list
					page->prev->next = page->next;
					page->next->prev = page->prev;

					// deallocate
					deallocate_page(page);
				}
			}
		}

		char_t* allocate_string(size_t length)
		{
			static const size_t max_encoded_offset = (1 << 16) * xml_memory_block_alignment;

			PUGI_IMPL_STATIC_ASSERT(xml_memory_page_size <= max_encoded_offset);

			// allocate memory for string and header block
			size_t size = sizeof(xml_memory_string_header) + length * sizeof(char_t);

			// round size up to block alignment boundary
			size_t full_size = (size + (xml_memory_block_alignment - 1)) & ~(xml_memory_block_alignment - 1);

			xml_memory_page* page;
			xml_memory_string_header* header = static_cast<xml_memory_string_header*>(allocate_memory(full_size, page));

			if (!header) return NULL;

			// setup header
			ptrdiff_t page_offset = reinterpret_cast<char*>(header) - reinterpret_cast<char*>(page) - sizeof(xml_memory_page);

			assert(page_offset % xml_memory_block_alignment == 0);
			assert(page_offset >= 0 && static_cast<size_t>(page_offset) < max_encoded_offset);
			header->page_offset = static_cast<uint16_t>(static_cast<size_t>(page_offset) / xml_memory_block_alignment);

			// full_size == 0 for large strings that occupy the whole page
			assert(full_size % xml_memory_block_alignment == 0);
			assert(full_size < max_encoded_offset || (page->busy_size == full_size && page_offset == 0));
			header->full_size = static_cast<uint16_t>(full_size < max_encoded_offset ? full_size / xml_memory_block_alignment : 0);

			// round-trip through void* to avoid 'cast increases required alignment of target type' warning
			// header is guaranteed a pointer-sized alignment, which should be enough for char_t
			return static_cast<char_t*>(static_cast<void*>(header + 1));
		}

		void deallocate_string(char_t* string)
		{
			// this function casts pointers through void* to avoid 'cast increases required alignment of target type' warnings
			// we're guaranteed the proper (pointer-sized) alignment on the input string if it was allocated via allocate_string

			// get header
			xml_memory_string_header* header = static_cast<xml_memory_string_header*>(static_cast<void*>(string)) - 1;
			assert(header);

			// deallocate
			size_t page_offset = sizeof(xml_memory_page) + header->page_offset * xml_memory_block_alignment;
			xml_memory_page* page = reinterpret_cast<xml_memory_page*>(static_cast<void*>(reinterpret_cast<char*>(header) - page_offset));

			// if full_size == 0 then this string occupies the whole page
			size_t full_size = header->full_size == 0 ? page->busy_size : header->full_size * xml_memory_block_alignment;

			deallocate_memory(header, full_size, page);
		}

		bool reserve()
		{
		#ifdef PUGIXML_COMPACT
			return _hash->reserve();
		#else
			return true;
		#endif
		}

		xml_memory_page* _root;
		size_t _busy_size;

	#ifdef PUGIXML_COMPACT
		compact_hash_table* _hash;
	#endif
	};

	PUGI_IMPL_FN_NO_INLINE void* xml_allocator::allocate_memory_oob(size_t size, xml_memory_page*& out_page)
	{
		const size_t large_allocation_threshold = xml_memory_page_size / 4;

		xml_memory_page* page = allocate_page(size <= large_allocation_threshold ? xml_memory_page_size : size);
		out_page = page;

		if (!page) return NULL;

		if (size <= large_allocation_threshold)
		{
			_root->busy_size = _busy_size;

			// insert page at the end of linked list
			page->prev = _root;
			_root->next = page;
			_root = page;

			_busy_size = size;
		}
		else
		{
			// insert page before the end of linked list, so that it is deleted as soon as possible
			// the last page is not deleted even if it's empty (see deallocate_memory)
			assert(_root->prev);

			page->prev = _root->prev;
			page->next = _root;

			_root->prev->next = page;
			_root->prev = page;

			page->busy_size = size;
		}

		return reinterpret_cast<char*>(page) + sizeof(xml_memory_page);
	}
PUGI_IMPL_NS_END

#ifdef PUGIXML_COMPACT
PUGI_IMPL_NS_BEGIN
	static const uintptr_t compact_alignment_log2 = 2;
	static const uintptr_t compact_alignment = 1 << compact_alignment_log2;

	class compact_header
	{
	public:
		compact_header(xml_memory_page* page, unsigned int flags)
		{
			PUGI_IMPL_STATIC_ASSERT(xml_memory_block_alignment == compact_alignment);

			ptrdiff_t offset = (reinterpret_cast<char*>(this) - reinterpret_cast<char*>(page->compact_page_marker));
			assert(offset % compact_alignment == 0 && static_cast<uintptr_t>(offset) < 256 * compact_alignment);

			_page = static_cast<unsigned char>(offset >> compact_alignment_log2);
			_flags = static_cast<unsigned char>(flags);
		}

		void operator&=(uintptr_t mod)
		{
			_flags &= static_cast<unsigned char>(mod);
		}

		void operator|=(uintptr_t mod)
		{
			_flags |= static_cast<unsigned char>(mod);
		}

		uintptr_t operator&(uintptr_t mod) const
		{
			return _flags & mod;
		}

		xml_memory_page* get_page() const
		{
			// round-trip through void* to silence 'cast increases required alignment of target type' warnings
			const char* page_marker = reinterpret_cast<const char*>(this) - (_page << compact_alignment_log2);
			const char* page = page_marker - *reinterpret_cast<const uint32_t*>(static_cast<const void*>(page_marker));

			return const_cast<xml_memory_page*>(reinterpret_cast<const xml_memory_page*>(static_cast<const void*>(page)));
		}

	private:
		unsigned char _page;
		unsigned char _flags;
	};

	PUGI_IMPL_FN xml_memory_page* compact_get_page(const void* object, int header_offset)
	{
		const compact_header* header = reinterpret_cast<const compact_header*>(static_cast<const char*>(object) - header_offset);

		return header->get_page();
	}

	template <int header_offset, typename T> PUGI_IMPL_FN_NO_INLINE T* compact_get_value(const void* object)
	{
		return static_cast<T*>(compact_get_page(object, header_offset)->allocator->_hash->find(object));
	}

	template <int header_offset, typename T> PUGI_IMPL_FN_NO_INLINE void compact_set_value(const void* object, T* value)
	{
		compact_get_page(object, header_offset)->allocator->_hash->insert(object, value);
	}

	template <typename T, int header_offset, int start = -126> class compact_pointer
	{
	public:
		compact_pointer(): _data(0)
		{
		}

		void operator=(const compact_pointer& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(T* value)
		{
			if (value)
			{
				// value is guaranteed to be compact-aligned; 'this' is not
				// our decoding is based on 'this' aligned to compact alignment downwards (see operator T*)
				// so for negative offsets (e.g. -3) we need to adjust the diff by compact_alignment - 1 to
				// compensate for arithmetic shift rounding for negative values
				ptrdiff_t diff = reinterpret_cast<char*>(value) - reinterpret_cast<char*>(this);
				ptrdiff_t offset = ((diff + int(compact_alignment - 1)) >> compact_alignment_log2) - start;

				if (static_cast<uintptr_t>(offset) <= 253)
					_data = static_cast<unsigned char>(offset + 1);
				else
				{
					compact_set_value<header_offset>(this, value);

					_data = 255;
				}
			}
			else
				_data = 0;
		}

		operator T*() const
		{
			if (_data)
			{
				if (_data < 255)
				{
					uintptr_t base = reinterpret_cast<uintptr_t>(this) & ~(compact_alignment - 1);

					return reinterpret_cast<T*>(base + (_data - 1 + start) * compact_alignment);
				}
				else
					return compact_get_value<header_offset, T>(this);
			}
			else
				return NULL;
		}

		T* operator->() const
		{
			return *this;
		}

	private:
		unsigned char _data;
	};

	template <typename T, int header_offset> class compact_pointer_parent
	{
	public:
		compact_pointer_parent(): _data(0)
		{
		}

		void operator=(const compact_pointer_parent& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(T* value)
		{
			if (value)
			{
				// value is guaranteed to be compact-aligned; 'this' is not
				// our decoding is based on 'this' aligned to compact alignment downwards (see operator T*)
				// so for negative offsets (e.g. -3) we need to adjust the diff by compact_alignment - 1 to
				// compensate for arithmetic shift behavior for negative values
				ptrdiff_t diff = reinterpret_cast<char*>(value) - reinterpret_cast<char*>(this);
				ptrdiff_t offset = ((diff + int(compact_alignment - 1)) >> compact_alignment_log2) + 65533;

				if (static_cast<uintptr_t>(offset) <= 65533)
				{
					_data = static_cast<unsigned short>(offset + 1);
				}
				else
				{
					xml_memory_page* page = compact_get_page(this, header_offset);

					if (PUGI_IMPL_UNLIKELY(page->compact_shared_parent == NULL))
						page->compact_shared_parent = value;

					if (page->compact_shared_parent == value)
					{
						_data = 65534;
					}
					else
					{
						compact_set_value<header_offset>(this, value);

						_data = 65535;
					}
				}
			}
			else
			{
				_data = 0;
			}
		}

		operator T*() const
		{
			if (_data)
			{
				if (_data < 65534)
				{
					uintptr_t base = reinterpret_cast<uintptr_t>(this) & ~(compact_alignment - 1);

					return reinterpret_cast<T*>(base + (_data - 1 - 65533) * compact_alignment);
				}
				else if (_data == 65534)
					return static_cast<T*>(compact_get_page(this, header_offset)->compact_shared_parent);
				else
					return compact_get_value<header_offset, T>(this);
			}
			else
				return NULL;
		}

		T* operator->() const
		{
			return *this;
		}

	private:
		uint16_t _data;
	};

	template <int header_offset, int base_offset> class compact_string
	{
	public:
		compact_string(): _data(0)
		{
		}

		void operator=(const compact_string& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(char_t* value)
		{
			if (value)
			{
				xml_memory_page* page = compact_get_page(this, header_offset);

				if (PUGI_IMPL_UNLIKELY(page->compact_string_base == NULL))
					page->compact_string_base = value;

				ptrdiff_t offset = value - page->compact_string_base;

				if (static_cast<uintptr_t>(offset) < (65535 << 7))
				{
					// round-trip through void* to silence 'cast increases required alignment of target type' warnings
					uint16_t* base = reinterpret_cast<uint16_t*>(static_cast<void*>(reinterpret_cast<char*>(this) - base_offset));

					if (*base == 0)
					{
						*base = static_cast<uint16_t>((offset >> 7) + 1);
						_data = static_cast<unsigned char>((offset & 127) + 1);
					}
					else
					{
						ptrdiff_t remainder = offset - ((*base - 1) << 7);

						if (static_cast<uintptr_t>(remainder) <= 253)
						{
							_data = static_cast<unsigned char>(remainder + 1);
						}
						else
						{
							compact_set_value<header_offset>(this, value);

							_data = 255;
						}
					}
				}
				else
				{
					compact_set_value<header_offset>(this, value);

					_data = 255;
				}
			}
			else
			{
				_data = 0;
			}
		}

		operator char_t*() const
		{
			if (_data)
			{
				if (_data < 255)
				{
					xml_memory_page* page = compact_get_page(this, header_offset);

					// round-trip through void* to silence 'cast increases required alignment of target type' warnings
					const uint16_t* base = reinterpret_cast<const uint16_t*>(static_cast<const void*>(reinterpret_cast<const char*>(this) - base_offset));
					assert(*base);

					ptrdiff_t offset = ((*base - 1) << 7) + (_data - 1);

					return page->compact_string_base + offset;
				}
				else
				{
					return compact_get_value<header_offset, char_t>(this);
				}
			}
			else
				return NULL;
		}

	private:
		unsigned char _data;
	};
PUGI_IMPL_NS_END
#endif

#ifdef PUGIXML_COMPACT
namespace pugi
{
	struct xml_attribute_struct
	{
		xml_attribute_struct(impl::xml_memory_page* page): header(page, 0), namevalue_base(0)
		{
			PUGI_IMPL_STATIC_ASSERT(sizeof(xml_attribute_struct) == 8);
		}

		impl::compact_header header;

		uint16_t namevalue_base;

		impl::compact_string<4, 2> name;
		impl::compact_string<5, 3> value;

		impl::compact_pointer<xml_attribute_struct, 6> prev_attribute_c;
		impl::compact_pointer<xml_attribute_struct, 7, 0> next_attribute;
	};

	struct xml_node_struct
	{
		xml_node_struct(impl::xml_memory_page* page, xml_node_type type): header(page, type), namevalue_base(0)
		{
			PUGI_IMPL_STATIC_ASSERT(sizeof(xml_node_struct) == 12);
		}

		impl::compact_header header;

		uint16_t namevalue_base;

		impl::compact_string<4, 2> name;
		impl::compact_string<5, 3> value;

		impl::compact_pointer_parent<xml_node_struct, 6> parent;

		impl::compact_pointer<xml_node_struct, 8, 0> first_child;

		impl::compact_pointer<xml_node_struct,  9>    prev_sibling_c;
		impl::compact_pointer<xml_node_struct, 10, 0> next_sibling;

		impl::compact_pointer<xml_attribute_struct, 11, 0> first_attribute;
	};
}
#else
namespace pugi
{
	struct xml_attribute_struct
	{
		xml_attribute_struct(impl::xml_memory_page* page): name(NULL), value(NULL), prev_attribute_c(NULL), next_attribute(NULL)
		{
			header = PUGI_IMPL_GETHEADER_IMPL(this, page, 0);
		}

		uintptr_t header;

		char_t*	name;
		char_t*	value;

		xml_attribute_struct* prev_attribute_c;
		xml_attribute_struct* next_attribute;
	};

	struct xml_node_struct
	{
		xml_node_struct(impl::xml_memory_page* page, xml_node_type type): name(NULL), value(NULL), parent(NULL), first_child(NULL), prev_sibling_c(NULL), next_sibling(NULL), first_attribute(NULL)
		{
			header = PUGI_IMPL_GETHEADER_IMPL(this, page, type);
		}

		uintptr_t header;

		char_t* name;
		char_t* value;

		xml_node_struct* parent;

		xml_node_struct* first_child;

		xml_node_struct* prev_sibling_c;
		xml_node_struct* next_sibling;

		xml_attribute_struct* first_attribute;
	};
}
#endif

PUGI_IMPL_NS_BEGIN
	struct xml_extra_buffer
	{
		char_t* buffer;
		xml_extra_buffer* next;
	};

	struct xml_document_struct: public xml_node_struct, public xml_allocator
	{
		xml_document_struct(xml_memory_page* page): xml_node_struct(page, node_document), xml_allocator(page), buffer(NULL), extra_buffers(NULL)
		{
		}

		const char_t* buffer;

		xml_extra_buffer* extra_buffers;

	#ifdef PUGIXML_COMPACT
		compact_hash_table hash;
	#endif
	};

	template <typename Object> inline xml_allocator& get_allocator(const Object* object)
	{
		assert(object);

		return *PUGI_IMPL_GETPAGE(object)->allocator;
	}

	template <typename Object> inline xml_document_struct& get_document(const Object* object)
	{
		assert(object);

		return *static_cast<xml_document_struct*>(PUGI_IMPL_GETPAGE(object)->allocator);
	}
PUGI_IMPL_NS_END

// Low-level DOM operations
PUGI_IMPL_NS_BEGIN
	inline xml_attribute_struct* allocate_attribute(xml_allocator& alloc)
	{
		xml_memory_page* page;
		void* memory = alloc.allocate_object(sizeof(xml_attribute_struct), page);
		if (!memory) return NULL;

		return new (memory) xml_attribute_struct(page);
	}

	inline xml_node_struct* allocate_node(xml_allocator& alloc, xml_node_type type)
	{
		xml_memory_page* page;
		void* memory = alloc.allocate_object(sizeof(xml_node_struct), page);
		if (!memory) return NULL;

		return new (memory) xml_node_struct(page, type);
	}

	inline void destroy_attribute(xml_attribute_struct* a, xml_allocator& alloc)
	{
		if (a->header & impl::xml_memory_page_name_allocated_mask)
			alloc.deallocate_string(a->name);

		if (a->header & impl::xml_memory_page_value_allocated_mask)
			alloc.deallocate_string(a->value);

		alloc.deallocate_memory(a, sizeof(xml_attribute_struct), PUGI_IMPL_GETPAGE(a));
	}

	inline void destroy_node(xml_node_struct* n, xml_allocator& alloc)
	{
		if (n->header & impl::xml_memory_page_name_allocated_mask)
			alloc.deallocate_string(n->name);

		if (n->header & impl::xml_memory_page_value_allocated_mask)
			alloc.deallocate_string(n->value);

		for (xml_attribute_struct* attr = n->first_attribute; attr; )
		{
			xml_attribute_struct* next = attr->next_attribute;

			destroy_attribute(attr, alloc);

			attr = next;
		}

		for (xml_node_struct* child = n->first_child; child; )
		{
			xml_node_struct* next = child->next_sibling;

			destroy_node(child, alloc);

			child = next;
		}

		alloc.deallocate_memory(n, sizeof(xml_node_struct), PUGI_IMPL_GETPAGE(n));
	}

	inline void append_node(xml_node_struct* child, xml_node_struct* node)
	{
		child->parent = node;

		xml_node_struct* head = node->first_child;

		if (head)
		{
			xml_node_struct* tail = head->prev_sibling_c;

			tail->next_sibling = child;
			child->prev_sibling_c = tail;
			head->prev_sibling_c = child;
		}
		else
		{
			node->first_child = child;
			child->prev_sibling_c = child;
		}
	}

	inline void prepend_node(xml_node_struct* child, xml_node_struct* node)
	{
		child->parent = node;

		xml_node_struct* head = node->first_child;

		if (head)
		{
			child->prev_sibling_c = head->prev_sibling_c;
			head->prev_sibling_c = child;
		}
		else
			child->prev_sibling_c = child;

		child->next_sibling = head;
		node->first_child = child;
	}

	inline void insert_node_after(xml_node_struct* child, xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		child->parent = parent;

		xml_node_struct* next = node->next_sibling;

		if (next)
			next->prev_sibling_c = child;
		else
			parent->first_child->prev_sibling_c = child;

		child->next_sibling = next;
		child->prev_sibling_c = node;

		node->next_sibling = child;
	}

	inline void insert_node_before(xml_node_struct* child, xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		child->parent = parent;

		xml_node_struct* prev = node->prev_sibling_c;

		if (prev->next_sibling)
			prev->next_sibling = child;
		else
			parent->first_child = child;

		child->prev_sibling_c = prev;
		child->next_sibling = node;

		node->prev_sibling_c = child;
	}

	inline void remove_node(xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		xml_node_struct* next = node->next_sibling;
		xml_node_struct* prev = node->prev_sibling_c;

		if (next)
			next->prev_sibling_c = prev;
		else
			parent->first_child->prev_sibling_c = prev;

		if (prev->next_sibling)
			prev->next_sibling = next;
		else
			parent->first_child = next;

		node->parent = NULL;
		node->prev_sibling_c = NULL;
		node->next_sibling = NULL;
	}

	inline void append_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		xml_attribute_struct* head = node->first_attribute;

		if (head)
		{
			xml_attribute_struct* tail = head->prev_attribute_c;

			tail->next_attribute = attr;
			attr->prev_attribute_c = tail;
			head->prev_attribute_c = attr;
		}
		else
		{
			node->first_attribute = attr;
			attr->prev_attribute_c = attr;
		}
	}

	inline void prepend_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		xml_attribute_struct* head = node->first_attribute;

		if (head)
		{
			attr->prev_attribute_c = head->prev_attribute_c;
			head->prev_attribute_c = attr;
		}
		else
			attr->prev_attribute_c = attr;

		attr->next_attribute = head;
		node->first_attribute = attr;
	}

	inline void insert_attribute_after(xml_attribute_struct* attr, xml_attribute_struct* place, xml_node_struct* node)
	{
		xml_attribute_struct* next = place->next_attribute;

		if (next)
			next->prev_attribute_c = attr;
		else
			node->first_attribute->prev_attribute_c = attr;

		attr->next_attribute = next;
		attr->prev_attribute_c = place;
		place->next_attribute = attr;
	}

	inline void insert_attribute_before(xml_attribute_struct* attr, xml_attribute_struct* place, xml_node_struct* node)
	{
		xml_attribute_struct* prev = place->prev_attribute_c;

		if (prev->next_attribute)
			prev->next_attribute = attr;
		else
			node->first_attribute = attr;

		attr->prev_attribute_c = prev;
		attr->next_attribute = place;
		place->prev_attribute_c = attr;
	}

	inline void remove_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		xml_attribute_struct* next = attr->next_attribute;
		xml_attribute_struct* prev = attr->prev_attribute_c;

		if (next)
			next->prev_attribute_c = prev;
		else
			node->first_attribute->prev_attribute_c = prev;

		if (prev->next_attribute)
			prev->next_attribute = next;
		else
			node->first_attribute = next;

		attr->prev_attribute_c = NULL;
		attr->next_attribute = NULL;
	}

	PUGI_IMPL_FN_NO_INLINE xml_node_struct* append_new_node(xml_node_struct* node, xml_allocator& alloc, xml_node_type type = node_element)
	{
		if (!alloc.reserve()) return NULL;

		xml_node_struct* child = allocate_node(alloc, type);
		if (!child) return NULL;

		append_node(child, node);

		return child;
	}

	PUGI_IMPL_FN_NO_INLINE xml_attribute_struct* append_new_attribute(xml_node_struct* node, xml_allocator& alloc)
	{
		if (!alloc.reserve()) return NULL;

		xml_attribute_struct* attr = allocate_attribute(alloc);
		if (!attr) return NULL;

		append_attribute(attr, node);

		return attr;
	}
PUGI_IMPL_NS_END

// Helper classes for code generation
PUGI_IMPL_NS_BEGIN
	struct opt_false
	{
		enum { value = 0 };
	};

	struct opt_true
	{
		enum { value = 1 };
	};
PUGI_IMPL_NS_END

// Unicode utilities
PUGI_IMPL_NS_BEGIN
	inline uint16_t endian_swap(uint16_t value)
	{
		return static_cast<uint16_t>(((value & 0xff) << 8) | (value >> 8));
	}

	inline uint32_t endian_swap(uint32_t value)
	{
		return ((value & 0xff) << 24) | ((value & 0xff00) << 8) | ((value & 0xff0000) >> 8) | (value >> 24);
	}

	struct utf8_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			// U+0000..U+007F
			if (ch < 0x80) return result + 1;
			// U+0080..U+07FF
			else if (ch < 0x800) return result + 2;
			// U+0800..U+FFFF
			else return result + 3;
		}

		static value_type high(value_type result, uint32_t)
		{
			// U+10000..U+10FFFF
			return result + 4;
		}
	};

	struct utf8_writer
	{
		typedef uint8_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			// U+0000..U+007F
			if (ch < 0x80)
			{
				*result = static_cast<uint8_t>(ch);
				return result + 1;
			}
			// U+0080..U+07FF
			else if (ch < 0x800)
			{
				result[0] = static_cast<uint8_t>(0xC0 | (ch >> 6));
				result[1] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
				return result + 2;
			}
			// U+0800..U+FFFF
			else
			{
				result[0] = static_cast<uint8_t>(0xE0 | (ch >> 12));
				result[1] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
				result[2] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
				return result + 3;
			}
		}

		static value_type high(value_type result, uint32_t ch)
		{
			// U+10000..U+10FFFF
			result[0] = static_cast<uint8_t>(0xF0 | (ch >> 18));
			result[1] = static_cast<uint8_t>(0x80 | ((ch >> 12) & 0x3F));
			result[2] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
			result[3] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
			return result + 4;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			return (ch < 0x10000) ? low(result, ch) : high(result, ch);
		}
	};

	struct utf16_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t)
		{
			return result + 1;
		}

		static value_type high(value_type result, uint32_t)
		{
			return result + 2;
		}
	};

	struct utf16_writer
	{
		typedef uint16_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = static_cast<uint16_t>(ch);

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			uint32_t msh = (ch - 0x10000U) >> 10;
			uint32_t lsh = (ch - 0x10000U) & 0x3ff;

			result[0] = static_cast<uint16_t>(0xD800 + msh);
			result[1] = static_cast<uint16_t>(0xDC00 + lsh);

			return result + 2;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			return (ch < 0x10000) ? low(result, ch) : high(result, ch);
		}
	};

	struct utf32_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t)
		{
			return result + 1;
		}

		static value_type high(value_type result, uint32_t)
		{
			return result + 1;
		}
	};

	struct utf32_writer
	{
		typedef uint32_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}
	};

	struct latin1_writer
	{
		typedef uint8_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = static_cast<uint8_t>(ch > 255 ? '?' : ch);

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			(void)ch;

			*result = '?';

			return result + 1;
		}
	};

	struct utf8_decoder
	{
		typedef uint8_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint8_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			const uint8_t utf8_byte_mask = 0x3f;

			while (size)
			{
				uint8_t lead = *data;

				// 0xxxxxxx -> U+0000..U+007F
				if (lead < 0x80)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;

					// process aligned single-byte (ascii) blocks
					if ((reinterpret_cast<uintptr_t>(data) & 3) == 0)
					{
						// round-trip through void* to silence 'cast increases required alignment of target type' warnings
						while (size >= 4 && (*static_cast<const uint32_t*>(static_cast<const void*>(data)) & 0x80808080) == 0)
						{
							result = Traits::low(result, data[0]);
							result = Traits::low(result, data[1]);
							result = Traits::low(result, data[2]);
							result = Traits::low(result, data[3]);
							data += 4;
							size -= 4;
						}
					}
				}
				// 110xxxxx -> U+0080..U+07FF
				else if (static_cast<unsigned int>(lead - 0xC0) < 0x20 && size >= 2 && (data[1] & 0xc0) == 0x80)
				{
					result = Traits::low(result, ((lead & ~0xC0) << 6) | (data[1] & utf8_byte_mask));
					data += 2;
					size -= 2;
				}
				// 1110xxxx -> U+0800-U+FFFF
				else if (static_cast<unsigned int>(lead - 0xE0) < 0x10 && size >= 3 && (data[1] & 0xc0) == 0x80 && (data[2] & 0xc0) == 0x80)
				{
					result = Traits::low(result, ((lead & ~0xE0) << 12) | ((data[1] & utf8_byte_mask) << 6) | (data[2] & utf8_byte_mask));
					data += 3;
					size -= 3;
				}
				// 11110xxx -> U+10000..U+10FFFF
				else if (static_cast<unsigned int>(lead - 0xF0) < 0x08 && size >= 4 && (data[1] & 0xc0) == 0x80 && (data[2] & 0xc0) == 0x80 && (data[3] & 0xc0) == 0x80)
				{
					result = Traits::high(result, ((lead & ~0xF0) << 18) | ((data[1] & utf8_byte_mask) << 12) | ((data[2] & utf8_byte_mask) << 6) | (data[3] & utf8_byte_mask));
					data += 4;
					size -= 4;
				}
				// 10xxxxxx or 11111xxx -> invalid
				else
				{
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	template <typename opt_swap> struct utf16_decoder
	{
		typedef uint16_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint16_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				uint16_t lead = opt_swap::value ? endian_swap(*data) : *data;

				// U+0000..U+D7FF
				if (lead < 0xD800)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// U+E000..U+FFFF
				else if (static_cast<unsigned int>(lead - 0xE000) < 0x2000)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// surrogate pair lead
				else if (static_cast<unsigned int>(lead - 0xD800) < 0x400 && size >= 2)
				{
					uint16_t next = opt_swap::value ? endian_swap(data[1]) : data[1];

					if (static_cast<unsigned int>(next - 0xDC00) < 0x400)
					{
						result = Traits::high(result, 0x10000 + ((lead & 0x3ff) << 10) + (next & 0x3ff));
						data += 2;
						size -= 2;
					}
					else
					{
						data += 1;
						size -= 1;
					}
				}
				else
				{
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	template <typename opt_swap> struct utf32_decoder
	{
		typedef uint32_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint32_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				uint32_t lead = opt_swap::value ? endian_swap(*data) : *data;

				// U+0000..U+FFFF
				if (lead < 0x10000)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// U+10000..U+10FFFF
				else
				{
					result = Traits::high(result, lead);
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	struct latin1_decoder
	{
		typedef uint8_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint8_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				result = Traits::low(result, *data);
				data += 1;
				size -= 1;
			}

			return result;
		}
	};

	template <size_t size> struct wchar_selector;

	template <> struct wchar_selector<2>
	{
		typedef uint16_t type;
		typedef utf16_counter counter;
		typedef utf16_writer writer;
		typedef utf16_decoder<opt_false> decoder;
	};

	template <> struct wchar_selector<4>
	{
		typedef uint32_t type;
		typedef utf32_counter counter;
		typedef utf32_writer writer;
		typedef utf32_decoder<opt_false> decoder;
	};

	typedef wchar_selector<sizeof(wchar_t)>::counter wchar_counter;
	typedef wchar_selector<sizeof(wchar_t)>::writer wchar_writer;

	struct wchar_decoder
	{
		typedef wchar_t type;

		template <typename Traits> static inline typename Traits::value_type process(const wchar_t* data, size_t size, typename Traits::value_type result, Traits traits)
		{
			typedef wchar_selector<sizeof(wchar_t)>::decoder decoder;

			return decoder::process(reinterpret_cast<const typename decoder::type*>(data), size, result, traits);
		}
	};

#ifdef PUGIXML_WCHAR_MODE
	PUGI_IMPL_FN void convert_wchar_endian_swap(wchar_t* result, const wchar_t* data, size_t length)
	{
		for (size_t i = 0; i < length; ++i)
			result[i] = static_cast<wchar_t>(endian_swap(static_cast<wchar_selector<sizeof(wchar_t)>::type>(data[i])));
	}
#endif
PUGI_IMPL_NS_END

PUGI_IMPL_NS_BEGIN
	enum chartype_t
	{
		ct_parse_pcdata = 1,	// \0, &, \r, <
		ct_parse_attr = 2,		// \0, &, \r, ', "
		ct_parse_attr_ws = 4,	// \0, &, \r, ', ", \n, tab
		ct_space = 8,			// \r, \n, space, tab
		ct_parse_cdata = 16,	// \0, ], >, \r
		ct_parse_comment = 32,	// \0, -, >, \r
		ct_symbol = 64,			// Any symbol > 127, a-z, A-Z, 0-9, _, :, -, .
		ct_start_symbol = 128	// Any symbol > 127, a-z, A-Z, _, :
	};

	static const unsigned char chartype_table[256] =
	{
		55,  0,   0,   0,   0,   0,   0,   0,      0,   12,  12,  0,   0,   63,  0,   0,   // 0-15
		0,   0,   0,   0,   0,   0,   0,   0,      0,   0,   0,   0,   0,   0,   0,   0,   // 16-31
		8,   0,   6,   0,   0,   0,   7,   6,      0,   0,   0,   0,   0,   96,  64,  0,   // 32-47
		64,  64,  64,  64,  64,  64,  64,  64,     64,  64,  192, 0,   1,   0,   48,  0,   // 48-63
		0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 64-79
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0,   0,   16,  0,   192, // 80-95
		0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 96-111
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0, 0, 0, 0, 0,           // 112-127

		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 128+
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192
	};

	enum chartypex_t
	{
		ctx_special_pcdata = 1,   // Any symbol >= 0 and < 32 (except \t, \r, \n), &, <, >
		ctx_special_attr = 2,     // Any symbol >= 0 and < 32, &, <, ", '
		ctx_start_symbol = 4,	  // Any symbol > 127, a-z, A-Z, _
		ctx_digit = 8,			  // 0-9
		ctx_symbol = 16			  // Any symbol > 127, a-z, A-Z, 0-9, _, -, .
	};

	static const unsigned char chartypex_table[256] =
	{
		3,  3,  3,  3,  3,  3,  3,  3,     3,  2,  2,  3,  3,  2,  3,  3,     // 0-15
		3,  3,  3,  3,  3,  3,  3,  3,     3,  3,  3,  3,  3,  3,  3,  3,     // 16-31
		0,  0,  2,  0,  0,  0,  3,  2,     0,  0,  0,  0,  0, 16, 16,  0,     // 32-47
		24, 24, 24, 24, 24, 24, 24, 24,    24, 24, 0,  0,  3,  0,  1,  0,     // 48-63

		0,  20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 64-79
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 0,  0,  0,  0,  20,    // 80-95
		0,  20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 96-111
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 0,  0,  0,  0,  0,     // 112-127

		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 128+
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20
	};

#ifdef PUGIXML_WCHAR_MODE
	#define PUGI_IMPL_IS_CHARTYPE_IMPL(c, ct, table) ((static_cast<unsigned int>(c) < 128 ? table[static_cast<unsigned int>(c)] : table[128]) & (ct))
#else
	#define PUGI_IMPL_IS_CHARTYPE_IMPL(c, ct, table) (table[static_cast<unsigned char>(c)] & (ct))
#endif

	#define PUGI_IMPL_IS_CHARTYPE(c, ct) PUGI_IMPL_IS_CHARTYPE_IMPL(c, ct, chartype_table)
	#define PUGI_IMPL_IS_CHARTYPEX(c, ct) PUGI_IMPL_IS_CHARTYPE_IMPL(c, ct, chartypex_table)

	PUGI_IMPL_FN bool is_little_endian()
	{
		unsigned int ui = 1;

		return *reinterpret_cast<unsigned char*>(&ui) == 1;
	}

	PUGI_IMPL_FN xml_encoding get_wchar_encoding()
	{
		PUGI_IMPL_STATIC_ASSERT(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4);

		if (sizeof(wchar_t) == 2)
			return is_little_endian() ? encoding_utf16_le : encoding_utf16_be;
		else
			return is_little_endian() ? encoding_utf32_le : encoding_utf32_be;
	}

	PUGI_IMPL_FN bool parse_declaration_encoding(const uint8_t* data, size_t size, const uint8_t*& out_encoding, size_t& out_length)
	{
	#define PUGI_IMPL_SCANCHAR(ch) { if (offset >= size || data[offset] != ch) return false; offset++; }
	#define PUGI_IMPL_SCANCHARTYPE(ct) { while (offset < size && PUGI_IMPL_IS_CHARTYPE(data[offset], ct)) offset++; }

		// check if we have a non-empty XML declaration
		if (size < 6 || !((data[0] == '<') & (data[1] == '?') & (data[2] == 'x') & (data[3] == 'm') & (data[4] == 'l') && PUGI_IMPL_IS_CHARTYPE(data[5], ct_space)))
			return false;

		// scan XML declaration until the encoding field
		for (size_t i = 6; i + 1 < size; ++i)
		{
			// declaration can not contain ? in quoted values
			if (data[i] == '?')
				return false;

			if (data[i] == 'e' && data[i + 1] == 'n')
			{
				size_t offset = i;

				// encoding follows the version field which can't contain 'en' so this has to be the encoding if XML is well formed
				PUGI_IMPL_SCANCHAR('e'); PUGI_IMPL_SCANCHAR('n'); PUGI_IMPL_SCANCHAR('c'); PUGI_IMPL_SCANCHAR('o');
				PUGI_IMPL_SCANCHAR('d'); PUGI_IMPL_SCANCHAR('i'); PUGI_IMPL_SCANCHAR('n'); PUGI_IMPL_SCANCHAR('g');

				// S? = S?
				PUGI_IMPL_SCANCHARTYPE(ct_space);
				PUGI_IMPL_SCANCHAR('=');
				PUGI_IMPL_SCANCHARTYPE(ct_space);

				// the only two valid delimiters are ' and "
				uint8_t delimiter = (offset < size && data[offset] == '"') ? '"' : '\'';

				PUGI_IMPL_SCANCHAR(delimiter);

				size_t start = offset;

				out_encoding = data + offset;

				PUGI_IMPL_SCANCHARTYPE(ct_symbol);

				out_length = offset - start;

				PUGI_IMPL_SCANCHAR(delimiter);

				return true;
			}
		}

		return false;

	#undef PUGI_IMPL_SCANCHAR
	#undef PUGI_IMPL_SCANCHARTYPE
	}

	PUGI_IMPL_FN xml_encoding guess_buffer_encoding(const uint8_t* data, size_t size)
	{
		// skip encoding autodetection if input buffer is too small
		if (size < 4) return encoding_utf8;

		uint8_t d0 = data[0], d1 = data[1], d2 = data[2], d3 = data[3];

		// look for BOM in first few bytes
		if (d0 == 0 && d1 == 0 && d2 == 0xfe && d3 == 0xff) return encoding_utf32_be;
		if (d0 == 0xff && d1 == 0xfe && d2 == 0 && d3 == 0) return encoding_utf32_le;
		if (d0 == 0xfe && d1 == 0xff) return encoding_utf16_be;
		if (d0 == 0xff && d1 == 0xfe) return encoding_utf16_le;
		if (d0 == 0xef && d1 == 0xbb && d2 == 0xbf) return encoding_utf8;

		// look for <, <? or <?xm in various encodings
		if (d0 == 0 && d1 == 0 && d2 == 0 && d3 == 0x3c) return encoding_utf32_be;
		if (d0 == 0x3c && d1 == 0 && d2 == 0 && d3 == 0) return encoding_utf32_le;
		if (d0 == 0 && d1 == 0x3c && d2 == 0 && d3 == 0x3f) return encoding_utf16_be;
		if (d0 == 0x3c && d1 == 0 && d2 == 0x3f && d3 == 0) return encoding_utf16_le;

		// look for utf16 < followed by node name (this may fail, but is better than utf8 since it's zero terminated so early)
		if (d0 == 0 && d1 == 0x3c) return encoding_utf16_be;
		if (d0 == 0x3c && d1 == 0) return encoding_utf16_le;

		// no known BOM detected; parse declaration
		const uint8_t* enc = NULL;
		size_t enc_length = 0;

		if (d0 == 0x3c && d1 == 0x3f && d2 == 0x78 && d3 == 0x6d && parse_declaration_encoding(data, size, enc, enc_length))
		{
			// iso-8859-1 (case-insensitive)
			if (enc_length == 10
				&& (enc[0] | ' ') == 'i' && (enc[1] | ' ') == 's' && (enc[2] | ' ') == 'o'
				&& enc[3] == '-' && enc[4] == '8' && enc[5] == '8' && enc[6] == '5' && enc[7] == '9'
				&& enc[8] == '-' && enc[9] == '1')
				return encoding_latin1;

			// latin1 (case-insensitive)
			if (enc_length == 6
				&& (enc[0] | ' ') == 'l' && (enc[1] | ' ') == 'a' && (enc[2] | ' ') == 't'
				&& (enc[3] | ' ') == 'i' && (enc[4] | ' ') == 'n'
				&& enc[5] == '1')
				return encoding_latin1;
		}

		return encoding_utf8;
	}

	PUGI_IMPL_FN xml_encoding get_buffer_encoding(xml_encoding encoding, const void* contents, size_t size)
	{
		// replace wchar encoding with utf implementation
		if (encoding == encoding_wchar) return get_wchar_encoding();

		// replace utf16 encoding with utf16 with specific endianness
		if (encoding == encoding_utf16) return is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

		// replace utf32 encoding with utf32 with specific endianness
		if (encoding == encoding_utf32) return is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

		// only do autodetection if no explicit encoding is requested
		if (encoding != encoding_auto) return encoding;

		// try to guess encoding (based on XML specification, Appendix F.1)
		const uint8_t* data = static_cast<const uint8_t*>(contents);

		return guess_buffer_encoding(data, size);
	}

	PUGI_IMPL_FN bool get_mutable_buffer(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, bool is_mutable)
	{
		size_t length = size / sizeof(char_t);

		if (is_mutable)
		{
			out_buffer = static_cast<char_t*>(const_cast<void*>(contents));
			out_length = length;
		}
		else
		{
			char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
			if (!buffer) return false;

			if (contents)
				memcpy(buffer, contents, length * sizeof(char_t));
			else
				assert(length == 0);

			buffer[length] = 0;

			out_buffer = buffer;
			out_length = length + 1;
		}

		return true;
	}

#ifdef PUGIXML_WCHAR_MODE
	PUGI_IMPL_FN bool need_endian_swap_utf(xml_encoding le, xml_encoding re)
	{
		return (le == encoding_utf16_be && re == encoding_utf16_le) || (le == encoding_utf16_le && re == encoding_utf16_be) ||
			   (le == encoding_utf32_be && re == encoding_utf32_le) || (le == encoding_utf32_le && re == encoding_utf32_be);
	}

	PUGI_IMPL_FN bool convert_buffer_endian_swap(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, bool is_mutable)
	{
		const char_t* data = static_cast<const char_t*>(contents);
		size_t length = size / sizeof(char_t);

		if (is_mutable)
		{
			char_t* buffer = const_cast<char_t*>(data);

			convert_wchar_endian_swap(buffer, data, length);

			out_buffer = buffer;
			out_length = length;
		}
		else
		{
			char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
			if (!buffer) return false;

			convert_wchar_endian_swap(buffer, data, length);
			buffer[length] = 0;

			out_buffer = buffer;
			out_length = length + 1;
		}

		return true;
	}

	template <typename D> PUGI_IMPL_FN bool convert_buffer_generic(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, D)
	{
		const typename D::type* data = static_cast<const typename D::type*>(contents);
		size_t data_length = size / sizeof(typename D::type);

		// first pass: get length in wchar_t units
		size_t length = D::process(data, data_length, 0, wchar_counter());

		// allocate buffer of suitable length
		char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
		if (!buffer) return false;

		// second pass: convert utf16 input to wchar_t
		wchar_writer::value_type obegin = reinterpret_cast<wchar_writer::value_type>(buffer);
		wchar_writer::value_type oend = D::process(data, data_length, obegin, wchar_writer());

		assert(oend == obegin + length);
		*oend = 0;

		out_buffer = buffer;
		out_length = length + 1;

		return true;
	}

	PUGI_IMPL_FN bool convert_buffer(char_t*& out_buffer, size_t& out_length, xml_encoding encoding, const void* contents, size_t size, bool is_mutable)
	{
		// get native encoding
		xml_encoding wchar_encoding = get_wchar_encoding();

		// fast path: no conversion required
		if (encoding == wchar_encoding)
			return get_mutable_buffer(out_buffer, out_length, contents, size, is_mutable);

		// only endian-swapping is required
		if (need_endian_swap_utf(encoding, wchar_encoding))
			return convert_buffer_endian_swap(out_buffer, out_length, contents, size, is_mutable);

		// source encoding is utf8
		if (encoding == encoding_utf8)
			return convert_buffer_generic(out_buffer, out_length, contents, size, utf8_decoder());

		// source encoding is utf16
		if (encoding == encoding_utf16_be || encoding == encoding_utf16_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

			return (native_encoding == encoding) ?
				convert_buffer_generic(out_buffer, out_length, contents, size, utf16_decoder<opt_false>()) :
				convert_buffer_generic(out_buffer, out_length, contents, size, utf16_decoder<opt_true>());
		}

		// source encoding is utf32
		if (encoding == encoding_utf32_be || encoding == encoding_utf32_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

			return (native_encoding == encoding) ?
				convert_buffer_generic(out_buffer, out_length, contents, size, utf32_decoder<opt_false>()) :
				convert_buffer_generic(out_buffer, out_length, contents, size, utf32_decoder<opt_true>());
		}

		// source encoding is latin1
		if (encoding == encoding_latin1)
			return convert_buffer_generic(out_buffer, out_length, contents, size, latin1_decoder());

		assert(false && "Invalid encoding"); // unreachable
		return false;
	}
#else
	template <typename D> PUGI_IMPL_FN bool convert_buffer_generic(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, D)
	{
		const typename D::type* data = static_cast<const typename D::type*>(contents);
		size_t data_length = size / sizeof(typename D::type);

		// first pass: get length in utf8 units
		size_t length = D::process(data, data_length, 0, utf8_counter());

		// allocate buffer of suitable length
		char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
		if (!buffer) return false;

		// second pass: convert utf16 input to utf8
		uint8_t* obegin = reinterpret_cast<uint8_t*>(buffer);
		uint8_t* oend = D::process(data, data_length, obegin, utf8_writer());

		assert(oend == obegin + length);
		*oend = 0;

		out_buffer = buffer;
		out_length = length + 1;

		return true;
	}

	PUGI_IMPL_FN size_t get_latin1_7bit_prefix_length(const uint8_t* data, size_t size)
	{
		for (size_t i = 0; i < size; ++i)
			if (data[i] > 127)
				return i;

		return size;
	}

	PUGI_IMPL_FN bool convert_buffer_latin1(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, bool is_mutable)
	{
		const uint8_t* data = static_cast<const uint8_t*>(contents);
		size_t data_length = size;

		// get size of prefix that does not need utf8 conversion
		size_t prefix_length = get_latin1_7bit_prefix_length(data, data_length);
		assert(prefix_length <= data_length);

		const uint8_t* postfix = data + prefix_length;
		size_t postfix_length = data_length - prefix_length;

		// if no conversion is needed, just return the original buffer
		if (postfix_length == 0) return get_mutable_buffer(out_buffer, out_length, contents, size, is_mutable);

		// first pass: get length in utf8 units
		size_t length = prefix_length + latin1_decoder::process(postfix, postfix_length, 0, utf8_counter());

		// allocate buffer of suitable length
		char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
		if (!buffer) return false;

		// second pass: convert latin1 input to utf8
		memcpy(buffer, data, prefix_length);

		uint8_t* obegin = reinterpret_cast<uint8_t*>(buffer);
		uint8_t* oend = latin1_decoder::process(postfix, postfix_length, obegin + prefix_length, utf8_writer());

		assert(oend == obegin + length);
		*oend = 0;

		out_buffer = buffer;
		out_length = length + 1;

		return true;
	}

	PUGI_IMPL_FN bool convert_buffer(char_t*& out_buffer, size_t& out_length, xml_encoding encoding, const void* contents, size_t size, bool is_mutable)
	{
		// fast path: no conversion required
		if (encoding == encoding_utf8)
			return get_mutable_buffer(out_buffer, out_length, contents, size, is_mutable);

		// source encoding is utf16
		if (encoding == encoding_utf16_be || encoding == encoding_utf16_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

			return (native_encoding == encoding) ?
				convert_buffer_generic(out_buffer, out_length, contents, size, utf16_decoder<opt_false>()) :
				convert_buffer_generic(out_buffer, out_length, contents, size, utf16_decoder<opt_true>());
		}

		// source encoding is utf32
		if (encoding == encoding_utf32_be || encoding == encoding_utf32_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

			return (native_encoding == encoding) ?
				convert_buffer_generic(out_buffer, out_length, contents, size, utf32_decoder<opt_false>()) :
				convert_buffer_generic(out_buffer, out_length, contents, size, utf32_decoder<opt_true>());
		}

		// source encoding is latin1
		if (encoding == encoding_latin1)
			return convert_buffer_latin1(out_buffer, out_length, contents, size, is_mutable);

		assert(false && "Invalid encoding"); // unreachable
		return false;
	}
#endif

	PUGI_IMPL_FN size_t as_utf8_begin(const wchar_t* str, size_t length)
	{
		// get length in utf8 characters
		return wchar_decoder::process(str, length, 0, utf8_counter());
	}

	PUGI_IMPL_FN void as_utf8_end(char* buffer, size_t size, const wchar_t* str, size_t length)
	{
		// convert to utf8
		uint8_t* begin = reinterpret_cast<uint8_t*>(buffer);
		uint8_t* end = wchar_decoder::process(str, length, begin, utf8_writer());

		assert(begin + size == end);
		(void)!end;
		(void)!size;
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN std::string as_utf8_impl(const wchar_t* str, size_t length)
	{
		// first pass: get length in utf8 characters
		size_t size = as_utf8_begin(str, length);

		// allocate resulting string
		std::string result;
		result.resize(size);

		// second pass: convert to utf8
		if (size > 0) as_utf8_end(&result[0], size, str, length);

		return result;
	}

	PUGI_IMPL_FN std::basic_string<wchar_t> as_wide_impl(const char* str, size_t size)
	{
		const uint8_t* data = reinterpret_cast<const uint8_t*>(str);

		// first pass: get length in wchar_t units
		size_t length = utf8_decoder::process(data, size, 0, wchar_counter());

		// allocate resulting string
		std::basic_string<wchar_t> result;
		result.resize(length);

		// second pass: convert to wchar_t
		if (length > 0)
		{
			wchar_writer::value_type begin = reinterpret_cast<wchar_writer::value_type>(&result[0]);
			wchar_writer::value_type end = utf8_decoder::process(data, size, begin, wchar_writer());

			assert(begin + length == end);
			(void)!end;
		}

		return result;
	}
#endif

	template <typename Header>
	inline bool strcpy_insitu_allow(size_t length, const Header& header, uintptr_t header_mask, char_t* target)
	{
		// never reuse shared memory
		if (header & xml_memory_page_contents_shared_mask) return false;

		size_t target_length = strlength(target);

		// always reuse document buffer memory if possible
		if ((header & header_mask) == 0) return target_length >= length;

		// reuse heap memory if waste is not too great
		const size_t reuse_threshold = 32;

		return target_length >= length && (target_length < reuse_threshold || target_length - length < target_length / 2);
	}

	template <typename String, typename Header>
	PUGI_IMPL_FN bool strcpy_insitu(String& dest, Header& header, uintptr_t header_mask, const char_t* source, size_t source_length)
	{
		assert((header & header_mask) == 0 || dest); // header bit indicates whether dest was previously allocated

		if (source_length == 0)
		{
			// empty string and null pointer are equivalent, so just deallocate old memory
			xml_allocator* alloc = PUGI_IMPL_GETPAGE_IMPL(header)->allocator;

			if (header & header_mask) alloc->deallocate_string(dest);

			// mark the string as not allocated
			dest = NULL;
			header &= ~header_mask;

			return true;
		}
		else if (dest && strcpy_insitu_allow(source_length, header, header_mask, dest))
		{
			// we can reuse old buffer, so just copy the new data (including zero terminator)
			memcpy(dest, source, source_length * sizeof(char_t));
			dest[source_length] = 0;

			return true;
		}
		else
		{
			xml_allocator* alloc = PUGI_IMPL_GETPAGE_IMPL(header)->allocator;

			if (!alloc->reserve()) return false;

			// allocate new buffer
			char_t* buf = alloc->allocate_string(source_length + 1);
			if (!buf) return false;

			// copy the string (including zero terminator)
			memcpy(buf, source, source_length * sizeof(char_t));
			buf[source_length] = 0;

			// deallocate old buffer (*after* the above to protect against overlapping memory and/or allocation failures)
			if (header & header_mask) alloc->deallocate_string(dest);

			// the string is now allocated, so set the flag
			dest = buf;
			header |= header_mask;

			return true;
		}
	}

	struct gap
	{
		char_t* end;
		size_t size;

		gap(): end(NULL), size(0)
		{
		}

		// Push new gap, move s count bytes further (skipping the gap).
		// Collapse previous gap.
		void push(char_t*& s, size_t count)
		{
			if (end) // there was a gap already; collapse it
			{
				// Move [old_gap_end, new_gap_start) to [old_gap_start, ...)
				assert(s >= end);
				memmove(end - size, end, (s - end) * sizeof(char_t));
			}

			s += count; // end of current gap

			// "merge" two gaps
			end = s;
			size += count;
		}

		// Collapse all gaps, return past-the-end pointer
		char_t* flush(char_t* s)
		{
			if (end)
			{
				// Move [old_gap_end, current_pos) to [old_gap_start, ...)
				assert(s >= end);
				memmove(end - size, end, (s - end) * sizeof(char_t));

				return s - size;
			}
			else return s;
		}
	};

	PUGI_IMPL_FN char_t* strconv_escape(char_t* s, gap& g)
	{
		char_t* stre = s + 1;

		switch (*stre)
		{
			case '#':	// &#...
			{
				unsigned int ucsc = 0;

				if (stre[1] == 'x') // &#x... (hex code)
				{
					stre += 2;

					char_t ch = *stre;

					if (ch == ';') return stre;

					for (;;)
					{
						if (static_cast<unsigned int>(ch - '0') <= 9)
							ucsc = 16 * ucsc + (ch - '0');
						else if (static_cast<unsigned int>((ch | ' ') - 'a') <= 5)
							ucsc = 16 * ucsc + ((ch | ' ') - 'a' + 10);
						else if (ch == ';')
							break;
						else // cancel
							return stre;

						ch = *++stre;
					}

					++stre;
				}
				else	// &#... (dec code)
				{
					char_t ch = *++stre;

					if (ch == ';') return stre;

					for (;;)
					{
						if (static_cast<unsigned int>(ch - '0') <= 9)
							ucsc = 10 * ucsc + (ch - '0');
						else if (ch == ';')
							break;
						else // cancel
							return stre;

						ch = *++stre;
					}

					++stre;
				}

			#ifdef PUGIXML_WCHAR_MODE
				s = reinterpret_cast<char_t*>(wchar_writer::any(reinterpret_cast<wchar_writer::value_type>(s), ucsc));
			#else
				s = reinterpret_cast<char_t*>(utf8_writer::any(reinterpret_cast<uint8_t*>(s), ucsc));
			#endif

				g.push(s, stre - s);
				return stre;
			}

			case 'a':	// &a
			{
				++stre;

				if (*stre == 'm') // &am
				{
					if (*++stre == 'p' && *++stre == ';') // &amp;
					{
						*s++ = '&';
						++stre;

						g.push(s, stre - s);
						return stre;
					}
				}
				else if (*stre == 'p') // &ap
				{
					if (*++stre == 'o' && *++stre == 's' && *++stre == ';') // &apos;
					{
						*s++ = '\'';
						++stre;

						g.push(s, stre - s);
						return stre;
					}
				}
				break;
			}

			case 'g': // &g
			{
				if (*++stre == 't' && *++stre == ';') // &gt;
				{
					*s++ = '>';
					++stre;

					g.push(s, stre - s);
					return stre;
				}
				break;
			}

			case 'l': // &l
			{
				if (*++stre == 't' && *++stre == ';') // &lt;
				{
					*s++ = '<';
					++stre;

					g.push(s, stre - s);
					return stre;
				}
				break;
			}

			case 'q': // &q
			{
				if (*++stre == 'u' && *++stre == 'o' && *++stre == 't' && *++stre == ';') // &quot;
				{
					*s++ = '"';
					++stre;

					g.push(s, stre - s);
					return stre;
				}
				break;
			}

			default:
				break;
		}

		return stre;
	}

	// Parser utilities
	#define PUGI_IMPL_ENDSWITH(c, e)        ((c) == (e) || ((c) == 0 && endch == (e)))
	#define PUGI_IMPL_SKIPWS()              { while (PUGI_IMPL_IS_CHARTYPE(*s, ct_space)) ++s; }
	#define PUGI_IMPL_OPTSET(OPT)           ( optmsk & (OPT) )
	#define PUGI_IMPL_PUSHNODE(TYPE)        { cursor = append_new_node(cursor, *alloc, TYPE); if (!cursor) PUGI_IMPL_THROW_ERROR(status_out_of_memory, s); }
	#define PUGI_IMPL_POPNODE()             { cursor = cursor->parent; }
	#define PUGI_IMPL_SCANFOR(X)            { while (*s != 0 && !(X)) ++s; }
	#define PUGI_IMPL_SCANWHILE(X)          { while (X) ++s; }
	#define PUGI_IMPL_SCANWHILE_UNROLL(X)   { for (;;) { char_t ss = s[0]; if (PUGI_IMPL_UNLIKELY(!(X))) { break; } ss = s[1]; if (PUGI_IMPL_UNLIKELY(!(X))) { s += 1; break; } ss = s[2]; if (PUGI_IMPL_UNLIKELY(!(X))) { s += 2; break; } ss = s[3]; if (PUGI_IMPL_UNLIKELY(!(X))) { s += 3; break; } s += 4; } }
	#define PUGI_IMPL_ENDSEG()              { ch = *s; *s = 0; ++s; }
	#define PUGI_IMPL_THROW_ERROR(err, m)   return error_offset = m, error_status = err, static_cast<char_t*>(NULL)
	#define PUGI_IMPL_CHECK_ERROR(err, m)   { if (*s == 0) PUGI_IMPL_THROW_ERROR(err, m); }

	PUGI_IMPL_FN char_t* strconv_comment(char_t* s, char_t endch)
	{
		gap g;

		while (true)
		{
			PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_comment));

			if (*s == '\r') // Either a single 0x0d or 0x0d 0x0a pair
			{
				*s++ = '\n'; // replace first one with 0x0a

				if (*s == '\n') g.push(s, 1);
			}
			else if (s[0] == '-' && s[1] == '-' && PUGI_IMPL_ENDSWITH(s[2], '>')) // comment ends here
			{
				*g.flush(s) = 0;

				return s + (s[2] == '>' ? 3 : 2);
			}
			else if (*s == 0)
			{
				return NULL;
			}
			else ++s;
		}
	}

	PUGI_IMPL_FN char_t* strconv_cdata(char_t* s, char_t endch)
	{
		gap g;

		while (true)
		{
			PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_cdata));

			if (*s == '\r') // Either a single 0x0d or 0x0d 0x0a pair
			{
				*s++ = '\n'; // replace first one with 0x0a

				if (*s == '\n') g.push(s, 1);
			}
			else if (s[0] == ']' && s[1] == ']' && PUGI_IMPL_ENDSWITH(s[2], '>')) // CDATA ends here
			{
				*g.flush(s) = 0;

				return s + 1;
			}
			else if (*s == 0)
			{
				return NULL;
			}
			else ++s;
		}
	}

	typedef char_t* (*strconv_pcdata_t)(char_t*);

	template <typename opt_trim, typename opt_eol, typename opt_escape> struct strconv_pcdata_impl
	{
		static char_t* parse(char_t* s)
		{
			gap g;

			char_t* begin = s;

			while (true)
			{
				PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_pcdata));

				if (*s == '<') // PCDATA ends here
				{
					char_t* end = g.flush(s);

					if (opt_trim::value)
						while (end > begin && PUGI_IMPL_IS_CHARTYPE(end[-1], ct_space))
							--end;

					*end = 0;

					return s + 1;
				}
				else if (opt_eol::value && *s == '\r') // Either a single 0x0d or 0x0d 0x0a pair
				{
					*s++ = '\n'; // replace first one with 0x0a

					if (*s == '\n') g.push(s, 1);
				}
				else if (opt_escape::value && *s == '&')
				{
					s = strconv_escape(s, g);
				}
				else if (*s == 0)
				{
					char_t* end = g.flush(s);

					if (opt_trim::value)
						while (end > begin && PUGI_IMPL_IS_CHARTYPE(end[-1], ct_space))
							--end;

					*end = 0;

					return s;
				}
				else ++s;
			}
		}
	};

	PUGI_IMPL_FN strconv_pcdata_t get_strconv_pcdata(unsigned int optmask)
	{
		PUGI_IMPL_STATIC_ASSERT(parse_escapes == 0x10 && parse_eol == 0x20 && parse_trim_pcdata == 0x0800);

		switch (((optmask >> 4) & 3) | ((optmask >> 9) & 4)) // get bitmask for flags (trim eol escapes); this simultaneously checks 3 options from assertion above
		{
		case 0: return strconv_pcdata_impl<opt_false, opt_false, opt_false>::parse;
		case 1: return strconv_pcdata_impl<opt_false, opt_false, opt_true>::parse;
		case 2: return strconv_pcdata_impl<opt_false, opt_true, opt_false>::parse;
		case 3: return strconv_pcdata_impl<opt_false, opt_true, opt_true>::parse;
		case 4: return strconv_pcdata_impl<opt_true, opt_false, opt_false>::parse;
		case 5: return strconv_pcdata_impl<opt_true, opt_false, opt_true>::parse;
		case 6: return strconv_pcdata_impl<opt_true, opt_true, opt_false>::parse;
		case 7: return strconv_pcdata_impl<opt_true, opt_true, opt_true>::parse;
		default: assert(false); return NULL; // unreachable
		}
	}

	typedef char_t* (*strconv_attribute_t)(char_t*, char_t);

	template <typename opt_escape> struct strconv_attribute_impl
	{
		static char_t* parse_wnorm(char_t* s, char_t end_quote)
		{
			gap g;

			// trim leading whitespaces
			if (PUGI_IMPL_IS_CHARTYPE(*s, ct_space))
			{
				char_t* str = s;

				do ++str;
				while (PUGI_IMPL_IS_CHARTYPE(*str, ct_space));

				g.push(s, str - s);
			}

			while (true)
			{
				PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_attr_ws | ct_space));

				if (*s == end_quote)
				{
					char_t* str = g.flush(s);

					do *str-- = 0;
					while (PUGI_IMPL_IS_CHARTYPE(*str, ct_space));

					return s + 1;
				}
				else if (PUGI_IMPL_IS_CHARTYPE(*s, ct_space))
				{
					*s++ = ' ';

					if (PUGI_IMPL_IS_CHARTYPE(*s, ct_space))
					{
						char_t* str = s + 1;
						while (PUGI_IMPL_IS_CHARTYPE(*str, ct_space)) ++str;

						g.push(s, str - s);
					}
				}
				else if (opt_escape::value && *s == '&')
				{
					s = strconv_escape(s, g);
				}
				else if (!*s)
				{
					return NULL;
				}
				else ++s;
			}
		}

		static char_t* parse_wconv(char_t* s, char_t end_quote)
		{
			gap g;

			while (true)
			{
				PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_attr_ws));

				if (*s == end_quote)
				{
					*g.flush(s) = 0;

					return s + 1;
				}
				else if (PUGI_IMPL_IS_CHARTYPE(*s, ct_space))
				{
					if (*s == '\r')
					{
						*s++ = ' ';

						if (*s == '\n') g.push(s, 1);
					}
					else *s++ = ' ';
				}
				else if (opt_escape::value && *s == '&')
				{
					s = strconv_escape(s, g);
				}
				else if (!*s)
				{
					return NULL;
				}
				else ++s;
			}
		}

		static char_t* parse_eol(char_t* s, char_t end_quote)
		{
			gap g;

			while (true)
			{
				PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_attr));

				if (*s == end_quote)
				{
					*g.flush(s) = 0;

					return s + 1;
				}
				else if (*s == '\r')
				{
					*s++ = '\n';

					if (*s == '\n') g.push(s, 1);
				}
				else if (opt_escape::value && *s == '&')
				{
					s = strconv_escape(s, g);
				}
				else if (!*s)
				{
					return NULL;
				}
				else ++s;
			}
		}

		static char_t* parse_simple(char_t* s, char_t end_quote)
		{
			gap g;

			while (true)
			{
				PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPE(ss, ct_parse_attr));

				if (*s == end_quote)
				{
					*g.flush(s) = 0;

					return s + 1;
				}
				else if (opt_escape::value && *s == '&')
				{
					s = strconv_escape(s, g);
				}
				else if (!*s)
				{
					return NULL;
				}
				else ++s;
			}
		}
	};

	PUGI_IMPL_FN strconv_attribute_t get_strconv_attribute(unsigned int optmask)
	{
		PUGI_IMPL_STATIC_ASSERT(parse_escapes == 0x10 && parse_eol == 0x20 && parse_wconv_attribute == 0x40 && parse_wnorm_attribute == 0x80);

		switch ((optmask >> 4) & 15) // get bitmask for flags (wnorm wconv eol escapes); this simultaneously checks 4 options from assertion above
		{
		case 0:  return strconv_attribute_impl<opt_false>::parse_simple;
		case 1:  return strconv_attribute_impl<opt_true>::parse_simple;
		case 2:  return strconv_attribute_impl<opt_false>::parse_eol;
		case 3:  return strconv_attribute_impl<opt_true>::parse_eol;
		case 4:  return strconv_attribute_impl<opt_false>::parse_wconv;
		case 5:  return strconv_attribute_impl<opt_true>::parse_wconv;
		case 6:  return strconv_attribute_impl<opt_false>::parse_wconv;
		case 7:  return strconv_attribute_impl<opt_true>::parse_wconv;
		case 8:  return strconv_attribute_impl<opt_false>::parse_wnorm;
		case 9:  return strconv_attribute_impl<opt_true>::parse_wnorm;
		case 10: return strconv_attribute_impl<opt_false>::parse_wnorm;
		case 11: return strconv_attribute_impl<opt_true>::parse_wnorm;
		case 12: return strconv_attribute_impl<opt_false>::parse_wnorm;
		case 13: return strconv_attribute_impl<opt_true>::parse_wnorm;
		case 14: return strconv_attribute_impl<opt_false>::parse_wnorm;
		case 15: return strconv_attribute_impl<opt_true>::parse_wnorm;
		default: assert(false); return NULL; // unreachable
		}
	}

	inline xml_parse_result make_parse_result(xml_parse_status status, ptrdiff_t offset = 0)
	{
		xml_parse_result result;
		result.status = status;
		result.offset = offset;

		return result;
	}

	struct xml_parser
	{
		xml_allocator* alloc;
		char_t* error_offset;
		xml_parse_status error_status;

		xml_parser(xml_allocator* alloc_): alloc(alloc_), error_offset(NULL), error_status(status_ok)
		{
		}

		// DOCTYPE consists of nested sections of the following possible types:
		// <!-- ... -->, <? ... ?>, "...", '...'
		// <![...]]>
		// <!...>
		// First group can not contain nested groups
		// Second group can contain nested groups of the same type
		// Third group can contain all other groups
		char_t* parse_doctype_primitive(char_t* s)
		{
			if (*s == '"' || *s == '\'')
			{
				// quoted string
				char_t ch = *s++;
				PUGI_IMPL_SCANFOR(*s == ch);
				if (!*s) PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

				s++;
			}
			else if (s[0] == '<' && s[1] == '?')
			{
				// <? ... ?>
				s += 2;
				PUGI_IMPL_SCANFOR(s[0] == '?' && s[1] == '>'); // no need for ENDSWITH because ?> can't terminate proper doctype
				if (!*s) PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

				s += 2;
			}
			else if (s[0] == '<' && s[1] == '!' && s[2] == '-' && s[3] == '-')
			{
				s += 4;
				PUGI_IMPL_SCANFOR(s[0] == '-' && s[1] == '-' && s[2] == '>'); // no need for ENDSWITH because --> can't terminate proper doctype
				if (!*s) PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

				s += 3;
			}
			else PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

			return s;
		}

		char_t* parse_doctype_ignore(char_t* s)
		{
			size_t depth = 0;

			assert(s[0] == '<' && s[1] == '!' && s[2] == '[');
			s += 3;

			while (*s)
			{
				if (s[0] == '<' && s[1] == '!' && s[2] == '[')
				{
					// nested ignore section
					s += 3;
					depth++;
				}
				else if (s[0] == ']' && s[1] == ']' && s[2] == '>')
				{
					// ignore section end
					s += 3;

					if (depth == 0)
						return s;

					depth--;
				}
				else s++;
			}

			PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);
		}

		char_t* parse_doctype_group(char_t* s, char_t endch)
		{
			size_t depth = 0;

			assert((s[0] == '<' || s[0] == 0) && s[1] == '!');
			s += 2;

			while (*s)
			{
				if (s[0] == '<' && s[1] == '!' && s[2] != '-')
				{
					if (s[2] == '[')
					{
						// ignore
						s = parse_doctype_ignore(s);
						if (!s) return s;
					}
					else
					{
						// some control group
						s += 2;
						depth++;
					}
				}
				else if (s[0] == '<' || s[0] == '"' || s[0] == '\'')
				{
					// unknown tag (forbidden), or some primitive group
					s = parse_doctype_primitive(s);
					if (!s) return s;
				}
				else if (*s == '>')
				{
					if (depth == 0)
						return s;

					depth--;
					s++;
				}
				else s++;
			}

			if (depth != 0 || endch != '>') PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

			return s;
		}

		char_t* parse_exclamation(char_t* s, xml_node_struct* cursor, unsigned int optmsk, char_t endch)
		{
			// parse node contents, starting with exclamation mark
			++s;

			if (*s == '-') // '<!-...'
			{
				++s;

				if (*s == '-') // '<!--...'
				{
					++s;

					if (PUGI_IMPL_OPTSET(parse_comments))
					{
						PUGI_IMPL_PUSHNODE(node_comment); // Append a new node on the tree.
						cursor->value = s; // Save the offset.
					}

					if (PUGI_IMPL_OPTSET(parse_eol) && PUGI_IMPL_OPTSET(parse_comments))
					{
						s = strconv_comment(s, endch);

						if (!s) PUGI_IMPL_THROW_ERROR(status_bad_comment, cursor->value);
					}
					else
					{
						// Scan for terminating '-->'.
						PUGI_IMPL_SCANFOR(s[0] == '-' && s[1] == '-' && PUGI_IMPL_ENDSWITH(s[2], '>'));
						PUGI_IMPL_CHECK_ERROR(status_bad_comment, s);

						if (PUGI_IMPL_OPTSET(parse_comments))
							*s = 0; // Zero-terminate this segment at the first terminating '-'.

						s += (s[2] == '>' ? 3 : 2); // Step over the '\0->'.
					}
				}
				else PUGI_IMPL_THROW_ERROR(status_bad_comment, s);
			}
			else if (*s == '[')
			{
				// '<![CDATA[...'
				if (*++s=='C' && *++s=='D' && *++s=='A' && *++s=='T' && *++s=='A' && *++s == '[')
				{
					++s;

					if (PUGI_IMPL_OPTSET(parse_cdata))
					{
						PUGI_IMPL_PUSHNODE(node_cdata); // Append a new node on the tree.
						cursor->value = s; // Save the offset.

						if (PUGI_IMPL_OPTSET(parse_eol))
						{
							s = strconv_cdata(s, endch);

							if (!s) PUGI_IMPL_THROW_ERROR(status_bad_cdata, cursor->value);
						}
						else
						{
							// Scan for terminating ']]>'.
							PUGI_IMPL_SCANFOR(s[0] == ']' && s[1] == ']' && PUGI_IMPL_ENDSWITH(s[2], '>'));
							PUGI_IMPL_CHECK_ERROR(status_bad_cdata, s);

							*s++ = 0; // Zero-terminate this segment.
						}
					}
					else // Flagged for discard, but we still have to scan for the terminator.
					{
						// Scan for terminating ']]>'.
						PUGI_IMPL_SCANFOR(s[0] == ']' && s[1] == ']' && PUGI_IMPL_ENDSWITH(s[2], '>'));
						PUGI_IMPL_CHECK_ERROR(status_bad_cdata, s);

						++s;
					}

					s += (s[1] == '>' ? 2 : 1); // Step over the last ']>'.
				}
				else PUGI_IMPL_THROW_ERROR(status_bad_cdata, s);
			}
			else if (s[0] == 'D' && s[1] == 'O' && s[2] == 'C' && s[3] == 'T' && s[4] == 'Y' && s[5] == 'P' && PUGI_IMPL_ENDSWITH(s[6], 'E'))
			{
				s -= 2;

				if (cursor->parent) PUGI_IMPL_THROW_ERROR(status_bad_doctype, s);

				char_t* mark = s + 9;

				s = parse_doctype_group(s, endch);
				if (!s) return s;

				assert((*s == 0 && endch == '>') || *s == '>');
				if (*s) *s++ = 0;

				if (PUGI_IMPL_OPTSET(parse_doctype))
				{
					while (PUGI_IMPL_IS_CHARTYPE(*mark, ct_space)) ++mark;

					PUGI_IMPL_PUSHNODE(node_doctype);

					cursor->value = mark;
				}
			}
			else if (*s == 0 && endch == '-') PUGI_IMPL_THROW_ERROR(status_bad_comment, s);
			else if (*s == 0 && endch == '[') PUGI_IMPL_THROW_ERROR(status_bad_cdata, s);
			else PUGI_IMPL_THROW_ERROR(status_unrecognized_tag, s);

			return s;
		}

		char_t* parse_question(char_t* s, xml_node_struct*& ref_cursor, unsigned int optmsk, char_t endch)
		{
			// load into registers
			xml_node_struct* cursor = ref_cursor;
			char_t ch = 0;

			// parse node contents, starting with question mark
			++s;

			// read PI target
			char_t* target = s;

			if (!PUGI_IMPL_IS_CHARTYPE(*s, ct_start_symbol)) PUGI_IMPL_THROW_ERROR(status_bad_pi, s);

			PUGI_IMPL_SCANWHILE(PUGI_IMPL_IS_CHARTYPE(*s, ct_symbol));
			PUGI_IMPL_CHECK_ERROR(status_bad_pi, s);

			// determine node type; stricmp / strcasecmp is not portable
			bool declaration = (target[0] | ' ') == 'x' && (target[1] | ' ') == 'm' && (target[2] | ' ') == 'l' && target + 3 == s;

			if (declaration ? PUGI_IMPL_OPTSET(parse_declaration) : PUGI_IMPL_OPTSET(parse_pi))
			{
				if (declaration)
				{
					// disallow non top-level declarations
					if (cursor->parent) PUGI_IMPL_THROW_ERROR(status_bad_pi, s);

					PUGI_IMPL_PUSHNODE(node_declaration);
				}
				else
				{
					PUGI_IMPL_PUSHNODE(node_pi);
				}

				cursor->name = target;

				PUGI_IMPL_ENDSEG();

				// parse value/attributes
				if (ch == '?')
				{
					// empty node
					if (!PUGI_IMPL_ENDSWITH(*s, '>')) PUGI_IMPL_THROW_ERROR(status_bad_pi, s);
					s += (*s == '>');

					PUGI_IMPL_POPNODE();
				}
				else if (PUGI_IMPL_IS_CHARTYPE(ch, ct_space))
				{
					PUGI_IMPL_SKIPWS();

					// scan for tag end
					char_t* value = s;

					PUGI_IMPL_SCANFOR(s[0] == '?' && PUGI_IMPL_ENDSWITH(s[1], '>'));
					PUGI_IMPL_CHECK_ERROR(status_bad_pi, s);

					if (declaration)
					{
						// replace ending ? with / so that 'element' terminates properly
						*s = '/';

						// we exit from this function with cursor at node_declaration, which is a signal to parse() to go to LOC_ATTRIBUTES
						s = value;
					}
					else
					{
						// store value and step over >
						cursor->value = value;

						PUGI_IMPL_POPNODE();

						PUGI_IMPL_ENDSEG();

						s += (*s == '>');
					}
				}
				else PUGI_IMPL_THROW_ERROR(status_bad_pi, s);
			}
			else
			{
				// scan for tag end
				PUGI_IMPL_SCANFOR(s[0] == '?' && PUGI_IMPL_ENDSWITH(s[1], '>'));
				PUGI_IMPL_CHECK_ERROR(status_bad_pi, s);

				s += (s[1] == '>' ? 2 : 1);
			}

			// store from registers
			ref_cursor = cursor;

			return s;
		}

		char_t* parse_tree(char_t* s, xml_node_struct* root, unsigned int optmsk, char_t endch)
		{
			strconv_attribute_t strconv_attribute = get_strconv_attribute(optmsk);
			strconv_pcdata_t strconv_pcdata = get_strconv_pcdata(optmsk);

			char_t ch = 0;
			xml_node_struct* cursor = root;
			char_t* mark = s;
			char_t* merged_pcdata = s;

			while (*s != 0)
			{
				if (*s == '<')
				{
					++s;

				LOC_TAG:
					if (PUGI_IMPL_IS_CHARTYPE(*s, ct_start_symbol)) // '<#...'
					{
						PUGI_IMPL_PUSHNODE(node_element); // Append a new node to the tree.

						cursor->name = s;

						PUGI_IMPL_SCANWHILE_UNROLL(PUGI_IMPL_IS_CHARTYPE(ss, ct_symbol)); // Scan for a terminator.
						PUGI_IMPL_ENDSEG(); // Save char in 'ch', terminate & step over.

						if (ch == '>')
						{
							// end of tag
						}
						else if (PUGI_IMPL_IS_CHARTYPE(ch, ct_space))
						{
						LOC_ATTRIBUTES:
							while (true)
							{
								PUGI_IMPL_SKIPWS(); // Eat any whitespace.

								if (PUGI_IMPL_IS_CHARTYPE(*s, ct_start_symbol)) // <... #...
								{
									xml_attribute_struct* a = append_new_attribute(cursor, *alloc); // Make space for this attribute.
									if (!a) PUGI_IMPL_THROW_ERROR(status_out_of_memory, s);

									a->name = s; // Save the offset.

									PUGI_IMPL_SCANWHILE_UNROLL(PUGI_IMPL_IS_CHARTYPE(ss, ct_symbol)); // Scan for a terminator.
									PUGI_IMPL_ENDSEG(); // Save char in 'ch', terminate & step over.

									if (PUGI_IMPL_IS_CHARTYPE(ch, ct_space))
									{
										PUGI_IMPL_SKIPWS(); // Eat any whitespace.

										ch = *s;
										++s;
									}

									if (ch == '=') // '<... #=...'
									{
										PUGI_IMPL_SKIPWS(); // Eat any whitespace.

										if (*s == '"' || *s == '\'') // '<... #="...'
										{
											ch = *s; // Save quote char to avoid breaking on "''" -or- '""'.
											++s; // Step over the quote.
											a->value = s; // Save the offset.

											s = strconv_attribute(s, ch);

											if (!s) PUGI_IMPL_THROW_ERROR(status_bad_attribute, a->value);

											// After this line the loop continues from the start;
											// Whitespaces, / and > are ok, symbols and EOF are wrong,
											// everything else will be detected
											if (PUGI_IMPL_IS_CHARTYPE(*s, ct_start_symbol)) PUGI_IMPL_THROW_ERROR(status_bad_attribute, s);
										}
										else PUGI_IMPL_THROW_ERROR(status_bad_attribute, s);
									}
									else PUGI_IMPL_THROW_ERROR(status_bad_attribute, s);
								}
								else if (*s == '/')
								{
									++s;

									if (*s == '>')
									{
										PUGI_IMPL_POPNODE();
										s++;
										break;
									}
									else if (*s == 0 && endch == '>')
									{
										PUGI_IMPL_POPNODE();
										break;
									}
									else PUGI_IMPL_THROW_ERROR(status_bad_start_element, s);
								}
								else if (*s == '>')
								{
									++s;

									break;
								}
								else if (*s == 0 && endch == '>')
								{
									break;
								}
								else PUGI_IMPL_THROW_ERROR(status_bad_start_element, s);
							}

							// !!!
						}
						else if (ch == '/') // '<#.../'
						{
							if (!PUGI_IMPL_ENDSWITH(*s, '>')) PUGI_IMPL_THROW_ERROR(status_bad_start_element, s);

							PUGI_IMPL_POPNODE(); // Pop.

							s += (*s == '>');
						}
						else if (ch == 0)
						{
							// we stepped over null terminator, backtrack & handle closing tag
							--s;

							if (endch != '>') PUGI_IMPL_THROW_ERROR(status_bad_start_element, s);
						}
						else PUGI_IMPL_THROW_ERROR(status_bad_start_element, s);
					}
					else if (*s == '/')
					{
						++s;

						mark = s;

						char_t* name = cursor->name;
						if (!name) PUGI_IMPL_THROW_ERROR(status_end_element_mismatch, mark);

						while (PUGI_IMPL_IS_CHARTYPE(*s, ct_symbol))
						{
							if (*s++ != *name++) PUGI_IMPL_THROW_ERROR(status_end_element_mismatch, mark);
						}

						if (*name)
						{
							if (*s == 0 && name[0] == endch && name[1] == 0) PUGI_IMPL_THROW_ERROR(status_bad_end_element, s);
							else PUGI_IMPL_THROW_ERROR(status_end_element_mismatch, mark);
						}

						PUGI_IMPL_POPNODE(); // Pop.

						PUGI_IMPL_SKIPWS();

						if (*s == 0)
						{
							if (endch != '>') PUGI_IMPL_THROW_ERROR(status_bad_end_element, s);
						}
						else
						{
							if (*s != '>') PUGI_IMPL_THROW_ERROR(status_bad_end_element, s);
							++s;
						}
					}
					else if (*s == '?') // '<?...'
					{
						s = parse_question(s, cursor, optmsk, endch);
						if (!s) return s;

						assert(cursor);
						if (PUGI_IMPL_NODETYPE(cursor) == node_declaration) goto LOC_ATTRIBUTES;
					}
					else if (*s == '!') // '<!...'
					{
						s = parse_exclamation(s, cursor, optmsk, endch);
						if (!s) return s;
					}
					else if (*s == 0 && endch == '?') PUGI_IMPL_THROW_ERROR(status_bad_pi, s);
					else PUGI_IMPL_THROW_ERROR(status_unrecognized_tag, s);
				}
				else
				{
					mark = s; // Save this offset while searching for a terminator.

					PUGI_IMPL_SKIPWS(); // Eat whitespace if no genuine PCDATA here.

					if (*s == '<' || !*s)
					{
						// We skipped some whitespace characters because otherwise we would take the tag branch instead of PCDATA one
						assert(mark != s);

						if (!PUGI_IMPL_OPTSET(parse_ws_pcdata | parse_ws_pcdata_single) || PUGI_IMPL_OPTSET(parse_trim_pcdata))
						{
							continue;
						}
						else if (PUGI_IMPL_OPTSET(parse_ws_pcdata_single))
						{
							if (s[0] != '<' || s[1] != '/' || cursor->first_child) continue;
						}
					}

					if (!PUGI_IMPL_OPTSET(parse_trim_pcdata))
						s = mark;

					if (cursor->parent || PUGI_IMPL_OPTSET(parse_fragment))
					{
						char_t* parsed_pcdata = s;

						s = strconv_pcdata(s);

						if (PUGI_IMPL_OPTSET(parse_embed_pcdata) && cursor->parent && !cursor->first_child && !cursor->value)
						{
							cursor->value = parsed_pcdata; // Save the offset.
						}
						else if (PUGI_IMPL_OPTSET(parse_merge_pcdata) && cursor->first_child && PUGI_IMPL_NODETYPE(cursor->first_child->prev_sibling_c) == node_pcdata)
						{
							assert(merged_pcdata >= cursor->first_child->prev_sibling_c->value);

							// Catch up to the end of last parsed value; only needed for the first fragment.
							merged_pcdata += strlength(merged_pcdata);

							size_t length = strlength(parsed_pcdata);

							// Must use memmove instead of memcpy as this move may overlap
							memmove(merged_pcdata, parsed_pcdata, (length + 1) * sizeof(char_t));
							merged_pcdata += length;
						}
						else
						{
							xml_node_struct* prev_cursor = cursor;
							PUGI_IMPL_PUSHNODE(node_pcdata); // Append a new node on the tree.

							cursor->value = parsed_pcdata; // Save the offset.
							merged_pcdata = parsed_pcdata; // Used for parse_merge_pcdata above, cheaper to save unconditionally

							cursor = prev_cursor; // Pop since this is a standalone.
						}

						if (!*s) break;
					}
					else
					{
						PUGI_IMPL_SCANFOR(*s == '<'); // '...<'
						if (!*s) break;

						++s;
					}

					// We're after '<'
					goto LOC_TAG;
				}
			}

			// check that last tag is closed
			if (cursor != root) PUGI_IMPL_THROW_ERROR(status_end_element_mismatch, s);

			return s;
		}

	#ifdef PUGIXML_WCHAR_MODE
		static char_t* parse_skip_bom(char_t* s)
		{
			unsigned int bom = 0xfeff;
			return (s[0] == static_cast<wchar_t>(bom)) ? s + 1 : s;
		}
	#else
		static char_t* parse_skip_bom(char_t* s)
		{
			return (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf') ? s + 3 : s;
		}
	#endif

		static bool has_element_node_siblings(xml_node_struct* node)
		{
			while (node)
			{
				if (PUGI_IMPL_NODETYPE(node) == node_element) return true;

				node = node->next_sibling;
			}

			return false;
		}

		static xml_parse_result parse(char_t* buffer, size_t length, xml_document_struct* xmldoc, xml_node_struct* root, unsigned int optmsk)
		{
			// early-out for empty documents
			if (length == 0)
				return make_parse_result(PUGI_IMPL_OPTSET(parse_fragment) ? status_ok : status_no_document_element);

			// get last child of the root before parsing
			xml_node_struct* last_root_child = root->first_child ? root->first_child->prev_sibling_c + 0 : NULL;

			// create parser on stack
			xml_parser parser(static_cast<xml_allocator*>(xmldoc));

			// save last character and make buffer zero-terminated (speeds up parsing)
			char_t endch = buffer[length - 1];
			buffer[length - 1] = 0;

			// skip BOM to make sure it does not end up as part of parse output
			char_t* buffer_data = parse_skip_bom(buffer);

			// perform actual parsing
			parser.parse_tree(buffer_data, root, optmsk, endch);

			xml_parse_result result = make_parse_result(parser.error_status, parser.error_offset ? parser.error_offset - buffer : 0);
			assert(result.offset >= 0 && static_cast<size_t>(result.offset) <= length);

			if (result)
			{
				// since we removed last character, we have to handle the only possible false positive (stray <)
				if (endch == '<')
					return make_parse_result(status_unrecognized_tag, length - 1);

				// check if there are any element nodes parsed
				xml_node_struct* first_root_child_parsed = last_root_child ? last_root_child->next_sibling + 0 : root->first_child + 0;

				if (!PUGI_IMPL_OPTSET(parse_fragment) && !has_element_node_siblings(first_root_child_parsed))
					return make_parse_result(status_no_document_element, length - 1);
			}
			else
			{
				// roll back offset if it occurs on a null terminator in the source buffer
				if (result.offset > 0 && static_cast<size_t>(result.offset) == length - 1 && endch == 0)
					result.offset--;
			}

			return result;
		}
	};

	// Output facilities
	PUGI_IMPL_FN xml_encoding get_write_native_encoding()
	{
	#ifdef PUGIXML_WCHAR_MODE
		return get_wchar_encoding();
	#else
		return encoding_utf8;
	#endif
	}

	PUGI_IMPL_FN xml_encoding get_write_encoding(xml_encoding encoding)
	{
		// replace wchar encoding with utf implementation
		if (encoding == encoding_wchar) return get_wchar_encoding();

		// replace utf16 encoding with utf16 with specific endianness
		if (encoding == encoding_utf16) return is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

		// replace utf32 encoding with utf32 with specific endianness
		if (encoding == encoding_utf32) return is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

		// only do autodetection if no explicit encoding is requested
		if (encoding != encoding_auto) return encoding;

		// assume utf8 encoding
		return encoding_utf8;
	}

	template <typename D, typename T> PUGI_IMPL_FN size_t convert_buffer_output_generic(typename T::value_type dest, const char_t* data, size_t length, D, T)
	{
		PUGI_IMPL_STATIC_ASSERT(sizeof(char_t) == sizeof(typename D::type));

		typename T::value_type end = D::process(reinterpret_cast<const typename D::type*>(data), length, dest, T());

		return static_cast<size_t>(end - dest) * sizeof(*dest);
	}

	template <typename D, typename T> PUGI_IMPL_FN size_t convert_buffer_output_generic(typename T::value_type dest, const char_t* data, size_t length, D, T, bool opt_swap)
	{
		PUGI_IMPL_STATIC_ASSERT(sizeof(char_t) == sizeof(typename D::type));

		typename T::value_type end = D::process(reinterpret_cast<const typename D::type*>(data), length, dest, T());

		if (opt_swap)
		{
			for (typename T::value_type i = dest; i != end; ++i)
				*i = endian_swap(*i);
		}

		return static_cast<size_t>(end - dest) * sizeof(*dest);
	}

#ifdef PUGIXML_WCHAR_MODE
	PUGI_IMPL_FN size_t get_valid_length(const char_t* data, size_t length)
	{
		if (length < 1) return 0;

		// discard last character if it's the lead of a surrogate pair
		return (sizeof(wchar_t) == 2 && static_cast<unsigned int>(static_cast<uint16_t>(data[length - 1]) - 0xD800) < 0x400) ? length - 1 : length;
	}

	PUGI_IMPL_FN size_t convert_buffer_output(char_t* r_char, uint8_t* r_u8, uint16_t* r_u16, uint32_t* r_u32, const char_t* data, size_t length, xml_encoding encoding)
	{
		// only endian-swapping is required
		if (need_endian_swap_utf(encoding, get_wchar_encoding()))
		{
			convert_wchar_endian_swap(r_char, data, length);

			return length * sizeof(char_t);
		}

		// convert to utf8
		if (encoding == encoding_utf8)
			return convert_buffer_output_generic(r_u8, data, length, wchar_decoder(), utf8_writer());

		// convert to utf16
		if (encoding == encoding_utf16_be || encoding == encoding_utf16_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

			return convert_buffer_output_generic(r_u16, data, length, wchar_decoder(), utf16_writer(), native_encoding != encoding);
		}

		// convert to utf32
		if (encoding == encoding_utf32_be || encoding == encoding_utf32_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

			return convert_buffer_output_generic(r_u32, data, length, wchar_decoder(), utf32_writer(), native_encoding != encoding);
		}

		// convert to latin1
		if (encoding == encoding_latin1)
			return convert_buffer_output_generic(r_u8, data, length, wchar_decoder(), latin1_writer());

		assert(false && "Invalid encoding"); // unreachable
		return 0;
	}
#else
	PUGI_IMPL_FN size_t get_valid_length(const char_t* data, size_t length)
	{
		if (length < 5) return 0;

		for (size_t i = 1; i <= 4; ++i)
		{
			uint8_t ch = static_cast<uint8_t>(data[length - i]);

			// either a standalone character or a leading one
			if ((ch & 0xc0) != 0x80) return length - i;
		}

		// there are four non-leading characters at the end, sequence tail is broken so might as well process the whole chunk
		return length;
	}

	PUGI_IMPL_FN size_t convert_buffer_output(char_t* /* r_char */, uint8_t* r_u8, uint16_t* r_u16, uint32_t* r_u32, const char_t* data, size_t length, xml_encoding encoding)
	{
		if (encoding == encoding_utf16_be || encoding == encoding_utf16_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

			return convert_buffer_output_generic(r_u16, data, length, utf8_decoder(), utf16_writer(), native_encoding != encoding);
		}

		if (encoding == encoding_utf32_be || encoding == encoding_utf32_le)
		{
			xml_encoding native_encoding = is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

			return convert_buffer_output_generic(r_u32, data, length, utf8_decoder(), utf32_writer(), native_encoding != encoding);
		}

		if (encoding == encoding_latin1)
			return convert_buffer_output_generic(r_u8, data, length, utf8_decoder(), latin1_writer());

		assert(false && "Invalid encoding"); // unreachable
		return 0;
	}
#endif

	class xml_buffered_writer
	{
		xml_buffered_writer(const xml_buffered_writer&);
		xml_buffered_writer& operator=(const xml_buffered_writer&);

	public:
		xml_buffered_writer(xml_writer& writer_, xml_encoding user_encoding): writer(writer_), bufsize(0), encoding(get_write_encoding(user_encoding))
		{
			PUGI_IMPL_STATIC_ASSERT(bufcapacity >= 8);
		}

		size_t flush()
		{
			flush(buffer, bufsize);
			bufsize = 0;
			return 0;
		}

		void flush(const char_t* data, size_t size)
		{
			if (size == 0) return;

			// fast path, just write data
			if (encoding == get_write_native_encoding())
				writer.write(data, size * sizeof(char_t));
			else
			{
				// convert chunk
				size_t result = convert_buffer_output(scratch.data_char, scratch.data_u8, scratch.data_u16, scratch.data_u32, data, size, encoding);
				assert(result <= sizeof(scratch));

				// write data
				writer.write(scratch.data_u8, result);
			}
		}

		void write_direct(const char_t* data, size_t length)
		{
			// flush the remaining buffer contents
			flush();

			// handle large chunks
			if (length > bufcapacity)
			{
				if (encoding == get_write_native_encoding())
				{
					// fast path, can just write data chunk
					writer.write(data, length * sizeof(char_t));
					return;
				}

				// need to convert in suitable chunks
				while (length > bufcapacity)
				{
					// get chunk size by selecting such number of characters that are guaranteed to fit into scratch buffer
					// and form a complete codepoint sequence (i.e. discard start of last codepoint if necessary)
					size_t chunk_size = get_valid_length(data, bufcapacity);
					assert(chunk_size);

					// convert chunk and write
					flush(data, chunk_size);

					// iterate
					data += chunk_size;
					length -= chunk_size;
				}

				// small tail is copied below
				bufsize = 0;
			}

			memcpy(buffer + bufsize, data, length * sizeof(char_t));
			bufsize += length;
		}

		void write_buffer(const char_t* data, size_t length)
		{
			size_t offset = bufsize;

			if (offset + length <= bufcapacity)
			{
				memcpy(buffer + offset, data, length * sizeof(char_t));
				bufsize = offset + length;
			}
			else
			{
				write_direct(data, length);
			}
		}

		void write_string(const char_t* data)
		{
			// write the part of the string that fits in the buffer
			size_t offset = bufsize;

			while (*data && offset < bufcapacity)
				buffer[offset++] = *data++;

			// write the rest
			if (offset < bufcapacity)
			{
				bufsize = offset;
			}
			else
			{
				// backtrack a bit if we have split the codepoint
				size_t length = offset - bufsize;
				size_t extra = length - get_valid_length(data - length, length);

				bufsize = offset - extra;

				write_direct(data - extra, strlength(data) + extra);
			}
		}

		void write(char_t d0)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 1) offset = flush();

			buffer[offset + 0] = d0;
			bufsize = offset + 1;
		}

		void write(char_t d0, char_t d1)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 2) offset = flush();

			buffer[offset + 0] = d0;
			buffer[offset + 1] = d1;
			bufsize = offset + 2;
		}

		void write(char_t d0, char_t d1, char_t d2)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 3) offset = flush();

			buffer[offset + 0] = d0;
			buffer[offset + 1] = d1;
			buffer[offset + 2] = d2;
			bufsize = offset + 3;
		}

		void write(char_t d0, char_t d1, char_t d2, char_t d3)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 4) offset = flush();

			buffer[offset + 0] = d0;
			buffer[offset + 1] = d1;
			buffer[offset + 2] = d2;
			buffer[offset + 3] = d3;
			bufsize = offset + 4;
		}

		void write(char_t d0, char_t d1, char_t d2, char_t d3, char_t d4)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 5) offset = flush();

			buffer[offset + 0] = d0;
			buffer[offset + 1] = d1;
			buffer[offset + 2] = d2;
			buffer[offset + 3] = d3;
			buffer[offset + 4] = d4;
			bufsize = offset + 5;
		}

		void write(char_t d0, char_t d1, char_t d2, char_t d3, char_t d4, char_t d5)
		{
			size_t offset = bufsize;
			if (offset > bufcapacity - 6) offset = flush();

			buffer[offset + 0] = d0;
			buffer[offset + 1] = d1;
			buffer[offset + 2] = d2;
			buffer[offset + 3] = d3;
			buffer[offset + 4] = d4;
			buffer[offset + 5] = d5;
			bufsize = offset + 6;
		}

		// utf8 maximum expansion: x4 (-> utf32)
		// utf16 maximum expansion: x2 (-> utf32)
		// utf32 maximum expansion: x1
		enum
		{
			bufcapacitybytes =
			#ifdef PUGIXML_MEMORY_OUTPUT_STACK
				PUGIXML_MEMORY_OUTPUT_STACK
			#else
				10240
			#endif
			,
			bufcapacity = bufcapacitybytes / (sizeof(char_t) + 4)
		};

		char_t buffer[bufcapacity];

		union
		{
			uint8_t data_u8[4 * bufcapacity];
			uint16_t data_u16[2 * bufcapacity];
			uint32_t data_u32[bufcapacity];
			char_t data_char[bufcapacity];
		} scratch;

		xml_writer& writer;
		size_t bufsize;
		xml_encoding encoding;
	};

	PUGI_IMPL_FN void text_output_escaped(xml_buffered_writer& writer, const char_t* s, chartypex_t type, unsigned int flags)
	{
		while (*s)
		{
			const char_t* prev = s;

			// While *s is a usual symbol
			PUGI_IMPL_SCANWHILE_UNROLL(!PUGI_IMPL_IS_CHARTYPEX(ss, type));

			writer.write_buffer(prev, static_cast<size_t>(s - prev));

			switch (*s)
			{
				case 0: break;
				case '&':
					writer.write('&', 'a', 'm', 'p', ';');
					++s;
					break;
				case '<':
					writer.write('&', 'l', 't', ';');
					++s;
					break;
				case '>':
					writer.write('&', 'g', 't', ';');
					++s;
					break;
				case '"':
					if (flags & format_attribute_single_quote)
						writer.write('"');
					else
						writer.write('&', 'q', 'u', 'o', 't', ';');
					++s;
					break;
				case '\'':
					if (flags & format_attribute_single_quote)
						writer.write('&', 'a', 'p', 'o', 's', ';');
					else
						writer.write('\'');
					++s;
					break;
				default: // s is not a usual symbol
				{
					unsigned int ch = static_cast<unsigned int>(*s++);
					assert(ch < 32);

					if (!(flags & format_skip_control_chars))
						writer.write('&', '#', static_cast<char_t>((ch / 10) + '0'), static_cast<char_t>((ch % 10) + '0'), ';');
				}
			}
		}
	}

	PUGI_IMPL_FN void text_output(xml_buffered_writer& writer, const char_t* s, chartypex_t type, unsigned int flags)
	{
		if (flags & format_no_escapes)
			writer.write_string(s);
		else
			text_output_escaped(writer, s, type, flags);
	}

	PUGI_IMPL_FN void text_output_cdata(xml_buffered_writer& writer, const char_t* s)
	{
		do
		{
			writer.write('<', '!', '[', 'C', 'D');
			writer.write('A', 'T', 'A', '[');

			const char_t* prev = s;

			// look for ]]> sequence - we can't output it as is since it terminates CDATA
			while (*s && !(s[0] == ']' && s[1] == ']' && s[2] == '>')) ++s;

			// skip ]] if we stopped at ]]>, > will go to the next CDATA section
			if (*s) s += 2;

			writer.write_buffer(prev, static_cast<size_t>(s - prev));

			writer.write(']', ']', '>');
		}
		while (*s);
	}

	PUGI_IMPL_FN void text_output_indent(xml_buffered_writer& writer, const char_t* indent, size_t indent_length, unsigned int depth)
	{
		switch (indent_length)
		{
		case 1:
		{
			for (unsigned int i = 0; i < depth; ++i)
				writer.write(indent[0]);
			break;
		}

		case 2:
		{
			for (unsigned int i = 0; i < depth; ++i)
				writer.write(indent[0], indent[1]);
			break;
		}

		case 3:
		{
			for (unsigned int i = 0; i < depth; ++i)
				writer.write(indent[0], indent[1], indent[2]);
			break;
		}

		case 4:
		{
			for (unsigned int i = 0; i < depth; ++i)
				writer.write(indent[0], indent[1], indent[2], indent[3]);
			break;
		}

		default:
		{
			for (unsigned int i = 0; i < depth; ++i)
				writer.write_buffer(indent, indent_length);
		}
		}
	}

	PUGI_IMPL_FN void node_output_comment(xml_buffered_writer& writer, const char_t* s)
	{
		writer.write('<', '!', '-', '-');

		while (*s)
		{
			const char_t* prev = s;

			// look for -\0 or -- sequence - we can't output it since -- is illegal in comment body
			while (*s && !(s[0] == '-' && (s[1] == '-' || s[1] == 0))) ++s;

			writer.write_buffer(prev, static_cast<size_t>(s - prev));

			if (*s)
			{
				assert(*s == '-');

				writer.write('-', ' ');
				++s;
			}
		}

		writer.write('-', '-', '>');
	}

	PUGI_IMPL_FN void node_output_pi_value(xml_buffered_writer& writer, const char_t* s)
	{
		while (*s)
		{
			const char_t* prev = s;

			// look for ?> sequence - we can't output it since ?> terminates PI
			while (*s && !(s[0] == '?' && s[1] == '>')) ++s;

			writer.write_buffer(prev, static_cast<size_t>(s - prev));

			if (*s)
			{
				assert(s[0] == '?' && s[1] == '>');

				writer.write('?', ' ', '>');
				s += 2;
			}
		}
	}

	PUGI_IMPL_FN void node_output_attributes(xml_buffered_writer& writer, xml_node_struct* node, const char_t* indent, size_t indent_length, unsigned int flags, unsigned int depth)
	{
		const char_t* default_name = PUGIXML_TEXT(":anonymous");
		const char_t enquotation_char = (flags & format_attribute_single_quote) ? '\'' : '"';

		for (xml_attribute_struct* a = node->first_attribute; a; a = a->next_attribute)
		{
			if ((flags & (format_indent_attributes | format_raw)) == format_indent_attributes)
			{
				writer.write('\n');

				text_output_indent(writer, indent, indent_length, depth + 1);
			}
			else
			{
				writer.write(' ');
			}

			writer.write_string(a->name ? a->name + 0 : default_name);
			writer.write('=', enquotation_char);

			if (a->value)
				text_output(writer, a->value, ctx_special_attr, flags);

			writer.write(enquotation_char);
		}
	}

	PUGI_IMPL_FN bool node_output_start(xml_buffered_writer& writer, xml_node_struct* node, const char_t* indent, size_t indent_length, unsigned int flags, unsigned int depth)
	{
		const char_t* default_name = PUGIXML_TEXT(":anonymous");
		const char_t* name = node->name ? node->name + 0 : default_name;

		writer.write('<');
		writer.write_string(name);

		if (node->first_attribute)
			node_output_attributes(writer, node, indent, indent_length, flags, depth);

		// element nodes can have value if parse_embed_pcdata was used
		if (!node->value)
		{
			if (!node->first_child)
			{
				if (flags & format_no_empty_element_tags)
				{
					writer.write('>', '<', '/');
					writer.write_string(name);
					writer.write('>');

					return false;
				}
				else
				{
					if ((flags & format_raw) == 0)
						writer.write(' ');

					writer.write('/', '>');

					return false;
				}
			}
			else
			{
				writer.write('>');

				return true;
			}
		}
		else
		{
			writer.write('>');

			text_output(writer, node->value, ctx_special_pcdata, flags);

			if (!node->first_child)
			{
				writer.write('<', '/');
				writer.write_string(name);
				writer.write('>');

				return false;
			}
			else
			{
				return true;
			}
		}
	}

	PUGI_IMPL_FN void node_output_end(xml_buffered_writer& writer, xml_node_struct* node)
	{
		const char_t* default_name = PUGIXML_TEXT(":anonymous");
		const char_t* name = node->name ? node->name + 0 : default_name;

		writer.write('<', '/');
		writer.write_string(name);
		writer.write('>');
	}

	PUGI_IMPL_FN void node_output_simple(xml_buffered_writer& writer, xml_node_struct* node, unsigned int flags)
	{
		const char_t* default_name = PUGIXML_TEXT(":anonymous");

		switch (PUGI_IMPL_NODETYPE(node))
		{
			case node_pcdata:
				text_output(writer, node->value ? node->value + 0 : PUGIXML_TEXT(""), ctx_special_pcdata, flags);
				break;

			case node_cdata:
				text_output_cdata(writer, node->value ? node->value + 0 : PUGIXML_TEXT(""));
				break;

			case node_comment:
				node_output_comment(writer, node->value ? node->value + 0 : PUGIXML_TEXT(""));
				break;

			case node_pi:
				writer.write('<', '?');
				writer.write_string(node->name ? node->name + 0 : default_name);

				if (node->value)
				{
					writer.write(' ');
					node_output_pi_value(writer, node->value);
				}

				writer.write('?', '>');
				break;

			case node_declaration:
				writer.write('<', '?');
				writer.write_string(node->name ? node->name + 0 : default_name);
				node_output_attributes(writer, node, PUGIXML_TEXT(""), 0, flags | format_raw, 0);
				writer.write('?', '>');
				break;

			case node_doctype:
				writer.write('<', '!', 'D', 'O', 'C');
				writer.write('T', 'Y', 'P', 'E');

				if (node->value)
				{
					writer.write(' ');
					writer.write_string(node->value);
				}

				writer.write('>');
				break;

			default:
				assert(false && "Invalid node type"); // unreachable
		}
	}

	enum indent_flags_t
	{
		indent_newline = 1,
		indent_indent = 2
	};

	PUGI_IMPL_FN void node_output(xml_buffered_writer& writer, xml_node_struct* root, const char_t* indent, unsigned int flags, unsigned int depth)
	{
		size_t indent_length = ((flags & (format_indent | format_indent_attributes)) && (flags & format_raw) == 0) ? strlength(indent) : 0;
		unsigned int indent_flags = indent_indent;

		xml_node_struct* node = root;

		do
		{
			assert(node);

			// begin writing current node
			if (PUGI_IMPL_NODETYPE(node) == node_pcdata || PUGI_IMPL_NODETYPE(node) == node_cdata)
			{
				node_output_simple(writer, node, flags);

				indent_flags = 0;
			}
			else
			{
				if ((indent_flags & indent_newline) && (flags & format_raw) == 0)
					writer.write('\n');

				if ((indent_flags & indent_indent) && indent_length)
					text_output_indent(writer, indent, indent_length, depth);

				if (PUGI_IMPL_NODETYPE(node) == node_element)
				{
					indent_flags = indent_newline | indent_indent;

					if (node_output_start(writer, node, indent, indent_length, flags, depth))
					{
						// element nodes can have value if parse_embed_pcdata was used
						if (node->value)
							indent_flags = 0;

						node = node->first_child;
						depth++;
						continue;
					}
				}
				else if (PUGI_IMPL_NODETYPE(node) == node_document)
				{
					indent_flags = indent_indent;

					if (node->first_child)
					{
						node = node->first_child;
						continue;
					}
				}
				else
				{
					node_output_simple(writer, node, flags);

					indent_flags = indent_newline | indent_indent;
				}
			}

			// continue to the next node
			while (node != root)
			{
				if (node->next_sibling)
				{
					node = node->next_sibling;
					break;
				}

				node = node->parent;

				// write closing node
				if (PUGI_IMPL_NODETYPE(node) == node_element)
				{
					depth--;

					if ((indent_flags & indent_newline) && (flags & format_raw) == 0)
						writer.write('\n');

					if ((indent_flags & indent_indent) && indent_length)
						text_output_indent(writer, indent, indent_length, depth);

					node_output_end(writer, node);

					indent_flags = indent_newline | indent_indent;
				}
			}
		}
		while (node != root);

		if ((indent_flags & indent_newline) && (flags & format_raw) == 0)
			writer.write('\n');
	}

	PUGI_IMPL_FN bool has_declaration(xml_node_struct* node)
	{
		for (xml_node_struct* child = node->first_child; child; child = child->next_sibling)
		{
			xml_node_type type = PUGI_IMPL_NODETYPE(child);

			if (type == node_declaration) return true;
			if (type == node_element) return false;
		}

		return false;
	}

	PUGI_IMPL_FN bool is_attribute_of(xml_attribute_struct* attr, xml_node_struct* node)
	{
		for (xml_attribute_struct* a = node->first_attribute; a; a = a->next_attribute)
			if (a == attr)
				return true;

		return false;
	}

	PUGI_IMPL_FN bool allow_insert_attribute(xml_node_type parent)
	{
		return parent == node_element || parent == node_declaration;
	}

	PUGI_IMPL_FN bool allow_insert_child(xml_node_type parent, xml_node_type child)
	{
		if (parent != node_document && parent != node_element) return false;
		if (child == node_document || child == node_null) return false;
		if (parent != node_document && (child == node_declaration || child == node_doctype)) return false;

		return true;
	}

	PUGI_IMPL_FN bool allow_move(xml_node parent, xml_node child)
	{
		// check that child can be a child of parent
		if (!allow_insert_child(parent.type(), child.type()))
			return false;

		// check that node is not moved between documents
		if (parent.root() != child.root())
			return false;

		// check that new parent is not in the child subtree
		xml_node cur = parent;

		while (cur)
		{
			if (cur == child)
				return false;

			cur = cur.parent();
		}

		return true;
	}

	template <typename String, typename Header>
	PUGI_IMPL_FN void node_copy_string(String& dest, Header& header, uintptr_t header_mask, char_t* source, Header& source_header, xml_allocator* alloc)
	{
		assert(!dest && (header & header_mask) == 0); // copies are performed into fresh nodes

		if (source)
		{
			if (alloc && (source_header & header_mask) == 0)
			{
				dest = source;

				// since strcpy_insitu can reuse document buffer memory we need to mark both source and dest as shared
				header |= xml_memory_page_contents_shared_mask;
				source_header |= xml_memory_page_contents_shared_mask;
			}
			else
				strcpy_insitu(dest, header, header_mask, source, strlength(source));
		}
	}

	PUGI_IMPL_FN void node_copy_contents(xml_node_struct* dn, xml_node_struct* sn, xml_allocator* shared_alloc)
	{
		node_copy_string(dn->name, dn->header, xml_memory_page_name_allocated_mask, sn->name, sn->header, shared_alloc);
		node_copy_string(dn->value, dn->header, xml_memory_page_value_allocated_mask, sn->value, sn->header, shared_alloc);

		for (xml_attribute_struct* sa = sn->first_attribute; sa; sa = sa->next_attribute)
		{
			xml_attribute_struct* da = append_new_attribute(dn, get_allocator(dn));

			if (da)
			{
				node_copy_string(da->name, da->header, xml_memory_page_name_allocated_mask, sa->name, sa->header, shared_alloc);
				node_copy_string(da->value, da->header, xml_memory_page_value_allocated_mask, sa->value, sa->header, shared_alloc);
			}
		}
	}

	PUGI_IMPL_FN void node_copy_tree(xml_node_struct* dn, xml_node_struct* sn)
	{
		xml_allocator& alloc = get_allocator(dn);
		xml_allocator* shared_alloc = (&alloc == &get_allocator(sn)) ? &alloc : NULL;

		node_copy_contents(dn, sn, shared_alloc);

		xml_node_struct* dit = dn;
		xml_node_struct* sit = sn->first_child;

		while (sit && sit != sn)
		{
			// loop invariant: dit is inside the subtree rooted at dn
			assert(dit);

			// when a tree is copied into one of the descendants, we need to skip that subtree to avoid an infinite loop
			if (sit != dn)
			{
				xml_node_struct* copy = append_new_node(dit, alloc, PUGI_IMPL_NODETYPE(sit));

				if (copy)
				{
					node_copy_contents(copy, sit, shared_alloc);

					if (sit->first_child)
					{
						dit = copy;
						sit = sit->first_child;
						continue;
					}
				}
			}

			// continue to the next node
			do
			{
				if (sit->next_sibling)
				{
					sit = sit->next_sibling;
					break;
				}

				sit = sit->parent;
				dit = dit->parent;

				// loop invariant: dit is inside the subtree rooted at dn while sit is inside sn
				assert(sit == sn || dit);
			}
			while (sit != sn);
		}

		assert(!sit || dit == dn->parent);
	}

	PUGI_IMPL_FN void node_copy_attribute(xml_attribute_struct* da, xml_attribute_struct* sa)
	{
		xml_allocator& alloc = get_allocator(da);
		xml_allocator* shared_alloc = (&alloc == &get_allocator(sa)) ? &alloc : NULL;

		node_copy_string(da->name, da->header, xml_memory_page_name_allocated_mask, sa->name, sa->header, shared_alloc);
		node_copy_string(da->value, da->header, xml_memory_page_value_allocated_mask, sa->value, sa->header, shared_alloc);
	}

	inline bool is_text_node(xml_node_struct* node)
	{
		xml_node_type type = PUGI_IMPL_NODETYPE(node);

		return type == node_pcdata || type == node_cdata;
	}

	// get value with conversion functions
	template <typename U> PUGI_IMPL_FN PUGI_IMPL_UNSIGNED_OVERFLOW U string_to_integer(const char_t* value, U minv, U maxv)
	{
		U result = 0;
		const char_t* s = value;

		while (PUGI_IMPL_IS_CHARTYPE(*s, ct_space))
			s++;

		bool negative = (*s == '-');

		s += (*s == '+' || *s == '-');

		bool overflow = false;

		if (s[0] == '0' && (s[1] | ' ') == 'x')
		{
			s += 2;

			// since overflow detection relies on length of the sequence skip leading zeros
			while (*s == '0')
				s++;

			const char_t* start = s;

			for (;;)
			{
				if (static_cast<unsigned>(*s - '0') < 10)
					result = result * 16 + (*s - '0');
				else if (static_cast<unsigned>((*s | ' ') - 'a') < 6)
					result = result * 16 + ((*s | ' ') - 'a' + 10);
				else
					break;

				s++;
			}

			size_t digits = static_cast<size_t>(s - start);

			overflow = digits > sizeof(U) * 2;
		}
		else
		{
			// since overflow detection relies on length of the sequence skip leading zeros
			while (*s == '0')
				s++;

			const char_t* start = s;

			for (;;)
			{
				if (static_cast<unsigned>(*s - '0') < 10)
					result = result * 10 + (*s - '0');
				else
					break;

				s++;
			}

			size_t digits = static_cast<size_t>(s - start);

			PUGI_IMPL_STATIC_ASSERT(sizeof(U) == 8 || sizeof(U) == 4 || sizeof(U) == 2);

			const size_t max_digits10 = sizeof(U) == 8 ? 20 : sizeof(U) == 4 ? 10 : 5;
			const char_t max_lead = sizeof(U) == 8 ? '1' : sizeof(U) == 4 ? '4' : '6';
			const size_t high_bit = sizeof(U) * 8 - 1;

			overflow = digits >= max_digits10 && !(digits == max_digits10 && (*start < max_lead || (*start == max_lead && result >> high_bit)));
		}

		if (negative)
		{
			// Workaround for crayc++ CC-3059: Expected no overflow in routine.
		#ifdef _CRAYC
			return (overflow || result > ~minv + 1) ? minv : ~result + 1;
		#else
			return (overflow || result > 0 - minv) ? minv : 0 - result;
		#endif
		}
		else
			return (overflow || result > maxv) ? maxv : result;
	}

	PUGI_IMPL_FN int get_value_int(const char_t* value)
	{
		return string_to_integer<unsigned int>(value, static_cast<unsigned int>(INT_MIN), INT_MAX);
	}

	PUGI_IMPL_FN unsigned int get_value_uint(const char_t* value)
	{
		return string_to_integer<unsigned int>(value, 0, UINT_MAX);
	}

	PUGI_IMPL_FN double get_value_double(const char_t* value)
	{
	#ifdef PUGIXML_WCHAR_MODE
		return wcstod(value, NULL);
	#else
		return strtod(value, NULL);
	#endif
	}

	PUGI_IMPL_FN float get_value_float(const char_t* value)
	{
	#ifdef PUGIXML_WCHAR_MODE
		return static_cast<float>(wcstod(value, NULL));
	#else
		return static_cast<float>(strtod(value, NULL));
	#endif
	}

	PUGI_IMPL_FN bool get_value_bool(const char_t* value)
	{
		// only look at first char
		char_t first = *value;

		// 1*, t* (true), T* (True), y* (yes), Y* (YES)
		return (first == '1' || first == 't' || first == 'T' || first == 'y' || first == 'Y');
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN long long get_value_llong(const char_t* value)
	{
		return string_to_integer<unsigned long long>(value, static_cast<unsigned long long>(LLONG_MIN), LLONG_MAX);
	}

	PUGI_IMPL_FN unsigned long long get_value_ullong(const char_t* value)
	{
		return string_to_integer<unsigned long long>(value, 0, ULLONG_MAX);
	}
#endif

	template <typename U> PUGI_IMPL_FN PUGI_IMPL_UNSIGNED_OVERFLOW char_t* integer_to_string(char_t* begin, char_t* end, U value, bool negative)
	{
		char_t* result = end - 1;
		U rest = negative ? 0 - value : value;

		do
		{
			*result-- = static_cast<char_t>('0' + (rest % 10));
			rest /= 10;
		}
		while (rest);

		assert(result >= begin);
		(void)begin;

		*result = '-';

		return result + !negative;
	}

	// set value with conversion functions
	template <typename String, typename Header>
	PUGI_IMPL_FN bool set_value_ascii(String& dest, Header& header, uintptr_t header_mask, char* buf)
	{
	#ifdef PUGIXML_WCHAR_MODE
		char_t wbuf[128];
		assert(strlen(buf) < sizeof(wbuf) / sizeof(wbuf[0]));

		size_t offset = 0;
		for (; buf[offset]; ++offset) wbuf[offset] = buf[offset];

		return strcpy_insitu(dest, header, header_mask, wbuf, offset);
	#else
		return strcpy_insitu(dest, header, header_mask, buf, strlen(buf));
	#endif
	}

	template <typename U, typename String, typename Header>
	PUGI_IMPL_FN bool set_value_integer(String& dest, Header& header, uintptr_t header_mask, U value, bool negative)
	{
		char_t buf[64];
		char_t* end = buf + sizeof(buf) / sizeof(buf[0]);
		char_t* begin = integer_to_string(buf, end, value, negative);

		return strcpy_insitu(dest, header, header_mask, begin, end - begin);
	}

	template <typename String, typename Header>
	PUGI_IMPL_FN bool set_value_convert(String& dest, Header& header, uintptr_t header_mask, float value, int precision)
	{
		char buf[128];
		PUGI_IMPL_SNPRINTF(buf, "%.*g", precision, double(value));

		return set_value_ascii(dest, header, header_mask, buf);
	}

	template <typename String, typename Header>
	PUGI_IMPL_FN bool set_value_convert(String& dest, Header& header, uintptr_t header_mask, double value, int precision)
	{
		char buf[128];
		PUGI_IMPL_SNPRINTF(buf, "%.*g", precision, value);

		return set_value_ascii(dest, header, header_mask, buf);
	}

	template <typename String, typename Header>
	PUGI_IMPL_FN bool set_value_bool(String& dest, Header& header, uintptr_t header_mask, bool value)
	{
		return strcpy_insitu(dest, header, header_mask, value ? PUGIXML_TEXT("true") : PUGIXML_TEXT("false"), value ? 4 : 5);
	}

	PUGI_IMPL_FN xml_parse_result load_buffer_impl(xml_document_struct* doc, xml_node_struct* root, void* contents, size_t size, unsigned int options, xml_encoding encoding, bool is_mutable, bool own, char_t** out_buffer)
	{
		// check input buffer
		if (!contents && size) return make_parse_result(status_io_error);

		// get actual encoding
		xml_encoding buffer_encoding = impl::get_buffer_encoding(encoding, contents, size);

		// if convert_buffer below throws bad_alloc, we still need to deallocate contents if we own it
		auto_deleter<void> contents_guard(own ? contents : NULL, xml_memory::deallocate);

		// get private buffer
		char_t* buffer = NULL;
		size_t length = 0;

		// coverity[var_deref_model]
		if (!impl::convert_buffer(buffer, length, buffer_encoding, contents, size, is_mutable)) return impl::make_parse_result(status_out_of_memory);

		// after this we either deallocate contents (below) or hold on to it via doc->buffer, so we don't need to guard it
		contents_guard.release();

		// delete original buffer if we performed a conversion
		if (own && buffer != contents && contents) impl::xml_memory::deallocate(contents);

		// grab onto buffer if it's our buffer, user is responsible for deallocating contents himself
		if (own || buffer != contents) *out_buffer = buffer;

		// store buffer for offset_debug
		doc->buffer = buffer;

		// parse
		xml_parse_result res = impl::xml_parser::parse(buffer, length, doc, root, options);

		// remember encoding
		res.encoding = buffer_encoding;

		return res;
	}

	template <typename T> PUGI_IMPL_FN xml_parse_status convert_file_size(T length, size_t& out_result)
	{
		// check for I/O errors
		if (length < 0) return status_io_error;

		// check for overflow
		size_t result = static_cast<size_t>(length);

		if (static_cast<T>(result) != length) return status_out_of_memory;

		out_result = result;
		return status_ok;
	}

	// we need to get length of entire file to load it in memory; the only (relatively) sane way to do it is via seek/tell trick
	PUGI_IMPL_FN xml_parse_status get_file_size(FILE* file, size_t& out_result)
	{
	#if defined(__linux__) || defined(__APPLE__)
		// this simultaneously retrieves the file size and file mode (to guard against loading non-files)
		struct stat st;
		if (fstat(fileno(file), &st) != 0) return status_io_error;

		// anything that's not a regular file doesn't have a coherent length
		if (!S_ISREG(st.st_mode)) return status_io_error;

		xml_parse_status status = convert_file_size(st.st_size, out_result);
	#elif defined(PUGI_IMPL_MSVC_CRT_VERSION) && PUGI_IMPL_MSVC_CRT_VERSION >= 1400
		// there are 64-bit versions of fseek/ftell, let's use them
		_fseeki64(file, 0, SEEK_END);
		__int64 length = _ftelli64(file);
		_fseeki64(file, 0, SEEK_SET);

		xml_parse_status status = convert_file_size(length, out_result);
	#elif defined(__MINGW32__) && !defined(__NO_MINGW_LFS) && (!defined(__STRICT_ANSI__) || defined(__MINGW64_VERSION_MAJOR))
		// there are 64-bit versions of fseek/ftell, let's use them
		fseeko64(file, 0, SEEK_END);
		off64_t length = ftello64(file);
		fseeko64(file, 0, SEEK_SET);

		xml_parse_status status = convert_file_size(length, out_result);
	#else
		// if this is a 32-bit OS, long is enough; if this is a unix system, long is 64-bit, which is enough; otherwise we can't do anything anyway.
		fseek(file, 0, SEEK_END);
		long length = ftell(file);
		fseek(file, 0, SEEK_SET);

		xml_parse_status status = convert_file_size(length, out_result);
	#endif

		return status;
	}

	// This function assumes that buffer has extra sizeof(char_t) writable bytes after size
	PUGI_IMPL_FN size_t zero_terminate_buffer(void* buffer, size_t size, xml_encoding encoding)
	{
		// We only need to zero-terminate if encoding conversion does not do it for us
	#ifdef PUGIXML_WCHAR_MODE
		xml_encoding wchar_encoding = get_wchar_encoding();

		if (encoding == wchar_encoding || need_endian_swap_utf(encoding, wchar_encoding))
		{
			size_t length = size / sizeof(char_t);

			static_cast<char_t*>(buffer)[length] = 0;
			return (length + 1) * sizeof(char_t);
		}
	#else
		if (encoding == encoding_utf8)
		{
			static_cast<char*>(buffer)[size] = 0;
			return size + 1;
		}
	#endif

		return size;
	}

	PUGI_IMPL_FN xml_parse_result load_file_impl(xml_document_struct* doc, FILE* file, unsigned int options, xml_encoding encoding, char_t** out_buffer)
	{
		if (!file) return make_parse_result(status_file_not_found);

		// get file size (can result in I/O errors)
		size_t size = 0;
		xml_parse_status size_status = get_file_size(file, size);
		if (size_status != status_ok) return make_parse_result(size_status);

		size_t max_suffix_size = sizeof(char_t);

		// allocate buffer for the whole file
		char* contents = static_cast<char*>(xml_memory::allocate(size + max_suffix_size));
		if (!contents) return make_parse_result(status_out_of_memory);

		// read file in memory
		size_t read_size = fread(contents, 1, size, file);

		if (read_size != size)
		{
			xml_memory::deallocate(contents);
			return make_parse_result(status_io_error);
		}

		xml_encoding real_encoding = get_buffer_encoding(encoding, contents, size);

		return load_buffer_impl(doc, doc, contents, zero_terminate_buffer(contents, size, real_encoding), options, real_encoding, true, true, out_buffer);
	}

	PUGI_IMPL_FN void close_file(FILE* file)
	{
		fclose(file);
	}

#ifndef PUGIXML_NO_STL
	template <typename T> struct xml_stream_chunk
	{
		static xml_stream_chunk* create()
		{
			void* memory = xml_memory::allocate(sizeof(xml_stream_chunk));
			if (!memory) return NULL;

			return new (memory) xml_stream_chunk();
		}

		static void destroy(xml_stream_chunk* chunk)
		{
			// free chunk chain
			while (chunk)
			{
				xml_stream_chunk* next_ = chunk->next;

				xml_memory::deallocate(chunk);

				chunk = next_;
			}
		}

		xml_stream_chunk(): next(NULL), size(0)
		{
		}

		xml_stream_chunk* next;
		size_t size;

		T data[xml_memory_page_size / sizeof(T)];
	};

	template <typename T> PUGI_IMPL_FN xml_parse_status load_stream_data_noseek(std::basic_istream<T>& stream, void** out_buffer, size_t* out_size)
	{
		auto_deleter<xml_stream_chunk<T> > chunks(NULL, xml_stream_chunk<T>::destroy);

		// read file to a chunk list
		size_t total = 0;
		xml_stream_chunk<T>* last = NULL;

		while (!stream.eof())
		{
			// allocate new chunk
			xml_stream_chunk<T>* chunk = xml_stream_chunk<T>::create();
			if (!chunk) return status_out_of_memory;

			// append chunk to list
			if (last) last = last->next = chunk;
			else chunks.data = last = chunk;

			// read data to chunk
			stream.read(chunk->data, static_cast<std::streamsize>(sizeof(chunk->data) / sizeof(T)));
			chunk->size = static_cast<size_t>(stream.gcount()) * sizeof(T);

			// read may set failbit | eofbit in case gcount() is less than read length, so check for other I/O errors
			if (stream.bad() || (!stream.eof() && stream.fail())) return status_io_error;

			// guard against huge files (chunk size is small enough to make this overflow check work)
			if (total + chunk->size < total) return status_out_of_memory;
			total += chunk->size;
		}

		size_t max_suffix_size = sizeof(char_t);

		// copy chunk list to a contiguous buffer
		char* buffer = static_cast<char*>(xml_memory::allocate(total + max_suffix_size));
		if (!buffer) return status_out_of_memory;

		char* write = buffer;

		for (xml_stream_chunk<T>* chunk = chunks.data; chunk; chunk = chunk->next)
		{
			assert(write + chunk->size <= buffer + total);
			memcpy(write, chunk->data, chunk->size);
			write += chunk->size;
		}

		assert(write == buffer + total);

		// return buffer
		*out_buffer = buffer;
		*out_size = total;

		return status_ok;
	}

	template <typename T> PUGI_IMPL_FN xml_parse_status load_stream_data_seek(std::basic_istream<T>& stream, void** out_buffer, size_t* out_size)
	{
		// get length of remaining data in stream
		typename std::basic_istream<T>::pos_type pos = stream.tellg();
		stream.seekg(0, std::ios::end);
		std::streamoff length = stream.tellg() - pos;
		stream.seekg(pos);

		if (stream.fail() || pos < 0) return status_io_error;

		// guard against huge files
		size_t read_length = static_cast<size_t>(length);

		if (static_cast<std::streamsize>(read_length) != length || length < 0) return status_out_of_memory;

		size_t max_suffix_size = sizeof(char_t);

		// read stream data into memory (guard against stream exceptions with buffer holder)
		auto_deleter<void> buffer(xml_memory::allocate(read_length * sizeof(T) + max_suffix_size), xml_memory::deallocate);
		if (!buffer.data) return status_out_of_memory;

		stream.read(static_cast<T*>(buffer.data), static_cast<std::streamsize>(read_length));

		// read may set failbit | eofbit in case gcount() is less than read_length (i.e. line ending conversion), so check for other I/O errors
		if (stream.bad() || (!stream.eof() && stream.fail())) return status_io_error;

		// return buffer
		size_t actual_length = static_cast<size_t>(stream.gcount());
		assert(actual_length <= read_length);

		*out_buffer = buffer.release();
		*out_size = actual_length * sizeof(T);

		return status_ok;
	}

	template <typename T> PUGI_IMPL_FN xml_parse_result load_stream_impl(xml_document_struct* doc, std::basic_istream<T>& stream, unsigned int options, xml_encoding encoding, char_t** out_buffer)
	{
		void* buffer = NULL;
		size_t size = 0;
		xml_parse_status status = status_ok;

		// if stream has an error bit set, bail out (otherwise tellg() can fail and we'll clear error bits)
		if (stream.fail()) return make_parse_result(status_io_error);

		// load stream to memory (using seek-based implementation if possible, since it's faster and takes less memory)
		if (stream.tellg() < 0)
		{
			stream.clear(); // clear error flags that could be set by a failing tellg
			status = load_stream_data_noseek(stream, &buffer, &size);
		}
		else
			status = load_stream_data_seek(stream, &buffer, &size);

		if (status != status_ok) return make_parse_result(status);

		xml_encoding real_encoding = get_buffer_encoding(encoding, buffer, size);

		return load_buffer_impl(doc, doc, buffer, zero_terminate_buffer(buffer, size, real_encoding), options, real_encoding, true, true, out_buffer);
	}
#endif

#if defined(PUGI_IMPL_MSVC_CRT_VERSION) || defined(__BORLANDC__) || (defined(__MINGW32__) && (!defined(__STRICT_ANSI__) || defined(__MINGW64_VERSION_MAJOR)))
	PUGI_IMPL_FN FILE* open_file_wide(const wchar_t* path, const wchar_t* mode)
	{
#if defined(PUGI_IMPL_MSVC_CRT_VERSION) && PUGI_IMPL_MSVC_CRT_VERSION >= 1400
		FILE* file = NULL;
		return _wfopen_s(&file, path, mode) == 0 ? file : NULL;
#else
		return _wfopen(path, mode);
#endif
	}
#else
	PUGI_IMPL_FN char* convert_path_heap(const wchar_t* str)
	{
		assert(str);

		// first pass: get length in utf8 characters
		size_t length = strlength_wide(str);
		size_t size = as_utf8_begin(str, length);

		// allocate resulting string
		char* result = static_cast<char*>(xml_memory::allocate(size + 1));
		if (!result) return NULL;

		// second pass: convert to utf8
		as_utf8_end(result, size, str, length);

		// zero-terminate
		result[size] = 0;

		return result;
	}

	PUGI_IMPL_FN FILE* open_file_wide(const wchar_t* path, const wchar_t* mode)
	{
		// there is no standard function to open wide paths, so our best bet is to try utf8 path
		char* path_utf8 = convert_path_heap(path);
		if (!path_utf8) return NULL;

		// convert mode to ASCII (we mirror _wfopen interface)
		char mode_ascii[4] = {0};
		for (size_t i = 0; mode[i]; ++i) mode_ascii[i] = static_cast<char>(mode[i]);

		// try to open the utf8 path
		FILE* result = fopen(path_utf8, mode_ascii);

		// free dummy buffer
		xml_memory::deallocate(path_utf8);

		return result;
	}
#endif

	PUGI_IMPL_FN FILE* open_file(const char* path, const char* mode)
	{
#if defined(PUGI_IMPL_MSVC_CRT_VERSION) && PUGI_IMPL_MSVC_CRT_VERSION >= 1400
		FILE* file = NULL;
		return fopen_s(&file, path, mode) == 0 ? file : NULL;
#else
		return fopen(path, mode);
#endif
	}

	PUGI_IMPL_FN bool save_file_impl(const xml_document& doc, FILE* file, const char_t* indent, unsigned int flags, xml_encoding encoding)
	{
		if (!file) return false;

		xml_writer_file writer(file);
		doc.save(writer, indent, flags, encoding);

		return fflush(file) == 0 && ferror(file) == 0;
	}

	struct name_null_sentry
	{
		xml_node_struct* node;
		char_t* name;

		name_null_sentry(xml_node_struct* node_): node(node_), name(node_->name)
		{
			node->name = NULL;
		}

		~name_null_sentry()
		{
			node->name = name;
		}
	};
PUGI_IMPL_NS_END

namespace pugi
{
	PUGI_IMPL_FN xml_writer::~xml_writer()
	{
	}

	PUGI_IMPL_FN xml_writer_file::xml_writer_file(void* file_): file(file_)
	{
	}

	PUGI_IMPL_FN void xml_writer_file::write(const void* data, size_t size)
	{
		size_t result = fwrite(data, 1, size, static_cast<FILE*>(file));
		(void)!result; // unfortunately we can't do proper error handling here
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN xml_writer_stream::xml_writer_stream(std::basic_ostream<char>& stream): narrow_stream(&stream), wide_stream(NULL)
	{
	}

	PUGI_IMPL_FN xml_writer_stream::xml_writer_stream(std::basic_ostream<wchar_t>& stream): narrow_stream(NULL), wide_stream(&stream)
	{
	}

	PUGI_IMPL_FN void xml_writer_stream::write(const void* data, size_t size)
	{
		if (narrow_stream)
		{
			assert(!wide_stream);
			narrow_stream->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		}
		else
		{
			assert(wide_stream);
			assert(size % sizeof(wchar_t) == 0);

			wide_stream->write(reinterpret_cast<const wchar_t*>(data), static_cast<std::streamsize>(size / sizeof(wchar_t)));
		}
	}
#endif

	PUGI_IMPL_FN xml_tree_walker::xml_tree_walker(): _depth(0)
	{
	}

	PUGI_IMPL_FN xml_tree_walker::~xml_tree_walker()
	{
	}

	PUGI_IMPL_FN int xml_tree_walker::depth() const
	{
		return _depth;
	}

	PUGI_IMPL_FN bool xml_tree_walker::begin(xml_node&)
	{
		return true;
	}

	PUGI_IMPL_FN bool xml_tree_walker::end(xml_node&)
	{
		return true;
	}

	PUGI_IMPL_FN xml_attribute::xml_attribute(): _attr(NULL)
	{
	}

	PUGI_IMPL_FN xml_attribute::xml_attribute(xml_attribute_struct* attr): _attr(attr)
	{
	}

	PUGI_IMPL_FN static void unspecified_bool_xml_attribute(xml_attribute***)
	{
	}

	PUGI_IMPL_FN xml_attribute::operator xml_attribute::unspecified_bool_type() const
	{
		return _attr ? unspecified_bool_xml_attribute : NULL;
	}

	PUGI_IMPL_FN bool xml_attribute::operator!() const
	{
		return !_attr;
	}

	PUGI_IMPL_FN bool xml_attribute::operator==(const xml_attribute& r) const
	{
		return (_attr == r._attr);
	}

	PUGI_IMPL_FN bool xml_attribute::operator!=(const xml_attribute& r) const
	{
		return (_attr != r._attr);
	}

	PUGI_IMPL_FN bool xml_attribute::operator<(const xml_attribute& r) const
	{
		return (_attr < r._attr);
	}

	PUGI_IMPL_FN bool xml_attribute::operator>(const xml_attribute& r) const
	{
		return (_attr > r._attr);
	}

	PUGI_IMPL_FN bool xml_attribute::operator<=(const xml_attribute& r) const
	{
		return (_attr <= r._attr);
	}

	PUGI_IMPL_FN bool xml_attribute::operator>=(const xml_attribute& r) const
	{
		return (_attr >= r._attr);
	}

	PUGI_IMPL_FN xml_attribute xml_attribute::next_attribute() const
	{
		if (!_attr) return xml_attribute();
		return xml_attribute(_attr->next_attribute);
	}

	PUGI_IMPL_FN xml_attribute xml_attribute::previous_attribute() const
	{
		if (!_attr) return xml_attribute();
		xml_attribute_struct* prev = _attr->prev_attribute_c;
		return prev->next_attribute ? xml_attribute(prev) : xml_attribute();
	}

	PUGI_IMPL_FN const char_t* xml_attribute::as_string(const char_t* def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? value : def;
	}

	PUGI_IMPL_FN int xml_attribute::as_int(int def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_int(value) : def;
	}

	PUGI_IMPL_FN unsigned int xml_attribute::as_uint(unsigned int def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_uint(value) : def;
	}

	PUGI_IMPL_FN double xml_attribute::as_double(double def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_double(value) : def;
	}

	PUGI_IMPL_FN float xml_attribute::as_float(float def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_float(value) : def;
	}

	PUGI_IMPL_FN bool xml_attribute::as_bool(bool def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_bool(value) : def;
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN long long xml_attribute::as_llong(long long def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_llong(value) : def;
	}

	PUGI_IMPL_FN unsigned long long xml_attribute::as_ullong(unsigned long long def) const
	{
		if (!_attr) return def;
		const char_t* value = _attr->value;
		return value ? impl::get_value_ullong(value) : def;
	}
#endif

	PUGI_IMPL_FN bool xml_attribute::empty() const
	{
		return !_attr;
	}

	PUGI_IMPL_FN const char_t* xml_attribute::name() const
	{
		if (!_attr) return PUGIXML_TEXT("");
		const char_t* name = _attr->name;
		return name ? name : PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN const char_t* xml_attribute::value() const
	{
		if (!_attr) return PUGIXML_TEXT("");
		const char_t* value = _attr->value;
		return value ? value : PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN size_t xml_attribute::hash_value() const
	{
		return reinterpret_cast<uintptr_t>(_attr) / sizeof(xml_attribute_struct);
	}

	PUGI_IMPL_FN xml_attribute_struct* xml_attribute::internal_object() const
	{
		return _attr;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(const char_t* rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(int rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(unsigned int rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(long rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(unsigned long rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(double rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(float rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(bool rhs)
	{
		set_value(rhs);
		return *this;
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(long long rhs)
	{
		set_value(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute::operator=(unsigned long long rhs)
	{
		set_value(rhs);
		return *this;
	}
#endif

	PUGI_IMPL_FN bool xml_attribute::set_name(const char_t* rhs)
	{
		if (!_attr) return false;

		return impl::strcpy_insitu(_attr->name, _attr->header, impl::xml_memory_page_name_allocated_mask, rhs, impl::strlength(rhs));
	}

	PUGI_IMPL_FN bool xml_attribute::set_name(const char_t* rhs, size_t size)
	{
		if (!_attr) return false;

		return impl::strcpy_insitu(_attr->name, _attr->header, impl::xml_memory_page_name_allocated_mask, rhs, size);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(const char_t* rhs)
	{
		if (!_attr) return false;

		return impl::strcpy_insitu(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, impl::strlength(rhs));
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(const char_t* rhs, size_t size)
	{
		if (!_attr) return false;

		return impl::strcpy_insitu(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, size);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(int rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned int>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(unsigned int rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned int>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, false);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(long rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned long>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(unsigned long rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned long>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, false);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(double rhs)
	{
		if (!_attr) return false;

		return impl::set_value_convert(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, default_double_precision);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(double rhs, int precision)
	{
		if (!_attr) return false;

		return impl::set_value_convert(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, precision);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(float rhs)
	{
		if (!_attr) return false;

		return impl::set_value_convert(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, default_float_precision);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(float rhs, int precision)
	{
		if (!_attr) return false;

		return impl::set_value_convert(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, precision);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(bool rhs)
	{
		if (!_attr) return false;

		return impl::set_value_bool(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs);
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN bool xml_attribute::set_value(long long rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned long long>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0);
	}

	PUGI_IMPL_FN bool xml_attribute::set_value(unsigned long long rhs)
	{
		if (!_attr) return false;

		return impl::set_value_integer<unsigned long long>(_attr->value, _attr->header, impl::xml_memory_page_value_allocated_mask, rhs, false);
	}
#endif

#ifdef __BORLANDC__
	PUGI_IMPL_FN bool operator&&(const xml_attribute& lhs, bool rhs)
	{
		return (bool)lhs && rhs;
	}

	PUGI_IMPL_FN bool operator||(const xml_attribute& lhs, bool rhs)
	{
		return (bool)lhs || rhs;
	}
#endif

	PUGI_IMPL_FN xml_node::xml_node(): _root(NULL)
	{
	}

	PUGI_IMPL_FN xml_node::xml_node(xml_node_struct* p): _root(p)
	{
	}

	PUGI_IMPL_FN static void unspecified_bool_xml_node(xml_node***)
	{
	}

	PUGI_IMPL_FN xml_node::operator xml_node::unspecified_bool_type() const
	{
		return _root ? unspecified_bool_xml_node : NULL;
	}

	PUGI_IMPL_FN bool xml_node::operator!() const
	{
		return !_root;
	}

	PUGI_IMPL_FN xml_node::iterator xml_node::begin() const
	{
		return iterator(_root ? _root->first_child + 0 : NULL, _root);
	}

	PUGI_IMPL_FN xml_node::iterator xml_node::end() const
	{
		return iterator(NULL, _root);
	}

	PUGI_IMPL_FN xml_node::attribute_iterator xml_node::attributes_begin() const
	{
		return attribute_iterator(_root ? _root->first_attribute + 0 : NULL, _root);
	}

	PUGI_IMPL_FN xml_node::attribute_iterator xml_node::attributes_end() const
	{
		return attribute_iterator(NULL, _root);
	}

	PUGI_IMPL_FN xml_object_range<xml_node_iterator> xml_node::children() const
	{
		return xml_object_range<xml_node_iterator>(begin(), end());
	}

	PUGI_IMPL_FN xml_object_range<xml_named_node_iterator> xml_node::children(const char_t* name_) const
	{
		return xml_object_range<xml_named_node_iterator>(xml_named_node_iterator(child(name_)._root, _root, name_), xml_named_node_iterator(NULL, _root, name_));
	}

	PUGI_IMPL_FN xml_object_range<xml_attribute_iterator> xml_node::attributes() const
	{
		return xml_object_range<xml_attribute_iterator>(attributes_begin(), attributes_end());
	}

	PUGI_IMPL_FN bool xml_node::operator==(const xml_node& r) const
	{
		return (_root == r._root);
	}

	PUGI_IMPL_FN bool xml_node::operator!=(const xml_node& r) const
	{
		return (_root != r._root);
	}

	PUGI_IMPL_FN bool xml_node::operator<(const xml_node& r) const
	{
		return (_root < r._root);
	}

	PUGI_IMPL_FN bool xml_node::operator>(const xml_node& r) const
	{
		return (_root > r._root);
	}

	PUGI_IMPL_FN bool xml_node::operator<=(const xml_node& r) const
	{
		return (_root <= r._root);
	}

	PUGI_IMPL_FN bool xml_node::operator>=(const xml_node& r) const
	{
		return (_root >= r._root);
	}

	PUGI_IMPL_FN bool xml_node::empty() const
	{
		return !_root;
	}

	PUGI_IMPL_FN const char_t* xml_node::name() const
	{
		if (!_root) return PUGIXML_TEXT("");
		const char_t* name = _root->name;
		return name ? name : PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN xml_node_type xml_node::type() const
	{
		return _root ? PUGI_IMPL_NODETYPE(_root) : node_null;
	}

	PUGI_IMPL_FN const char_t* xml_node::value() const
	{
		if (!_root) return PUGIXML_TEXT("");
		const char_t* value = _root->value;
		return value ? value : PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN xml_node xml_node::child(const char_t* name_) const
	{
		if (!_root) return xml_node();

		for (xml_node_struct* i = _root->first_child; i; i = i->next_sibling)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
				return xml_node(i);
		}

		return xml_node();
	}

	PUGI_IMPL_FN xml_attribute xml_node::attribute(const char_t* name_) const
	{
		if (!_root) return xml_attribute();

		for (xml_attribute_struct* i = _root->first_attribute; i; i = i->next_attribute)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
				return xml_attribute(i);
		}

		return xml_attribute();
	}

	PUGI_IMPL_FN xml_node xml_node::next_sibling(const char_t* name_) const
	{
		if (!_root) return xml_node();

		for (xml_node_struct* i = _root->next_sibling; i; i = i->next_sibling)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
				return xml_node(i);
		}

		return xml_node();
	}

	PUGI_IMPL_FN xml_node xml_node::next_sibling() const
	{
		return _root ? xml_node(_root->next_sibling) : xml_node();
	}

	PUGI_IMPL_FN xml_node xml_node::previous_sibling(const char_t* name_) const
	{
		if (!_root) return xml_node();

		for (xml_node_struct* i = _root->prev_sibling_c; i->next_sibling; i = i->prev_sibling_c)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
				return xml_node(i);
		}

		return xml_node();
	}

	PUGI_IMPL_FN xml_attribute xml_node::attribute(const char_t* name_, xml_attribute& hint_) const
	{
		xml_attribute_struct* hint = hint_._attr;

		// if hint is not an attribute of node, behavior is not defined
		assert(!hint || (_root && impl::is_attribute_of(hint, _root)));

		if (!_root) return xml_attribute();

		// optimistically search from hint up until the end
		for (xml_attribute_struct* i = hint; i; i = i->next_attribute)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
			{
				// update hint to maximize efficiency of searching for consecutive attributes
				hint_._attr = i->next_attribute;

				return xml_attribute(i);
			}
		}

		// wrap around and search from the first attribute until the hint
		// 'j' null pointer check is technically redundant, but it prevents a crash in case the assertion above fails
		for (xml_attribute_struct* j = _root->first_attribute; j && j != hint; j = j->next_attribute)
		{
			const char_t* jname = j->name;
			if (jname && impl::strequal(name_, jname))
			{
				// update hint to maximize efficiency of searching for consecutive attributes
				hint_._attr = j->next_attribute;

				return xml_attribute(j);
			}
		}

		return xml_attribute();
	}

	PUGI_IMPL_FN xml_node xml_node::previous_sibling() const
	{
		if (!_root) return xml_node();
		xml_node_struct* prev = _root->prev_sibling_c;
		return prev->next_sibling ? xml_node(prev) : xml_node();
	}

	PUGI_IMPL_FN xml_node xml_node::parent() const
	{
		return _root ? xml_node(_root->parent) : xml_node();
	}

	PUGI_IMPL_FN xml_node xml_node::root() const
	{
		return _root ? xml_node(&impl::get_document(_root)) : xml_node();
	}

	PUGI_IMPL_FN xml_text xml_node::text() const
	{
		return xml_text(_root);
	}

	PUGI_IMPL_FN const char_t* xml_node::child_value() const
	{
		if (!_root) return PUGIXML_TEXT("");

		// element nodes can have value if parse_embed_pcdata was used
		if (PUGI_IMPL_NODETYPE(_root) == node_element && _root->value)
			return _root->value;

		for (xml_node_struct* i = _root->first_child; i; i = i->next_sibling)
		{
			const char_t* ivalue = i->value;
			if (impl::is_text_node(i) && ivalue)
				return ivalue;
		}

		return PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN const char_t* xml_node::child_value(const char_t* name_) const
	{
		return child(name_).child_value();
	}

	PUGI_IMPL_FN xml_attribute xml_node::first_attribute() const
	{
		if (!_root) return xml_attribute();
		return xml_attribute(_root->first_attribute);
	}

	PUGI_IMPL_FN xml_attribute xml_node::last_attribute() const
	{
		if (!_root) return xml_attribute();
		xml_attribute_struct* first = _root->first_attribute;
		return first ? xml_attribute(first->prev_attribute_c) : xml_attribute();
	}

	PUGI_IMPL_FN xml_node xml_node::first_child() const
	{
		if (!_root) return xml_node();
		return xml_node(_root->first_child);
	}

	PUGI_IMPL_FN xml_node xml_node::last_child() const
	{
		if (!_root) return xml_node();
		xml_node_struct* first = _root->first_child;
		return first ? xml_node(first->prev_sibling_c) : xml_node();
	}

	PUGI_IMPL_FN bool xml_node::set_name(const char_t* rhs)
	{
		xml_node_type type_ = _root ? PUGI_IMPL_NODETYPE(_root) : node_null;

		if (type_ != node_element && type_ != node_pi && type_ != node_declaration)
			return false;

		return impl::strcpy_insitu(_root->name, _root->header, impl::xml_memory_page_name_allocated_mask, rhs, impl::strlength(rhs));
	}

	PUGI_IMPL_FN bool xml_node::set_name(const char_t* rhs, size_t size)
	{
		xml_node_type type_ = _root ? PUGI_IMPL_NODETYPE(_root) : node_null;

		if (type_ != node_element && type_ != node_pi && type_ != node_declaration)
			return false;

		return impl::strcpy_insitu(_root->name, _root->header, impl::xml_memory_page_name_allocated_mask, rhs, size);
	}

	PUGI_IMPL_FN bool xml_node::set_value(const char_t* rhs)
	{
		xml_node_type type_ = _root ? PUGI_IMPL_NODETYPE(_root) : node_null;

		if (type_ != node_pcdata && type_ != node_cdata && type_ != node_comment && type_ != node_pi && type_ != node_doctype)
			return false;

		return impl::strcpy_insitu(_root->value, _root->header, impl::xml_memory_page_value_allocated_mask, rhs, impl::strlength(rhs));
	}

	PUGI_IMPL_FN bool xml_node::set_value(const char_t* rhs, size_t size)
	{
		xml_node_type type_ = _root ? PUGI_IMPL_NODETYPE(_root) : node_null;

		if (type_ != node_pcdata && type_ != node_cdata && type_ != node_comment && type_ != node_pi && type_ != node_doctype)
			return false;

		return impl::strcpy_insitu(_root->value, _root->header, impl::xml_memory_page_value_allocated_mask, rhs, size);
	}

	PUGI_IMPL_FN xml_attribute xml_node::append_attribute(const char_t* name_)
	{
		if (!impl::allow_insert_attribute(type())) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::append_attribute(a._attr, _root);

		a.set_name(name_);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::prepend_attribute(const char_t* name_)
	{
		if (!impl::allow_insert_attribute(type())) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::prepend_attribute(a._attr, _root);

		a.set_name(name_);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::insert_attribute_after(const char_t* name_, const xml_attribute& attr)
	{
		if (!impl::allow_insert_attribute(type())) return xml_attribute();
		if (!attr || !impl::is_attribute_of(attr._attr, _root)) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::insert_attribute_after(a._attr, attr._attr, _root);

		a.set_name(name_);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::insert_attribute_before(const char_t* name_, const xml_attribute& attr)
	{
		if (!impl::allow_insert_attribute(type())) return xml_attribute();
		if (!attr || !impl::is_attribute_of(attr._attr, _root)) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::insert_attribute_before(a._attr, attr._attr, _root);

		a.set_name(name_);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::append_copy(const xml_attribute& proto)
	{
		if (!proto) return xml_attribute();
		if (!impl::allow_insert_attribute(type())) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::append_attribute(a._attr, _root);
		impl::node_copy_attribute(a._attr, proto._attr);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::prepend_copy(const xml_attribute& proto)
	{
		if (!proto) return xml_attribute();
		if (!impl::allow_insert_attribute(type())) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::prepend_attribute(a._attr, _root);
		impl::node_copy_attribute(a._attr, proto._attr);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::insert_copy_after(const xml_attribute& proto, const xml_attribute& attr)
	{
		if (!proto) return xml_attribute();
		if (!impl::allow_insert_attribute(type())) return xml_attribute();
		if (!attr || !impl::is_attribute_of(attr._attr, _root)) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::insert_attribute_after(a._attr, attr._attr, _root);
		impl::node_copy_attribute(a._attr, proto._attr);

		return a;
	}

	PUGI_IMPL_FN xml_attribute xml_node::insert_copy_before(const xml_attribute& proto, const xml_attribute& attr)
	{
		if (!proto) return xml_attribute();
		if (!impl::allow_insert_attribute(type())) return xml_attribute();
		if (!attr || !impl::is_attribute_of(attr._attr, _root)) return xml_attribute();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_attribute();

		xml_attribute a(impl::allocate_attribute(alloc));
		if (!a) return xml_attribute();

		impl::insert_attribute_before(a._attr, attr._attr, _root);
		impl::node_copy_attribute(a._attr, proto._attr);

		return a;
	}

	PUGI_IMPL_FN xml_node xml_node::append_child(xml_node_type type_)
	{
		if (!impl::allow_insert_child(type(), type_)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::append_node(n._root, _root);

		if (type_ == node_declaration) n.set_name(PUGIXML_TEXT("xml"));

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::prepend_child(xml_node_type type_)
	{
		if (!impl::allow_insert_child(type(), type_)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::prepend_node(n._root, _root);

		if (type_ == node_declaration) n.set_name(PUGIXML_TEXT("xml"));

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_child_before(xml_node_type type_, const xml_node& node)
	{
		if (!impl::allow_insert_child(type(), type_)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::insert_node_before(n._root, node._root);

		if (type_ == node_declaration) n.set_name(PUGIXML_TEXT("xml"));

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_child_after(xml_node_type type_, const xml_node& node)
	{
		if (!impl::allow_insert_child(type(), type_)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::insert_node_after(n._root, node._root);

		if (type_ == node_declaration) n.set_name(PUGIXML_TEXT("xml"));

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::append_child(const char_t* name_)
	{
		xml_node result = append_child(node_element);

		result.set_name(name_);

		return result;
	}

	PUGI_IMPL_FN xml_node xml_node::prepend_child(const char_t* name_)
	{
		xml_node result = prepend_child(node_element);

		result.set_name(name_);

		return result;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_child_after(const char_t* name_, const xml_node& node)
	{
		xml_node result = insert_child_after(node_element, node);

		result.set_name(name_);

		return result;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_child_before(const char_t* name_, const xml_node& node)
	{
		xml_node result = insert_child_before(node_element, node);

		result.set_name(name_);

		return result;
	}

	PUGI_IMPL_FN xml_node xml_node::append_copy(const xml_node& proto)
	{
		xml_node_type type_ = proto.type();
		if (!impl::allow_insert_child(type(), type_)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::append_node(n._root, _root);
		impl::node_copy_tree(n._root, proto._root);

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::prepend_copy(const xml_node& proto)
	{
		xml_node_type type_ = proto.type();
		if (!impl::allow_insert_child(type(), type_)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::prepend_node(n._root, _root);
		impl::node_copy_tree(n._root, proto._root);

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_copy_after(const xml_node& proto, const xml_node& node)
	{
		xml_node_type type_ = proto.type();
		if (!impl::allow_insert_child(type(), type_)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::insert_node_after(n._root, node._root);
		impl::node_copy_tree(n._root, proto._root);

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_copy_before(const xml_node& proto, const xml_node& node)
	{
		xml_node_type type_ = proto.type();
		if (!impl::allow_insert_child(type(), type_)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		xml_node n(impl::allocate_node(alloc, type_));
		if (!n) return xml_node();

		impl::insert_node_before(n._root, node._root);
		impl::node_copy_tree(n._root, proto._root);

		return n;
	}

	PUGI_IMPL_FN xml_node xml_node::append_move(const xml_node& moved)
	{
		if (!impl::allow_move(*this, moved)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		// disable document_buffer_order optimization since moving nodes around changes document order without changing buffer pointers
		impl::get_document(_root).header |= impl::xml_memory_page_contents_shared_mask;

		impl::remove_node(moved._root);
		impl::append_node(moved._root, _root);

		return moved;
	}

	PUGI_IMPL_FN xml_node xml_node::prepend_move(const xml_node& moved)
	{
		if (!impl::allow_move(*this, moved)) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		// disable document_buffer_order optimization since moving nodes around changes document order without changing buffer pointers
		impl::get_document(_root).header |= impl::xml_memory_page_contents_shared_mask;

		impl::remove_node(moved._root);
		impl::prepend_node(moved._root, _root);

		return moved;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_move_after(const xml_node& moved, const xml_node& node)
	{
		if (!impl::allow_move(*this, moved)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();
		if (moved._root == node._root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		// disable document_buffer_order optimization since moving nodes around changes document order without changing buffer pointers
		impl::get_document(_root).header |= impl::xml_memory_page_contents_shared_mask;

		impl::remove_node(moved._root);
		impl::insert_node_after(moved._root, node._root);

		return moved;
	}

	PUGI_IMPL_FN xml_node xml_node::insert_move_before(const xml_node& moved, const xml_node& node)
	{
		if (!impl::allow_move(*this, moved)) return xml_node();
		if (!node._root || node._root->parent != _root) return xml_node();
		if (moved._root == node._root) return xml_node();

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return xml_node();

		// disable document_buffer_order optimization since moving nodes around changes document order without changing buffer pointers
		impl::get_document(_root).header |= impl::xml_memory_page_contents_shared_mask;

		impl::remove_node(moved._root);
		impl::insert_node_before(moved._root, node._root);

		return moved;
	}

	PUGI_IMPL_FN bool xml_node::remove_attribute(const char_t* name_)
	{
		return remove_attribute(attribute(name_));
	}

	PUGI_IMPL_FN bool xml_node::remove_attribute(const xml_attribute& a)
	{
		if (!_root || !a._attr) return false;
		if (!impl::is_attribute_of(a._attr, _root)) return false;

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return false;

		impl::remove_attribute(a._attr, _root);
		impl::destroy_attribute(a._attr, alloc);

		return true;
	}

	PUGI_IMPL_FN bool xml_node::remove_attributes()
	{
		if (!_root) return false;

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return false;

		for (xml_attribute_struct* attr = _root->first_attribute; attr; )
		{
			xml_attribute_struct* next = attr->next_attribute;

			impl::destroy_attribute(attr, alloc);

			attr = next;
		}

		_root->first_attribute = NULL;

		return true;
	}

	PUGI_IMPL_FN bool xml_node::remove_child(const char_t* name_)
	{
		return remove_child(child(name_));
	}

	PUGI_IMPL_FN bool xml_node::remove_child(const xml_node& n)
	{
		if (!_root || !n._root || n._root->parent != _root) return false;

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return false;

		impl::remove_node(n._root);
		impl::destroy_node(n._root, alloc);

		return true;
	}

	PUGI_IMPL_FN bool xml_node::remove_children()
	{
		if (!_root) return false;

		impl::xml_allocator& alloc = impl::get_allocator(_root);
		if (!alloc.reserve()) return false;

		for (xml_node_struct* cur = _root->first_child; cur; )
		{
			xml_node_struct* next = cur->next_sibling;

			impl::destroy_node(cur, alloc);

			cur = next;
		}

		_root->first_child = NULL;

		return true;
	}

	PUGI_IMPL_FN xml_parse_result xml_node::append_buffer(const void* contents, size_t size, unsigned int options, xml_encoding encoding)
	{
		// append_buffer is only valid for elements/documents
		if (!impl::allow_insert_child(type(), node_element)) return impl::make_parse_result(status_append_invalid_root);

		// append buffer can not merge PCDATA into existing PCDATA nodes
		if ((options & parse_merge_pcdata) != 0 && last_child().type() == node_pcdata) return impl::make_parse_result(status_append_invalid_root);

		// get document node
		impl::xml_document_struct* doc = &impl::get_document(_root);

		// disable document_buffer_order optimization since in a document with multiple buffers comparing buffer pointers does not make sense
		doc->header |= impl::xml_memory_page_contents_shared_mask;

		// get extra buffer element (we'll store the document fragment buffer there so that we can deallocate it later)
		impl::xml_memory_page* page = NULL;
		impl::xml_extra_buffer* extra = static_cast<impl::xml_extra_buffer*>(doc->allocate_memory(sizeof(impl::xml_extra_buffer) + sizeof(void*), page));
		(void)page;

		if (!extra) return impl::make_parse_result(status_out_of_memory);

	#ifdef PUGIXML_COMPACT
		// align the memory block to a pointer boundary; this is required for compact mode where memory allocations are only 4b aligned
		// note that this requires up to sizeof(void*)-1 additional memory, which the allocation above takes into account
		extra = reinterpret_cast<impl::xml_extra_buffer*>((reinterpret_cast<uintptr_t>(extra) + (sizeof(void*) - 1)) & ~(sizeof(void*) - 1));
	#endif

		// add extra buffer to the list
		extra->buffer = NULL;
		extra->next = doc->extra_buffers;
		doc->extra_buffers = extra;

		// name of the root has to be NULL before parsing - otherwise closing node mismatches will not be detected at the top level
		impl::name_null_sentry sentry(_root);

		return impl::load_buffer_impl(doc, _root, const_cast<void*>(contents), size, options, encoding, false, false, &extra->buffer);
	}

	PUGI_IMPL_FN xml_node xml_node::find_child_by_attribute(const char_t* name_, const char_t* attr_name, const char_t* attr_value) const
	{
		if (!_root) return xml_node();

		for (xml_node_struct* i = _root->first_child; i; i = i->next_sibling)
		{
			const char_t* iname = i->name;
			if (iname && impl::strequal(name_, iname))
			{
				for (xml_attribute_struct* a = i->first_attribute; a; a = a->next_attribute)
				{
					const char_t* aname = a->name;
					if (aname && impl::strequal(attr_name, aname))
					{
						const char_t* avalue = a->value;
						if (impl::strequal(attr_value, avalue ? avalue : PUGIXML_TEXT("")))
							return xml_node(i);
					}
				}
			}
		}

		return xml_node();
	}

	PUGI_IMPL_FN xml_node xml_node::find_child_by_attribute(const char_t* attr_name, const char_t* attr_value) const
	{
		if (!_root) return xml_node();

		for (xml_node_struct* i = _root->first_child; i; i = i->next_sibling)
			for (xml_attribute_struct* a = i->first_attribute; a; a = a->next_attribute)
			{
				const char_t* aname = a->name;
				if (aname && impl::strequal(attr_name, aname))
				{
					const char_t* avalue = a->value;
					if (impl::strequal(attr_value, avalue ? avalue : PUGIXML_TEXT("")))
						return xml_node(i);
				}
			}

		return xml_node();
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN string_t xml_node::path(char_t delimiter) const
	{
		if (!_root) return string_t();

		size_t offset = 0;

		for (xml_node_struct* i = _root; i; i = i->parent)
		{
			const char_t* iname = i->name;
			offset += (i != _root);
			offset += iname ? impl::strlength(iname) : 0;
		}

		string_t result;
		result.resize(offset);

		for (xml_node_struct* j = _root; j; j = j->parent)
		{
			if (j != _root)
				result[--offset] = delimiter;

			const char_t* jname = j->name;
			if (jname)
			{
				size_t length = impl::strlength(jname);

				offset -= length;
				memcpy(&result[offset], jname, length * sizeof(char_t));
			}
		}

		assert(offset == 0);

		return result;
	}
#endif

	PUGI_IMPL_FN xml_node xml_node::first_element_by_path(const char_t* path_, char_t delimiter) const
	{
		xml_node context = path_[0] == delimiter ? root() : *this;

		if (!context._root) return xml_node();

		const char_t* path_segment = path_;

		while (*path_segment == delimiter) ++path_segment;

		const char_t* path_segment_end = path_segment;

		while (*path_segment_end && *path_segment_end != delimiter) ++path_segment_end;

		if (path_segment == path_segment_end) return context;

		const char_t* next_segment = path_segment_end;

		while (*next_segment == delimiter) ++next_segment;

		if (*path_segment == '.' && path_segment + 1 == path_segment_end)
			return context.first_element_by_path(next_segment, delimiter);
		else if (*path_segment == '.' && *(path_segment+1) == '.' && path_segment + 2 == path_segment_end)
			return context.parent().first_element_by_path(next_segment, delimiter);
		else
		{
			for (xml_node_struct* j = context._root->first_child; j; j = j->next_sibling)
			{
				const char_t* jname = j->name;
				if (jname && impl::strequalrange(jname, path_segment, static_cast<size_t>(path_segment_end - path_segment)))
				{
					xml_node subsearch = xml_node(j).first_element_by_path(next_segment, delimiter);

					if (subsearch) return subsearch;
				}
			}

			return xml_node();
		}
	}

	PUGI_IMPL_FN bool xml_node::traverse(xml_tree_walker& walker)
	{
		walker._depth = -1;

		xml_node arg_begin(_root);
		if (!walker.begin(arg_begin)) return false;

		xml_node_struct* cur = _root ? _root->first_child + 0 : NULL;

		if (cur)
		{
			++walker._depth;

			do
			{
				xml_node arg_for_each(cur);
				if (!walker.for_each(arg_for_each))
					return false;

				if (cur->first_child)
				{
					++walker._depth;
					cur = cur->first_child;
				}
				else if (cur->next_sibling)
					cur = cur->next_sibling;
				else
				{
					while (!cur->next_sibling && cur != _root && cur->parent)
					{
						--walker._depth;
						cur = cur->parent;
					}

					if (cur != _root)
						cur = cur->next_sibling;
				}
			}
			while (cur && cur != _root);
		}

		assert(walker._depth == -1);

		xml_node arg_end(_root);
		return walker.end(arg_end);
	}

	PUGI_IMPL_FN size_t xml_node::hash_value() const
	{
		return reinterpret_cast<uintptr_t>(_root) / sizeof(xml_node_struct);
	}

	PUGI_IMPL_FN xml_node_struct* xml_node::internal_object() const
	{
		return _root;
	}

	PUGI_IMPL_FN void xml_node::print(xml_writer& writer, const char_t* indent, unsigned int flags, xml_encoding encoding, unsigned int depth) const
	{
		if (!_root) return;

		impl::xml_buffered_writer buffered_writer(writer, encoding);

		impl::node_output(buffered_writer, _root, indent, flags, depth);

		buffered_writer.flush();
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN void xml_node::print(std::basic_ostream<char>& stream, const char_t* indent, unsigned int flags, xml_encoding encoding, unsigned int depth) const
	{
		xml_writer_stream writer(stream);

		print(writer, indent, flags, encoding, depth);
	}

	PUGI_IMPL_FN void xml_node::print(std::basic_ostream<wchar_t>& stream, const char_t* indent, unsigned int flags, unsigned int depth) const
	{
		xml_writer_stream writer(stream);

		print(writer, indent, flags, encoding_wchar, depth);
	}
#endif

	PUGI_IMPL_FN ptrdiff_t xml_node::offset_debug() const
	{
		if (!_root) return -1;

		impl::xml_document_struct& doc = impl::get_document(_root);

		// we can determine the offset reliably only if there is exactly once parse buffer
		if (!doc.buffer || doc.extra_buffers) return -1;

		switch (type())
		{
		case node_document:
			return 0;

		case node_element:
		case node_declaration:
		case node_pi:
			return _root->name && (_root->header & impl::xml_memory_page_name_allocated_or_shared_mask) == 0 ? _root->name - doc.buffer : -1;

		case node_pcdata:
		case node_cdata:
		case node_comment:
		case node_doctype:
			return _root->value && (_root->header & impl::xml_memory_page_value_allocated_or_shared_mask) == 0 ? _root->value - doc.buffer : -1;

		default:
			assert(false && "Invalid node type"); // unreachable
			return -1;
		}
	}

#ifdef __BORLANDC__
	PUGI_IMPL_FN bool operator&&(const xml_node& lhs, bool rhs)
	{
		return (bool)lhs && rhs;
	}

	PUGI_IMPL_FN bool operator||(const xml_node& lhs, bool rhs)
	{
		return (bool)lhs || rhs;
	}
#endif

	PUGI_IMPL_FN xml_text::xml_text(xml_node_struct* root): _root(root)
	{
	}

	PUGI_IMPL_FN xml_node_struct* xml_text::_data() const
	{
		if (!_root || impl::is_text_node(_root)) return _root;

		// element nodes can have value if parse_embed_pcdata was used
		if (PUGI_IMPL_NODETYPE(_root) == node_element && _root->value)
			return _root;

		for (xml_node_struct* node = _root->first_child; node; node = node->next_sibling)
			if (impl::is_text_node(node))
				return node;

		return NULL;
	}

	PUGI_IMPL_FN xml_node_struct* xml_text::_data_new()
	{
		xml_node_struct* d = _data();
		if (d) return d;

		return xml_node(_root).append_child(node_pcdata).internal_object();
	}

	PUGI_IMPL_FN xml_text::xml_text(): _root(NULL)
	{
	}

	PUGI_IMPL_FN static void unspecified_bool_xml_text(xml_text***)
	{
	}

	PUGI_IMPL_FN xml_text::operator xml_text::unspecified_bool_type() const
	{
		return _data() ? unspecified_bool_xml_text : NULL;
	}

	PUGI_IMPL_FN bool xml_text::operator!() const
	{
		return !_data();
	}

	PUGI_IMPL_FN bool xml_text::empty() const
	{
		return _data() == NULL;
	}

	PUGI_IMPL_FN const char_t* xml_text::get() const
	{
		xml_node_struct* d = _data();
		if (!d) return PUGIXML_TEXT("");
		const char_t* value = d->value;
		return value ? value : PUGIXML_TEXT("");
	}

	PUGI_IMPL_FN const char_t* xml_text::as_string(const char_t* def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? value : def;
	}

	PUGI_IMPL_FN int xml_text::as_int(int def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_int(value) : def;
	}

	PUGI_IMPL_FN unsigned int xml_text::as_uint(unsigned int def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_uint(value) : def;
	}

	PUGI_IMPL_FN double xml_text::as_double(double def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_double(value) : def;
	}

	PUGI_IMPL_FN float xml_text::as_float(float def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_float(value) : def;
	}

	PUGI_IMPL_FN bool xml_text::as_bool(bool def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_bool(value) : def;
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN long long xml_text::as_llong(long long def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_llong(value) : def;
	}

	PUGI_IMPL_FN unsigned long long xml_text::as_ullong(unsigned long long def) const
	{
		xml_node_struct* d = _data();
		if (!d) return def;
		const char_t* value = d->value;
		return value ? impl::get_value_ullong(value) : def;
	}
#endif

	PUGI_IMPL_FN bool xml_text::set(const char_t* rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::strcpy_insitu(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, impl::strlength(rhs)) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(const char_t* rhs, size_t size)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::strcpy_insitu(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, size) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(int rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned int>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(unsigned int rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned int>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, false) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(long rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned long>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(unsigned long rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned long>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, false) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(float rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_convert(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, default_float_precision) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(float rhs, int precision)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_convert(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, precision) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(double rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_convert(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, default_double_precision) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(double rhs, int precision)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_convert(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, precision) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(bool rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_bool(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs) : false;
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN bool xml_text::set(long long rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned long long>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, rhs < 0) : false;
	}

	PUGI_IMPL_FN bool xml_text::set(unsigned long long rhs)
	{
		xml_node_struct* dn = _data_new();

		return dn ? impl::set_value_integer<unsigned long long>(dn->value, dn->header, impl::xml_memory_page_value_allocated_mask, rhs, false) : false;
	}
#endif

	PUGI_IMPL_FN xml_text& xml_text::operator=(const char_t* rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(int rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(unsigned int rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(long rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(unsigned long rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(double rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(float rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(bool rhs)
	{
		set(rhs);
		return *this;
	}

#ifdef PUGIXML_HAS_LONG_LONG
	PUGI_IMPL_FN xml_text& xml_text::operator=(long long rhs)
	{
		set(rhs);
		return *this;
	}

	PUGI_IMPL_FN xml_text& xml_text::operator=(unsigned long long rhs)
	{
		set(rhs);
		return *this;
	}
#endif

	PUGI_IMPL_FN xml_node xml_text::data() const
	{
		return xml_node(_data());
	}

#ifdef __BORLANDC__
	PUGI_IMPL_FN bool operator&&(const xml_text& lhs, bool rhs)
	{
		return (bool)lhs && rhs;
	}

	PUGI_IMPL_FN bool operator||(const xml_text& lhs, bool rhs)
	{
		return (bool)lhs || rhs;
	}
#endif

	PUGI_IMPL_FN xml_node_iterator::xml_node_iterator()
	{
	}

	PUGI_IMPL_FN xml_node_iterator::xml_node_iterator(const xml_node& node): _wrap(node), _parent(node.parent())
	{
	}

	PUGI_IMPL_FN xml_node_iterator::xml_node_iterator(xml_node_struct* ref, xml_node_struct* parent): _wrap(ref), _parent(parent)
	{
	}

	PUGI_IMPL_FN bool xml_node_iterator::operator==(const xml_node_iterator& rhs) const
	{
		return _wrap._root == rhs._wrap._root && _parent._root == rhs._parent._root;
	}

	PUGI_IMPL_FN bool xml_node_iterator::operator!=(const xml_node_iterator& rhs) const
	{
		return _wrap._root != rhs._wrap._root || _parent._root != rhs._parent._root;
	}

	PUGI_IMPL_FN xml_node& xml_node_iterator::operator*() const
	{
		assert(_wrap._root);
		return _wrap;
	}

	PUGI_IMPL_FN xml_node* xml_node_iterator::operator->() const
	{
		assert(_wrap._root);
		return &_wrap;
	}

	PUGI_IMPL_FN xml_node_iterator& xml_node_iterator::operator++()
	{
		assert(_wrap._root);
		_wrap._root = _wrap._root->next_sibling;
		return *this;
	}

	PUGI_IMPL_FN xml_node_iterator xml_node_iterator::operator++(int)
	{
		xml_node_iterator temp = *this;
		++*this;
		return temp;
	}

	PUGI_IMPL_FN xml_node_iterator& xml_node_iterator::operator--()
	{
		_wrap = _wrap._root ? _wrap.previous_sibling() : _parent.last_child();
		return *this;
	}

	PUGI_IMPL_FN xml_node_iterator xml_node_iterator::operator--(int)
	{
		xml_node_iterator temp = *this;
		--*this;
		return temp;
	}

	PUGI_IMPL_FN xml_attribute_iterator::xml_attribute_iterator()
	{
	}

	PUGI_IMPL_FN xml_attribute_iterator::xml_attribute_iterator(const xml_attribute& attr, const xml_node& parent): _wrap(attr), _parent(parent)
	{
	}

	PUGI_IMPL_FN xml_attribute_iterator::xml_attribute_iterator(xml_attribute_struct* ref, xml_node_struct* parent): _wrap(ref), _parent(parent)
	{
	}

	PUGI_IMPL_FN bool xml_attribute_iterator::operator==(const xml_attribute_iterator& rhs) const
	{
		return _wrap._attr == rhs._wrap._attr && _parent._root == rhs._parent._root;
	}

	PUGI_IMPL_FN bool xml_attribute_iterator::operator!=(const xml_attribute_iterator& rhs) const
	{
		return _wrap._attr != rhs._wrap._attr || _parent._root != rhs._parent._root;
	}

	PUGI_IMPL_FN xml_attribute& xml_attribute_iterator::operator*() const
	{
		assert(_wrap._attr);
		return _wrap;
	}

	PUGI_IMPL_FN xml_attribute* xml_attribute_iterator::operator->() const
	{
		assert(_wrap._attr);
		return &_wrap;
	}

	PUGI_IMPL_FN xml_attribute_iterator& xml_attribute_iterator::operator++()
	{
		assert(_wrap._attr);
		_wrap._attr = _wrap._attr->next_attribute;
		return *this;
	}

	PUGI_IMPL_FN xml_attribute_iterator xml_attribute_iterator::operator++(int)
	{
		xml_attribute_iterator temp = *this;
		++*this;
		return temp;
	}

	PUGI_IMPL_FN xml_attribute_iterator& xml_attribute_iterator::operator--()
	{
		_wrap = _wrap._attr ? _wrap.previous_attribute() : _parent.last_attribute();
		return *this;
	}

	PUGI_IMPL_FN xml_attribute_iterator xml_attribute_iterator::operator--(int)
	{
		xml_attribute_iterator temp = *this;
		--*this;
		return temp;
	}

	PUGI_IMPL_FN xml_named_node_iterator::xml_named_node_iterator(): _name(NULL)
	{
	}

	PUGI_IMPL_FN xml_named_node_iterator::xml_named_node_iterator(const xml_node& node, const char_t* name): _wrap(node), _parent(node.parent()), _name(name)
	{
	}

	PUGI_IMPL_FN xml_named_node_iterator::xml_named_node_iterator(xml_node_struct* ref, xml_node_struct* parent, const char_t* name): _wrap(ref), _parent(parent), _name(name)
	{
	}

	PUGI_IMPL_FN bool xml_named_node_iterator::operator==(const xml_named_node_iterator& rhs) const
	{
		return _wrap._root == rhs._wrap._root && _parent._root == rhs._parent._root;
	}

	PUGI_IMPL_FN bool xml_named_node_iterator::operator!=(const xml_named_node_iterator& rhs) const
	{
		return _wrap._root != rhs._wrap._root || _parent._root != rhs._parent._root;
	}

	PUGI_IMPL_FN xml_node& xml_named_node_iterator::operator*() const
	{
		assert(_wrap._root);
		return _wrap;
	}

	PUGI_IMPL_FN xml_node* xml_named_node_iterator::operator->() const
	{
		assert(_wrap._root);
		return &_wrap;
	}

	PUGI_IMPL_FN xml_named_node_iterator& xml_named_node_iterator::operator++()
	{
		assert(_wrap._root);
		_wrap = _wrap.next_sibling(_name);
		return *this;
	}

	PUGI_IMPL_FN xml_named_node_iterator xml_named_node_iterator::operator++(int)
	{
		xml_named_node_iterator temp = *this;
		++*this;
		return temp;
	}

	PUGI_IMPL_FN xml_named_node_iterator& xml_named_node_iterator::operator--()
	{
		if (_wrap._root)
			_wrap = _wrap.previous_sibling(_name);
		else
		{
			_wrap = _parent.last_child();

			if (!impl::strequal(_wrap.name(), _name))
				_wrap = _wrap.previous_sibling(_name);
		}

		return *this;
	}

	PUGI_IMPL_FN xml_named_node_iterator xml_named_node_iterator::operator--(int)
	{
		xml_named_node_iterator temp = *this;
		--*this;
		return temp;
	}

	PUGI_IMPL_FN xml_parse_result::xml_parse_result(): status(status_internal_error), offset(0), encoding(encoding_auto)
	{
	}

	PUGI_IMPL_FN xml_parse_result::operator bool() const
	{
		return status == status_ok;
	}

	PUGI_IMPL_FN const char* xml_parse_result::description() const
	{
		switch (status)
		{
		case status_ok: return "No error";

		case status_file_not_found: return "File was not found";
		case status_io_error: return "Error reading from file/stream";
		case status_out_of_memory: return "Could not allocate memory";
		case status_internal_error: return "Internal error occurred";

		case status_unrecognized_tag: return "Could not determine tag type";

		case status_bad_pi: return "Error parsing document declaration/processing instruction";
		case status_bad_comment: return "Error parsing comment";
		case status_bad_cdata: return "Error parsing CDATA section";
		case status_bad_doctype: return "Error parsing document type declaration";
		case status_bad_pcdata: return "Error parsing PCDATA section";
		case status_bad_start_element: return "Error parsing start element tag";
		case status_bad_attribute: return "Error parsing element attribute";
		case status_bad_end_element: return "Error parsing end element tag";
		case status_end_element_mismatch: return "Start-end tags mismatch";

		case status_append_invalid_root: return "Unable to append nodes: root is not an element or document";

		case status_no_document_element: return "No document element found";

		default: return "Unknown error";
		}
	}

	PUGI_IMPL_FN xml_document::xml_document(): _buffer(NULL)
	{
		_create();
	}

	PUGI_IMPL_FN xml_document::~xml_document()
	{
		_destroy();
	}

#ifdef PUGIXML_HAS_MOVE
	PUGI_IMPL_FN xml_document::xml_document(xml_document&& rhs) PUGIXML_NOEXCEPT_IF_NOT_COMPACT: _buffer(NULL)
	{
		_create();
		_move(rhs);
	}

	PUGI_IMPL_FN xml_document& xml_document::operator=(xml_document&& rhs) PUGIXML_NOEXCEPT_IF_NOT_COMPACT
	{
		if (this == &rhs) return *this;

		_destroy();
		_create();
		_move(rhs);

		return *this;
	}
#endif

	PUGI_IMPL_FN void xml_document::reset()
	{
		_destroy();
		_create();
	}

	PUGI_IMPL_FN void xml_document::reset(const xml_document& proto)
	{
		reset();

		impl::node_copy_tree(_root, proto._root);
	}

	PUGI_IMPL_FN void xml_document::_create()
	{
		assert(!_root);

	#ifdef PUGIXML_COMPACT
		// space for page marker for the first page (uint32_t), rounded up to pointer size; assumes pointers are at least 32-bit
		const size_t page_offset = sizeof(void*);
	#else
		const size_t page_offset = 0;
	#endif

		// initialize sentinel page
		PUGI_IMPL_STATIC_ASSERT(sizeof(impl::xml_memory_page) + sizeof(impl::xml_document_struct) + page_offset <= sizeof(_memory));

		// prepare page structure
		impl::xml_memory_page* page = impl::xml_memory_page::construct(_memory);
		assert(page);

		page->busy_size = impl::xml_memory_page_size;

		// setup first page marker
	#ifdef PUGIXML_COMPACT
		// round-trip through void* to avoid 'cast increases required alignment of target type' warning
		page->compact_page_marker = reinterpret_cast<uint32_t*>(static_cast<void*>(reinterpret_cast<char*>(page) + sizeof(impl::xml_memory_page)));
		*page->compact_page_marker = sizeof(impl::xml_memory_page);
	#endif

		// allocate new root
		_root = new (reinterpret_cast<char*>(page) + sizeof(impl::xml_memory_page) + page_offset) impl::xml_document_struct(page);
		_root->prev_sibling_c = _root;

		// setup sentinel page
		page->allocator = static_cast<impl::xml_document_struct*>(_root);

		// setup hash table pointer in allocator
	#ifdef PUGIXML_COMPACT
		page->allocator->_hash = &static_cast<impl::xml_document_struct*>(_root)->hash;
	#endif

		// verify the document allocation
		assert(reinterpret_cast<char*>(_root) + sizeof(impl::xml_document_struct) <= _memory + sizeof(_memory));
	}

	PUGI_IMPL_FN void xml_document::_destroy()
	{
		assert(_root);

		// destroy static storage
		if (_buffer)
		{
			impl::xml_memory::deallocate(_buffer);
			_buffer = NULL;
		}

		// destroy extra buffers (note: no need to destroy linked list nodes, they're allocated using document allocator)
		for (impl::xml_extra_buffer* extra = static_cast<impl::xml_document_struct*>(_root)->extra_buffers; extra; extra = extra->next)
		{
			if (extra->buffer) impl::xml_memory::deallocate(extra->buffer);
		}

		// destroy dynamic storage, leave sentinel page (it's in static memory)
		impl::xml_memory_page* root_page = PUGI_IMPL_GETPAGE(_root);
		assert(root_page && !root_page->prev);
		assert(reinterpret_cast<char*>(root_page) >= _memory && reinterpret_cast<char*>(root_page) < _memory + sizeof(_memory));

		for (impl::xml_memory_page* page = root_page->next; page; )
		{
			impl::xml_memory_page* next = page->next;

			impl::xml_allocator::deallocate_page(page);

			page = next;
		}

	#ifdef PUGIXML_COMPACT
		// destroy hash table
		static_cast<impl::xml_document_struct*>(_root)->hash.clear();
	#endif

		_root = NULL;
	}

#ifdef PUGIXML_HAS_MOVE
	PUGI_IMPL_FN void xml_document::_move(xml_document& rhs) PUGIXML_NOEXCEPT_IF_NOT_COMPACT
	{
		impl::xml_document_struct* doc = static_cast<impl::xml_document_struct*>(_root);
		impl::xml_document_struct* other = static_cast<impl::xml_document_struct*>(rhs._root);

		// save first child pointer for later; this needs hash access
		xml_node_struct* other_first_child = other->first_child;

	#ifdef PUGIXML_COMPACT
		// reserve space for the hash table up front; this is the only operation that can fail
		// if it does, we have no choice but to throw (if we have exceptions)
		if (other_first_child)
		{
			size_t other_children = 0;
			for (xml_node_struct* node = other_first_child; node; node = node->next_sibling)
				other_children++;

			// in compact mode, each pointer assignment could result in a hash table request
			// during move, we have to relocate document first_child and parents of all children
			// normally there's just one child and its parent has a pointerless encoding but
			// we assume the worst here
			if (!other->_hash->reserve(other_children + 1))
			{
			#ifdef PUGIXML_NO_EXCEPTIONS
				return;
			#else
				throw std::bad_alloc();
			#endif
			}
		}
	#endif

		// move allocation state
		// note that other->_root may point to the embedded document page, in which case we should keep original (empty) state
		if (other->_root != PUGI_IMPL_GETPAGE(other))
		{
			doc->_root = other->_root;
			doc->_busy_size = other->_busy_size;
		}

		// move buffer state
		doc->buffer = other->buffer;
		doc->extra_buffers = other->extra_buffers;
		_buffer = rhs._buffer;

	#ifdef PUGIXML_COMPACT
		// move compact hash; note that the hash table can have pointers to other but they will be "inactive", similarly to nodes removed with remove_child
		doc->hash = other->hash;
		doc->_hash = &doc->hash;

		// make sure we don't access other hash up until the end when we reinitialize other document
		other->_hash = NULL;
	#endif

		// move page structure
		impl::xml_memory_page* doc_page = PUGI_IMPL_GETPAGE(doc);
		assert(doc_page && !doc_page->prev && !doc_page->next);

		impl::xml_memory_page* other_page = PUGI_IMPL_GETPAGE(other);
		assert(other_page && !other_page->prev);

		// relink pages since root page is embedded into xml_document
		if (impl::xml_memory_page* page = other_page->next)
		{
			assert(page->prev == other_page);

			page->prev = doc_page;

			doc_page->next = page;
			other_page->next = NULL;
		}

		// make sure pages point to the correct document state
		for (impl::xml_memory_page* page = doc_page->next; page; page = page->next)
		{
			assert(page->allocator == other);

			page->allocator = doc;

		#ifdef PUGIXML_COMPACT
			// this automatically migrates most children between documents and prevents ->parent assignment from allocating
			if (page->compact_shared_parent == other)
				page->compact_shared_parent = doc;
		#endif
		}

		// move tree structure
		assert(!doc->first_child);

		doc->first_child = other_first_child;

		for (xml_node_struct* node = other_first_child; node; node = node->next_sibling)
		{
		#ifdef PUGIXML_COMPACT
			// most children will have migrated when we reassigned compact_shared_parent
			assert(node->parent == other || node->parent == doc);

			node->parent = doc;
		#else
			assert(node->parent == other);
			node->parent = doc;
		#endif
		}

		// reset other document
		new (other) impl::xml_document_struct(PUGI_IMPL_GETPAGE(other));
		rhs._buffer = NULL;
	}
#endif

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN xml_parse_result xml_document::load(std::basic_istream<char>& stream, unsigned int options, xml_encoding encoding)
	{
		reset();

		return impl::load_stream_impl(static_cast<impl::xml_document_struct*>(_root), stream, options, encoding, &_buffer);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load(std::basic_istream<wchar_t>& stream, unsigned int options)
	{
		reset();

		return impl::load_stream_impl(static_cast<impl::xml_document_struct*>(_root), stream, options, encoding_wchar, &_buffer);
	}
#endif

	PUGI_IMPL_FN xml_parse_result xml_document::load_string(const char_t* contents, unsigned int options)
	{
		// Force native encoding (skip autodetection)
	#ifdef PUGIXML_WCHAR_MODE
		xml_encoding encoding = encoding_wchar;
	#else
		xml_encoding encoding = encoding_utf8;
	#endif

		return load_buffer(contents, impl::strlength(contents) * sizeof(char_t), options, encoding);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load(const char_t* contents, unsigned int options)
	{
		return load_string(contents, options);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load_file(const char* path_, unsigned int options, xml_encoding encoding)
	{
		reset();

		using impl::auto_deleter; // MSVC7 workaround
		auto_deleter<FILE> file(impl::open_file(path_, "rb"), impl::close_file);

		return impl::load_file_impl(static_cast<impl::xml_document_struct*>(_root), file.data, options, encoding, &_buffer);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load_file(const wchar_t* path_, unsigned int options, xml_encoding encoding)
	{
		reset();

		using impl::auto_deleter; // MSVC7 workaround
		auto_deleter<FILE> file(impl::open_file_wide(path_, L"rb"), impl::close_file);

		return impl::load_file_impl(static_cast<impl::xml_document_struct*>(_root), file.data, options, encoding, &_buffer);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load_buffer(const void* contents, size_t size, unsigned int options, xml_encoding encoding)
	{
		reset();

		return impl::load_buffer_impl(static_cast<impl::xml_document_struct*>(_root), _root, const_cast<void*>(contents), size, options, encoding, false, false, &_buffer);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load_buffer_inplace(void* contents, size_t size, unsigned int options, xml_encoding encoding)
	{
		reset();

		return impl::load_buffer_impl(static_cast<impl::xml_document_struct*>(_root), _root, contents, size, options, encoding, true, false, &_buffer);
	}

	PUGI_IMPL_FN xml_parse_result xml_document::load_buffer_inplace_own(void* contents, size_t size, unsigned int options, xml_encoding encoding)
	{
		reset();

		return impl::load_buffer_impl(static_cast<impl::xml_document_struct*>(_root), _root, contents, size, options, encoding, true, true, &_buffer);
	}

	PUGI_IMPL_FN void xml_document::save(xml_writer& writer, const char_t* indent, unsigned int flags, xml_encoding encoding) const
	{
		impl::xml_buffered_writer buffered_writer(writer, encoding);

		if ((flags & format_write_bom) && encoding != encoding_latin1)
		{
			// BOM always represents the codepoint U+FEFF, so just write it in native encoding
		#ifdef PUGIXML_WCHAR_MODE
			unsigned int bom = 0xfeff;
			buffered_writer.write(static_cast<wchar_t>(bom));
		#else
			buffered_writer.write('\xef', '\xbb', '\xbf');
		#endif
		}

		if (!(flags & format_no_declaration) && !impl::has_declaration(_root))
		{
			buffered_writer.write_string(PUGIXML_TEXT("<?xml version=\"1.0\""));
			if (encoding == encoding_latin1) buffered_writer.write_string(PUGIXML_TEXT(" encoding=\"ISO-8859-1\""));
			buffered_writer.write('?', '>');
			if (!(flags & format_raw)) buffered_writer.write('\n');
		}

		impl::node_output(buffered_writer, _root, indent, flags, 0);

		buffered_writer.flush();
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN void xml_document::save(std::basic_ostream<char>& stream, const char_t* indent, unsigned int flags, xml_encoding encoding) const
	{
		xml_writer_stream writer(stream);

		save(writer, indent, flags, encoding);
	}

	PUGI_IMPL_FN void xml_document::save(std::basic_ostream<wchar_t>& stream, const char_t* indent, unsigned int flags) const
	{
		xml_writer_stream writer(stream);

		save(writer, indent, flags, encoding_wchar);
	}
#endif

	PUGI_IMPL_FN bool xml_document::save_file(const char* path_, const char_t* indent, unsigned int flags, xml_encoding encoding) const
	{
		using impl::auto_deleter; // MSVC7 workaround
		auto_deleter<FILE> file(impl::open_file(path_, (flags & format_save_file_text) ? "w" : "wb"), impl::close_file);

		return impl::save_file_impl(*this, file.data, indent, flags, encoding) && fclose(file.release()) == 0;
	}

	PUGI_IMPL_FN bool xml_document::save_file(const wchar_t* path_, const char_t* indent, unsigned int flags, xml_encoding encoding) const
	{
		using impl::auto_deleter; // MSVC7 workaround
		auto_deleter<FILE> file(impl::open_file_wide(path_, (flags & format_save_file_text) ? L"w" : L"wb"), impl::close_file);

		return impl::save_file_impl(*this, file.data, indent, flags, encoding) && fclose(file.release()) == 0;
	}

	PUGI_IMPL_FN xml_node xml_document::document_element() const
	{
		assert(_root);

		for (xml_node_struct* i = _root->first_child; i; i = i->next_sibling)
			if (PUGI_IMPL_NODETYPE(i) == node_element)
				return xml_node(i);

		return xml_node();
	}

#ifndef PUGIXML_NO_STL
	PUGI_IMPL_FN std::string PUGIXML_FUNCTION as_utf8(const wchar_t* str)
	{
		assert(str);

		return impl::as_utf8_impl(str, impl::strlength_wide(str));
	}

	PUGI_IMPL_FN std::string PUGIXML_FUNCTION as_utf8(const std::basic_string<wchar_t>& str)
	{
		return impl::as_utf8_impl(str.c_str(), str.size());
	}

	PUGI_IMPL_FN std::basic_string<wchar_t> PUGIXML_FUNCTION as_wide(const char* str)
	{
		assert(str);

		return impl::as_wide_impl(str, strlen(str));
	}

	PUGI_IMPL_FN std::basic_string<wchar_t> PUGIXML_FUNCTION as_wide(const std::string& str)
	{
		return impl::as_wide_impl(str.c_str(), str.size());
	}
#endif

	PUGI_IMPL_FN void PUGIXML_FUNCTION set_memory_management_functions(allocation_function allocate, deallocation_function deallocate)
	{
		impl::xml_memory::allocate = allocate;
		impl::xml_memory::deallocate = deallocate;
	}

	PUGI_IMPL_FN allocation_function PUGIXML_FUNCTION get_memory_allocation_function()
	{
		return impl::xml_memory::allocate;
	}

	PUGI_IMPL_FN deallocation_function PUGIXML_FUNCTION get_memory_deallocation_function()
	{
		return impl::xml_memory::deallocate;
	}
}

#if !defined(PUGIXML_NO_STL) && (defined(_MSC_VER) || defined(__ICC))
namespace std
{
	// Workarounds for (non-standard) iterator category detection for older versions (MSVC7/IC8 and earlier)
	PUGI_IMPL_FN std::bidirectional_iterator_tag _Iter_cat(const pugi::xml_node_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}

	PUGI_IMPL_FN std::bidirectional_iterator_tag _Iter_cat(const pugi::xml_attribute_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}

	PUGI_IMPL_FN std::bidirectional_iterator_tag _Iter_cat(const pugi::xml_named_node_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}
}
#endif

#if !defined(PUGIXML_NO_STL) && defined(__SUNPRO_CC)
namespace std
{
	// Workarounds for (non-standard) iterator category detection
	PUGI_IMPL_FN std::bidirectional_iterator_tag __iterator_category(const pugi::xml_node_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}

	PUGI_IMPL_FN std::bidirectional_iterator_tag __iterator_category(const pugi::xml_attribute_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}

	PUGI_IMPL_FN std::bidirectional_iterator_tag __iterator_category(const pugi::xml_named_node_iterator&)
	{
		return std::bidirectional_iterator_tag();
	}
}
#endif

#ifdef __BORLANDC__
#	pragma option pop
#endif

#if defined(_MSC_VER) && defined(__c2__)
#	pragma clang diagnostic pop
#endif

#if defined(__clang__)
#	pragma clang diagnostic pop
#endif

// Intel C++ does not properly keep warning state for function templates,
// so popping warning state at the end of translation unit leads to warnings in the middle.
#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#	pragma warning(pop)
#endif

// Undefine all local macros (makes sure we're not leaking macros in header-only mode)
#undef PUGI_IMPL_NO_INLINE
#undef PUGI_IMPL_UNLIKELY
#undef PUGI_IMPL_STATIC_ASSERT
#undef PUGI_IMPL_DMC_VOLATILE
#undef PUGI_IMPL_UNSIGNED_OVERFLOW
#undef PUGI_IMPL_MSVC_CRT_VERSION
#undef PUGI_IMPL_SNPRINTF
#undef PUGI_IMPL_NS_BEGIN
#undef PUGI_IMPL_NS_END
#undef PUGI_IMPL_FN
#undef PUGI_IMPL_FN_NO_INLINE
#undef PUGI_IMPL_GETHEADER_IMPL
#undef PUGI_IMPL_GETPAGE_IMPL
#undef PUGI_IMPL_GETPAGE
#undef PUGI_IMPL_NODETYPE
#undef PUGI_IMPL_IS_CHARTYPE_IMPL
#undef PUGI_IMPL_IS_CHARTYPE
#undef PUGI_IMPL_IS_CHARTYPEX
#undef PUGI_IMPL_ENDSWITH
#undef PUGI_IMPL_SKIPWS
#undef PUGI_IMPL_OPTSET
#undef PUGI_IMPL_PUSHNODE
#undef PUGI_IMPL_POPNODE
#undef PUGI_IMPL_SCANFOR
#undef PUGI_IMPL_SCANWHILE
#undef PUGI_IMPL_SCANWHILE_UNROLL
#undef PUGI_IMPL_ENDSEG
#undef PUGI_IMPL_THROW_ERROR
#undef PUGI_IMPL_CHECK_ERROR

#endif

/**
 * Copyright (c) 2006-2024 Arseny Kapoulkine
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
