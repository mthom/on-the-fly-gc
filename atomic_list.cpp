#include "atomic_list.hpp"
#include "large_block_list.hpp"
#include "node_pool.hpp"
#include "stub_list.hpp"

namespace otf_gc
{
  template <typename T>
  node_pool<atomic_list<T>>& atomic_list_pool()
  {
    static thread_local node_pool<atomic_list<T>> pool;
    return pool;
  }
  
  template <typename T>
  node_pool<list<T>>& list_pool()
  {
    static thread_local node_pool<list<T>> pool;
    return pool;
  }

  template <typename T>
  void* list<T>::node_type::operator new(std::size_t)
  {
    return list_pool<T>().get();
  }

  template <typename T>
  void list<T>::node_type::operator delete(void* ptr, std::size_t)
  {
    list_pool<T>().put(ptr);
  }
  
  template <typename T>
  void* atomic_list<T>::node_type::operator new(std::size_t)
  {
    return atomic_list_pool<T>().get();
  }

  template <typename T>
  void atomic_list<T>::node_type::operator delete(void* ptr, std::size_t)
  {
    atomic_list_pool<T>().put(ptr);
  }
  
  template class list<void*>;
  
  template class atomic_list<list<void*>>;
  template class atomic_list<large_block_list>;
  template class atomic_list<stub_list>;

  template node_pool<list<void*>>& list_pool<void*>();
  template node_pool<list<list<void*>>>& list_pool<list<void*>>();

  template node_pool<atomic_list<list<void*>>>& atomic_list_pool<list<void*>>();
  template node_pool<atomic_list<large_block_list>>& atomic_list_pool<large_block_list>();
  template node_pool<atomic_list<stub_list>>& atomic_list_pool<stub_list>();
}
