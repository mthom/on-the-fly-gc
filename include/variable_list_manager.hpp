#ifndef VARIABLE_LIST_MANAGER_HPP_INCLUDED
#define VARIABLE_LIST_MANAGER_HPP_INCLUDED

#include <cassert>

#include "large_block_list.hpp"

namespace otf_gc
{
  class variable_list_manager
  {
  private:
    large_block_list free_list, used_list;
  public:
    inline large_block_list release_free_list()
    {
      auto result = free_list;
      free_list.reset();
      return result;
    }

    inline large_block_list release_used_list()
    {
      auto result = used_list;
      used_list.reset();
      return result;
    }
    
    inline void push_front_used(void* blk, size_t sz, size_t shift = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      used_list.push_front(blk, sz, shift);
    }

    inline void push_front_free(void* blk, size_t sz, size_t shift = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      free_list.push_front(blk, sz, shift);
    }

    inline void append(large_block_list&& bl)
    {
      free_list.append(std::move(bl));
    }

    inline block_cursor get_block(int sz)
    {
      block_cursor blk_c(free_list.get_block(sz));

      if(!blk_c.null_block())
	used_list.push_back(reinterpret_cast<void*>(blk_c.start()),
			    blk_c.size(),
			    blk_c.split());
      
      return blk_c;
    }
  };  
}
#endif
