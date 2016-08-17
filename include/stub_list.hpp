#ifndef STUB_LIST_HPP_INCLUDED
#define STUB_LIST_HPP_INCLUDED

#include <atomic>
#include <cassert>

namespace otf_gc
{
  struct stub {
    void* start;
    size_t size;

    stub* next;
    stub* prev;

    stub(void* start_, size_t size_)
      : start(start_), size(size_), next(nullptr), prev(nullptr)
    {}
  };

  class stub_list
  {
  private:
    stub* head;
    stub* tail;
  public:
    stub_list(stub* head_ = nullptr, stub* tail_ = nullptr)
      : head(head_)
      , tail(tail_)
    {}

    ~stub_list()
    {
      stub* next = head;

      while(next) {
	stub* nnext = next->next;
	delete next;
	next = nnext;
      }
    }    
    
    inline stub_list& operator=(stub_list&& sl)
    {
      head = sl.head;
      tail = sl.tail;
      
      sl.head = sl.tail = nullptr;      
      return *this;
    }

    inline void append(stub_list* sl)
    {
      assert(sl != nullptr);

      if(tail) {
	tail->next = sl->head;
	      
	if(sl->head)
	  sl->head->prev = tail;
      } else {
	assert(head == nullptr);
	head = sl->head;
      }

      tail = sl->tail;
    }

    inline void atomic_vacate_and_append(std::atomic<stub_list*>& sl)
    {
      if(head == nullptr)
	return;

      stub_list* copy_sl   = new stub_list(head, tail);
      head = tail = nullptr;
      stub_list* atomic_sl = sl.exchange(nullptr, std::memory_order_relaxed);

      while(true)
      {
	if(atomic_sl) {
	  copy_sl->append(atomic_sl);
	  atomic_sl->head = atomic_sl->tail = nullptr;
	  delete atomic_sl;	  
	}

	copy_sl = sl.exchange(copy_sl, std::memory_order_relaxed);
	
	if(copy_sl) {
	  atomic_sl = sl.exchange(nullptr, std::memory_order_relaxed);
	} else {
	  break;
	}	
      }
    }

    inline stub* erase(stub* st)
    {
      assert(st != nullptr);
      if(st->prev)
	st->prev->next = st->next;
      if(st->next)
	st->next->prev = st->prev;
      st->next = st->prev = nullptr;
      return st;
    }

    inline void push_front(stub* st)
    {
      assert(st != nullptr);
      st->next = head;      
      if(head)
	head->prev = st;
      else
	tail = st;
      
      head = st;
      st->prev = nullptr;
    }

    inline void push_back(stub* st)
    {
      assert(st != nullptr);
      st->prev = tail;
      if(tail)
	tail->next = st;
      else
	head = st;
      tail = st;
      st->next = nullptr;
    }

    inline stub* front() {
      return head;
    }

    inline stub* back() {
      return tail;
    }

    inline void pop_front()
    {
      assert(head != nullptr);
      assert(head->prev == nullptr);
      
      stub* st = head;
      
      if(head->next)
	head->next->prev = nullptr;
      else
	tail = nullptr;
      
      head = head->next;
      st->next = nullptr;
    }

    inline void pop_back()
    {
      assert(tail != nullptr);
      assert(tail->next == nullptr);
      stub* st = tail;
      
      if(tail->prev)
	tail->prev->next = nullptr;
      else
	head = nullptr;
      
      tail = tail->prev;
      st->prev = nullptr;
    }

    inline bool empty() const
    {
      return head == nullptr;
    }
  };
}

#endif
