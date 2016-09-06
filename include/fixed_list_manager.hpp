#ifndef FIXED_LIST_MANAGER_HPP_INCLUDED
#define FIXED_LIST_MANAGER_HPP_INCLUDED

#include <atomic>
#include <cstdlib>
#include <memory>

#include "impl_details.hpp"
#include "stub_list.hpp"

namespace otf_gc
{
  class fixed_list_manager
  {
  private:
    stub* alloc;
    size_t offset;
    const size_t obj_size;
    size_t log_multiplier;

    stub_list free_list;
  public:
    stub_list used_list;

    fixed_list_manager(size_t size_)
      : alloc{nullptr}
      , offset{0}
      , obj_size{size_}
      , log_multiplier{3}
    {}

    inline stub_list release_free_list() {
      auto result = free_list;
      free_list.reset();
      return result;
    }

    inline stub_list release_used_list() {
      auto result = used_list;
      used_list.reset();
      return result;
    }

    // sz here is the size in bytes.
    inline void push_front(void* blk, size_t sz)
    {
      assert(blk != nullptr);
      stub* st = new stub(blk, sz);

      if(alloc)
	free_list.push_back(st);
      else
	alloc = st;
    }

    inline void append(stub_list&& sl)
    {
      free_list.append(std::move(sl));
    }

    inline void* get_block()
    {
      if(!alloc)
	return nullptr;

      assert(alloc->start != nullptr);

      void* ptr = nullptr;
      std::ptrdiff_t st = reinterpret_cast<std::ptrdiff_t>(alloc->start);

      if(offset < alloc->size) {
	ptr = reinterpret_cast<void*>(st + offset);
	offset += (1ULL << obj_size);
      } else {
	assert(offset == alloc->size);
	alloc = free_list.front();

	if(alloc) {
	  free_list.pop_front();
	  ptr = alloc->start;
	  offset = (1 << obj_size);
	}
      }

      if(ptr)
	used_list.push_back(new stub(ptr, 1 << obj_size));

      return ptr;
    }

    inline void* get_new_block()
    {
      assert(!alloc);

      void* blk = aligned_alloc(alignof(impl_details::header_t), 1ULL << (obj_size + log_multiplier));
      push_front(blk, 1ULL << (obj_size + log_multiplier));

      if(obj_size + log_multiplier < impl_details::small_block_size_limit + obj_size)
	++log_multiplier;

      offset = 1 << obj_size;

      if(alloc->start)
	used_list.push_back(new stub(alloc->start, 1ULL << obj_size));

      return alloc->start;
    }
  };
}
#endif
