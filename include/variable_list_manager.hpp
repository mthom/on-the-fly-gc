#ifndef VARIABLE_LIST_MANAGER_HPP_INCLUDED
#define VARIABLE_LIST_MANAGER_HPP_INCLUDED

#include <cassert>

#include "large_block_list.hpp"

namespace otf_gc
{
  class variable_list_manager
  {
  private:
    large_block_list used_list;
  public:
    inline large_block_list release_used_list()
    {
      auto result = used_list;
      used_list.reset();
      return result;
    }
    
    inline void push_front_used(void* blk)
    {
      assert(blk != nullptr);
      used_list.push_front(blk);
    }
  };  
}
#endif
