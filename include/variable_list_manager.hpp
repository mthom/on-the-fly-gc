#ifndef VARIABLE_LIST_MANAGER_HPP_INCLUDED
#define VARIABLE_LIST_MANAGER_HPP_INCLUDED

#include <cassert>
#include <memory>

#include "large_block_list.hpp"

namespace otf_gc
{
  class variable_list_manager
  {
  private:
    std::unique_ptr<large_block_list> free_list;
  public:
    std::unique_ptr<large_block_list> used_list;
    
    variable_list_manager()
      : free_list(std::make_unique<large_block_list>())
      , used_list(std::make_unique<large_block_list>())
    {}

    inline large_block_list* release_free_list()
    {
      return free_list.release();
    }

    inline large_block_list* release_used_list()
    {
      return used_list.release();
    }
    
    inline void push_front_used(void* blk, size_t sz, size_t shift = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      used_list->push_front(blk, sz, shift);
    }

    inline void push_front_free(void* blk, size_t sz, size_t shift = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      free_list->push_front(blk, sz, shift);
    }

    inline void append(large_block_list* bl)
    {
      assert(bl != nullptr);
      free_list->append(bl);
    }

    inline block_cursor get_block(int sz)
    {
      block_cursor blk_c(free_list->get_block(sz));

      if(!blk_c.null_block())
	used_list->push_back(reinterpret_cast<void*>(blk_c.start()),
			     blk_c.size(),
			     blk_c.split());
      
      return blk_c;
    }
  };  
}
#endif
