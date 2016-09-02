#ifndef NODE_POOL_HPP_INCLUDED
#define NODE_POOL_HPP_INCLUDED

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
      static constexpr size_t sz =
	impl_details::pool_chunk_size * sizeof(typename List::node_type) + sizeof(list_node<void*>);
      
      if(pool.empty())
      {		
	if(!chunk || offset == sz)	                       
	{
	  chunk = aligned_alloc(alignof(list_node<void*>), sz);
	  offset = sizeof(list_node<void*>);

	  allocation_dump.node_push_front(reinterpret_cast<list_node<void*>*>(chunk));
	}

	size_t node = reinterpret_cast<size_t>(chunk) + offset;
	offset += sizeof(typename List::node_type);
	return reinterpret_cast<void*>(node);
      } else {
	auto node = pool.front_ptr();
	pool.node_pop_front();
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
