#include "atomic_list.hpp"
#include "node_pool.hpp"

namespace otf_gc
{
  template <typename T>
  node_pool<list<T>>& list_pool()
  {
    static thread_local node_pool<list<T>> pool;
    return pool;
  }

  template <typename T>
  void* list_node<T>::operator new(std::size_t)
  {
    return list_pool<T>().get();
  }

  template <typename T>
  void list_node<T>::operator delete(void* ptr, std::size_t)
  {
    list_pool<T>().put(ptr);
  }
  
  template class list<void*>;
  template class atomic_list<list<void*>>;

  template struct list_node<void*>;
  template struct list_node<list<void*>>;

  template node_pool<list<void*>>& list_pool<void*>();
  template node_pool<list<list<void*>>>& list_pool<list<void*>>();
}
