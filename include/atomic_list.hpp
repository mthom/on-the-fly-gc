#ifndef ATOMIC_LIST_HPP_INCLUDED
#define ATOMIC_LIST_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <initializer_list>
#include <utility>

namespace otf_gc
{
  template <typename T>
  class list
  {
  private:
    struct list_node
    {
      T data;
      list_node* next;

      static void* operator new(std::size_t);
      static void operator delete(void*, std::size_t);
    };
  
    struct list_node_iterator
    {
      list_node* focus;

      list_node_iterator operator++(int) const
      {
	return list_node_iterator { focus->next };
      }
      
      list_node_iterator& operator++()
      {
	focus = focus->next;
	return *this;
      }

      T operator*()
      {
	return focus->data;
      }
    
      list_node_iterator& operator=(const list_node_iterator& it)
      {
	focus = it.focus;
	return *this;
      }

      inline bool operator==(const list_node_iterator& it)
      {
	return focus == it.focus;
      }

      inline bool operator!=(const list_node_iterator& it)
      {
	return focus != it.focus;
      }      
    };
      
    list_node* head;
    list_node* tail;    
  public:
    using node_type = list_node;
    
    list() noexcept : head(nullptr), tail(nullptr) {}
    list(list_node* head_) noexcept : head(head_), tail(head_) {}     
    list(list_node* head_, list_node* tail_) noexcept : head(head_), tail(tail_) {}
    list(std::initializer_list<T> lst) : head(nullptr), tail(nullptr)
    {
      for(const T& t : lst)
	push_back(t);
    }
    
    inline void* head_ptr() {
      return reinterpret_cast<void*>(head);
    }
    
    inline list_node_iterator begin() const
    {
      return { head };
    }
    
    inline list_node_iterator end() const
    {
      return { nullptr };
    }

    inline void reset()
    {
      head = tail = nullptr;
    }    
    
    void pop_front()
    {
      list_node* old_head = head;
      head = head->next;

      if(head == nullptr)
	tail = nullptr;
      
      delete old_head;
    }

    list_node* node_pop_front()
    {
      assert(head);

      list_node* old_head = head;      
      head = head->next;

      if(head == nullptr)
	tail = nullptr;

      return old_head;
    }
    
    T& front()
    {
      return head->data;
    }
    
    list_node* front_ptr()
    {
      return head;
    }

    bool empty() const
    {
      return head == nullptr;
    }

    void push_front(list_node* node)
    {
      assert(node);

      node->next = head;

      if(head == nullptr) {
	head = tail = node;
      } else {
	head = node;
      }
    }
    
    void push_front(const T& data)
    {
      head = new list_node{data, head};
      
      if(tail == nullptr)
	tail = head;
    }

    template <class Q = T>
    std::enable_if_t<std::is_same<Q, void*>::value, void>
    node_push_front(list_node* chunk)
    {
      chunk->data = reinterpret_cast<void*>(chunk);
      chunk->next = head;

      if(tail == nullptr)
	tail = chunk;

      head = chunk;
    }

    void push_back(const T& data)
    {
      if(head == nullptr) {
	head = new list_node{data, head};
	tail = head;
      } else {
	tail->next = new list_node{data, nullptr};
	tail = tail->next;
      }
    }

    void clear()
    {
      auto next = head;
      
      while(next) {
	auto node = next->next;
	delete next;
	next = node;
      }

      head = tail = nullptr;
    }

    list<T> append(list<T>&& lst)
    {
      if(head) {
	tail->next = lst.head;
	if(lst.tail)
	  tail = lst.tail;
      } else {
	head = lst.head;
	tail = lst.tail;
      }

      lst.head = lst.tail = nullptr;
      
      return *this;
    }
    
    void atomic_vacate_and_append(std::atomic<list<T>>& lst)
    {
      if(head == nullptr)
	return;
    
      list<T> copy_lst(head, tail);
      head = tail = nullptr;
      list<T> atomic_lst = lst.exchange(nullptr, std::memory_order_relaxed);
    
      while(true)
      {
	if(!atomic_lst.empty()) {
	  copy_lst.append(std::move(atomic_lst));
	}
      
	copy_lst = lst.exchange(copy_lst, std::memory_order_relaxed);
      
	if(!copy_lst.empty()) {
	  atomic_lst = lst.exchange(nullptr, std::memory_order_relaxed);
	} else {
	  break;
	}
      }
    }
  };

  template <typename T>
  class atomic_list
  {
  private:
    struct atomic_list_node;

    struct counted_node_ptr
    {
      int external_count;
      atomic_list_node* ptr;
    };

    struct atomic_list_node
    {
      static_assert(std::is_trivially_copyable<T>::value, "needs trivially copyable type.");

      T data;
      std::atomic<int> internal_count;
      counted_node_ptr next;

      atomic_list_node(T const& data_)
	: data(data_)
	, internal_count(0)
      {}

      static void* operator new(std::size_t);
      static void operator delete(void*, std::size_t);
    };

    std::atomic<counted_node_ptr> head;

    void increase_head_count(counted_node_ptr& old_counter)
    {
      counted_node_ptr new_counter;

      do
	{
	  new_counter = old_counter;
	  ++new_counter.external_count;
	} while(!head.compare_exchange_strong(old_counter,
					      new_counter,
					      std::memory_order_acquire,
					      std::memory_order_relaxed));

      old_counter.external_count = new_counter.external_count;
    }

  public:
    using node_type = atomic_list_node;

    atomic_list() : head(counted_node_ptr{1, nullptr}) {}
  
    void push_front(atomic_list_node* node)
    {
      counted_node_ptr new_node;
    
      new_node.ptr = node;
      new_node.external_count = 1;
      new_node.ptr->next = head.load(std::memory_order_relaxed);

      while(!head.compare_exchange_weak(new_node.ptr->next,
					new_node,
					std::memory_order_release,
					std::memory_order_relaxed));
    }
  
    void push_front(const T& data)
    {
      counted_node_ptr new_node;
    
      new_node.ptr = new atomic_list_node(data);
      new_node.external_count = 1;
      new_node.ptr->next = head.load(std::memory_order_relaxed);

      while(!head.compare_exchange_weak(new_node.ptr->next,
					new_node,
					std::memory_order_release,
					std::memory_order_relaxed));
    }

    T pop_front()
    {
      counted_node_ptr old_head = head.load(std::memory_order_relaxed);

      while(true)
	{
	  increase_head_count(old_head);
	  atomic_list_node* const ptr = old_head.ptr;

	  if(!ptr)
	    return T();

	  if(head.compare_exchange_strong(old_head,
					  ptr->next,
					  std::memory_order_relaxed))
	    {
	      T res(ptr->data);
	
	      int const count_increase = old_head.external_count-2;

	      if(ptr->internal_count.fetch_add(count_increase, std::memory_order_release) == count_increase)
		{
		  delete ptr;
		}

	      return res;
	    } else if(ptr->internal_count.fetch_add(-1, std::memory_order_relaxed) == 1) {
	    ptr->internal_count.load(std::memory_order_acquire);
	    delete ptr;
	  }	
	}
    }

    atomic_list_node* node_pop_front()
    {
      counted_node_ptr old_head = head.load(std::memory_order_relaxed);

      while(true)
	{            
	  atomic_list_node* const ptr = old_head.ptr;
      
	  if(!ptr)
	    return nullptr;
      
	  if(head.compare_exchange_strong(old_head, ptr->next, std::memory_order_relaxed))      	
	    return ptr;
	}
    }
  
    bool empty() const {
      return !head.load(std::memory_order_relaxed).ptr;
    }
  };

  template <typename>
  class node_pool;

  template <typename T>
  node_pool<list<T>>& list_pool();

  template <typename T>
  node_pool<atomic_list<T>>& atomic_list_pool();
}
#endif
