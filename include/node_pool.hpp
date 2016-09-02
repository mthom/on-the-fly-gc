#ifndef NODE_POOL_HPP_INCLUDED

#include <cstdlib>

#include "atomic_list.hpp"
#include "impl_details.hpp"

namespace otf_gc
{
  template <class List>
  class node_pool
  {
  private:
    void* chunk;
    size_t offset;

    list<void*> allocation_dump;
    
    List pool;
  public:
    node_pool()
      : chunk(nullptr)
      , offset(0)
    {}
    
    void* get()
    {
      if(pool.empty())
      {
	if(!chunk || offset == impl_details::pool_chunk_size * sizeof(typename List::node_type))
	{
	  chunk = aligned_alloc(alignof(typename List::node_type),
				impl_details::pool_chunk_size * sizeof(typename List::node_type));
	  offset = 0;

	  allocation_dump.push_front(chunk);
	}

	auto node = reinterpret_cast<size_t>(chunk) + offset;
	offset += sizeof(typename List::node_type);
	return reinterpret_cast<void*>(node);
      } else {
	auto node = pool.front();
	pool.pop_front();
	return reinterpret_cast<void*>(node);
      }
    }

    inline void put(void* node)
    {
      pool.push_front(reinterpret_cast<typename List::node_type*>(node));
    }

    inline list<void*> reset_allocation_dump()
    {
      list<void*> result(allocation_dump);
      allocation_dump.reset();
      
      return result;
    }
  };
}
#endif
