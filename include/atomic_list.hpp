#ifndef ATOMIC_LIST_HPP_INCLUDED
#define ATOMIC_LIST_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <initializer_list>
#include <utility>

namespace otf_gc
{
  template <typename T>
  struct list_node
  {
    T data;
    list_node<T>* next;

    static void* operator new(std::size_t);
    static void operator delete(void*, std::size_t);
  };
  
  template <typename T>
  struct list_node_iterator
  {
    list_node<T>* focus;

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
  
  template <typename T>
  class list
  {
  private:
    list_node<T>* head;
    list_node<T>* tail;    
  public:
    using node_type = list_node<T>;
    
    list() noexcept : head(nullptr), tail(nullptr) {}
    list(list_node<T>* head_) noexcept : head(head_), tail(head_) {}     
    list(list_node<T>* head_, list_node<T>* tail_) noexcept : head(head_), tail(tail_) {}
    list(std::initializer_list<T> lst) : head(nullptr), tail(nullptr)
    {
      for(const T& t : lst)
	push_back(t);
    }
    
    inline void* head_ptr() {
      return reinterpret_cast<void*>(head);
    }
    
    inline list_node_iterator<T> begin() const
    {
      return { head };
    }
    
    inline list_node_iterator<T> end() const
    {
      return { nullptr };
    }

    inline void reset()
    {
      head = tail = nullptr;
    }    
    
    void pop_front()
    {
      list_node<T>* old_head = head;
      head = head->next;

      if(head == nullptr)
	tail = nullptr;
      
      delete old_head;
    }

    void node_pop_front()
    {
      assert(head);
      
      head = head->next;

      if(head == nullptr)
	tail = nullptr;
    }
    
    T& front()
    {
      return head->data;
    }
    
    list_node<T>* front_ptr()
    {
      return head;
    }

    bool empty() const
    {
      return head == nullptr;
    }

    void push_front(list_node<T>* node)
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
      head = new list_node<T>{data, head};
      
      if(tail == nullptr)
	tail = head;
    }

    template <class Q = T>
    std::enable_if_t<std::is_same<Q, void*>::value, void>
    node_push_front(list_node<Q>* chunk)
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
	head = new list_node<T>{data, head};
	tail = head;
      } else {
	tail->next = new list_node<T>{data, nullptr};
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
    friend class list<T>;
    std::atomic<list_node<T>*> head;
  
  public:
    atomic_list() : head(nullptr) {}

    atomic_list(atomic_list&& lst)
      : head(lst.head.exchange(nullptr, std::memory_order_relaxed))
    {}
    
    virtual ~atomic_list() {}
    
    inline list_node_iterator<T> begin() const {
      return list_node_iterator<T> { head.load(std::memory_order_relaxed) };
    }

    inline list_node_iterator<T> end() const {
      return list_node_iterator<T> { nullptr };
    }
    
    void push_front(const T& data)
    {
      list_node<T>* new_head = new list_node<T>{data, nullptr};
      while(!head.compare_exchange_weak(new_head->next,
					new_head,
					std::memory_order_relaxed,
					std::memory_order_relaxed));
    }
    
    list_node<T>* front()
    {
      return head.load(std::memory_order_acquire);
    }

    list_node<T>* vacate()
    {
      return head.exchange(nullptr, std::memory_order_relaxed);
    }

    bool empty() const {
      return !head.load(std::memory_order_relaxed);
    }
    
    void clear()
    {
      list_node<T>* next = head.exchange(nullptr, std::memory_order_relaxed);      
      
      while(next) {
	auto node = next->next;
	delete next;
	next = node;
      }      
    }
  };
  
  template <typename>
  class node_pool;

  template <typename T>
  node_pool<list<T>>& list_pool();
}
#endif
